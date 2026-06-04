# BCC reproduction staging

This directory contains the staged reproduction harness for
`BCC: Re-architecting Congestion Control in DCNs`.

Stage 1 is a DCQCN/ECN baseline on top of the existing RDMA ns-3 simulator.
It intentionally does not implement BCC yet. The current goal is to keep a
clean baseline with reproducible topology, workload, CSV metrics, and figures.

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

## Current mismatch notes

- This is a runnable DCQCN/ECN baseline, not the BCC controller.
- Egress queue limit is approximated through the existing switch MMU buffer
  model with a 16MB switch buffer. The BCC paper's dynamic threshold details
  are not fully matched yet.
- ECN thresholds are configured as Kmin=5KB and Kmax=200KB for every link rate.
- The 320-server topology matches the requested switch/server counts, link
  rates, and delays, but it is a folded-Clos approximation rather than a direct
  paper artifact.
