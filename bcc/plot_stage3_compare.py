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


def plot_timeseries(rows_by_scheme, field, ylabel, output):
    plt.figure(figsize=(7, 3))
    for scheme, rows in rows_by_scheme.items():
        if not rows:
            continue
        start = int(rows[0]["time_ns"])
        xs = [(int(r["time_ns"]) - start) / 1000.0 for r in rows]
        ys = [float(r[field]) for r in rows]
        plt.plot(xs, ys, label=scheme)
    plt.xlabel("time since monitor start (us)")
    plt.ylabel(ylabel)
    plt.legend()
    plt.tight_layout()
    plt.savefig(output, dpi=160)
    plt.close()


def main():
    parser = argparse.ArgumentParser(description="Plot stage-3 BCC/DCQCN comparison.")
    parser.add_argument("--dcqcn-id", required=True)
    parser.add_argument("--bcc-id", required=True)
    parser.add_argument("--output-dir", default="mix/output")
    args = parser.parse_args()

    compare_dir = os.path.join(args.output_dir, "stage3_compare_{}_{}".format(
        args.dcqcn_id, args.bcc_id))
    fig_dir = os.path.join(compare_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    queue = {
        "dcqcn": read_csv(os.path.join(args.output_dir, args.dcqcn_id, "stage1_queue_timeseries.csv")),
        "bcc": read_csv(os.path.join(args.output_dir, args.bcc_id, "stage1_queue_timeseries.csv")),
    }
    rate = {
        "dcqcn": read_csv(os.path.join(args.output_dir, args.dcqcn_id, "stage1_rate_timeseries.csv")),
        "bcc": read_csv(os.path.join(args.output_dir, args.bcc_id, "stage1_rate_timeseries.csv")),
    }
    plot_timeseries(queue, "max_queue_bytes", "max egress queue (bytes)",
                    os.path.join(fig_dir, "queue_length_compare.png"))
    plot_timeseries(rate, "aggregate_sending_gbps", "aggregate sending rate (Gbps)",
                    os.path.join(fig_dir, "aggregate_sending_rate_compare.png"))
    print("figures -> {}".format(fig_dir))


if __name__ == "__main__":
    main()
