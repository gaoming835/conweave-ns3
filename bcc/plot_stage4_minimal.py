#!/usr/bin/env python3
import argparse
import csv
import os

import matplotlib.pyplot as plt


def read_csv(path):
    if not os.path.exists(path):
        return []
    with open(path) as fin:
        return list(csv.DictReader(fin))


def plot(rows_by_scheme, field, ylabel, output):
    plt.figure(figsize=(7, 3))
    capacity = None
    for scheme, rows in rows_by_scheme.items():
        xs = [float(r["time_us"]) for r in rows]
        ys = [float(r[field]) for r in rows]
        if rows and "bottleneck_capacity_gbps" in rows[0]:
            capacity = float(rows[0]["bottleneck_capacity_gbps"])
        plt.plot(xs, ys, label=scheme)
    if capacity and field == "aggregate_sending_gbps":
        plt.axhline(capacity, color="0.35", linestyle="--", linewidth=1, label="bottleneck")
    plt.xlabel("time since flow start (us)")
    plt.ylabel(ylabel)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output, dpi=160)
    plt.close()


def main():
    parser = argparse.ArgumentParser(description="Plot Stage-4 Fig.5-style comparison.")
    parser.add_argument("--dcqcn-id", required=True)
    parser.add_argument("--bcc-id", required=True)
    parser.add_argument("--output-dir", default="mix/output")
    args = parser.parse_args()

    compare_dir = os.path.join(args.output_dir, "stage4_minimal_{}_{}".format(
        args.dcqcn_id, args.bcc_id))
    fig_dir = os.path.join(compare_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)
    rates = {
        "dcqcn": read_csv(os.path.join(args.output_dir, args.dcqcn_id, "rate-vs-time.csv")),
        "bcc": read_csv(os.path.join(args.output_dir, args.bcc_id, "rate-vs-time.csv")),
    }
    queues = {
        "dcqcn": read_csv(os.path.join(args.output_dir, args.dcqcn_id, "queue-vs-time.csv")),
        "bcc": read_csv(os.path.join(args.output_dir, args.bcc_id, "queue-vs-time.csv")),
    }
    plot(rates, "aggregate_sending_gbps", "bottleneck aggregate rate (Gbps)",
         os.path.join(fig_dir, "rate-vs-time.png"))
    plot(queues, "queue_bytes", "bottleneck queue (bytes)",
         os.path.join(fig_dir, "queue-vs-time.png"))
    print("figures -> {}".format(fig_dir))


if __name__ == "__main__":
    main()
