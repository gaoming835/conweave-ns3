#!/usr/bin/env python3
import argparse
import csv
import os
from collections import defaultdict

import numpy as np


def percentile(values, pct):
    return float(np.percentile(values, pct)) if values else 0.0


def read_fct(path):
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path) as fin:
        for line in fin:
            f = line.split()
            if len(f) < 8:
                continue
            rows.append({
                "src": int(f[0]),
                "dst": int(f[1]),
                "size_bytes": int(f[4]),
                "start_ns": int(f[5]),
                "fct_ns": int(f[6]),
                "standalone_ns": max(1, int(f[7])),
                "slowdown": max(1.0, int(f[6]) / max(1, int(f[7]))),
            })
    return rows


def read_queue(path):
    values = []
    if not os.path.exists(path):
        return values
    with open(path) as fin:
        for line in fin:
            f = line.strip().split(",")
            if len(f) == 6:
                values.append(int(f[4]))
    return values


def read_link_util(path):
    raw = defaultdict(dict)
    if not os.path.exists(path):
        return []
    with open(path) as fin:
        for line in fin:
            f = line.strip().split(",")
            if len(f) != 5:
                continue
            t = int(f[0])
            key = (int(f[1]), int(f[2]))
            raw[key][t] = (int(f[3]), int(f[4]))
    util = []
    for samples in raw.values():
        times = sorted(samples)
        for prev, cur in zip(times, times[1:]):
            prev_bytes, _ = samples[prev]
            cur_bytes, rate_bps = samples[cur]
            delta_ns = max(1, cur - prev)
            gbps = max(0, cur_bytes - prev_bytes) * 8.0 / delta_ns
            cap = rate_bps / 1e9
            if cap > 0:
                util.append(gbps / cap)
    return util


def read_source_rate(path):
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path) as fin:
        for line in fin:
            f = line.strip().split(",")
            if len(f) == 4:
                rows.append({
                    "time_ns": int(f[0]),
                    "aggregate_rate_gbps": int(f[1]) / 1e9,
                    "active_qps": int(f[2]),
                    "inflight_bytes": int(f[3]),
                })
    return rows


def rct_slowdown(fct_rows):
    groups = defaultdict(list)
    for r in fct_rows:
        groups[(r["start_ns"], r["dst"], r["size_bytes"])].append(r)
    slowdowns = []
    for (_start, _dst, _size), rows in groups.items():
        if len(rows) < 4:
            continue
        start = min(r["start_ns"] for r in rows)
        done = max(r["start_ns"] + r["fct_ns"] for r in rows)
        standalone = max(r["standalone_ns"] for r in rows)
        slowdowns.append(max(1.0, (done - start) / standalone))
    return slowdowns


def convergence_time_us(source_rates):
    rates = [r for r in source_rates if r["active_qps"] > 0]
    if len(rates) < 4:
        return 0.0
    values = [r["aggregate_rate_gbps"] for r in rates]
    peak = max(values)
    if peak <= 0:
        return 0.0
    threshold = 0.9 * peak
    seen_high = False
    first_drop = None
    for r in rates:
        if r["aggregate_rate_gbps"] >= threshold:
            seen_high = True
        elif seen_high:
            first_drop = r
            break
    if first_drop is None:
        return 0.0
    for r in rates:
        if r["time_ns"] > first_drop["time_ns"] and r["aggregate_rate_gbps"] >= threshold:
            return (r["time_ns"] - first_drop["time_ns"]) / 1000.0
    return 0.0


def write_csv(path, rows, fieldnames):
    with open(path, "w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description="Summarize one Stage-5 run.")
    parser.add_argument("--id", required=True)
    parser.add_argument("--scheme", required=True)
    parser.add_argument("--workload", required=True)
    parser.add_argument("--output-dir", default="mix/output")
    parser.add_argument("--k1-kb", type=float, default=5.0)
    parser.add_argument("--k2-kb", type=float, default=200.0)
    parser.add_argument("--s", type=float, default=1.0)
    parser.add_argument("--u", type=float, default=0.9)
    args = parser.parse_args()

    run_dir = os.path.join(args.output_dir, args.id)
    fct = read_fct(os.path.join(run_dir, "{}_out_fct.txt".format(args.id)))
    queues = read_queue(os.path.join(run_dir, "{}_out_qlen.txt".format(args.id)))
    util = read_link_util(os.path.join(run_dir, "{}_out_rate.txt".format(args.id)))
    source_rates = read_source_rate(os.path.join(run_dir, "{}_out_source_rate.txt".format(args.id)))
    rct = rct_slowdown(fct)
    slowdowns = [r["slowdown"] for r in fct]

    metric = {
        "run_id": args.id,
        "scheme": args.scheme,
        "workload": args.workload,
        "k1_kb": args.k1_kb,
        "k2_kb": args.k2_kb,
        "s": args.s,
        "u": args.u,
        "flow_count": len(fct),
        "avg_fct_slowdown": float(np.average(slowdowns)) if slowdowns else 0.0,
        "p99_fct_slowdown": percentile(slowdowns, 99),
        "avg_rct_slowdown": float(np.average(rct)) if rct else 0.0,
        "p99_rct_slowdown": percentile(rct, 99),
        "avg_queue_bytes": float(np.average(queues)) if queues else 0.0,
        "p99_queue_bytes": percentile(queues, 99),
        "max_queue_bytes": max(queues) if queues else 0,
        "avg_link_utilization": float(np.average(util)) if util else 0.0,
        "p99_link_utilization": percentile(util, 99),
        "convergence_time_us": convergence_time_us(source_rates),
    }
    write_csv(os.path.join(run_dir, "stage5_metrics.csv"), [metric], list(metric.keys()))
    print(metric)


if __name__ == "__main__":
    main()
