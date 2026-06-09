# BCC long-term implementation plan

Last updated: 2026-06-05

This document tracks the long-term plan for moving the current BCC reproduction
from a staged simulator approximation toward a paper-aligned implementation.
The current code can build and run, but several protocol semantics are still
approximated.

## Current status

The repository already has:

- Switch-side BCC egress state detection with `BccPortState`.
- `BccTag` telemetry attached to packets at switch dequeue.
- ACK echo of `BccTag` from receiver to sender.
- Source-side `cc_mode == 10` handling for TC/TU transient control and PC/NC
  persistent control.
- Phase-6 config validation for BCC mode/marking/ACK-priority combinations.
- Named Phase-6 scenario runner for smoke, TC incast, TU departure, and PC
  long-flow inspection.
- Source-controller CSV export with final path state, source mode, pause timing,
  `R_hat`, inflight bound, and TU utilization.
- Staged experiment scripts under `bcc/`.

Known gaps:

- BCC state is carried by ns-3 `PacketTag`, not by ECN/header fields.
- Multiple switch markings are overwritten by the last switch instead of merged
  by `TC > PC > NC > TU`.
- Switch classification is currently stateless, not the full four-state
  transition machine.
- TCM/PCM timer handoff is approximate.
- PCM is a gentle DCQCN-style approximation, not a direct reuse of the existing
  DCQCN controller with all transition details.
- `CustomHeader::GetAckSerializedSize()` does not include all ACK fields that
  are serialized.
- Named Phase-6 scenarios are compact debugging harnesses, not a full validation
  matrix or paper-level reproduction.

## Goal

Reach three levels of confidence:

1. MVP correctness: four-state feedback reaches the source and drives different
   source actions for TC, TU, PC, and NC.
2. Paper-semantics alignment: ECN/header encoding, state transitions, and
   TCM/PCM handoff match the BCC design closely enough for protocol reasoning.
3. Experiment reproducibility: small, staged, and paper-style experiments can be
   run with logs that explain why the controller behaved as expected.

## Phase 0: baseline and guardrails

Purpose: freeze a known-good baseline before deeper protocol changes.

Tasks:

- Keep a minimal `cc_mode=10` plus `ENABLE_BCC=1` smoke configuration.
- Make sure the small BCC run produces queue, rate, FCT, and BCC state logs.
- Document the required parameter combination for BCC.
- Keep Docker as the default build and run path.

Acceptance:

- `./waf build` passes in the README Docker container.
- A short BCC smoke test completes and writes `bcc_state`, queue, rate, and FCT
  outputs.

## Phase 1: path-state priority merge

Purpose: make multi-hop feedback semantically correct.

Tasks:

- Add a BCC state priority helper implementing `TC > PC > NC > TU`.
- When a switch marks a packet, read the existing BCC state first.
- Preserve the higher-priority path state instead of blindly replacing it.
- Keep debug fields such as switch id, egress port, queue length, slope, and
  utilization for the selected final state.
- Add a targeted multi-hop test where one hop is TC and a later hop is NC.

Acceptance:

- If any hop is TC, the source sees TC.
- If the path has PC and TU, the source sees PC.
- If the path has only TU feedback, the source sees TU.

## Phase 2: real four-state switch state machine

Purpose: replace per-packet stateless classification with BCC state transitions.

Tasks:

- Treat `BccPortState.state` as the previous state.
- Implement explicit transitions:
  - `NC -> PC` when `Qlen > K1`.
  - `PC -> NC` when `Qlen < K1`.
  - `PC -> TC` when `Slope > S` or `Qlen > K2`.
  - `TC -> PC` when `Slope < S` and `Qlen < K2`.
  - `NC -> TU` when link utilization `< U`.
  - `TU -> NC` when link utilization `> U`.
- Decide and document conflict order when low utilization and queue congestion
  signals appear close together.
- Normalize and document units for queue length, slope, and utilization.

Acceptance:

- `bcc_state` logs show stable transitions instead of packet-by-packet
  reclassification noise.
- Synthetic incast, long-flow, and flow-departure cases trigger TC, PC, and TU
  respectively.

## Phase 3: header semantic alignment

Purpose: move feedback from simulator-only tags toward protocol header fields.

Tasks:

- Encode the four BCC states in packet header fields, matching the intended ECN
  two-bit semantics:
  - `TC = 00`
  - `NC = 01`
  - `TU = 10`
  - `PC = 11`
- Add ACK/qbb header support for relaying BCC state back to the sender.
- Add a quantized utilization field for TU feedback, approximating the
  transport reserved 3-bit field described by BCC.
- Keep `BccTag` as optional debug telemetry, but make source control read the
  header path first.
- Fix ACK serialized-size accounting so `CustomHeader` matches the fields
  actually written by `qbbHeader`.

Acceptance:

- BCC still works if `PacketTag` relay is disabled.
- ACK traces expose BCC state and TU utilization explicitly.
- Header serialization/deserialization remains consistent with and without IRN.

## Phase 4: complete TCM behavior

Purpose: make TC pause/ramp-down and TU ramp-up match the BCC transient control
model more closely.

Tasks:

- Estimate `R_hat` from ACKed inflight bytes over the BCC control period `T`.
- On TC, compute `Tp = I / R_hat - Tb`.
- Extend pause only when the new resume time is later than the existing one.
- Ramp down to the estimated bottleneck-consumption rate.
- Update inflight boundary as `B = R_hat * Tb`.
- On TU, decode utilization `u`, set `R_hat = R_hat / u`, and update `B`.
- Audit how `m_inflightBound`, `m_win`, and variable window mode interact.

Acceptance:

- In an incast case, sender rate drops quickly, source pause occurs, and queue
  drains before recovery.
- In a flow-departure case, sender rate recovers faster than AIMD.
- `R_hat`, pause time, resume time, and inflight bound can be logged per flow.

## Phase 5: PCM and mode handoff

Purpose: make long-lived fairness control and TCM/PCM transitions explicit.

Tasks:

- On PCM to TCM transition, cancel or freeze DCQCN increase/decrease timers.
- Keep DCQCN target rate synchronized with the latest TCM rate.
- On TCM to PCM transition, cancel transient-only timers and hand the final
  rate to PCM.
- Decide whether PCM should call the existing DCQCN path directly or keep a BCC
  gentle-DCQCN wrapper.
- If keeping a wrapper, document alpha, MD factor, and timer behavior clearly.

Acceptance:

- Long-flow coexistence converges without large persistent oscillations.
- Mode changes do not leave stale timers that later change the rate
  unexpectedly.
- PCM behavior is explainable from logs and code comments.

## Phase 6: configuration and experiment ergonomics

Purpose: make BCC hard to misconfigure and easy to inspect.

Tasks:

- Add config validation for:
  - `ENABLE_BCC=1` with `CC_MODE=10`.
  - `CC_MODE=10` with switch BCC marking enabled.
  - ACK high priority when BCC feedback uses ACKs.
- Add or maintain named BCC experiment configs:
  - `bcc_smoke`.
  - `bcc_tc_incast`.
  - `bcc_tu_departure`.
  - `bcc_pc_longflows`.
- Extend monitoring with:
  - source mode, TCM or PCM.
  - pause start and resume time.
  - `R_hat`.
  - inflight bound.
  - final path state.
  - TU utilization.

Acceptance:

- Invalid BCC parameter combinations fail early or print a clear warning.
  Implemented in `run.py` and mirrored in `scratch/network-load-balance.cc`.
- Each named BCC scenario can be run with one Docker command. Implemented by
  `bcc/run_phase6_scenario.py`.
- Logs are sufficient to explain state transitions and source-rate changes.
  Raw `bcc_tcm` output is exported by `bcc/export_bcc_tcm.py` into named CSV
  fields.

## Phase 7: validation matrix

Purpose: build confidence before large paper-style runs.

Tests:

- Single-switch TC incast.
- Single-switch TU flow departure.
- Single-switch PC long-flow fairness.
- NC steady full-utilization case.
- Multi-hop priority merge cases.
- DCQCN versus BCC comparison.
- BCC without TCM ablation.
- BCC without TU ramp-up ablation.

Metrics:

- Average and P99 FCT slowdown.
- Queue peak and queue drain time.
- Link utilization recovery time.
- Source pause count and total pause duration.
- Rate oscillation after PCM handoff.
- BCC state fraction over time.

Acceptance:

- Small tests are deterministic enough for debugging.
- Larger tests run across multiple seeds before claiming paper-level confidence.
- Every reported improvement has queue, rate, and state evidence behind it.

## Recommended priority

Do these first:

1. Implement multi-hop priority merge.
2. Replace stateless classification with the four-state transition machine.
3. Implement header-based state and TU-utilization relay.
4. Fix ACK serialized-size accounting.
5. Clean up TCM/PCM timer handoff.

After those are stable, focus on parameter sensitivity and paper-style
reproduction quality.
