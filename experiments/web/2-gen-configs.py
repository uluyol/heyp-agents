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


_SYS_TO_RUN = ["hsc", "nl", "qd", "qdlrl", "rl"]
_ALL_X = [2]  # [2, 4, 6, 8]
_ALL_Y = [2.5]  # [2.5, 5]
_ALL_C = [1.5]  # [1.25, 1.5]

D = 1

_CONFIGS = []
for x in _ALL_X:
    for y in _ALL_Y:
        for c in _ALL_C:
            _CONFIGS.append({
                "name": "X-{0}-C-{1}-Y-{2}".format(x, c, y),
                "BE1_APPROVED_BPS": int(gbps(x)),
                "BE1_SURPLUS_BPS": int(gbps(c * x - x)),
                "BE2_APPROVED_BPS": int(gbps(y)),
                "BE1_CONNS": int(10 + 25 * D * c * x),
                "BE1_BPS": int(D * c * gbps(x)),
                "BE2_CONNS": int(10 + 25 * y),
                "BE2_BPS": int(gbps(y)),
            })

parser = argparse.ArgumentParser()
parser.add_argument("outdir")

args = parser.parse_args()

shutil.rmtree(args.outdir, ignore_errors=True)
os.makedirs(os.path.join(args.outdir, "configs"))
for sys in _SYS_TO_RUN:
    template_path = "config-{0}.template.textproto".format(sys)
    template = slurp(template_path)
    for cfg in _CONFIGS:
        towrite = template
        for k, v in cfg.items():
            if k == "name":
                continue
            towrite = towrite.replace("%" + k + "%", str(v))
        outpath = os.path.join(
            args.outdir,
            "configs",
            cfg["name"] + "-" + sys + ".textproto",
        )
        with open(outpath, "w") as fout:
            fout.write(towrite)
