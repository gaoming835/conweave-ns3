# BCC reproduction staging

This directory contains the staged reproduction harness for
`BCC: Re-architecting Congestion Control in DCNs`.

For the implementation roadmap and known protocol gaps, see
`bcc/long_term_plan.md`.

## Phase 0 BCC guardrail smoke

Phase 0 freezes the current BCC baseline before deeper protocol changes. Run it
from inside the README Docker container:

```sh
./waf configure --build-profile=optimized
./waf build
python3 bcc/run_phase0_smoke.py --simul-time 0.01 --netload 20
```

Equivalent host-side Docker command from this repo root:

```sh
docker run --rm -v "$(pwd)":/root cw-sim:sigcomm23ae bash -lc "cd /root && ./waf configure --build-profile=optimized && ./waf build && python3 bcc/run_phase0_smoke.py --simul-time 0.01 --netload 20"
```

The required BCC parameter combination is:

- `--cc bcc`, which writes `CC_MODE 10`.
- `--enable_bcc 1`, which enables switch-side BCC marking and the
  `bcc_state` monitor.
- `--pfc 1 --irn 0`, matching the current lossless RDMA smoke setup.
- `--lb fecmp`, keeping load balancing simple while testing BCC feedback.

`run_phase0_smoke.py` fails if `config.txt` does not contain `CC_MODE 10` and
`ENABLE_BCC 1`, or if any required output is missing or empty:

- raw FCT: `<run_id>_out_fct.txt`
- raw queue: `<run_id>_out_qlen.txt`
- raw switch rate: `<run_id>_out_rate.txt`
- raw source rate: `<run_id>_out_source_rate.txt`
- raw BCC state: `<run_id>_out_bcc_state.txt`
- raw BCC TCM controller state: `<run_id>_out_bcc_tcm.txt`
- summary CSVs: `stage1_metrics.csv`, `stage1_queue_timeseries.csv`,
  `stage1_rate_timeseries.csv`, `stage2_bcc_state_summary.csv`,
  `rate-vs-time.csv`, and `queue-vs-time.csv`

## Phase 1 priority merge smoke

Phase 1 keeps the highest-priority BCC state seen along a path instead of
letting a later switch overwrite the existing packet tag. The priority order is
`TC > PC > NC > TU`.

Run the targeted smoke from inside the README Docker container:

```sh
./waf configure --build-profile=optimized
./waf build
./waf --run bcc-priority-merge-test
```

The smoke covers:

- first hop `TC`, later hop `NC` -> final path state `TC`
- path has `PC` and `TU` -> final path state `PC`
- path has only `TU` feedback -> final path state `TU`
- debug telemetry remains attached to the hop that supplied the selected final
  path state

## Phase 2 switch state-machine smoke

Phase 2 replaces per-packet stateless switch classification with explicit
per-egress-port BCC state transitions. `BccPortState.state` is treated as the
previous state and updated by queue length, normalized queue slope, and link
utilization.

Run the targeted smoke from inside the README Docker container:

```sh
./waf configure --build-profile=optimized
./waf build
./waf --run bcc-switch-state-machine-test
```

The transition table is:

```text
NC -> PC when queue_len > K1
PC -> NC when queue_len < K1
PC -> TC when queue_slope > S or queue_len > K2
TC -> PC when queue_slope < S and queue_len < K2
NC -> TU when link_utilization < U
TU -> NC when link_utilization > U
```

Conflict order is congestion first, then under-utilization. In particular,
`queue_len > K1` prevents `NC -> TU`, and a `TU` port moves to `PC` or `TC`
when queue congestion appears before utilization has recovered.

Unit conventions:

- `queue_len`, `K1`, and `K2` are bytes from the egress port queue and ECN
  thresholds.
- `queue_slope` is the queue-byte delta divided by bytes the link could
  transmit during the update interval.
- `link_utilization` is transmitted bytes divided by bytes the link could
  transmit during the update interval, clamped to `[0, 1]`.

## Phase 3 header feedback smoke

Phase 3 moves BCC feedback onto simulator headers while keeping `BccTag` as
debug telemetry and fallback. Data packets encode BCC state in the IPv4 ECN
bits:

```text
TC = 00
NC = 01
TU = 10
PC = 11
```

ACK/NACK packets carry a qbbHeader BCC-valid flag, BCC state, and a 3-bit
quantized utilization field for TU feedback. The source-side BCC controller
reads qbbHeader feedback first and falls back to `BccTag` only when the header
does not contain BCC feedback.

Run the targeted smoke from inside the README Docker container:

```sh
./waf configure --build-profile=optimized
./waf build
./waf --run bcc-header-feedback-test
```

The smoke checks ECN state mapping, qbbHeader BCC serialization, 3-bit
utilization preservation, and ACK serialized-size accounting.

## Phase 4 TCM behavior smoke

Phase 4 tightens the source-side transient controller. `R_hat` is estimated
from ACKed bytes over the BCC control period. TC feedback computes
`Tp = I / R_hat - Tb`, extends the source pause only when the new resume time is
later than the current one, ramps down toward `R_hat`, and sets
`B = R_hat * Tb`. TU feedback decodes utilization `u`, updates
`R_hat = R_hat / u`, and updates the same inflight bound.

Run the targeted smoke from inside the README Docker container:

```sh
./waf configure --build-profile=optimized
./waf build
./waf --run bcc-tcm-helper-test
```

`RdmaQueuePair::GetWin()` gives `bcc.m_inflightBound` priority over `m_win` and
variable-window scaling, so the Phase 4 bound directly gates source
transmission. BCC runs also write `<run_id>_out_bcc_tcm.txt` with per-flow
`R_hat`, pause time, resume time, inflight bound, inflight bytes, decoded
utilization, and mode-transition counters.

## Phase 5 PCM handoff smoke

Phase 5 makes the TCM/PCM handoff explicit. BCC keeps a gentle-DCQCN wrapper
for PCM instead of calling the full DCQCN path directly:

- On PCM -> TCM, BCC cancels the DCQCN/Mellanox alpha, decrease, and rate
  increase timers and synchronizes `mlx.m_targetRate` to the latest TCM rate.
- While in TCM, stale DCQCN/Mellanox timer callbacks are suppressed if they fire
  after cancellation.
- On TCM -> PCM, BCC hands the final TCM rate to `mlx.m_targetRate`, resets PCM
  stage bookkeeping, and restarts the wrapper timers.
- PCM starts with `alpha = min(1, 2 * BCC_MD_FACTOR)` and uses
  `BCC_MD_FACTOR` for the gentle multiplicative decrease step.

Run the targeted smoke from inside the README Docker container:

```sh
./waf configure --build-profile=optimized
./waf build
./waf --run bcc-mode-handoff-test
```

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
The switch egress state machine uses the same Phase 2 transition table:

```text
NC -> PC when queue_len > K1
PC -> NC when queue_len < K1
PC -> TC when queue_slope > S or queue_len > K2
TC -> PC when queue_slope < S and queue_len < K2
NC -> TU when link_utilization < U
TU -> NC when link_utilization > U
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

## Stage 4 minimal Fig.5-style test

Run from inside the README Docker container:

```sh
python3 bcc/run_stage4_minimal.py --simul-time 0.06 --nic-gbps 25 --transient-time-us 20000
```

This creates a 1-switch/5-server topology, starts four long-lived flows from
servers `0..3` to server `4`, and injects a transient incast to server `4`.
It runs DCQCN and BCC on the same trace and writes the required time series:

```text
mix/output/<run_id>/rate-vs-time.csv
mix/output/<run_id>/queue-vs-time.csv
mix/output/stage4_minimal_<dcqcn_run_id>_<bcc_run_id>/rate-vs-time.csv
mix/output/stage4_minimal_<dcqcn_run_id>_<bcc_run_id>/queue-vs-time.csv
```

`rate-vs-time.csv` uses a sender-side monitor that sums active QP `m_rate`
values, matching the Fig.5 aggregate sending-rate view more closely than
bottleneck egress throughput. `queue-vs-time.csv` tracks the bottleneck switch
egress queue toward server `4`.

## Stage 5 Fat-Tree experiments

Run the 320-server Fat-Tree suite from inside the README Docker container:

```sh
python3 bcc/run_stage5_fattree.py --duration 0.01 --load 0.01 --max-background-flows 20000
```

This generates and runs:

- RPC workload.
- WebServer workload.
- RPC background traffic plus repeated incast events.
- DCQCN and BCC on the same workload traces.

The run writes:

```text
mix/output/stage5_fattree/stage5_summary.csv
mix/output/stage5_fattree/stage5_runs.csv
mix/output/stage5_fattree/figures/
```

`stage5_summary.csv` includes average FCT slowdown, 99p FCT slowdown, average
and 99p incast RCT slowdown, queue occupancy, link utilization, and convergence
time. To add BCC parameter sensitivity runs for `K1`, `K2`, `S`, and `U`, append:

```sh
--include-sensitivity
```

For a fast wiring check before a full run, use a short duration and a small
flow cap:

```sh
python3 bcc/run_stage5_fattree.py --workloads rpc --schemes dcqcn --duration 0.006 --load 0.0001 --max-background-flows 100 --output-dir mix/output/stage5_smoke
```

## Current mismatch notes

- Stage 2 implements switch-side BCC state marking only. The source-side
  feedback path and bimodal controller are now implemented as a Stage 3
  approximation.
- Stage 3 uses `PacketTag` echo on ACKs instead of a real P4/header format.
- Phase 4 implements the main TCM equations for `R_hat`, TC pause/ramp-down,
  TU ramp-up, and inflight-bound updates. It is still a simulator
  approximation rather than a line-for-line implementation of the BCC prototype.
- Phase 5 keeps PCM as a gentle DCQCN-style wrapper rather than a line-for-line
  implementation of the BCC prototype.
- Stage 4 is a minimal qualitative reproduction target. It focuses on the
  Fig.5-style aggregate sender-rate and bottleneck queue behavior, not full
  paper-scale FCT evaluation.
- Stage 5 provides the large-scale experiment harness and metrics. Full
  paper-level confidence still requires running longer durations, multiple
  seeds, and comparing against the paper's exact traffic distributions.
- Egress queue limit is approximated through the existing switch MMU buffer
  model with a 16MB switch buffer. The BCC paper's dynamic threshold details
  are not fully matched yet.
- ECN thresholds are configured as Kmin=5KB and Kmax=200KB for every link rate.
- The 320-server topology matches the requested switch/server counts, link
  rates, and delays, but it is a folded-Clos approximation rather than a direct
  paper artifact.
