#!/usr/bin/env python3
import argparse
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


def main():
    parser = argparse.ArgumentParser(description="Run BCC stage-1 DCQCN/ECN baseline.")
    parser.add_argument("--testbed", choices=["small", "fat320"], default="small")
    parser.add_argument("--workload", choices=["rpc", "webserver", "incast-mix"], default="incast-mix")
    parser.add_argument("--simul-time", type=float, default=0.01)
    parser.add_argument("--netload", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--skip-sim", action="store_true")
    args = parser.parse_args()

    run([sys.executable, "bcc/gen_topologies.py"])

    if args.testbed == "small":
        topo = "bcc_single_switch_5_25G_OS1"
        hosts = 5
        monitor_interval_ns = 1000
    else:
        topo = "bcc_fat_320_25G_400G_OS1"
        hosts = 320
        monitor_interval_ns = 10000

    trace = "config/bcc_{}_{}_{}ms_seed{}.txt".format(
        args.testbed, args.workload, int(args.simul_time * 1000), args.seed)
    run([
        sys.executable, "bcc/gen_workload.py",
        "--hosts", str(hosts),
        "--kind", args.workload,
        "--duration", str(args.simul_time),
        "--load", str(args.netload / 100.0),
        "--bandwidth-gbps", "25",
        "--seed", str(args.seed),
        "--output", trace,
    ])

    if args.skip_sim:
        return

    before = list_run_dirs()
    run([
        sys.executable, "run.py",
        "--cc", "dcqcn",
        "--lb", "fecmp",
        "--pfc", "1",
        "--irn", "0",
        "--simul_time", str(args.simul_time),
        "--buffer", "16",
        "--netload", str(args.netload),
        "--bw", "25",
        "--topo", topo,
        "--flow_file", trace,
        "--ecn_kmin_kb", "5",
        "--ecn_kmax_kb", "200",
        "--ecn_pmax", "1.0",
        "--dcqcn_ti_us", "55",
        "--dcqcn_td_us", "50",
        "--sw_monitoring_interval", str(monitor_interval_ns),
    ])
    run_id = pick_new_run(before)
    run([sys.executable, "bcc/summarize_run.py", "--id", run_id])
    run([sys.executable, "bcc/plot_stage1.py", "--run-dir", os.path.join("mix/output", run_id)])
    print("stage1_run_id={}".format(run_id))


if __name__ == "__main__":
    main()
