#!/usr/bin/env python3

import argparse
import json
import os
import shutil

from os.path import join as pjoin

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

with open(args.aligned_input) as fin:
    for line in fin:
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
                        if field not in outfiles:
                            outfiles[field] = open(pjoin(args.outdir, fg, field), "w")
                        outfiles[field].write("{},{}\n".format(rec["unixSec"], val))
                field = "got_real_rto"
                saw_base_rto = flow_info.get("aux", {}).get("rtoMs", None)
                saw_backoff = flow_info.get("aux", {}).get("backoff", None)
                if saw_backoff is not None and saw_base_rto is not None:
                    val = saw_base_rto << int(saw_backoff)
                    if field not in outfiles:
                        outfiles[field] = open(pjoin(args.outdir, fg, field), "w")
                    outfiles[field].write("{},{}\n".format(rec["unixSec"], val))

