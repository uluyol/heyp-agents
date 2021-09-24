package detectors

import (
	"log"
	"sort"

	"github.com/uluyol/heyp-agents/go/cmd/detectlosseval/sysconfig"
	"github.com/uluyol/heyp-agents/go/pb"
)

type LossDetectorArgs struct {
	UnixSec    float64
	HostInfos  []*pb.InfoBundle
	Admissions map[string]sysconfig.FGAdmissions
	FGs        []string
}

// A LossDetector determines whether an FG (enumerated in fgs) should respond to loss
// in the network.
//
// As input, it should go over each args.FGs[i] in fgs (provided by the caller) and look at the
// FG's local state (first aggregate the state of each host, then aggregate the state across hosts)
// in snap and admissions (it should not look at the state of other FGs
// since that is unavailable in a real implementation). If it determines that
//
// 1. loss is significant and
// 2. the FG is respecting the HIPRI/LOPRI admissions
//
// it will mark detectedLoss[i] = true.
//
// (2) might not be met if the shape of demands changes across hosts,
// since that can cause the cluster-agent to struggle with admission enforcement.
type LossDetector interface {
	FGsWithLOPRILoss(args LossDetectorArgs, detectedLoss []bool, score []float64)
}

type filterFunc = func([]*pb.FlowInfo, []bool)

func ignoreAppLimited(infos []*pb.FlowInfo, include []bool) {
	for i := range infos {
		if !include[i] {
			continue
		}
		if infos[i].GetAux().AppLimited {
			include[i] = false
		}
	}
}

func makeIgnoreWithCumBytesLessThan(thresh int64) filterFunc {
	return func(infos []*pb.FlowInfo, include []bool) {
		for i := range infos {
			if !include[i] {
				continue
			}
			if infos[i].CumUsageBytes < thresh {
				include[i] = false
			}
		}
	}
}

var _ filterFunc = ignoreAppLimited

func forEachFGHost(fgs []string, infoBundles []*pb.InfoBundle, filters []filterFunc, fn func(fgID int, hostID uint64, infos []*pb.FlowInfo)) {
	type fgState struct {
		infos []*pb.FlowInfo
	}

	states := make([]fgState, len(fgs))
	for _, bundle := range infoBundles {
		for i, st := range states {
			states[i].infos = st.infos[:0]
		}

		hostInfos := bundle.GetFlowInfos()
		include := make([]bool, len(hostInfos))
		for i := range include {
			include[i] = true
		}

		for _, filter := range filters {
			filter(hostInfos, include)
		}

		for i, info := range hostInfos {
			if !include[i] {
				continue
			}
			fg := info.GetFlow().GetSrcDc() + "_TO_" + info.GetFlow().GetDstDc()
			fgID := sort.SearchStrings(fgs, fg)
			if fgID >= len(fgs) {
				continue
			}

			states[fgID].infos = append(states[fgID].infos, info)
		}

		for fgID, state := range states {
			fn(fgID, bundle.GetBundler().GetHostId(), state.infos)
		}
	}
}

type hostConn struct {
	dstAddr  string
	protocol pb.Protocol
	hostID   uint64
	srcPort  uint16
	dstPort  uint16
	seqnum   uint64
}

func hostConnFromProto(f *pb.FlowMarker) hostConn {
	return hostConn{
		dstAddr:  f.DstAddr,
		protocol: f.Protocol,
		hostID:   f.HostId,
		srcPort:  uint16(f.SrcPort),
		dstPort:  uint16(f.DstPort),
		seqnum:   f.Seqnum,
	}
}

type trackedStats struct {
	UsageBytes float64
	NumRetrans float64
}

func (s trackedStats) RetransPerByte() float64 { return s.NumRetrans / s.UsageBytes }

type avgRetransDetectorFGState struct {
	Cur, Smoothed struct {
		HIPRI, LOPRI trackedStats
	}
	SmoothedSeen int
}

type AvgRetransDetector struct {
	Filters                          []filterFunc // really this detector wants all flows
	AllowedAdmissionEnforcementSlack float64
	MaxAllowedFracLossLOPRI          float64
	MinNumSeen                       int

	lastUnixSec float64
	fgStats     []avgRetransDetectorFGState

	prevInfos map[hostConn]*pb.FlowInfo
	curInfos  map[hostConn]*pb.FlowInfo
}

func (d *AvgRetransDetector) FGsWithLOPRILoss(args LossDetectorArgs, detectedLoss []bool, scores []float64) {
	if d.fgStats == nil {
		d.fgStats = make([]avgRetransDetectorFGState, len(args.FGs))
		d.prevInfos = make(map[hostConn]*pb.FlowInfo)
		d.curInfos = make(map[hostConn]*pb.FlowInfo)
	} else {
		for i := range d.fgStats {
			d.fgStats[i].Cur.HIPRI = trackedStats{}
			d.fgStats[i].Cur.LOPRI = trackedStats{}
		}
	}
	t := d.prevInfos
	d.prevInfos = d.curInfos
	for k := range t {
		delete(t, k)
	}
	d.curInfos = t

	forEachFGHost(args.FGs, args.HostInfos, d.Filters, d.procHost)

	for fgID := range d.fgStats {
		state := &d.fgStats[fgID]
		state.SmoothedSeen++
		updateEWMA(state.Cur.HIPRI, &state.Smoothed.HIPRI)
		updateEWMA(state.Cur.LOPRI, &state.Smoothed.LOPRI)
		if state.SmoothedSeen > d.MinNumSeen {
			limits := args.Admissions[args.FGs[fgID]]
			totalUsageBps := 8 * (state.Smoothed.HIPRI.UsageBytes + state.Smoothed.LOPRI.UsageBytes) / (args.UnixSec - d.lastUnixSec)
			if totalUsageBps > (limits.HIPRI+limits.LOPRI)*(1+d.AllowedAdmissionEnforcementSlack) {
				log.Print("print some info")
				continue
			}
			retransPerByteHIPRI := state.Smoothed.HIPRI.RetransPerByte()
			retransPerByteLOPRI := state.Smoothed.LOPRI.RetransPerByte()
			// TODO: compare

			total := retransPerByteHIPRI + retransPerByteLOPRI
			var lopriOverTotal float64
			if total > 0 {
				lopriOverTotal = retransPerByteLOPRI / total
			}
			if lopriOverTotal > d.MaxAllowedFracLossLOPRI {
				detectedLoss[fgID] = true
				scores[fgID] = lopriOverTotal
			}
		}
	}

	d.lastUnixSec = args.UnixSec
}

func updateEWMA(cur trackedStats, smoothed *trackedStats) {
	const alpha = 0.3
	smoothed.UsageBytes = alpha*cur.UsageBytes + (1-alpha)*smoothed.UsageBytes
	smoothed.NumRetrans = alpha*cur.NumRetrans + (1-alpha)*smoothed.NumRetrans
}

func (d *AvgRetransDetector) procHost(fgID int, hostID uint64, infos []*pb.FlowInfo) {
	for _, cur := range infos {
		hostConn := hostConnFromProto(cur.GetFlow())
		d.curInfos[hostConn] = cur
		prev, ok := d.prevInfos[hostConn]
		if !ok {
			continue
		}

		usageBytes := cur.CumUsageBytes - prev.CumUsageBytes
		numRetrans := cur.GetAux().RetransTotal - prev.GetAux().RetransTotal

		if cur.CurrentlyLopri {
			d.fgStats[fgID].Cur.LOPRI.UsageBytes += float64(usageBytes)
			d.fgStats[fgID].Cur.LOPRI.NumRetrans += float64(numRetrans)
		} else {
			d.fgStats[fgID].Cur.HIPRI.UsageBytes += float64(usageBytes)
			d.fgStats[fgID].Cur.HIPRI.NumRetrans += float64(numRetrans)
		}
	}
}
