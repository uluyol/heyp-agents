#!/usr/bin/env python3

import argparse
import subprocess
from os.path import join as pjoin

parser = argparse.ArgumentParser()
parser.add_argument("--latex", default=False, action="store_true")
parser.add_argument("outdir")
args = parser.parse_args()

STAT = "mean"

SEP_TABLES = frozenset([
    "MaxInc",
    "PGain",
    "IgErr",
    "IgK",
])

FIELD_NAMES = [
    "MaxInc",
    "PGain",
    "IgErr",
    "IgK",
    "Demands",
    "#task",
    "LPCap",
    "InitDownFrac",
    "ShiftT",
    "FinOver",
    "FinShort",
    "InterOver",
    "InterShort",
    "NumConv",
    "ConvIters",
    "NumOscil",
    "UndoneQoS",
]

TABLE_COLS = [
    "ShiftT",
    "#task",
    "Demands",
    "LPCap",
    "InitDownFrac",
]

TABLE_COLS_SET = frozenset(TABLE_COLS)

PAPER_NAMES_AND_ORDER = [
    ("Demands", "Demand Dist."),
    ("#task", "No. of tasks"),
    ("LPCap", "LOPRI Cap. Frac"),
    ("InitDownFrac", "Init. Frac. Downgraded"),
    ("ShiftT", "Unsat. Demand Shifts to HIPRI?"),
    ("NumConv", "No. of Converged Runs"),
    ("ConvIters", "Convergence Time (#periods)"),
    ("UndoneQoS", "No. of QoS changes undone"),
    ("NumOscil", "No. of Oscillations"),
    ("FinOver", "Final Overage"),
    ("FinShort", "Final Shortage"),
    ("InterOver", "Intermediate Overage"),
    ("InterShort", "Intermediate Shortage"),
]

PAPER_NAMES_MAP = {n[0]: n[1] for n in PAPER_NAMES_AND_ORDER}

def check_paper_names():
    paper_names_set = set(n[0] for n in PAPER_NAMES_AND_ORDER)
    all_names = paper_names_set.union(SEP_TABLES)
    if len(all_names) != len(FIELD_NAMES):
        raise ValueError("missing paper names: len(all_names): {} vs len(FIELD_NAMES): {}".format(
            len(all_names), len(FIELD_NAMES)))
    for fname in FIELD_NAMES:
        if fname not in all_names:
            raise ValueError("did not find {} in {}".format(fname, all_names))

check_paper_names()

def get_key_for_table(r):
    return tuple(r[k] for k in TABLE_COLS)

def data_for(churn_metric):
    fields = [
        ".result.scenario.controller.maxInc",
        ".result.scenario.controller.propGain",
        ".result.scenario.controller.ignoreErrBelow",
        ".result.scenario.controller.ignoreErrByCountMultiplier",
        ".hostDemandsGen",
        ".numHosts",
        ".result.scenario.lopriCapOverExpectedDemand",
        ".result.initDowngradeFrac",
        ".result.shiftTraffic",
        ".result.downgradeSummary.realizedOverage." + STAT,
        ".result.downgradeSummary.realizedShortage." + STAT,
        ".result.downgradeSummary.intermediateOverage." + STAT,
        ".result.downgradeSummary.intermediateShortage." + STAT,
        ".result.feedbackControlSummary.numRunsConverged",
        ".result.feedbackControlSummary.itersToConverge." + STAT,
        ".result.feedbackControlSummary.numOscillations." + STAT,
        churn_metric,
    ]
    return "\"" + " ".join("\\(" + f + ")" for f in fields) + "\""

def should_downgrade_data():
    return data_for(".result.feedbackControlSummary.numUpgraded." + STAT)

def should_upgrade_data():
    return data_for(".result.feedbackControlSummary.numDowngraded." + STAT)

def rewrite_demands(s):
    if s.startswith("elephantsMice-"):
        return "EM-" + str(int(100*float(s[len("elephantsMice-"):]))) + "%"
    if s == "exponential":
        return "EXP"
    if s == "fb15":
        return "FB15"
    if s == "uniform":
        return "UNI"
    return s

def rewrite_float2pct(s):
    return "{:.0f}".format(100*float(s))

def rewrite_yn(s):
    if s == "true":
        return "Y"
    if s == "false":
        return "N"
    return s

REWRITES = {
    "Demands": rewrite_demands,
    "ShiftT": rewrite_yn,
    "LPCap": rewrite_float2pct,
    "InitDownFrac": rewrite_float2pct,
    "FinOver": rewrite_float2pct,
    "FinShort": rewrite_float2pct,
    "InterOver": rewrite_float2pct,
    "InterShort": rewrite_float2pct,
}

# aoe >= 1: no downgrade
# aoe <  1: do downgrade
# aoe >= 1 && idf == 0 -> don't use
# aoe >= 1 && idf == 1 -> don't use
# aoe >= 1 && idf == 0 -> churn == upgrades
# aoe <  1 && idf == 1 -> churn == downgrades

out = subprocess.check_output(["jq", "-r", 
    "if .result.initDowngradeFrac == 0 then " + should_downgrade_data() + " else " + should_upgrade_data() + " end",
    pjoin(args.outdir, "sim-data.json")])

tables = {}
for line in out.decode("utf-8").splitlines():
    fields = line.split()
    table_key = dict()

    table_val = dict()
    if len(fields) != len(FIELD_NAMES):
        raise ValueError
    for i in range(len(fields)):
        fname = FIELD_NAMES[i]
        field = fields[i]
        if fname in SEP_TABLES:
            table_key[fname] = field
        else:
            table_val[fname] = field
    table_key = tuple(sorted(table_key.items()))
    if table_key not in tables:
        tables[table_key] = []
    tables[table_key].append(table_val)

SEP = "\t"
END = ""
if args.latex:
    SEP = " & "
    END = " \\\\ \\hline"

table_keys = sorted(tables.keys())

for table_key in table_keys:
    table_vals = tables[table_key]
    print(" ".join('{0}={1}'.format(*k) for k in table_key))

    sorted_table_vals = sorted(table_vals, key=get_key_for_table)
    names = FIELD_NAMES
    if args.latex:
        names = (n[0] for n in PAPER_NAMES_AND_ORDER)
    for fname in names:
        if fname in SEP_TABLES:
            continue
        row = [fname]
        if args.latex:
            row = [PAPER_NAMES_MAP[fname]]
        for v in sorted_table_vals:
            t = v[fname]
            if fname in REWRITES:
                t = REWRITES[fname](t)
            row.append(t)
        if args.latex and fname in TABLE_COLS_SET:
            newrow = []
            cur = None
            count = 0
            for r in row:
                if r != cur:
                    if count == 1:
                        newrow.append(cur)
                    elif count > 0:
                        newrow.append("\multicolumn{{{0}}}{{c|}}{{{1}}}".format(count, cur))
                    cur = r
                    count = 0
                count += 1
            if count == 1:
                newrow.append(cur)
            elif count > 0:
                newrow.append("\multicolumn{{{0}}}{{c|}}{{{1}}}".format(count, cur))
            row = newrow

        print(SEP.join(row) + END)
    print("\n")
