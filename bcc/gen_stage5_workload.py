#!/usr/bin/env python3
import argparse
import bisect
import csv
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


def sample_size(cdf):
    x = random.random()
    probs = [p for _, p in cdf]
    idx = bisect.bisect_left(probs, x)
    return cdf[min(idx, len(cdf) - 1)][0]


def poisson(mean):
    return -math.log(1.0 - random.random()) * mean


def avg_size(cdf):
    return sum(size * (prob - (cdf[i - 1][1] if i else 0.0)) for i, (size, prob) in enumerate(cdf))


def add_background(flows, hosts, duration_s, load, bandwidth_gbps, cdf, max_flows):
    mean_iat_ns = 1.0 / (bandwidth_gbps * 1e9 * load / 8.0 / avg_size(cdf)) * 1e9
    end_ns = int((BASE_TIME + duration_s) * 1e9)
    for src in range(hosts):
        t_ns = int(BASE_TIME * 1e9) + int(poisson(mean_iat_ns))
        while t_ns <= end_ns:
            dst = random.randrange(hosts)
            while dst == src:
                dst = random.randrange(hosts)
            flows.append({
                "time": t_ns * 1e-9,
                "src": src,
                "dst": dst,
                "pg": 3,
                "size": sample_size(cdf),
                "class": "background",
                "event_id": "",
            })
            if max_flows and len([f for f in flows if f["class"] == "background"]) >= max_flows:
                return
            t_ns += int(poisson(mean_iat_ns))


def add_incast(flows, hosts, duration_s, degree, size_bytes, period_us):
    degree = min(degree, hosts - 1)
    event = 0
    t = BASE_TIME + period_us * 1e-6
    while t < BASE_TIME + duration_s:
        dst = (hosts - 1 - event) % hosts
        candidates = [h for h in range(hosts) if h != dst]
        random.shuffle(candidates)
        for src in candidates[:degree]:
            flows.append({
                "time": t,
                "src": src,
                "dst": dst,
                "pg": 3,
                "size": size_bytes,
                "class": "incast",
                "event_id": event,
            })
        event += 1
        t += period_us * 1e-6


def write_trace(path, flows):
    flows.sort(key=lambda f: (f["time"], f["src"], f["dst"], f["size"]))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as fout:
        fout.write("{}\n".format(len(flows)))
        for f in flows:
            fout.write("{} {} {} {} {:.9f}\n".format(
                f["src"], f["dst"], f["pg"], f["size"], f["time"]))


def write_metadata(path, flows):
    with open(path, "w", newline="") as fout:
        fields = ["flow_index", "time_s", "src", "dst", "pg", "size_bytes", "class", "event_id"]
        writer = csv.DictWriter(fout, fieldnames=fields)
        writer.writeheader()
        for idx, f in enumerate(sorted(flows, key=lambda x: (x["time"], x["src"], x["dst"], x["size"]))):
            writer.writerow({
                "flow_index": idx,
                "time_s": "{:.9f}".format(f["time"]),
                "src": f["src"],
                "dst": f["dst"],
                "pg": f["pg"],
                "size_bytes": f["size"],
                "class": f["class"],
                "event_id": f["event_id"],
            })


def main():
    parser = argparse.ArgumentParser(description="Generate Stage-5 Fat-Tree workloads.")
    parser.add_argument("--hosts", type=int, default=320)
    parser.add_argument("--kind", choices=["rpc", "webserver", "incast-mix"], required=True)
    parser.add_argument("--background-kind", choices=["rpc", "webserver"], default="rpc")
    parser.add_argument("--duration", type=float, default=0.02)
    parser.add_argument("--load", type=float, default=0.02)
    parser.add_argument("--bandwidth-gbps", type=float, default=25.0)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--max-background-flows", type=int, default=50000)
    parser.add_argument("--incast-degree", type=int, default=16)
    parser.add_argument("--incast-size-bytes", type=int, default=65536)
    parser.add_argument("--incast-period-us", type=float, default=1000.0)
    parser.add_argument("--output", required=True)
    parser.add_argument("--metadata", required=True)
    args = parser.parse_args()

    random.seed(args.seed)
    cdf = RPC_CDF if args.kind == "rpc" or args.background_kind == "rpc" else WEBSERVER_CDF
    if args.kind == "webserver":
        cdf = WEBSERVER_CDF

    flows = []
    if args.kind in ("rpc", "webserver"):
        add_background(flows, args.hosts, args.duration, args.load, args.bandwidth_gbps,
                       cdf, args.max_background_flows)
    else:
        background_cdf = RPC_CDF if args.background_kind == "rpc" else WEBSERVER_CDF
        add_background(flows, args.hosts, args.duration, args.load, args.bandwidth_gbps,
                       background_cdf, args.max_background_flows)
        add_incast(flows, args.hosts, args.duration, args.incast_degree,
                   args.incast_size_bytes, args.incast_period_us)

    write_trace(args.output, flows)
    write_metadata(args.metadata, flows)
    print("flows={} -> {} metadata={}".format(len(flows), args.output, args.metadata))


if __name__ == "__main__":
    main()
