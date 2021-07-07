package main

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io/ioutil"
	"math"
	"net/url"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"

	"fortio.org/fortio/log"
	"github.com/uluyol/heyp-agents/go/fortio/stagedperiodic"
	"github.com/uluyol/heyp-agents/go/fortio/wanhttp"
	"github.com/uluyol/heyp-agents/go/pb"
	"github.com/uluyol/heyp-agents/go/stats"
	"google.golang.org/protobuf/encoding/prototext"
)

var (
	profileFlag            = flag.String("profile", "", "write .cpu and .mem profiles to `file`")
	allowInitialErrorsFlag = flag.Bool("allow-initial-errors", false, "Allow and don't abort on initial warmup errors")
	abortOnFlag            = flag.Int("abort-on", 0,
		"Http `code` that if encountered aborts the run. e.g. 503 or -1 for socket errors.")

	addrsFlag  = flag.String("addrs", "", "csv of addresses of remote servers")
	configFlag = flag.String("c", "", "path to config file")
	labelsFlag = flag.String("labels", "",
		"Additional config data/labels to add to the resulting JSON, defaults to target URL and hostname")
	outFlag       = flag.String("out", "data.log", "file to output stats to")
	summaryFlag   = flag.String("summary", "summary.json", "file to write results summary to")
	startTimeFlag = flag.String("start_time", time.Now().Add(3*time.Second).Format(time.RFC3339Nano), "time to start run")
)

func meanBitsPerReq(sizeDist []*pb.FortioClientConfig_SizeAndWeight) float64 {
	var sumWeightedBytes, sumWeight float64
	for _, sd := range sizeDist {
		sumWeightedBytes += float64(sd.GetRespSizeBytes()) * float64(sd.GetWeight())
		sumWeight += float64(sd.GetWeight())
	}
	return 8 * sumWeightedBytes / sumWeight
}

func convertStages(stages []*pb.FortioClientConfig_WorkloadStage, bitsPerReq float64) ([]stagedperiodic.WorkloadStage, error) {
	out := make([]stagedperiodic.WorkloadStage, len(stages))
	for i, s := range stages {
		dur, err := time.ParseDuration(s.GetRunDur())
		if err != nil {
			return nil, fmt.Errorf("failed to parse duration %q in stage %d: %v",
				s.GetRunDur(), i, err)
		}
		out[i].QPS = s.GetTargetAverageBps() / bitsPerReq
		out[i].Duration = dur
	}
	return out, nil
}

func withDists(addr string, c *pb.FortioClientConfig) (string, error) {
	url, err := url.Parse(addr)
	if err != nil {
		return "", fmt.Errorf("failed to parse addr %q: %w", addr, err)
	}
	url.Scheme = "http"

	var sdBuf strings.Builder
	for j, sd := range c.GetSizeDist() {
		if j > 0 {
			sdBuf.WriteByte(',')
		}
		fmt.Fprintf(&sdBuf, "%d:%d", sd.GetRespSizeBytes(), sd.GetWeight())
	}
	query := url.Query()
	if sdBuf.Len() > 0 {
		query.Add("size", sdBuf.String())
	}

	var ddBuf strings.Builder
	for j, dd := range c.GetDelayDist() {
		if _, err := time.ParseDuration(dd.GetDelayDur()); err != nil {
			return "", fmt.Errorf("invalid delay %q: %w", dd.GetDelayDur(), err)
		}
		if j > 0 {
			ddBuf.WriteByte(',')
		}
		fmt.Fprintf(&ddBuf, "%s:%d", dd.GetDelayDur(), dd.GetWeight())
	}
	if ddBuf.Len() > 0 {
		query.Add("delay", ddBuf.String())
	}

	url.RawQuery = query.Encode()
	return url.String(), nil
}

func toHTTPOptions(addr string, c *pb.FortioClientConfig) (wanhttp.HTTPOptions, error) {
	httpc := c.GetHttpOptions()

	url, err := withDists(addr, c)
	if err != nil {
		return wanhttp.HTTPOptions{}, err
	}

	opts := wanhttp.HTTPOptions{
		URL:               url,
		Compression:       httpc.GetCompressionOn(),
		DisableFastClient: !httpc.GetUseFastClient(),
		DisableKeepAlive:  !httpc.GetUseKeepAlive(),
		AllowHalfClose:    httpc.GetAllowHalfClose(),
		Insecure:          httpc.GetAllowInsecureTls(),
		FollowRedirects:   httpc.GetFollowRedirects(),
		ContentType:       httpc.GetContentType(),
		Payload:           []byte(httpc.GetPayload()),
	}

	if !opts.DisableFastClient && opts.FollowRedirects {
		return wanhttp.HTTPOptions{}, errors.New(
			"fast client does not support following redirects")
	}

	timeout, err := time.ParseDuration(httpc.GetReqTimeoutDur())
	if err != nil {
		return wanhttp.HTTPOptions{}, fmt.Errorf(
			"failed to parse timeout: %w", err)
	}
	opts.HTTPReqTimeOut = timeout

	return opts, nil
}

func maxSize(sds []*pb.FortioClientConfig_SizeAndWeight) int32 {
	maxSize := int32(1024)
	for _, sd := range sds {
		if sd.GetRespSizeBytes() > maxSize {
			maxSize = sd.GetRespSizeBytes()
		}
	}
	return maxSize
}

func workerMain(shardIndex, numShards int, config *pb.FortioClientConfig, startTime time.Time) {
	*log.LogPrefix = fmt.Sprintf("> [shard %d] ", shardIndex)

	for _, s := range config.GetWorkloadStages() {
		bps := s.GetTargetAverageBps() / float64(numShards)
		s.TargetAverageBps = &bps
	}

	stages, err := convertStages(config.GetWorkloadStages(), meanBitsPerReq(config.GetSizeDist()))
	if err != nil {
		log.Fatalf("failed to convert stages: %v", err)
	}

	f, err := os.Create(*outFlag + "." + strconv.Itoa(shardIndex))
	if err != nil {
		log.Fatalf("failed to create stats log: %v", err)
	}

	ro := stagedperiodic.RunnerOptions{
		NumThreads: int(config.GetNumConns()),
		Resolution: config.GetDurResolutionSec(),
		Out:        os.Stdout,
		Recorder:   stats.NewRecorder(f), // takes ownership of f
		Labels:     "",
		Jitter:     config.GetJitterOn(),
		Stages:     stages,
		StartTime:  &startTime,
	}

	if shardIndex == 0 {
		log.Infof("going to run stages: %+v", stages)
	}

	addrs := strings.Split(*addrsFlag, ",")
	addr := addrs[shardIndex%len(addrs)]

	wanhttp.BufferSizeKb = int(math.Ceil(float64(maxSize(config.GetSizeDist()))/1024) + 2)

	httpOpts, err := toHTTPOptions(addr, config)
	if err != nil {
		log.Fatalf("failed to populate http options: %v", err)
	}

	o := &wanhttp.HTTPRunnerOptions{
		HTTPOptions:        httpOpts,
		RunnerOptions:      ro,
		Profiler:           *profileFlag,
		AllowInitialErrors: *allowInitialErrorsFlag,
		AbortOn:            *abortOnFlag,
	}

	res, err := wanhttp.RunHTTPTest(o)

	if err != nil {
		log.Fatalf("failed to run workload: %v", err)
	}

	f, err = os.Create(*summaryFlag + "." + strconv.Itoa(shardIndex))
	if err != nil {
		log.Fatalf("failed to create summary file: %v", err)
	}

	enc := json.NewEncoder(f)
	if err := enc.Encode(res); err != nil {
		log.Fatalf("failed to marshal summary: %v; data:\n%+v", err, res)
	}

	f.Close()
}

func main() {
	flag.Parse()

	if *configFlag == "" {
		log.Fatalf("must specify config path")
	}

	if *addrsFlag == "" {
		log.Fatalf("must populate -addrs flag")
	}

	startTime, err := time.Parse(time.RFC3339Nano, *startTimeFlag)
	if err != nil {
		log.Fatalf("failed to parse start time; must be ISO 8601: %v", err)
	}

	configData, err := ioutil.ReadFile(*configFlag)
	if err != nil {
		log.Fatalf("failed to read in config: %v", err)
	}
	config := new(pb.FortioClientConfig)
	if err := prototext.Unmarshal(configData, config); err != nil {
		log.Fatalf("failed to unmarshal config: %v", err)
	}

	if config.GetNumShards() < 2 {
		workerMain(0, 1, config, startTime)
	} else {
		if wiStr := os.Getenv("FORTIO_CLIENT_WORKER_INDEX"); wiStr != "" {
			index, err := strconv.Atoi(wiStr)
			if err != nil {
				log.Fatalf("internal error: bad index %d", index)
			}
			workerMain(index, int(config.GetNumShards()), config, startTime)
			return
		}

		// Act as coordinator: start workers
		execPath, err := os.Executable()
		if err != nil {
			log.Warnf("failed to find executable, falling back to argv[0]: %v", err)
			execPath = os.Args[0]
		}
		var cmds []*exec.Cmd
		for i := 0; i < int(config.GetNumShards()); i++ {
			cmd := exec.Command(execPath, os.Args[1:]...)
			cmd.Env = append([]string(nil), os.Environ()...)
			cmd.Env = append(cmd.Env, "FORTIO_CLIENT_WORKER_INDEX="+strconv.Itoa(i))
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			if err := cmd.Start(); err != nil {
				log.Fatalf("failed to start subprocess client: %v", err)
			}
			cmds = append(cmds, cmd)
		}

		ret := 0
		for i, c := range cmds {
			if err := c.Wait(); err != nil {
				ret = 1
				log.Warnf("failure in shard %d: %v", i, err)
			}
		}
		os.Exit(ret)
	}
}
