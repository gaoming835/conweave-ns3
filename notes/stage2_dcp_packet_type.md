# Stage 2 DCP Packet Type

Stage 2 adds packet-level DCP identification without implementing packet trimming
or retransmission.

## Existing RDMA Packet Metadata

- RDMA data packets are created in `src/point-to-point/model/rdma-hw.cc`
  (`RdmaHw::GetNxtPacket`).
- RDMA data packets already carry the packet sequence number in `SeqTsHeader`,
  which is parsed as `ch.udp.seq` by `CustomHeader`.
- ACK/NACK packets use `qbbHeader` in
  `src/point-to-point/model/qbb-header.{h,cc}`.
- Existing packet tags include `FlowIDNUMTag`, `FlowStatTag`, `BccTag`, and
  ConWeave/Conga/Letflow tags. Stage 2 follows that pattern and uses a packet
  tag instead of changing serialized RDMA headers.

## DCP Packet Type

`src/point-to-point/model/dcp-tag.{h,cc}` defines `DcpTag` with these packet
types:

- `DCP_NON = 0`
- `DCP_ACK = 1`
- `DCP_DATA = 2`
- `DCP_HO = 3`

The tag carries:

- flow id from `RdmaQueuePair::m_flow_id`;
- packet sequence number / PSN from the RDMA sequence field;
- source and destination IPv4 addresses;
- source and destination UDP ports;
- priority group.

`DCP_DATA` tags are attached in `RdmaHw::GetNxtPacket` when DCP is enabled.
`DCP_ACK` tags are attached to generated ACK/NACK packets in `RdmaHw::ReceiveUdp`.
`DCP_HO` is defined with the same metadata layout so a later stage can attach the
original DCP_DATA flow id, PSN, and src/dst information to HO packets.

## Current Limits

- No packet trimming is implemented.
- No HO packet is generated in this stage.
- No retransmission logic is changed.
- Non-DCP RDMA, IRN, and ConWeave paths do not receive DCP tags because all DCP
  tagging is gated by `Settings::enable_dcp`.

## Smoke Test

Run:

```bash
bash scripts/run_dcp_packet_type_smoke.sh
```

The script first runs `scratch/dcp-packet-type-test.cc`, which verifies the enum
values and that DATA/HO tags preserve flow id, PSN, and src/dst metadata through
ns-3 packet tag serialization.

Expected DCP stats:

- `dcp_data_packets > 0`
- `dcp_ho_packets == 0`
