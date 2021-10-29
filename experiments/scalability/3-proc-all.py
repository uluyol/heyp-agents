#!/usr/bin/env python3

import argparse
import glob
import subprocess

from concurrent import futures
from os.path import basename
from os.path import join as pjoin

parser = argparse.ArgumentParser()
parser.add_argument("outdir")
parser.add_argument("--p", default=16, type=int)

args = parser.parse_args()


def proc_one(datadir):
    procdir = pjoin(args.outdir, "proc-indiv", basename(datadir))
    config = pjoin(args.outdir, "configs", basename(datadir) + ".textproto")
    print("proc", datadir, "=====")
    subprocess.run(["./proc.bash", datadir, procdir, config])


datadirs = glob.glob(pjoin(args.outdir, "data", "*"))

with futures.ThreadPoolExecutor(max_workers=args.p) as e:
    e.map(proc_one, datadirs)
