# DCP NS3 Progress

This file records stage-by-stage DCP NS3 validation results. The current
accepted baseline follows `notes/dcp_ns3_implementation_plan.md`.

## Phase 0: Baseline and Documentation

Date: 2026-06-10

Validation environment:

- Workflow: README Docker path, repo mounted at `/root` inside the container.
- Docker image: `cw-sim:sigcomm23ae`
- Build command used by smoke scripts: `./waf configure --build-profile=optimized && ./waf`
- Topology for simulation smokes: `bcc_stage4_single_switch_5_25G_OS1`
- Common simulation settings: `--transport dcp --cc dcqcn --lb fecmp --pfc 1 --irn 0 --simul_time 0.01 --netload 10 --bw 25 --skip_fct_analysis 1`

Result summary:

| Script | Status | Run ID | Key stats |
| --- | --- | --- | --- |
| `scripts/run_dcp_config_smoke.sh` | pass | `741753285` | `dcp_data_packets=2`, `dcp_ack_packets=1`, `dcp_ho_packets=0`, `dcp_trim_events=0`, `dcp_completed_messages=1`, `dcp_data_dropped=0`, `dcp_ho_dropped=0` |
| `scripts/run_dcp_packet_type_smoke.sh` | pass | `52466995` | `dcp_data_packets=5`, `dcp_ack_packets=1`, `dcp_ho_packets=0`, `dcp_trim_events=0`, `dcp_completed_messages=1` |
| `scripts/run_dcp_trim_smoke.sh` | pass | `823914423` | `dcp_trim_events=29880587`, `dcp_ho_generated=29880587`, `dcp_ho_packets=29880587`, `dcp_ho_dropped=0`, `dcp_data_dropped=0` |
| `scripts/run_dcp_ho_return_smoke.sh` | pass | `285664346` | `dcp_ho_rx_at_receiver=29880584`, `dcp_ho_returned=29880584`, `dcp_ho_rx_at_sender=29880578`, `dcp_ho_dropped=0` |
| `scripts/run_dcp_retrans_smoke.sh` | pass | `208137545` | `dcp_retransq_enqueue=29880578`, `dcp_retransq_dequeue=29880463`, `dcp_precise_retx=29880463`, `dcp_timeout_retx=0`, `dcp_spurious_retx=0` |
| `scripts/run_dcp_ooo_smoke.sh` | pass | N/A, unit smoke | `dcp_ooo_packets=1`, `dcp_completed_messages=1`, `dcp_ack_packets=1`, `dcp_spurious_retx=0` |

Accepted baseline:

- Config plumbing works: `ENABLE_DCP 1`, `TRANSPORT_MODE dcp`, and `DCP_STATS_FILE` are emitted in the generated config.
- Packet type plumbing works for the current PacketTag-based model: DCP data packets are counted and the packet-type unit smoke passes.
- Trim path works: DCP DATA can be converted to HO under `DCP_TRIM_THRESHOLD=1000`.
- HO return works: receiver-side HO packets are returned and observed at the sender.
- Precise retransmission path works: returned HO packets enqueue/dequeue RetransQ entries and trigger precise retransmission without timeout or spurious retransmission in the smoke.
- OOO smoke works: the DCP receiver unit smoke completes an out-of-order message without spurious retransmission.

Notes:

- Smoke scripts that locate output directories by diffing `mix/output` should be run serially; parallel runs can make a script select the wrong new run directory.
- The trim, HO-return, and retrans smokes intentionally create very large packet/counter values in the short congestion scenario. Phase 0 treats these as wiring validation, not performance or paper-scale claims.

## Phase 1: Paper-Compatible DCP Packet Type Encoding

Date: 2026-06-10

Implemented behavior:

- DCP type is encoded in IPv4 ToS bits `[3:2]`, inside the DSCP field.
- IPv4 ToS bits `[1:0]` remain ECN bits and are preserved by DCP helper APIs.
- `DcpTag` remains the simulation metadata carrier for flow id, PSN, ports, PG, and
  original data tuple.
- RDMA DATA, ACK, switch-generated HO, and returned HO packets now write the DCP type
  into the IPv4 header.
- Switch-side DCP DATA detection prefers IPv4 ToS/DSCP type and falls back to
  `DcpTag` only for old packets whose IP DCP type is `DCP_NON`.

Helper APIs added in `src/point-to-point/model/dcp-tag.{h,cc}`:

- `PreserveEcnAndSetDcpType(...)`
- `SetDcpTypeInIpHeader(...)`
- `GetDcpTypeFromIpHeader(...)`
- `GetDcpTypeFromTos(...)`

Validation:

| Script | Status | Run ID | Key stats |
| --- | --- | --- | --- |
| `scripts/run_dcp_packet_type_smoke.sh` | pass | `277749882` | `dcp_packet_type_unit=pass`, `dcp_data_packets=5`, `dcp_ack_packets=1`, `dcp_ho_packets=0`, `dcp_completed_messages=1` |
| `scripts/run_dcp_ho_return_smoke.sh` | pass | `655606511` | `dcp_ho_rx_at_receiver=29880584`, `dcp_ho_returned=29880584`, `dcp_ho_rx_at_sender=29880578` |
| `scripts/run_dcp_trim_smoke.sh` | pass | `81645104` | `dcp_trim_events=29880587`, `dcp_ho_generated=29880587`, `dcp_ho_dropped=0` |

Accepted result:

- `dcp-packet-type-test` now verifies DCP ACK/DATA/HO can be encoded in and decoded
  from `Ipv4Header` ToS.
- The unit test verifies ECN bits survive DCP type writes.
- The existing packet-type, trim, and HO-return smoke paths still pass after switching
  runtime packet classification to prefer IP ToS/DSCP.

## Phase 2: Complete Packet Trimming Semantics

Date: 2026-06-10

Implemented behavior:

- Switch-side DCP admission is centralized in `SwitchNode::EvaluateDcpAdmission(...)`.
- DCP HO packets are forced to the control queue.
- DCP DATA packets enter the data queue below `DCP_TRIM_THRESHOLD` and are trimmed to
  HO packets above the threshold.
- DCP ACK packets are dropped when the corresponding data PG queue is above the trim
  threshold, while preserving the existing ACK high-priority behavior below the threshold.
- non-DCP packets using data queues are dropped above the trim threshold.
- `DCP_HO_SIZE` is configurable; the default `0` keeps the current parsed NS-3
  header-only size.
- New stats are exported in the DCP stats CSV:
  `dcp_non_dropped`, `dcp_ack_dropped`, `dcp_ho_bytes`, and
  `dcp_data_bytes_trimmed`.

Validation:

| Script | Status | Run ID | Key stats |
| --- | --- | --- | --- |
| `./waf --run dcp-trim-semantics-test` | pass | N/A, unit smoke | `dcp_trim_semantics_unit=pass` |
| `scripts/run_dcp_trim_smoke.sh` | pass | `13968323` | `dcp_trim_events=29880587`, `dcp_ho_generated=29880587`, `dcp_ho_dropped=0`, `dcp_ho_bytes=1434268176`, `dcp_data_bytes_trimmed=29880587000`, `dcp_non_dropped=0`, `dcp_ack_dropped=0` |
| `scripts/run_dcp_config_smoke.sh` | pass | `442555251` | final script checks `DCP_HO_SIZE 0` and all Phase 2 stats fields |
| `scripts/run_dcp_packet_type_smoke.sh` | pass | `599768941` | `dcp_packet_type_unit=pass`, `dcp_data_packets=5`, `dcp_ho_packets=0` |
| `scripts/run_dcp_ho_return_smoke.sh` | pass | `890850655` | `dcp_ho_rx_at_receiver=29880584`, `dcp_ho_returned=29880584`, `dcp_ho_rx_at_sender=29880578` |

Accepted result:

- Packet trimming semantics now cover DCP DATA trim, DCP HO control admission, DCP
  ACK threshold drop, and non-DCP data threshold drop.
- The new counters are present in config smoke and nonzero for HO byte/trimmed-byte
  accounting in trim smoke.
- The trim smoke remains a wiring validation scenario; the large HO counter values are
  expected for this short congested topology and are not a paper-scale performance claim.

## Phase 3: DCP Control Queue WRR Scheduler

Date: 2026-06-10

Implemented behavior:

- Switch egress queues now support a DCP-only control/data WRR dequeue path.
- `DCP_ENABLE_WRR` gates the new scheduler behind `ENABLE_DCP`; the default remains off
  so existing RDMA/PFC/ACK behavior keeps the old queue scheduler unless explicitly enabled.
- Queue `0` is treated as the DCP control class for HO and existing high-priority control
  packets; queues `1..7` are treated as the data class.
- New config keys are emitted by `run.py` and parsed by `network-load-balance.cc`:
  `DCP_ENABLE_WRR`, `DCP_CONTROL_WEIGHT`, `DCP_DATA_WEIGHT`, `DCP_INC_SCALE_N`, and
  `DCP_HO_DATA_RATIO_R`.
- Direct weight configuration is supported. If either direct weight is `0`, the current
  NS3 approximation computes `control_weight=max(1, ceil(N * R))` and
  `data_weight=1` from `DCP_INC_SCALE_N` and `DCP_HO_DATA_RATIO_R`.
- DCP stats now include WRR configuration, max/average sampled control/data queue
  lengths, queue drop counters, and control/data dequeue packet/byte counters.

Validation:

| Script | Status | Run ID | Key stats |
| --- | --- | --- | --- |
| `scripts/run_dcp_wrr_smoke.sh` 5-host single-switch | pass | `684128414` | `dcp_ho_dropped=0`, `dcp_control_dequeue_packets=4`, `dcp_data_dequeue_packets=20`, `dcp_control_queue_max_len=60`, `dcp_data_queue_max_len=1048` |
| `scripts/run_dcp_wrr_smoke.sh` 127-to-1 leaf-spine | pass | `531481623` | `dcp_ho_dropped=0`, `dcp_control_dequeue_packets=4459`, `dcp_data_dequeue_packets=2475`, `dcp_trim_events=957`, `dcp_completed_messages=127` |
| `scripts/run_dcp_config_smoke.sh` | pass | `56984259` | default config still emits `DCP_ENABLE_WRR 0`; all Phase 3 stats fields are present |
| `scripts/run_dcp_trim_smoke.sh` | pass | `866376792` | `dcp_trim_events=29880587`, `dcp_ho_generated=29880587`, `dcp_ho_dropped=0`, `dcp_non_dropped=0`, `dcp_ack_dropped=0` |

Accepted result:

- DCP control traffic no longer depends only on the old strict-high-priority queue
  approximation when `DCP_ENABLE_WRR=1`.
- Small single-switch and 127-to-1 leaf-spine incast smoke runs both show control and
  data dequeue progress with zero HO drops.
- Queue length averages are event-sampled on switch enqueue/dequeue, not time-weighted.
  They are intended as smoke/diagnostic counters rather than paper-grade queue occupancy
  metrics.
- The 127-to-1 smoke uses small flows and moderate trim threshold to keep runtime short;
  it validates WRR plumbing and HO/data progress, not paper-scale performance.

## Phase 4: Packet-Level Adaptive Routing AR

Date: 2026-06-10

Implemented behavior:

- `run.py --lb ar` now maps to `LB_MODE 11`.
- `SwitchNode::GetOutDev(...)` dispatches UDP data packets to packet-level AR when
  `LB_MODE 11` is active.
- AR chooses the available next-hop with the smallest local egress queue bytes.
- Ties are broken per packet using the 5-tuple plus UDP sequence number, so equal-load
  paths can split packets from the same flow.
- Control packets, ACK/NACK, PFC/QCN, and DCP HO packets fall back to flow ECMP.
- New AR/IRN stats are exported to `AR_STATS_FILE` for both RDMA/IRN and DCP runs:
  `ar_packets`, `ar_path_switches`, `ar_used_next_hops`, `irn_ooo_packets`, and
  `irn_nack_packets`.
- DCP stats also include `ar_packets`, `ar_path_switches`, and `ar_used_next_hops`.
- Added `config/dcp_ar_2path_100G_OS1.txt`, a small two-host/two-path fixture with
  equal hop count but asymmetric path delay, for deterministic AR OOO validation.

Validation:

| Script | Status | Run ID | Key stats |
| --- | --- | --- | --- |
| `scripts/run_dcp_ar_smoke.sh` IRN+AR | pass | `364533103` | `ar_packets=384`, `ar_path_switches>0`, `ar_used_next_hops>=2`, `irn_ooo_packets=90`, `irn_nack_packets=162` |
| `scripts/run_dcp_ar_smoke.sh` DCP+AR | pass | `630594678` | `ar_packets=384`, `ar_path_switches=63`, `ar_used_next_hops=10`, `dcp_ooo_packets>0`, `dcp_spurious_retx=0` |

Accepted result:

- `run.py --lb ar` is runnable.
- The dedicated small topology shows packet-level path diversity for packets from the
  same flow.
- In the same topology and traffic pattern, IRN+AR observes OOO/NACK behavior while
  DCP+AR tolerates OOO packets without spurious DCP retransmission.
- The AR policy is still a local queue-byte heuristic, not a full paper-scale AR
  evaluation; Phase 7 should use larger workloads for paper figure reproduction.
