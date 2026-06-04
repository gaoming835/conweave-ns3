#!/usr/bin/env python3
import argparse
import csv
import os
from collections import Counter


STATES = ["TU", "TC", "NC", "PC"]


def main():
    parser = argparse.ArgumentParser(description="Summarize BCC switch-state samples.")
    parser.add_argument("--id", required=True)
    parser.add_argument("--output-dir", default="mix/output")
    args = parser.parse_args()

    run_dir = os.path.join(args.output_dir, args.id)
    state_file = os.path.join(run_dir, "{}_out_bcc_state.txt".format(args.id))
    if not os.path.exists(state_file):
        raise RuntimeError("missing BCC state monitor file: {}".format(state_file))

    counts = Counter()
    with open(state_file) as fin:
        for line in fin:
            fields = line.strip().split(",")
            if len(fields) != 8:
                continue
            counts[fields[7]] += 1

    total = sum(counts.values())
    rows = []
    for state in STATES:
        count = counts[state]
        rows.append({
            "state": state,
            "samples": count,
            "fraction": count / total if total else 0.0,
        })

    out_file = os.path.join(run_dir, "stage2_bcc_state_summary.csv")
    with open(out_file, "w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=["state", "samples", "fraction"])
        writer.writeheader()
        writer.writerows(rows)

    print("bcc state summary -> {}".format(out_file))
    for row in rows:
        print(row)


if __name__ == "__main__":
    main()
