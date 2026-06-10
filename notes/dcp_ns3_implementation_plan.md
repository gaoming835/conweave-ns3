# DCP NS3 Implementation Plan

本文档只覆盖 DCP 在 NS3 仿真中的对齐和复现实验工作，不包含 P4 switch、
FPGA RNIC、PCIe/WQE/MTT 等硬件微架构实现。目标是把当前可运行的 DCP
NS3 近似模型，逐步推进到可以复现论文 NS3 部分关键结论的状态。

## Scope

### In scope

- DCP packet type 在 NS3 packet/header 中的表达。
- DCP-Switch packet trimming、HO packet、control queue 行为。
- DCP-RNIC 的 HO return、RetransQ、precise retransmission 抽象。
- DCP order-tolerant reception 和 bitmap-free tracking 的 NS3 抽象。
- Packet-level adaptive routing, 即论文中的 AR。
- 论文 NS3 实验的可复现 harness、CSV 输出和绘图。

### Out of scope

- P4 switch pipeline。
- FPGA RNIC prototype。
- 真实 RDMA verbs/API 行为。
- WQE fetch/drop、PCIe transaction、MTT translation 的周期级建模。
- 硬件资源开销表的复现。

## Current State

当前代码已经具备以下 DCP NS3 基础能力：

- `run.py --transport dcp` 可生成 DCP 配置块和 DCP stats 文件。
- `DcpTag` 定义了 `DCP_NON`、`DCP_ACK`、`DCP_DATA`、`DCP_HO` 四类包。
- RDMA sender 在 DCP 模式下为 data packet 附加 DCP metadata。
- Switch 在 data queue 超过 `DCP_TRIM_THRESHOLD` 时，可以把 DCP DATA
  转换为 header-only packet，并送入高优先级队列。
- Receiver 收到 HO packet 后会返回 sender。
- Sender 收到返回 HO 后将 PSN 放入 per-QP retransmission queue，并可触发
  precise retransmission。
- Receiver 侧有 OOO 接收近似逻辑，当前目标是避免 DCP 因 packet-level OOO
  触发 IRN 式 SACK 和 spurious retransmission。
- 已有 smoke scripts：
  - `scripts/run_dcp_config_smoke.sh`
  - `scripts/run_dcp_packet_type_smoke.sh`
  - `scripts/run_dcp_trim_smoke.sh`
  - `scripts/run_dcp_ho_return_smoke.sh`
  - `scripts/run_dcp_retrans_smoke.sh`
  - `scripts/run_dcp_ooo_smoke.sh`

## Known Gaps Against the Paper's NS3 Portion

1. DCP packet type 目前主要通过 ns-3 `PacketTag` 表达，不是论文中的 IP ToS
   two-bit tag。
2. Control queue 目前复用 `qIndex=0` 高优先级队列，不是论文中的 data/control
   WRR scheduler，也没有基于 incast scale 和 HO/data size ratio 的权重计算。
3. Packet trimming 只覆盖 DCP DATA。论文中 non-DCP 和 DCP ACK 在 data queue
   超阈值时也有明确 drop 规则。
4. 当前没有论文使用的 packet-level adaptive routing AR mode。`run.py` 目前只
   暴露 `fecmp`、`drill`、`conga`、`letflow`、`conweave`。
5. Receiver tracking 当前使用已收区间表推进 completion，不是论文中基于
   MSN/eMSN/sRetryNo 的 message-level bitmap-free tracking 抽象。
6. Retransmission rate control 目前主要依赖 `DCP_RETRANS_PER_ROUND`，还没有
   和 CC available window / normal send quota 建立足够清晰的 NS3 抽象关系。
7. 论文 NS3 实验 harness 还没有成体系，包括 Fig.1、Fig.2、Fig.13、Fig.14、
   Fig.15、Fig.16、Fig.17 和 Table 5。

## Implementation Principles

- 每个阶段只解决一个主要语义缺口。
- 每个阶段必须提供最小验证脚本或扩展现有 smoke。
- 新增实验必须保存 config snapshot、CSV metrics 和绘图脚本。
- 小 topology 先通过，再扩展到论文规模。
- 优先修仿真语义，再做论文规模复现实验。
- 保留当前 smoke scripts，避免破坏已有 DCP 基线。

## Phase 0: Baseline and Documentation

### Goal

确认当前 DCP NS3 基线可运行，并建立后续阶段的进度记录。

### Tasks

- 运行现有 6 个 DCP smoke scripts。
- 记录每个 script 的 pass/fail、输出 run id、关键 stats。
- 新增或维护一个进度文件，例如 `notes/dcp_ns3_progress.md`。
- 明确当前代码的 accepted baseline：
  - config plumbing works；
  - packet type works；
  - trim path works；
  - HO return works；
  - precise retrans path works；
  - OOO smoke works。

### Validation

```bash
bash scripts/run_dcp_config_smoke.sh
bash scripts/run_dcp_packet_type_smoke.sh
bash scripts/run_dcp_trim_smoke.sh
bash scripts/run_dcp_ho_return_smoke.sh
bash scripts/run_dcp_retrans_smoke.sh
bash scripts/run_dcp_ooo_smoke.sh
```

### Exit Criteria

- 所有 smoke scripts 通过，或已记录失败原因和后续修复项。
- `notes/dcp_ns3_progress.md` 中有 Phase 0 结果。

## Phase 1: Paper-Compatible DCP Packet Type Encoding

### Goal

让 NS3 packet 同时具备论文中的 ToS two-bit DCP type 语义和当前仿真所需的
metadata。

### Tasks

- 定义 DCP type 在 IP ToS/DSCP 中的 bit layout。
- 保留 ECN bits，不覆盖已有 ECN/CNP 行为。
- 在 sender 生成 DCP DATA 时写入 DCP type。
- 在 ACK/HO 生成路径写入 DCP ACK 或 DCP HO type。
- Switch 优先从 IP ToS/DSCP 解析 DCP type，`PacketTag` 只作为仿真 metadata。
- 增加 helper API：
  - `SetDcpTypeInIpHeader(...)`
  - `GetDcpTypeFromIpHeader(...)`
  - `PreserveEcnAndSetDcpType(...)`

### Code Areas

- `src/point-to-point/model/dcp-tag.{h,cc}`
- `src/point-to-point/model/rdma-hw.cc`
- `src/point-to-point/model/switch-node.cc`
- `scratch/dcp-packet-type-test.cc`

### Validation

- 扩展 `dcp-packet-type-test`，检查 DCP type 可以从 IP header 解析。
- 跑 `scripts/run_dcp_packet_type_smoke.sh`。
- 检查 DCP DATA/ACK/HO counters 与 packet type 一致。

### Exit Criteria

- DCP DATA、ACK、HO 均可从 IP ToS/DSCP 解析出类型。
- ECN marking 行为未被 DCP type 破坏。

## Phase 2: Complete Packet Trimming Semantics

### Goal

补齐论文 packet trimming module 在 NS3 中的语义。

### Tasks

- 统一 switch 侧 DCP packet admission 流程：
  - HO packet 直接进入 control queue；
  - DCP DATA 在 data queue 未超阈值时进入 data queue；
  - DCP DATA 在 data queue 超阈值时 trim 成 HO；
  - non-DCP 在 data queue 超阈值时 drop；
  - DCP ACK 在 data queue 超阈值时 drop。
- 增加 HO size 配置，默认接近论文 57B：
  - `DCP_HO_SIZE`
  - 默认值可先设为当前 header size，后续校准到 57B。
- 增加 stats：
  - `dcp_non_dropped`
  - `dcp_ack_dropped`
  - `dcp_ho_bytes`
  - `dcp_data_bytes_trimmed`
- 让 trim/drop 逻辑发生在一个清晰的函数中，减少散落在 admission control 中的
  分支。

### Code Areas

- `src/point-to-point/model/switch-node.{h,cc}`
- `src/point-to-point/model/settings.{h,cc}`
- `scratch/network-load-balance.cc`
- `run.py`

### Validation

- 新增 `scratch/dcp-trim-semantics-test.cc` 或扩展现有 trim smoke。
- 构造最小 single-switch topology，分别发送 DCP DATA、DCP ACK、DCP HO、
  non-DCP packet。
- 检查：
  - DCP DATA 产生 HO；
  - DCP ACK 和 non-DCP 在阈值触发时被 drop；
  - HO 不被 data queue threshold drop。

### Exit Criteria

- Packet trimming 行为覆盖论文四类包处理规则。
- 新增 counters 在 smoke 中可观测。

## Phase 3: DCP Control Queue WRR Scheduler

### Goal

把当前简单高优先级队列近似改成可配置的 DCP data/control WRR scheduler。

### Tasks

- 在 switch egress 队列层增加 DCP control queue 和 data queue 的 WRR 逻辑。
- 新增配置：
  - `DCP_ENABLE_WRR`
  - `DCP_CONTROL_WEIGHT`
  - `DCP_DATA_WEIGHT`
  - 可选：`DCP_INC_SCALE_N`
  - 可选：`DCP_HO_DATA_RATIO_R`
- 支持两种配置方式：
  - 直接设置 control/data weight；
  - 按论文公式计算 weight。
- 记录 control queue 和 data queue 的最大长度、平均长度、drop count。
- 保证 non-DCP/PFC/ACK 旧逻辑不被破坏。必要时只在 `ENABLE_DCP=1` 时启用新
  WRR。

### Code Areas

- `src/point-to-point/model/qbb-net-device.{h,cc}`
- `src/point-to-point/model/switch-node.{h,cc}`
- `src/point-to-point/model/settings.{h,cc}`
- `scratch/network-load-balance.cc`
- `run.py`

### Validation

- 新增 `scripts/run_dcp_wrr_smoke.sh`。
- 先用 5-host single-switch incast 验证 HO queue 不被饿死。
- 再用 128-to-1 incast 小规模验证：
  - `dcp_ho_dropped == 0` 或接近 0；
  - data packets 仍有发送进展；
  - control/data dequeue ratio 接近配置的 WRR ratio。

### Exit Criteria

- DCP control queue 不再只是 `qIndex=0` 高优先级近似。
- 可以用配置改变 HO loss/drop 行为。

## Phase 4: Packet-Level Adaptive Routing AR

### Goal

补齐论文 NS3 实验依赖的 packet-level AR load balancing。

### Tasks

- 在 `run.py` 中新增 `--lb ar`。
- 在 `lb_modes` 中分配新 mode，例如 `ar: 11`。
- 在 `SwitchNode::GetOutDev` 中增加 `DoLbAdaptiveRouting(...)`。
- 初始 AR 策略：
  - 对每个 data packet，从可用 next-hops 中选择 egress queue bytes 最小的端口；
  - control packet、ACK、HO 先走 ECMP 或固定反向路径，避免引入过多变量；
  - 可选 tie-break 使用 hash 或 random。
- 增加 per-flow path diversity stats：
  - `ar_packets`
  - `ar_path_switches`
  - `ar_used_next_hops`

### Code Areas

- `run.py`
- `src/point-to-point/model/switch-node.{h,cc}`
- `src/point-to-point/model/settings.{h,cc}`
- `scratch/network-load-balance.cc`

### Validation

- 新增 `scripts/run_dcp_ar_smoke.sh`。
- 小 topology 中确认同一 flow 的 packets 可走多条 next-hop。
- 运行 IRN+AR 与 DCP+AR 的无 loss OOO 场景：
  - IRN+AR 应出现 spurious retrans 或 SACK/NACK 触发；
  - DCP+AR 不应出现 spurious retrans。

### Exit Criteria

- `run.py --lb ar` 可运行。
- DCP+AR 和 IRN+AR 可以在同一 topology/flow 下对比 OOO 行为。

## Phase 5: DCP Bitmap-Free Tracking NS3 Abstraction

### Goal

把 receiver tracking 从当前区间表近似推进到更接近论文的 message-level
bitmap-free tracking 抽象。

### Tasks

- 为 DCP packet metadata 增加或抽象以下字段：
  - MSN
  - eMSN
  - sRetryNo
  - message size
  - packet offset within message
- 在 receiver 侧维护 message-level counters，而不是只维护 flow-level received
  intervals。
- ACK 语义从 flow completion 逐步改为 message completion 或 eMSN advancement。
- 对 duplicate packet、retransmitted packet、OOO packet 分别计数。
- 保留一个 debug/compat mode，便于与当前区间表实现对照。

### Code Areas

- `src/point-to-point/model/dcp-tag.{h,cc}`
- `src/point-to-point/model/rdma-queue-pair.{h,cc}`
- `src/point-to-point/model/rdma-hw.cc`
- `scratch/dcp-ooo-receiver-test.cc`

### Validation

- 扩展 OOO unit test：
  - 单 message OOO；
  - 多 message OOO；
  - duplicate retransmission；
  - tail loss followed by HO retransmission；
  - message completion ACK。
- 跑 `scripts/run_dcp_ooo_smoke.sh` 和 `scripts/run_dcp_retrans_smoke.sh`。

### Exit Criteria

- DCP receiver 不依赖 IRN SACK/bitmap 行为。
- OOO data packet 不触发 spurious retrans。
- Message-level completion 和 ACK 可观测。

## Phase 6: Retransmission Rate Control and Timeout Fallback

### Goal

让 DCP retransmission 在 NS3 中更清楚地体现论文中 RetransQ 与 CC/rate control
解耦但可调节的关系。

### Tasks

- 明确 retransmission dequeue 与 normal data sending 的优先级关系。
- 将 `DCP_RETRANS_PER_ROUND` 扩展为更可解释的配置：
  - `DCP_RETRANS_BATCH_SIZE`
  - `DCP_RETRANS_QUOTA_BYTES`
  - `DCP_RETRANS_RESPECT_WIN`
- 确认 retransmitted data packet 仍走 DCP DATA type，并可再次被 trim。
- 完善 timeout fallback：
  - 默认关闭，用于论文 DCP no-timeout 主路径；
  - 可开启用于 HO loss 或 failure 场景；
  - stats 区分 HO retrans 和 timeout retrans。
- 增加 stats：
  - `dcp_retrans_bytes`
  - `dcp_retrans_from_ho`
  - `dcp_retrans_from_timeout`
  - `dcp_retrans_retrimmed`

### Code Areas

- `src/point-to-point/model/rdma-queue-pair.{h,cc}`
- `src/point-to-point/model/rdma-hw.cc`
- `src/point-to-point/model/settings.{h,cc}`
- `scratch/network-load-balance.cc`
- `run.py`

### Validation

- 扩展 `scripts/run_dcp_retrans_smoke.sh`。
- 构造 HO loss 场景，验证 timeout fallback 开启后可以恢复。
- 构造 severe incast 场景，比较不同 retrans quota 对 tail FCT 和 HO/drop 的影响。

### Exit Criteria

- HO retrans 和 timeout retrans 在 stats 中可区分。
- Retransmission rate 可以通过配置影响结果。

## Phase 7: Paper NS3 Experiment Harness

### Goal

建立论文 NS3 部分的可复现实验脚本、CSV 和 plotting pipeline。

### Experiment Set

#### Fig.1: IRN spurious retransmission under AR

- Topology：two-layer CLOS, 256 hosts, 32 switches。
- Workload：WebSearch, load 0.3。
- Compare：IRN+AR vs DCP+AR。
- Metrics：
  - retransmission ratio by flow size；
  - CDF by small/medium/large flows；
  - packet loss count。

#### Fig.2: IRN timeout under incast

- Topology：CLOS。
- Workload：WebSearch load 0.3 plus 128-to-1 incast load 0.1。
- Compare：IRN+ECMP, IRN+AR, DCP。
- Metrics：
  - timeout count for background flows；
  - timeout count for incast flows。

#### Fig.13: General WebSearch workload

- Compare：DCP, PFC, IRN, MP-RDMA if available。
- If MP-RDMA is not implemented, mark as unsupported instead of using a fake
  baseline。
- Metrics：
  - P50 FCT slowdown；
  - P95 FCT slowdown；
  - load 0.3 and 0.5。

#### Fig.14: AI workloads

- Workloads：
  - AllReduce；
  - AllToAll。
- Initial implementation can use generated flow traces that match the paper's
  flow structure.
- Metrics：
  - JCT per group；
  - individual flow FCT CDF。

#### Fig.15: Cross-DC scenarios

- Distances：
  - 100 km, 500 us propagation between leaf and spine；
  - 1000 km, 5 ms propagation between leaf and spine。
- Workload：WebSearch load 0.5。
- Metrics：
  - P50/P95 FCT slowdown；
  - buffer sensitivity。

#### Fig.16 and Table 5: Severe incast and HO robustness

- Workload：WebSearch load 0.5 plus 128-to-1 incast load 0.05。
- Compare：DCP/IRN/MP-RDMA with and without CC where supported。
- HO robustness：
  - 128-to-1；
  - 255-to-1；
  - different WRR ratios。
- Metrics：
  - P50/P99 FCT slowdown；
  - HO packet loss ratio。

#### Fig.17: Loss recovery efficiency

- Long-running flow under ECMP。
- Artificial packet loss rates at switches。
- Compare：
  - DCP；
  - IRN；
  - timeout-based；
  - RACK-TLP only if implemented。
- Metrics：
  - goodput；
  - timeout count；
  - retransmission count。

### Harness Requirements

- Add scripts under `scripts/dcp_experiments/` or `dcp/experiments/`.
- Every run should save:
  - command line；
  - git commit；
  - generated `config.txt`；
  - topology file；
  - flow file；
  - raw output；
  - summary CSV。
- Every plot should be generated from CSV, not directly from raw logs.
- Baselines that are not implemented must be marked `unsupported`, not silently
  approximated.

### Validation

- Start with a reduced topology for each figure.
- Only scale to paper topology after the reduced run produces sane counters.
- Add one `summary_all.csv` per figure group.

### Exit Criteria

- Each target figure has:
  - run script；
  - summary script；
  - CSV；
  - plotting script；
  - README or notes explaining unsupported baselines and remaining deviations。

## Suggested Execution Order

Recommended order:

1. Phase 0: Baseline and Documentation
2. Phase 2: Complete Packet Trimming Semantics
3. Phase 3: DCP Control Queue WRR Scheduler
4. Phase 4: Packet-Level Adaptive Routing AR
5. Phase 1: Paper-Compatible DCP Packet Type Encoding
6. Phase 5: DCP Bitmap-Free Tracking NS3 Abstraction
7. Phase 6: Retransmission Rate Control and Timeout Fallback
8. Phase 7: Paper NS3 Experiment Harness

This order prioritizes simulation behavior that most directly affects results:
trimming, control queue scheduling, and AR. ToS encoding is important for paper
fidelity, but it should not block validating the core data/control path.

## Minimum Milestone Definitions

### Milestone A: Functional DCP NS3 Model

Includes Phases 0, 2, 3, and 4.

Expected outcome:

- DCP DATA can be trimmed into HO.
- HO uses DCP control queue WRR.
- DCP+AR can run.
- DCP avoids spurious retransmission under AR OOO.

### Milestone B: Paper-Semantic DCP NS3 Model

Includes Phases 1, 5, and 6.

Expected outcome:

- DCP type is visible in packet header bits.
- Receiver tracking is message-level rather than only flow interval based.
- HO retransmission and timeout fallback are separated and measurable.

### Milestone C: Paper NS3 Reproduction

Includes Phase 7.

Expected outcome:

- Reduced experiments for Fig.1, Fig.2, Fig.13, Fig.16, and Table 5 run
  end-to-end.
- Large-scale experiments can be launched with documented resource expectations.
- Unsupported baselines such as MP-RDMA or RACK-TLP are explicitly tracked.

## Risks and Mitigations

### Risk: WRR changes break existing PFC/ConWeave behavior

Mitigation:

- Gate all DCP WRR changes behind `ENABLE_DCP` and `DCP_ENABLE_WRR`.
- Keep non-DCP queue behavior unchanged by default.

### Risk: ToS encoding conflicts with ECN

Mitigation:

- Reserve DCP bits outside ECN bits.
- Add tests that set ECN and DCP type simultaneously.

### Risk: AR implementation creates unrealistic routing loops or unstable paths

Mitigation:

- Limit AR to choosing among routing-table next-hops only.
- Keep TTL and existing forwarding checks.
- Start with two-layer topology before fat-tree.

### Risk: Paper baselines are missing

Mitigation:

- Do not fake MP-RDMA or RACK-TLP.
- Add `unsupported` status in summary CSV until a real implementation exists.
- Compare only implemented baselines in early milestones.

### Risk: Full paper-scale runs are too slow

Mitigation:

- Maintain reduced smoke versions for every experiment.
- Run full scale only after reduced runs pass.
- Save intermediate raw outputs and summaries.

## Immediate Next Step

Start with Phase 0. Run the six existing DCP smoke scripts, record current
results in `notes/dcp_ns3_progress.md`, and treat any failing smoke as a blocker
before changing semantics.
