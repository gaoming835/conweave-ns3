# Stage 1 DCP Config Skeleton

## Scope

Stage 1 adds only DCP configuration and statistics plumbing. It does not implement
DCP data-plane behavior, packet formats, retransmission logic, trimming, or
header-only packet handling.

## Added Config Entrypoints

- `run.py --transport dcp`

The generated `config.txt` includes:

```text
ENABLE_DCP 1
TRANSPORT_MODE dcp
DCP_STATS_FILE mix/output/<run_id>/<run_id>_out_dcp_stats.txt
```

For now, DCP mode reuses the existing DCQCN transport behavior. This preserves a
runnable baseline while making DCP-specific config and output fields visible for
later stages.

## C++ Plumbing

- `scratch/network-load-balance.cc` reads `ENABLE_DCP`, `TRANSPORT_MODE`, and
  `DCP_STATS_FILE`.
- `src/point-to-point/model/settings.h` and `settings.cc` define global DCP
  config state and DCP statistics counters.
- DCP statistics are written only when DCP is enabled.
- Existing non-DCP configs do not receive DCP config lines and should not emit
  DCP stats files.

## DCP Statistics

The DCP stats CSV uses `field,value` format and currently exports these fields,
all initialized to `0`:

- `dcp_data_packets`
- `dcp_ack_packets`
- `dcp_ho_packets`
- `dcp_trim_events`
- `dcp_ho_generated`
- `dcp_ho_returned`
- `dcp_precise_retx`
- `dcp_spurious_retx`
- `dcp_timeout_retx`
- `dcp_ooo_packets`
- `dcp_completed_messages`
- `dcp_ho_dropped`

## Smoke Test

Run:

```bash
./scripts/run_dcp_config_smoke.sh
```

The smoke script uses the existing README Docker image `cw-sim:sigcomm23ae`,
builds the simulator in the container, runs a 10ms DCP skeleton simulation on the
small single-switch topology, skips FCT analysis, and verifies:

- the program runs;
- `config.txt` contains the DCP config lines;
- the DCP stats file exists;
- all DCP statistics are present and equal to `0`.

Observed stage-1 smoke result:

```text
dcp_config_smoke=pass
run_id=719779793
stats_file=mix/output/719779793/719779793_out_dcp_stats.txt
```

## Baseline Safety

- No DCP counters are incremented in packet-processing paths in this stage.
- IRN, Lossless/PFC, and ConWeave control paths are not modified.
- Non-DCP generated configs remain free of DCP config lines, preserving the
  existing output surface for normal modes.
