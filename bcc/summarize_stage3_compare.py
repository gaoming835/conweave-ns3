#!/usr/bin/env python3
import argparse
import csv
import os


def read_one(path):
    if not os.path.exists(path):
        return {}
    with open(path) as fin:
        rows = list(csv.DictReader(fin))
    return rows[0] if rows else {}


def main():
    parser = argparse.ArgumentParser(description="Compare stage-3 DCQCN and BCC metrics.")
    parser.add_argument("--dcqcn-id", required=True)
    parser.add_argument("--bcc-id", required=True)
    parser.add_argument("--output-dir", default="mix/output")
    args = parser.parse_args()

    rows = []
    for label, run_id in [("dcqcn", args.dcqcn_id), ("bcc", args.bcc_id)]:
        run_dir = os.path.join(args.output_dir, run_id)
        metric = read_one(os.path.join(run_dir, "stage1_metrics.csv"))
        if not metric:
            continue
        metric["scheme"] = label
        rows.append(metric)

    compare_dir = os.path.join(args.output_dir, "stage3_compare_{}_{}".format(
        args.dcqcn_id, args.bcc_id))
    os.makedirs(compare_dir, exist_ok=True)
    out = os.path.join(compare_dir, "stage3_compare_metrics.csv")
    fieldnames = ["scheme", "run_id", "flow_count", "avg_fct_us", "p99_fct_us",
                  "avg_slowdown", "p99_slowdown", "avg_active_queue_bytes",
                  "p99_active_queue_bytes", "max_queue_bytes",
                  "avg_aggregate_sending_gbps", "avg_aggregate_utilization"]
    with open(out, "w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print("stage3_compare_dir={}".format(compare_dir))
    print("stage3_compare_metrics={}".format(out))


if __name__ == "__main__":
    main()
