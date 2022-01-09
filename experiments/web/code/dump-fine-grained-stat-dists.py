#!/usr/bin/env python3

import argparse
import json
import os
import shutil

from os.path import join as pjoin

IS_CUM = 0
NOT_CUM = 1
UNKNOWN = 2

FIELD_TYPE = {
    "aux_advmss": NOT_CUM,
    "aux_appLimited": NOT_CUM,
    "aux_atoMs": NOT_CUM,
    "aux_backoff": NOT_CUM,
    "aux_bbrBw": NOT_CUM,
    "aux_bbrCwndGain": NOT_CUM,
    "aux_bbrMinRttMs": NOT_CUM,
    "aux_bbrPacingGain": NOT_CUM,
    "aux_busyTimeMs": IS_CUM,
    "aux_bytesAcked": IS_CUM,
    "aux_bytesReceived": IS_CUM,
    "aux_bytesRetrans": IS_CUM,
    "aux_cwnd": NOT_CUM,
    "aux_dataSegsIn": IS_CUM,
    "aux_dataSegsOut": IS_CUM,
    "aux_delivered": IS_CUM,
    "aux_deliveredCe": IS_CUM,
    "aux_deliveryRate": NOT_CUM,
    "aux_dsackDups": IS_CUM,
    "aux_fackets": UNKNOWN,
    "aux_lastackMs": NOT_CUM,
    "aux_lastrcvMs": NOT_CUM,
    "aux_lastsndMs": NOT_CUM,
    "aux_lost": IS_CUM,
    "aux_minRttMs": NOT_CUM,
    "aux_mss": NOT_CUM,
    "aux_notSent": NOT_CUM,
    "aux_pacingRate": NOT_CUM,
    "aux_pacingRateMax": NOT_CUM,
    "aux_pmtu": UNKNOWN,
    "aux_qack": NOT_CUM,
    "aux_rcvmss": NOT_CUM,
    "aux_rcvRttMs": NOT_CUM,
    "aux_rcvSpace": NOT_CUM,
    "aux_rcvSsthresh": NOT_CUM,
    "aux_rcvWscale": NOT_CUM,
    "aux_reordering": IS_CUM,
    "aux_reordSeen": IS_CUM,
    "aux_retrans": UNKNOWN,
    "aux_retransTotal": UNKNOWN,
    "aux_rtoMs": NOT_CUM,
    "aux_rttMs": NOT_CUM,
    "aux_rttVarMs": NOT_CUM,
    "aux_rwndLimitedMs": IS_CUM,
    "aux_sacked": UNKNOWN,
    "aux_segsIn": IS_CUM,
    "aux_segsOut": IS_CUM,
    "aux_sndbufLimitedMs": IS_CUM,
    "aux_sndWscale": NOT_CUM,
    "aux_ssthresh": NOT_CUM,
    "aux_unacked": IS_CUM,
    "cumUsageBytes": IS_CUM,
    "cumHipriUsageBytes": IS_CUM,
    "cumLopriUsageBytes": IS_CUM,
    "currentlyLopri": NOT_CUM,
    "ewmaUsageBps": NOT_CUM,
    "predictedDemandBps": NOT_CUM,
}

def blow_up(field, val):
    if not isinstance(val, dict):
        yield field, val
    else:
        for f2, val2 in val.items():
            for f2_bu, val2_bu in blow_up(f2, val2):
                yield field + "_" + f2_bu, val2_bu

parser = argparse.ArgumentParser()
parser.add_argument("--trim-sec", dest="trim_sec", default=15, type=int)
parser.add_argument("aligned_input")
parser.add_argument("outdir")
args = parser.parse_args()

min_time = None
max_time = None
with open(args.aligned_input) as fin:
    for line in fin:
        rec = json.loads(line)
        if min_time is None:
            min_time = rec["unixSec"]
            max_time = rec["unixSec"]
        else:
            min_time = min(min_time, rec["unixSec"])
            max_time = max(max_time, rec["unixSec"])

shutil.rmtree(args.outdir, ignore_errors=True)

fg_outfiles = {
    "AA_TO_EDGE": dict(),
    "WA_TO_EDGE": dict(),
}

os.makedirs(pjoin(args.outdir, "AA_TO_EDGE"))
os.makedirs(pjoin(args.outdir, "WA_TO_EDGE"))

min_keep_time = min_time + args.trim_sec
max_keep_time = max_time - args.trim_sec

class FlowStats:
    def __init__(self, line_i, val):
        self.line_i = line_i
        self.val = val

last_flow_stats = dict()

with open(args.aligned_input) as fin:
    line_i = 0
    for line in fin:
        line_i += 1
        rec = json.loads(line)
        if rec["unixSec"] < min_keep_time or rec["unixSec"] > max_keep_time:
            continue
        for node_id, node_data in rec["data"].items():
            for flow_info in node_data["flowInfos"]:
                fg = flow_info["flow"]["srcDc"] + "_TO_" + flow_info["flow"]["dstDc"]
                outfiles = fg_outfiles.get(fg, None)
                if outfiles is None:
                    continue
                for field, val in flow_info.items():
                    if field == "flow":
                        continue
                    for field, val in blow_up(field, val):
                        ftype = FIELD_TYPE.get(field, UNKNOWN)
                        if ftype == UNKNOWN:
                            continue
                        if ftype == IS_CUM:
                            flow_field = (tuple(sorted(flow_info["flow"].items())), field)
                            if flow_field not in last_flow_stats:
                                last_flow_stats[flow_field] = FlowStats(line_i, val)
                                continue
                            last_stats = last_flow_stats[flow_field]
                            if last_stats.line_i >= line_i:
                                print("WHAT")
                            if last_stats.line_i < line_i - 1:
                                # dead flow, rewrite stats
                                last_flow_stats[flow_field] = FlowStats(line_i, val)
                                continue
                            diff = int(val) - int(last_stats.val)
                            last_stats.line_i = line_i
                            last_stats.val = val
                            diff_field = field + "_diff"
                            if diff_field not in outfiles:
                                outfiles[diff_field] = open(pjoin(args.outdir, fg, diff_field), "w", buffering=819200)
                            outfiles[diff_field].write("{},{}\n".format(rec["unixSec"], diff))
                        elif ftype == NOT_CUM:
                            if field not in outfiles:
                                outfiles[field] = open(pjoin(args.outdir, fg, field), "w", buffering=819200)
                            outfiles[field].write("{},{}\n".format(rec["unixSec"], val))
                        else:
                            raise ValueError
                field = "got_real_rto"
                saw_base_rto = flow_info.get("aux", {}).get("rtoMs", None)
                saw_backoff = flow_info.get("aux", {}).get("backoff", None)
                if saw_backoff is not None and saw_base_rto is not None:
                    val = saw_base_rto << int(saw_backoff)
                    if field not in outfiles:
                        outfiles[field] = open(pjoin(args.outdir, fg, field), "w")
                    outfiles[field].write("{},{}\n".format(rec["unixSec"], val))

