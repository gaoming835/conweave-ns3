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


def require_nonempty(path, label):
    if not os.path.exists(path):
        raise RuntimeError("missing {}: {}".format(label, path))
    if os.path.getsize(path) == 0:
        raise RuntimeError("empty {}: {}".format(label, path))


def read_config_value(config_path, key):
    with open(config_path) as fin:
        for line in fin:
            fields = line.strip().split()
            if len(fields) >= 2 and fields[0] == key:
                return fields[1]
    raise RuntimeError("missing {} in {}".format(key, config_path))


def verify_phase0_outputs(run_id):
    run_dir = os.path.join("mix/output", run_id)
    config_path = os.path.join(run_dir, "config.txt")
    require_nonempty(config_path, "config")

    cc_mode = read_config_value(config_path, "CC_MODE")
    enable_bcc = read_config_value(config_path, "ENABLE_BCC")
    if cc_mode != "10" or enable_bcc != "1":
        raise RuntimeError(
            "Phase-0 smoke requires CC_MODE 10 and ENABLE_BCC 1; got CC_MODE {} ENABLE_BCC {}".format(
                cc_mode, enable_bcc))

    checks = [
        ("FCT log", "{}_out_fct.txt".format(run_id)),
        ("queue log", "{}_out_qlen.txt".format(run_id)),
        ("switch rate log", "{}_out_rate.txt".format(run_id)),
        ("source rate log", "{}_out_source_rate.txt".format(run_id)),
        ("BCC state log", "{}_out_bcc_state.txt".format(run_id)),
        ("BCC TCM log", "{}_out_bcc_tcm.txt".format(run_id)),
        ("FCT metrics CSV", "stage1_metrics.csv"),
        ("queue CSV", "stage1_queue_timeseries.csv"),
        ("rate CSV", "stage1_rate_timeseries.csv"),
        ("BCC state summary CSV", "stage2_bcc_state_summary.csv"),
        ("Fig.5-style rate CSV", "rate-vs-time.csv"),
        ("Fig.5-style queue CSV", "queue-vs-time.csv"),
    ]
    for label, name in checks:
        require_nonempty(os.path.join(run_dir, name), label)


def main():
    parser = argparse.ArgumentParser(description="Run the Phase-0 BCC guardrail smoke test.")
    parser.add_argument("--simul-time", type=float, default=0.01)
    parser.add_argument("--netload", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--control-period-us", type=float, default=55.0)
    parser.add_argument("--md-factor", type=float, default=0.1)
    parser.add_argument("--monitor-interval-ns", type=int, default=1000)
    args = parser.parse_args()

    run([sys.executable, "bcc/gen_topologies.py"])
    trace = "config/bcc_phase0_smoke_{}ms_seed{}.txt".format(
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

    before = list_run_dirs()
    run([
        sys.executable, "run.py",
        "--cc", "bcc",
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
        "--enable_bcc", "1",
        "--bcc_u", "0.9",
        "--bcc_s", "1.0",
        "--bcc_control_period_us", str(args.control_period_us),
        "--bcc_md_factor", str(args.md_factor),
        "--sw_monitoring_interval", str(args.monitor_interval_ns),
    ])
    run_id = pick_new_run(before)

    run([sys.executable, "bcc/summarize_run.py", "--id", run_id])
    run([sys.executable, "bcc/summarize_bcc_states.py", "--id", run_id])
    run([sys.executable, "bcc/export_stage4_timeseries.py", "--id", run_id,
         "--scheme", "bcc", "--switch-id", "5", "--out-dev", "5",
         "--capacity-gbps", "25"])
    verify_phase0_outputs(run_id)

    print("phase0_run_id={}".format(run_id))
    print("phase0_run_dir={}".format(os.path.join("mix/output", run_id)))
    print("phase0_status=pass")


if __name__ == "__main__":
    main()
