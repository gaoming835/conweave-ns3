#!/usr/bin/env python3
import argparse
import csv
import os
from collections import Counter


RAW_FIELDS = [
    "time_ns",
    "node_id",
    "flow_id",
    "final_path_state",
    "source_mode",
    "current_rate_bps",
    "r_hat_bps",
    "pause_ns",
    "resume_ns",
    "inflight_bound",
    "inflight_bytes",
    "last_acked_bytes",
    "last_control_inflight",
    "tu_utilization",
    "mode_transitions",
    "last_mode_transition_ns",
]


def source_mode_name(mode):
    return "TCM" if str(mode) == "0" else "PCM"


def read_rows(path):
    rows = []
    with open(path) as fin:
        for line in fin:
            fields = line.strip().split(",")
            if len(fields) != len(RAW_FIELDS):
                continue
            row = dict(zip(RAW_FIELDS, fields))
            row["source_mode_name"] = source_mode_name(row["source_mode"])
            rows.append(row)
    return rows


def write_csv(path, rows, fields):
    with open(path, "w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def summarize(rows, run_id):
    states = Counter(row["final_path_state"] for row in rows)
    modes = Counter(row["source_mode_name"] for row in rows)
    pause_events = sum(1 for row in rows if int(row["pause_ns"]) > 0)
    max_pause_ns = max([int(row["pause_ns"]) for row in rows] or [0])
    max_resume_ns = max([int(row["resume_ns"]) for row in rows] or [0])
    max_r_hat_bps = max([int(row["r_hat_bps"]) for row in rows] or [0])
    max_inflight_bound = max([int(row["inflight_bound"]) for row in rows] or [0])
    max_mode_transitions = max([int(row["mode_transitions"]) for row in rows] or [0])
    max_tu_utilization = max([float(row["tu_utilization"]) for row in rows] or [0.0])

    summary = {
        "run_id": run_id,
        "samples": len(rows),
        "tcm_samples": modes["TCM"],
        "pcm_samples": modes["PCM"],
        "tc_samples": states["TC"],
        "tu_samples": states["TU"],
        "pc_samples": states["PC"],
        "nc_samples": states["NC"],
        "pause_events": pause_events,
        "max_pause_ns": max_pause_ns,
        "max_resume_ns": max_resume_ns,
        "max_r_hat_bps": max_r_hat_bps,
        "max_inflight_bound": max_inflight_bound,
        "max_mode_transitions": max_mode_transitions,
        "max_tu_utilization": max_tu_utilization,
    }
    return summary


def main():
    parser = argparse.ArgumentParser(description="Export BCC source-controller monitor rows to CSV.")
    parser.add_argument("--id", required=True)
    parser.add_argument("--output-dir", default="mix/output")
    args = parser.parse_args()

    run_dir = os.path.join(args.output_dir, args.id)
    raw_path = os.path.join(run_dir, "{}_out_bcc_tcm.txt".format(args.id))
    if not os.path.exists(raw_path):
        raise RuntimeError("missing BCC TCM monitor file: {}".format(raw_path))

    rows = read_rows(raw_path)
    if not rows:
        raise RuntimeError("empty BCC TCM monitor rows: {}".format(raw_path))

    fields = RAW_FIELDS + ["source_mode_name"]
    out_path = os.path.join(run_dir, "bcc_tcm_timeseries.csv")
    write_csv(out_path, rows, fields)

    summary = summarize(rows, args.id)
    summary_path = os.path.join(run_dir, "bcc_tcm_summary.csv")
    write_csv(summary_path, [summary], list(summary.keys()))

    print("bcc tcm csv -> {}".format(out_path))
    print("bcc tcm summary -> {}".format(summary_path))
    print(summary)


if __name__ == "__main__":
    main()
