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


def main():
    parser = argparse.ArgumentParser(description="Plot BCC stage-1 baseline CSVs.")
    parser.add_argument("--run-dir", required=True)
    args = parser.parse_args()

    queue = read_csv(os.path.join(args.run_dir, "stage1_queue_timeseries.csv"))
    rate = read_csv(os.path.join(args.run_dir, "stage1_rate_timeseries.csv"))
    fig_dir = os.path.join(args.run_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)

    if queue:
        xs = [(int(r["time_ns"]) - int(queue[0]["time_ns"])) / 1000.0 for r in queue]
        ys = [int(r["max_queue_bytes"]) / 1000.0 for r in queue]
        plt.figure(figsize=(7, 3))
        plt.plot(xs, ys)
        plt.xlabel("time since monitor start (us)")
        plt.ylabel("max egress queue (KB)")
        plt.tight_layout()
        plt.savefig(os.path.join(fig_dir, "queue_length.png"), dpi=160)
        plt.close()

    if rate:
        xs = [(int(r["time_ns"]) - int(rate[0]["time_ns"])) / 1000.0 for r in rate]
        ys = [float(r["aggregate_sending_gbps"]) for r in rate]
        plt.figure(figsize=(7, 3))
        plt.plot(xs, ys)
        plt.xlabel("time since monitor start (us)")
        plt.ylabel("aggregate sending rate (Gbps)")
        plt.tight_layout()
        plt.savefig(os.path.join(fig_dir, "aggregate_sending_rate.png"), dpi=160)
        plt.close()

    print("figures -> {}".format(fig_dir))


if __name__ == "__main__":
    main()
