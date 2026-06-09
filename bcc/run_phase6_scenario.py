#!/usr/bin/env python3
import argparse
import os
import subprocess
import sys


BASE_TIME = 2.0
SCENARIOS = ("bcc_smoke", "bcc_tc_incast", "bcc_tu_departure", "bcc_pc_longflows")


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


def write_trace(path, flows):
    flows = sorted(flows, key=lambda row: row[4])
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as fout:
        fout.write("{}\n".format(len(flows)))
        for src, dst, pg, size, time_s in flows:
            fout.write("{} {} {} {} {:.9f}\n".format(src, dst, pg, size, time_s))
    print("flows={} -> {}".format(len(flows), path))


def generate_smoke(args, trace):
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


def generate_tc_incast(args, trace):
    flows = []
    receiver = 4
    for event in range(3):
        t = BASE_TIME + 0.001 + event * 0.0005
        for src in range(4):
            flows.append((src, receiver, 3, args.incast_size_bytes, t))
    write_trace(trace, flows)


def generate_tu_departure(args, trace):
    flows = []
    receiver = 4
    short_size = int(25e9 / 8.0 * args.departure_short_ms / 1000.0 / 4.0)
    long_size = int(25e9 / 8.0 * args.simul_time * 0.8)
    for src in range(4):
        flows.append((src, receiver, 3, short_size, BASE_TIME))
    flows.append((0, receiver, 3, long_size, BASE_TIME))
    write_trace(trace, flows)


def generate_pc_longflows(args, trace):
    receiver = 4
    flow_size = int(25e9 / 8.0 * args.simul_time * 0.7 / 4.0)
    flows = [(src, receiver, 3, flow_size, BASE_TIME) for src in range(4)]
    write_trace(trace, flows)


def scenario_params(args):
    common = {
        "ecn_kmin_kb": "5",
        "ecn_kmax_kb": "200",
        "ecn_pmax": "1.0",
        "bcc_u": "0.9",
        "bcc_s": "1.0",
    }
    if args.scenario == "bcc_tc_incast":
        common.update({"ecn_kmax_kb": "50", "bcc_s": "0.2"})
    elif args.scenario == "bcc_tu_departure":
        common.update({"bcc_u": "0.95", "bcc_s": "10.0"})
    elif args.scenario == "bcc_pc_longflows":
        common.update({"ecn_kmax_kb": "800", "bcc_u": "0.2", "bcc_s": "10.0"})
    return common


def generate_trace(args, trace):
    if args.scenario == "bcc_smoke":
        generate_smoke(args, trace)
    elif args.scenario == "bcc_tc_incast":
        generate_tc_incast(args, trace)
    elif args.scenario == "bcc_tu_departure":
        generate_tu_departure(args, trace)
    elif args.scenario == "bcc_pc_longflows":
        generate_pc_longflows(args, trace)
    else:
        raise RuntimeError("unknown scenario: {}".format(args.scenario))


def require_nonempty(path, label):
    if not os.path.exists(path):
        raise RuntimeError("missing {}: {}".format(label, path))
    if os.path.getsize(path) == 0:
        raise RuntimeError("empty {}: {}".format(label, path))


def verify_outputs(run_id):
    run_dir = os.path.join("mix/output", run_id)
    checks = [
        ("config", "config.txt"),
        ("raw BCC switch state", "{}_out_bcc_state.txt".format(run_id)),
        ("raw BCC source controller", "{}_out_bcc_tcm.txt".format(run_id)),
        ("BCC switch-state summary", "stage2_bcc_state_summary.csv"),
        ("BCC source-controller CSV", "bcc_tcm_timeseries.csv"),
        ("BCC source-controller summary", "bcc_tcm_summary.csv"),
        ("source rate CSV", "rate-vs-time.csv"),
        ("queue CSV", "queue-vs-time.csv"),
    ]
    for label, name in checks:
        require_nonempty(os.path.join(run_dir, name), label)


def main():
    parser = argparse.ArgumentParser(description="Run named Phase-6 BCC scenarios.")
    parser.add_argument("--scenario", choices=SCENARIOS, default="bcc_smoke")
    parser.add_argument("--simul-time", type=float, default=0.02)
    parser.add_argument("--netload", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--monitor-interval-ns", type=int, default=1000)
    parser.add_argument("--control-period-us", type=float, default=55.0)
    parser.add_argument("--md-factor", type=float, default=0.1)
    parser.add_argument("--incast-size-bytes", type=int, default=2 * 1024 * 1024)
    parser.add_argument("--departure-short-ms", type=float, default=5.0)
    parser.add_argument("--skip-fct-analysis", type=int, default=1)
    args = parser.parse_args()

    run([sys.executable, "bcc/gen_topologies.py"])
    trace = "config/bcc_phase6_{}_{}ms_seed{}.txt".format(
        args.scenario, int(args.simul_time * 1000), args.seed)
    generate_trace(args, trace)

    params = scenario_params(args)
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
        "--ecn_kmin_kb", params["ecn_kmin_kb"],
        "--ecn_kmax_kb", params["ecn_kmax_kb"],
        "--ecn_pmax", params["ecn_pmax"],
        "--dcqcn_ti_us", "55",
        "--dcqcn_td_us", "50",
        "--enable_bcc", "1",
        "--ack_high_prio", "1",
        "--bcc_u", params["bcc_u"],
        "--bcc_s", params["bcc_s"],
        "--bcc_control_period_us", str(args.control_period_us),
        "--bcc_md_factor", str(args.md_factor),
        "--sw_monitoring_interval", str(args.monitor_interval_ns),
        "--skip_fct_analysis", str(args.skip_fct_analysis),
    ])
    run_id = pick_new_run(before)

    run([sys.executable, "bcc/summarize_run.py", "--id", run_id])
    run([sys.executable, "bcc/summarize_bcc_states.py", "--id", run_id])
    run([sys.executable, "bcc/export_stage4_timeseries.py", "--id", run_id,
         "--scheme", args.scenario, "--switch-id", "5", "--out-dev", "5",
         "--capacity-gbps", "25"])
    run([sys.executable, "bcc/export_bcc_tcm.py", "--id", run_id])
    verify_outputs(run_id)

    print("phase6_scenario={}".format(args.scenario))
    print("phase6_run_id={}".format(run_id))
    print("phase6_run_dir={}".format(os.path.join("mix/output", run_id)))
    print("phase6_status=pass")


if __name__ == "__main__":
    main()
