#!/usr/bin/env python3
import argparse
import csv
import os
from collections import defaultdict

import numpy as np


def percentile(values, pct):
    if not values:
        return 0.0
    return float(np.percentile(values, pct))


def read_fct(path):
    fct_us = []
    slowdown = []
    if not os.path.exists(path):
        return fct_us, slowdown
    with open(path) as fin:
        for line in fin:
            fields = line.split()
            if len(fields) < 8:
                continue
            fct_ns = int(fields[6])
            standalone_ns = max(1, int(fields[7]))
            fct_us.append(fct_ns / 1000.0)
            slowdown.append(max(1.0, fct_ns / standalone_ns))
    return fct_us, slowdown


def read_queue(path):
    by_time = defaultdict(lambda: [0, 0])
    queue_values = []
    if not os.path.exists(path):
        return queue_values, []
    with open(path) as fin:
        for line in fin:
            fields = line.strip().split(",")
            if len(fields) != 6:
                continue
            t = int(fields[0])
            q_bytes = int(fields[4])
            total_bytes = int(fields[5])
            queue_values.append(q_bytes)
            by_time[t][0] = max(by_time[t][0], q_bytes)
            by_time[t][1] = max(by_time[t][1], total_bytes)
    rows = [
        {"time_ns": t, "max_queue_bytes": vals[0], "max_total_buffer_bytes": vals[1]}
        for t, vals in sorted(by_time.items())
    ]
    return queue_values, rows


def read_rate(path):
    raw = defaultdict(dict)
    if not os.path.exists(path):
        return []
    with open(path) as fin:
        for line in fin:
            fields = line.strip().split(",")
            if len(fields) != 5:
                continue
            t = int(fields[0])
            key = (int(fields[1]), int(fields[2]))
            raw[key][t] = (int(fields[3]), int(fields[4]))

    aggregate = defaultdict(lambda: [0.0, 0.0])
    for samples in raw.values():
        times = sorted(samples)
        for prev, cur in zip(times, times[1:]):
            prev_bytes, _ = samples[prev]
            cur_bytes, rate_bps = samples[cur]
            delta_bytes = max(0, cur_bytes - prev_bytes)
            delta_ns = max(1, cur - prev)
            gbps = delta_bytes * 8.0 / delta_ns
            aggregate[cur][0] += gbps
            aggregate[cur][1] += rate_bps / 1e9

    rows = []
    for t, (gbps, capacity_gbps) in sorted(aggregate.items()):
        rows.append({
            "time_ns": t,
            "aggregate_sending_gbps": gbps,
            "aggregate_capacity_gbps": capacity_gbps,
            "aggregate_utilization": gbps / capacity_gbps if capacity_gbps else 0.0,
        })
    return rows


def write_csv(path, rows, fieldnames):
    with open(path, "w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description="Summarize a BCC/DCQCN baseline run.")
    parser.add_argument("--id", required=True)
    parser.add_argument("--output-dir", default="mix/output")
    args = parser.parse_args()

    run_dir = os.path.join(args.output_dir, args.id)
    fct_us, slowdown = read_fct(os.path.join(run_dir, "{}_out_fct.txt".format(args.id)))
    queue_values, queue_rows = read_queue(os.path.join(run_dir, "{}_out_qlen.txt".format(args.id)))
    rate_rows = read_rate(os.path.join(run_dir, "{}_out_rate.txt".format(args.id)))

    metric = {
        "run_id": args.id,
        "flow_count": len(fct_us),
        "avg_fct_us": float(np.average(fct_us)) if fct_us else 0.0,
        "p99_fct_us": percentile(fct_us, 99),
        "avg_slowdown": float(np.average(slowdown)) if slowdown else 0.0,
        "p99_slowdown": percentile(slowdown, 99),
        "avg_active_queue_bytes": float(np.average(queue_values)) if queue_values else 0.0,
        "p99_active_queue_bytes": percentile(queue_values, 99),
        "max_queue_bytes": max(queue_values) if queue_values else 0,
        "avg_aggregate_sending_gbps": float(np.average([r["aggregate_sending_gbps"] for r in rate_rows])) if rate_rows else 0.0,
        "avg_aggregate_utilization": float(np.average([r["aggregate_utilization"] for r in rate_rows])) if rate_rows else 0.0,
    }

    write_csv(os.path.join(run_dir, "stage1_metrics.csv"), [metric], list(metric.keys()))
    write_csv(os.path.join(run_dir, "stage1_queue_timeseries.csv"), queue_rows,
              ["time_ns", "max_queue_bytes", "max_total_buffer_bytes"])
    write_csv(os.path.join(run_dir, "stage1_rate_timeseries.csv"), rate_rows,
              ["time_ns", "aggregate_sending_gbps", "aggregate_capacity_gbps", "aggregate_utilization"])
    print("summary -> {}".format(run_dir))
    print(metric)


if __name__ == "__main__":
    main()
