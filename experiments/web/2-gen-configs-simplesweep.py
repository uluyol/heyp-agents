#!/usr/bin/env python3

import argparse
import os
import os.path
import shutil


def gbps(x):
    return x * 1024 * 1024 * 1024


def slurp(p):
    with open(p) as f:
        data = f.read()
    return data


_TEMPLATE = "config-12.template.textproto"
_NUM_SHARDS = 4

_CONFIGS = []
for x in range(1, 11):
    _CONFIGS.append({
        "name": "gbps-" + str(x),
        "BPS": int(gbps(x) / _NUM_SHARDS),
        "CONNS": 45 + 5 * x,
    })

parser = argparse.ArgumentParser()
parser.add_argument("outdir")

args = parser.parse_args()

shutil.rmtree(args.outdir, ignore_errors=True)
os.makedirs(os.path.join(args.outdir, "configs"))
template = slurp(_TEMPLATE)
for cfg in _CONFIGS:
    towrite = template
    for k, v in cfg.items():
        if k == "name":
            continue
        towrite = towrite.replace("%" + k + "%", str(v))
    outpath = os.path.join(
        args.outdir,
        "configs",
        cfg["name"] + ".textproto",
    )
    with open(outpath, "w") as fout:
        fout.write(towrite)
