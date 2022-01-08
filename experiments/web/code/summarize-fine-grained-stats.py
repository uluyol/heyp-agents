#!/usr/bin/env python3

import argparse
import os

from concurrent import futures
from os.path import basename
from os.path import join as pjoin

def summarize_metric(metric_path):
    metric_name = basename(metric_path)
    total = 0
    num = 0
    with open(metric_path) as fin:
        for line in fin:
            fields = line.strip().split(",")
            val = None
            if fields[1] == "True":
                val = float(1)
            elif fields[1] == "False":
                val = float(0)
            else:
                val = float(fields[1])
            total += val
            num += 1
    return [
        (metric_name, "Mean", total / max(1, num)),
        (metric_name, "Sum", total),
    ]

parser = argparse.ArgumentParser()
parser.add_argument("statsdir")
parser.add_argument("dataset_name")
args = parser.parse_args()

with futures.ProcessPoolExecutor() as pool:
    for fg in os.listdir(args.statsdir):
        metric_summaries = pool.map(
            summarize_metric,
            [pjoin(args.statsdir, fg, m) for m in os.listdir(pjoin(args.statsdir, fg))],
        )
        for summary in metric_summaries:
            for metric, stat, value in summary:
                print("{},{},{},{},{}".format(args.dataset_name, fg, metric, stat, value))
