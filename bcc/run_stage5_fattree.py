#!/usr/bin/env python3
import argparse
import csv
import os
import subprocess
import sys


def run(cmd):
    print("+ {}".format(" ".join(cmd)))
    subprocess.check_call(cmd)


def list_run_dirs():
    root = "mix/output"
    if not os.path.isdir(root):
        return set()
    return {d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))}


def pick_new_run(before):
    after = list_run_dirs()
    new_dirs = list(after - before)
    if new_dirs:
        return max(new_dirs, key=lambda d: os.path.getmtime(os.path.join("mix/output", d)))
    return max(after, key=lambda d: os.path.getmtime(os.path.join("mix/output", d)))


def parse_list(value):
    return [x.strip() for x in value.split(",") if x.strip()]


def read_one_metric(run_id):
    path = os.path.join("mix/output", run_id, "stage5_metrics.csv")
    with open(path) as fin:
        return list(csv.DictReader(fin))[0]


def run_one(args, workload, scheme, trace, params):
    before = list_run_dirs()
    run([
        sys.executable, "run.py",
        "--cc", scheme,
        "--lb", "fecmp",
        "--pfc", "1",
        "--irn", "0",
        "--simul_time", str(args.duration),
        "--buffer", "16",
        "--netload", str(args.netload),
        "--bw", "25",
        "--topo", "bcc_fat_320_25G_400G_OS1",
        "--flow_file", trace,
        "--ecn_kmin_kb", str(int(params["k1"])),
        "--ecn_kmax_kb", str(int(params["k2"])),
        "--ecn_pmax", "1.0",
        "--dcqcn_ti_us", "55",
        "--dcqcn_td_us", "50",
        "--enable_bcc", "1" if scheme == "bcc" else "0",
        "--bcc_u", str(params["u"]),
        "--bcc_s", str(params["s"]),
        "--bcc_control_period_us", "55",
        "--bcc_md_factor", "0.1",
        "--sw_monitoring_interval", str(args.monitor_interval_ns),
        "--skip_fct_analysis", "1",
    ])
    run_id = pick_new_run(before)
    run([sys.executable, "bcc/summarize_stage5_run.py",
         "--id", run_id, "--scheme", scheme, "--workload", workload,
         "--k1-kb", str(params["k1"]), "--k2-kb", str(params["k2"]),
         "--s", str(params["s"]), "--u", str(params["u"])])
    return run_id


def write_summary(rows, output_dir):
    os.makedirs(output_dir, exist_ok=True)
    out = os.path.join(output_dir, "stage5_summary.csv")
    if not rows:
        return out
    with open(out, "w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    return out


def main():
    parser = argparse.ArgumentParser(description="Run Stage-5 Fat-Tree BCC experiments.")
    parser.add_argument("--workloads", default="rpc,webserver,incast-mix")
    parser.add_argument("--schemes", default="dcqcn,bcc")
    parser.add_argument("--duration", type=float, default=0.01)
    parser.add_argument("--load", type=float, default=0.01)
    parser.add_argument("--netload", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-background-flows", type=int, default=20000)
    parser.add_argument("--monitor-interval-ns", type=int, default=10000)
    parser.add_argument("--include-sensitivity", action="store_true")
    parser.add_argument("--sensitivity-workload", default="incast-mix")
    parser.add_argument("--output-dir", default=None)
    args = parser.parse_args()

    run([sys.executable, "bcc/gen_topologies.py"])
    workloads = parse_list(args.workloads)
    schemes = parse_list(args.schemes)
    params = {"k1": 5, "k2": 200, "s": 1.0, "u": 0.9}
    rows = []
    run_records = []

    for workload in workloads:
        trace = "config/bcc_stage5_{}_{}ms_seed{}.txt".format(
            workload, int(args.duration * 1000), args.seed)
        meta = trace.replace(".txt", "_meta.csv")
        run([
            sys.executable, "bcc/gen_stage5_workload.py",
            "--hosts", "320",
            "--kind", workload,
            "--duration", str(args.duration),
            "--load", str(args.load),
            "--seed", str(args.seed),
            "--max-background-flows", str(args.max_background_flows),
            "--output", trace,
            "--metadata", meta,
        ])
        for scheme in schemes:
            run_id = run_one(args, workload, scheme, trace, params)
            rows.append(read_one_metric(run_id))
            run_records.append({"workload": workload, "scheme": scheme, "run_id": run_id,
                                "trace": trace, "metadata": meta})

    if args.include_sensitivity:
        workload = args.sensitivity_workload
        trace = "config/bcc_stage5_{}_{}ms_seed{}.txt".format(
            workload, int(args.duration * 1000), args.seed)
        meta = trace.replace(".txt", "_meta.csv")
        if not os.path.exists(trace):
            run([
                sys.executable, "bcc/gen_stage5_workload.py",
                "--hosts", "320", "--kind", workload,
                "--duration", str(args.duration), "--load", str(args.load),
                "--seed", str(args.seed),
                "--max-background-flows", str(args.max_background_flows),
                "--output", trace, "--metadata", meta,
            ])
        sensitivity = []
        for key, vals in {
            "k1": [2, 5, 10],
            "k2": [100, 200, 400],
            "s": [0.5, 1.0, 2.0],
            "u": [0.8, 0.9, 0.95],
        }.items():
            for val in vals:
                p = dict(params)
                p[key] = val
                if p in sensitivity:
                    continue
                sensitivity.append(p)
        for p in sensitivity:
            run_id = run_one(args, workload, "bcc", trace, p)
            rows.append(read_one_metric(run_id))
            run_records.append({"workload": workload, "scheme": "bcc", "run_id": run_id,
                                "trace": trace, "metadata": meta})

    output_dir = args.output_dir or os.path.join("mix/output", "stage5_fattree")
    summary = write_summary(rows, output_dir)
    records = os.path.join(output_dir, "stage5_runs.csv")
    if run_records:
        with open(records, "w", newline="") as fout:
            writer = csv.DictWriter(fout, fieldnames=list(run_records[0].keys()))
            writer.writeheader()
            writer.writerows(run_records)
    run([sys.executable, "bcc/plot_stage5.py", "--summary", summary, "--output-dir", output_dir])
    print("stage5_summary={}".format(summary))
    print("stage5_runs={}".format(records))


if __name__ == "__main__":
    main()
