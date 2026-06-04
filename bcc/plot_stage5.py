#!/usr/bin/env python3
import argparse
import csv
import os

import matplotlib.pyplot as plt


def read_rows(path):
    with open(path) as fin:
        return list(csv.DictReader(fin))


def bar(rows, metric, output, ylabel):
    labels = ["{}-{}".format(r["workload"], r["scheme"]) for r in rows]
    values = [float(r[metric]) for r in rows]
    plt.figure(figsize=(max(7, len(labels) * 0.8), 3.2))
    plt.bar(labels, values)
    plt.ylabel(ylabel)
    plt.xticks(rotation=30, ha="right")
    plt.tight_layout()
    plt.savefig(output, dpi=160)
    plt.close()


def sensitivity(rows, output_dir):
    bcc_rows = [r for r in rows if r["scheme"] == "bcc"]
    metrics = [
        ("k1_kb", "p99_fct_slowdown"),
        ("k2_kb", "p99_fct_slowdown"),
        ("s", "p99_fct_slowdown"),
        ("u", "p99_fct_slowdown"),
        ("k1_kb", "p99_queue_bytes"),
        ("k2_kb", "p99_queue_bytes"),
        ("s", "p99_queue_bytes"),
        ("u", "p99_queue_bytes"),
    ]
    for param, metric in metrics:
        data = [(float(r[param]), float(r[metric])) for r in bcc_rows if r.get(param) != ""]
        if len(data) < 2:
            continue
        data.sort()
        xs, ys = zip(*data)
        plt.figure(figsize=(4.5, 3))
        plt.plot(xs, ys, marker="o")
        plt.xlabel(param)
        plt.ylabel(metric)
        plt.tight_layout()
        plt.savefig(os.path.join(output_dir, "sensitivity_{}_{}.png".format(param, metric)), dpi=160)
        plt.close()


def main():
    parser = argparse.ArgumentParser(description="Plot Stage-5 Fat-Tree experiment results.")
    parser.add_argument("--summary", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()

    rows = read_rows(args.summary)
    fig_dir = os.path.join(args.output_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)
    bar(rows, "avg_fct_slowdown", os.path.join(fig_dir, "avg_fct_slowdown.png"),
        "average FCT slowdown")
    bar(rows, "p99_fct_slowdown", os.path.join(fig_dir, "p99_fct_slowdown.png"),
        "99p FCT slowdown")
    bar(rows, "p99_rct_slowdown", os.path.join(fig_dir, "p99_rct_slowdown.png"),
        "99p RCT slowdown")
    bar(rows, "p99_queue_bytes", os.path.join(fig_dir, "p99_queue_occupancy.png"),
        "99p queue bytes")
    bar(rows, "avg_link_utilization", os.path.join(fig_dir, "avg_link_utilization.png"),
        "average link utilization")
    bar(rows, "convergence_time_us", os.path.join(fig_dir, "convergence_time.png"),
        "convergence time (us)")
    sensitivity(rows, fig_dir)
    print("figures -> {}".format(fig_dir))


if __name__ == "__main__":
    main()
