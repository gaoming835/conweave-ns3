#!/usr/bin/env python3
import argparse
import csv
import os

import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser(description="Plot BCC stage-2 state fractions.")
    parser.add_argument("--run-dir", required=True)
    args = parser.parse_args()

    summary_file = os.path.join(args.run_dir, "stage2_bcc_state_summary.csv")
    with open(summary_file) as fin:
        rows = list(csv.DictReader(fin))

    states = [row["state"] for row in rows]
    fractions = [float(row["fraction"]) for row in rows]

    fig_dir = os.path.join(args.run_dir, "figures")
    os.makedirs(fig_dir, exist_ok=True)
    plt.figure(figsize=(5, 3))
    plt.bar(states, fractions)
    plt.xlabel("BCC state")
    plt.ylabel("sample fraction")
    plt.ylim(0, 1)
    plt.tight_layout()
    plt.savefig(os.path.join(fig_dir, "bcc_state_fraction.png"), dpi=160)
    plt.close()
    print("figure -> {}".format(os.path.join(fig_dir, "bcc_state_fraction.png")))


if __name__ == "__main__":
    main()
