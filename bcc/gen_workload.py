#!/usr/bin/env python3
import argparse
import bisect
import math
import os
import random


BASE_TIME = 2.0

RPC_CDF = [
    (512, 0.35),
    (1024, 0.60),
    (2048, 0.78),
    (4096, 0.90),
    (8192, 0.97),
    (16384, 1.0),
]

WEBSERVER_CDF = [
    (150, 0.10),
    (1024, 0.45),
    (4096, 0.70),
    (16384, 0.85),
    (65536, 0.94),
    (262144, 0.985),
    (1048576, 1.0),
]


def poisson(mean):
    return -math.log(1.0 - random.random()) * mean


def sample_size(cdf):
    x = random.random()
    probs = [p for _, p in cdf]
    idx = bisect.bisect_left(probs, x)
    return cdf[min(idx, len(cdf) - 1)][0]


def add_poisson_background(flows, hosts, duration_s, load, bandwidth_gbps, cdf):
    avg_size = sum(size * (prob - (cdf[i - 1][1] if i else 0.0)) for i, (size, prob) in enumerate(cdf))
    bandwidth_bps = bandwidth_gbps * 1e9
    mean_iat_ns = 1.0 / (bandwidth_bps * load / 8.0 / avg_size) * 1e9
    for src in range(hosts):
        t_ns = int(BASE_TIME * 1e9) + int(poisson(mean_iat_ns))
        end_ns = int((BASE_TIME + duration_s) * 1e9)
        while t_ns <= end_ns:
            dst = random.randrange(hosts)
            while dst == src:
                dst = random.randrange(hosts)
            flows.append((t_ns * 1e-9, src, dst, sample_size(cdf)))
            t_ns += int(poisson(mean_iat_ns))


def add_incast(flows, hosts, duration_s, degree, size_bytes, period_us):
    degree = min(degree, hosts - 1)
    event = 0
    t = BASE_TIME + 0.001
    while t < BASE_TIME + duration_s:
        dst = event % hosts
        senders = [h for h in range(hosts) if h != dst][:degree]
        for src in senders:
            flows.append((t, src, dst, size_bytes))
        event += 1
        t += period_us * 1e-6


def main():
    parser = argparse.ArgumentParser(description="Generate BCC stage-1 flow traces.")
    parser.add_argument("--hosts", type=int, required=True)
    parser.add_argument("--kind", choices=["rpc", "webserver", "incast-mix"], default="rpc")
    parser.add_argument("--duration", type=float, default=0.01)
    parser.add_argument("--load", type=float, default=0.20, help="Per-host offered load fraction")
    parser.add_argument("--bandwidth-gbps", type=float, default=25)
    parser.add_argument("--incast-degree", type=int, default=4)
    parser.add_argument("--incast-size-bytes", type=int, default=65536)
    parser.add_argument("--incast-period-us", type=float, default=500)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    random.seed(args.seed)
    cdf = RPC_CDF if args.kind in ("rpc", "incast-mix") else WEBSERVER_CDF
    flows = []
    if args.kind in ("rpc", "webserver", "incast-mix"):
        add_poisson_background(flows, args.hosts, args.duration, args.load, args.bandwidth_gbps, cdf)
    if args.kind == "incast-mix":
        add_incast(flows, args.hosts, args.duration, args.incast_degree, args.incast_size_bytes,
                   args.incast_period_us)

    flows.sort(key=lambda x: x[0])
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    with open(args.output, "w") as fout:
        fout.write("{}\n".format(len(flows)))
        for t, src, dst, size in flows:
            fout.write("{} {} 3 {} {:.9f}\n".format(src, dst, size, t))
    print("flows={} -> {}".format(len(flows), args.output))


if __name__ == "__main__":
    main()
