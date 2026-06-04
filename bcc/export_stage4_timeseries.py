#!/usr/bin/env python3
import argparse
import csv
import os
from collections import defaultdict


def read_rate(path, switch_id, out_dev):
    samples = {}
    capacity_bps = 0
    if not os.path.exists(path):
        return []
    with open(path) as fin:
        for line in fin:
            fields = line.strip().split(",")
            if len(fields) != 5:
                continue
            t, sw, port, tx_bytes, rate_bps = fields
            if int(sw) != switch_id or int(port) != out_dev:
                continue
            samples[int(t)] = (int(tx_bytes), int(rate_bps))
            capacity_bps = int(rate_bps)

    rows = []
    times = sorted(samples)
    start = times[0] if times else 0
    for prev, cur in zip(times, times[1:]):
        prev_bytes, _ = samples[prev]
        cur_bytes, _ = samples[cur]
        delta_bytes = max(0, cur_bytes - prev_bytes)
        delta_ns = max(1, cur - prev)
        gbps = delta_bytes * 8.0 / delta_ns
        rows.append({
            "time_ns": cur,
            "time_us": (cur - start) / 1000.0,
            "aggregate_sending_gbps": gbps,
            "bottleneck_capacity_gbps": capacity_bps / 1e9 if capacity_bps else 0.0,
            "utilization": gbps / (capacity_bps / 1e9) if capacity_bps else 0.0,
        })
    return rows


def read_source_rate(path, capacity_gbps):
    rows = []
    if not os.path.exists(path):
        return rows
    start = None
    with open(path) as fin:
        for line in fin:
            fields = line.strip().split(",")
            if len(fields) != 4:
                continue
            t = int(fields[0])
            aggregate_bps = int(fields[1])
            if start is None:
                start = t
            gbps = aggregate_bps / 1e9
            rows.append({
                "time_ns": t,
                "time_us": (t - start) / 1000.0,
                "aggregate_sending_gbps": gbps,
                "bottleneck_capacity_gbps": capacity_gbps,
                "utilization": gbps / capacity_gbps if capacity_gbps else 0.0,
            })
    return rows


def read_queue(path, switch_id, out_dev, sample_times):
    by_time = defaultdict(int)
    if os.path.exists(path):
        with open(path) as fin:
            for line in fin:
                fields = line.strip().split(",")
                if len(fields) != 6:
                    continue
                t, sw, port, _q, q_bytes, _total = fields
                if int(sw) == switch_id and int(port) == out_dev:
                    by_time[int(t)] = max(by_time[int(t)], int(q_bytes))

    if sample_times:
        start = sample_times[0]
        times = sample_times
    else:
        times = sorted(by_time)
        start = times[0] if times else 0
    return [{
        "time_ns": t,
        "time_us": (t - start) / 1000.0,
        "queue_bytes": by_time.get(t, 0),
    } for t in times]


def write_csv(path, rows, fields):
    with open(path, "w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description="Export Stage-4 bottleneck rate/queue CSVs.")
    parser.add_argument("--id", required=True)
    parser.add_argument("--scheme", required=True)
    parser.add_argument("--output-dir", default="mix/output")
    parser.add_argument("--switch-id", type=int, default=5)
    parser.add_argument("--out-dev", type=int, default=5)
    parser.add_argument("--capacity-gbps", type=float, default=25.0)
    args = parser.parse_args()

    run_dir = os.path.join(args.output_dir, args.id)
    rate_rows = read_source_rate(os.path.join(run_dir, "{}_out_source_rate.txt".format(args.id)),
                                 args.capacity_gbps)
    if not rate_rows:
        rate_rows = read_rate(os.path.join(run_dir, "{}_out_rate.txt".format(args.id)),
                              args.switch_id, args.out_dev)
    queue_rows = read_queue(os.path.join(run_dir, "{}_out_qlen.txt".format(args.id)),
                            args.switch_id, args.out_dev,
                            [int(r["time_ns"]) for r in rate_rows])

    for row in rate_rows:
        row["scheme"] = args.scheme
    for row in queue_rows:
        row["scheme"] = args.scheme

    write_csv(os.path.join(run_dir, "rate-vs-time.csv"), rate_rows,
              ["scheme", "time_ns", "time_us", "aggregate_sending_gbps",
               "bottleneck_capacity_gbps", "utilization"])
    write_csv(os.path.join(run_dir, "queue-vs-time.csv"), queue_rows,
              ["scheme", "time_ns", "time_us", "queue_bytes"])
    print("stage4 csv -> {}".format(run_dir))


if __name__ == "__main__":
    main()
