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


def write_topology(nic_gbps):
    topo = "bcc_stage4_single_switch_5_{}G_OS1".format(int(nic_gbps))
    path = os.path.join("config", topo + ".txt")
    os.makedirs("config", exist_ok=True)
    with open(path, "w") as fout:
        fout.write("6 1 5\n")
        fout.write("5\n")
        for host in range(5):
            fout.write("{} 5 {}Gbps 1000ns 0.000000\n".format(host, int(nic_gbps)))
    print("stage4 topology -> {}".format(path))
    return topo


def run_scheme(args, scheme, topo, trace):
    before = list_run_dirs()
    run([
        sys.executable, "run.py",
        "--cc", scheme,
        "--lb", "fecmp",
        "--pfc", "1",
        "--irn", "0",
        "--simul_time", str(args.simul_time),
        "--buffer", "16",
        "--netload", "40",
        "--bw", str(int(args.nic_gbps)),
        "--topo", topo,
        "--flow_file", trace,
        "--ecn_kmin_kb", "5",
        "--ecn_kmax_kb", "200",
        "--ecn_pmax", "1.0",
        "--dcqcn_ti_us", "55",
        "--dcqcn_td_us", "50",
        "--enable_bcc", "1" if scheme == "bcc" else "0",
        "--bcc_u", "0.9",
        "--bcc_s", "1.0",
        "--bcc_control_period_us", str(args.control_period_us),
        "--bcc_md_factor", str(args.md_factor),
        "--sw_monitoring_interval", str(args.monitor_interval_ns),
        "--skip_fct_analysis", "1",
    ])
    run_id = pick_new_run(before)
    run([sys.executable, "bcc/export_stage4_timeseries.py", "--id", run_id,
         "--scheme", scheme, "--switch-id", "5", "--out-dev", "5",
         "--capacity-gbps", str(args.nic_gbps)])
    return run_id


def combine_csv(compare_dir, runs, name):
    rows = []
    for scheme, run_id in runs:
        path = os.path.join("mix/output", run_id, name)
        with open(path) as fin:
            rows.extend(csv.DictReader(fin))
    out = os.path.join(compare_dir, name)
    with open(out, "w", newline="") as fout:
        writer = csv.DictWriter(fout, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description="Run Stage-4 minimal BCC Fig.5-style experiment.")
    parser.add_argument("--simul-time", type=float, default=0.04)
    parser.add_argument("--nic-gbps", type=float, choices=[10.0, 25.0], default=25.0)
    parser.add_argument("--transient-time-us", type=float, default=8000.0)
    parser.add_argument("--transient-size-bytes", type=int, default=2 * 1024 * 1024)
    parser.add_argument("--control-period-us", type=float, default=55.0)
    parser.add_argument("--md-factor", type=float, default=0.1)
    parser.add_argument("--monitor-interval-ns", type=int, default=1000)
    args = parser.parse_args()

    topo = write_topology(args.nic_gbps)
    trace = "config/bcc_stage4_fig5_{}G_{}ms.txt".format(
        int(args.nic_gbps), int(args.simul_time * 1000))
    run([
        sys.executable, "bcc/gen_stage4_fig5_workload.py",
        "--duration", str(args.simul_time),
        "--nic-gbps", str(args.nic_gbps),
        "--transient-time-us", str(args.transient_time_us),
        "--transient-size-bytes", str(args.transient_size_bytes),
        "--output", trace,
    ])

    dcqcn_id = run_scheme(args, "dcqcn", topo, trace)
    bcc_id = run_scheme(args, "bcc", topo, trace)

    compare_dir = os.path.join("mix/output", "stage4_minimal_{}_{}".format(dcqcn_id, bcc_id))
    os.makedirs(compare_dir, exist_ok=True)
    combine_csv(compare_dir, [("dcqcn", dcqcn_id), ("bcc", bcc_id)], "rate-vs-time.csv")
    combine_csv(compare_dir, [("dcqcn", dcqcn_id), ("bcc", bcc_id)], "queue-vs-time.csv")
    run([sys.executable, "bcc/plot_stage4_minimal.py",
         "--dcqcn-id", dcqcn_id, "--bcc-id", bcc_id])
    print("stage4_dcqcn_run_id={}".format(dcqcn_id))
    print("stage4_bcc_run_id={}".format(bcc_id))
    print("stage4_compare_dir={}".format(compare_dir))


if __name__ == "__main__":
    main()
