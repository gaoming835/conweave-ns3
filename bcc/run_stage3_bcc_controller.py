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


def run_scheme(args, scheme, trace):
    before = list_run_dirs()
    run([
        sys.executable, "run.py",
        "--cc", scheme,
        "--lb", "fecmp",
        "--pfc", "1",
        "--irn", "0",
        "--simul_time", str(args.simul_time),
        "--buffer", "16",
        "--netload", str(args.netload),
        "--bw", "25",
        "--topo", "bcc_single_switch_5_25G_OS1",
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
        "--sw_monitoring_interval", "1000",
    ])
    run_id = pick_new_run(before)
    run([sys.executable, "bcc/summarize_run.py", "--id", run_id])
    run([sys.executable, "bcc/plot_stage1.py", "--run-dir", os.path.join("mix/output", run_id)])
    if scheme == "bcc":
        run([sys.executable, "bcc/summarize_bcc_states.py", "--id", run_id])
        run([sys.executable, "bcc/plot_stage2_bcc_states.py",
             "--run-dir", os.path.join("mix/output", run_id)])
    return run_id


def main():
    parser = argparse.ArgumentParser(description="Run stage-3 BCC source controller smoke test.")
    parser.add_argument("--simul-time", type=float, default=0.01)
    parser.add_argument("--netload", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--control-period-us", type=float, default=55.0)
    parser.add_argument("--md-factor", type=float, default=0.1)
    args = parser.parse_args()

    run([sys.executable, "bcc/gen_topologies.py"])
    trace = "config/bcc_stage3_small_incast-mix_{}ms_seed{}.txt".format(
        int(args.simul_time * 1000), args.seed)
    run([
        sys.executable, "bcc/gen_workload.py",
        "--hosts", "5",
        "--kind", "incast-mix",
        "--duration", str(args.simul_time),
        "--load", str(args.netload / 100.0),
        "--bandwidth-gbps", "25",
        "--seed", str(args.seed),
        "--output", trace,
    ])

    dcqcn_id = run_scheme(args, "dcqcn", trace)
    bcc_id = run_scheme(args, "bcc", trace)
    run([sys.executable, "bcc/summarize_stage3_compare.py",
         "--dcqcn-id", dcqcn_id, "--bcc-id", bcc_id])
    run([sys.executable, "bcc/plot_stage3_compare.py",
         "--dcqcn-id", dcqcn_id, "--bcc-id", bcc_id])
    print("stage3_dcqcn_run_id={}".format(dcqcn_id))
    print("stage3_bcc_run_id={}".format(bcc_id))


if __name__ == "__main__":
    main()
