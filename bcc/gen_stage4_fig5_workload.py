#!/usr/bin/env python3
import argparse
import os


BASE_TIME = 2.0


def default_long_flow_size(args):
    bottleneck_bytes = args.nic_gbps * 1e9 / 8.0 * args.duration
    return int(bottleneck_bytes * args.long_flow_fraction / 4.0)


def main():
    parser = argparse.ArgumentParser(description="Generate Stage-4 Fig.5-style BCC workload.")
    parser.add_argument("--duration", type=float, default=0.04)
    parser.add_argument("--nic-gbps", type=float, default=25.0)
    parser.add_argument("--receiver", type=int, default=4)
    parser.add_argument("--pg", type=int, default=3)
    parser.add_argument("--transient-time-us", type=float, default=8000.0)
    parser.add_argument("--transient-size-bytes", type=int, default=2 * 1024 * 1024)
    parser.add_argument("--long-flow-size-bytes", type=int, default=0)
    parser.add_argument("--long-flow-fraction", type=float, default=0.9)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    senders = [0, 1, 2, 3]
    long_size = args.long_flow_size_bytes or default_long_flow_size(args)
    flows = []
    for src in senders:
        flows.append((BASE_TIME, src, args.receiver, args.pg, long_size))

    transient_t = BASE_TIME + args.transient_time_us * 1e-6
    for src in senders:
        flows.append((transient_t, src, args.receiver, args.pg, args.transient_size_bytes))

    flows.sort(key=lambda row: row[0])
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w") as fout:
        fout.write("{}\n".format(len(flows)))
        for t, src, dst, pg, size in flows:
            fout.write("{} {} {} {} {:.9f}\n".format(src, dst, pg, size, t))

    print("flows={} long_size={} transient_size={} -> {}".format(
        len(flows), long_size, args.transient_size_bytes, args.output))


if __name__ == "__main__":
    main()
