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
