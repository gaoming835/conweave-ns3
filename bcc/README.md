# BCC reproduction staging

This directory contains the staged reproduction harness for
`BCC: Re-architecting Congestion Control in DCNs`.

Stage 1 is a DCQCN/ECN baseline on top of the existing RDMA ns-3 simulator.
It intentionally does not implement BCC yet. The current goal is to keep a
clean baseline with reproducible topology, workload, CSV metrics, and figures.

Stage 2 adds switch-side BCC egress state detection. Each switch egress port
maintains queue length, previous queue length, update time, normalized queue
slope, link utilization, and one state in `TU`, `TC`, `NC`, or `PC`. Data
packets leaving a switch egress port receive an `ns3::BccTag` with the current
state and telemetry.

Stage 3 adds the first source-side BCC controller approximation. ACK packets
echo the switch `BccTag` back to the sender. `TC`/`TU` feedback uses a transient
controller with pause/ramp-down/ramp-up and an inflight bound; `NC`/`PC`
feedback uses a DCQCN-style persistent controller with a gentle multiplicative
decrease factor.

## Stage 1 outputs

Each run writes the existing raw simulator files under `mix/output/<run_id>/`
plus these baseline CSVs:

- `stage1_metrics.csv`
- `stage1_queue_timeseries.csv`
- `stage1_rate_timeseries.csv`

`plot_stage1.py` writes:

- `figures/queue_length.png`
- `figures/aggregate_sending_rate.png`

## Small testbed

Run from inside the README Docker container:

```sh
python3 bcc/run_stage1_baseline.py --testbed small --workload incast-mix --simul-time 0.01 --netload 20
```

Equivalent host-side Docker command from this repo root:

```sh
docker run --rm -v "$(pwd)":/root cw-sim:sigcomm23ae bash -lc "cd /root && ./waf configure --build-profile=optimized && ./waf && python3 bcc/run_stage1_baseline.py --testbed small --workload incast-mix --simul-time 0.01 --netload 20"
```

## 320-server baseline

The generated 320-server topology uses 20 ToR, 20 aggregation switches, and
16 core switches. Server links are 25Gbps; switch-to-switch links are 400Gbps;
all link delays are 1us.

```sh
python3 bcc/run_stage1_baseline.py --testbed fat320 --workload webserver --simul-time 0.1 --netload 40
```

## Stage 2 switch-state test

Run from inside the README Docker container:

```sh
python3 bcc/run_stage2_switch_state.py --simul-time 0.01 --netload 20
```

This writes `mix/output/<run_id>/<run_id>_out_bcc_state.txt` plus
`stage2_bcc_state_summary.csv` and `figures/bcc_state_fraction.png`.
The state approximation is:

```text
if queue_len > K2 or queue_slope > S: TC
else if queue_len > K1: PC
else if link_utilization < U: TU
else: NC
```

where `U=0.9`, `S=1.0`, `K1=ECN Kmin`, and `K2=ECN Kmax`.

## Stage 3 source-controller test

Run from inside the README Docker container:

```sh
python3 bcc/run_stage3_bcc_controller.py --simul-time 0.01 --netload 20
```

This runs the same generated 5-server incast/background trace twice: first with
DCQCN, then with `--cc bcc`. It writes per-run `stage1_metrics.csv`,
`stage1_queue_timeseries.csv`, `stage1_rate_timeseries.csv`, BCC state summary
for the BCC run, and a comparison directory:

```text
mix/output/stage3_compare_<dcqcn_run_id>_<bcc_run_id>/
```

containing `stage3_compare_metrics.csv` and comparison figures.

## Current mismatch notes

- Stage 2 implements switch-side BCC state marking only. The source-side
  feedback path and bimodal controller are now implemented as a Stage 3
  approximation.
- Stage 3 uses `PacketTag` echo on ACKs instead of a real P4/header format.
- Stage 3's TCM uses ACKed bytes over a configurable control period to estimate
  arrival rate; it does not yet reproduce every timing detail from the paper.
- Stage 3's PCM is a gentle DCQCN-style controller rather than a line-for-line
  implementation of the BCC prototype.
- Egress queue limit is approximated through the existing switch MMU buffer
  model with a 16MB switch buffer. The BCC paper's dynamic threshold details
  are not fully matched yet.
- ECN thresholds are configured as Kmin=5KB and Kmax=200KB for every link rate.
- The 320-server topology matches the requested switch/server counts, link
  rates, and delays, but it is a folded-Clos approximation rather than a direct
  paper artifact.
