#!/usr/bin/env python3
import argparse
import os


def write_topology(path, node_count, switch_ids, links):
    with open(path, "w") as fout:
        fout.write("{} {} {}\n".format(node_count, len(switch_ids), len(links)))
        fout.write("{}\n".format(" ".join(str(x) for x in switch_ids)))
        for src, dst, rate, delay_ns in links:
            fout.write("{} {} {}Gbps {}ns 0.000000\n".format(src, dst, rate, delay_ns))


def single_switch_5():
    switch_id = 5
    links = [(host, switch_id, 25, 1000) for host in range(5)]
    return "bcc_single_switch_5_25G_OS1", 6, [switch_id], links


def bcc_fat_320():
    n_servers = 320
    n_tor = 20
    n_agg = 20
    n_core = 16
    servers_per_tor = n_servers // n_tor
    tor_base = n_servers
    agg_base = tor_base + n_tor
    core_base = agg_base + n_agg

    links = []
    for tor in range(n_tor):
        tor_id = tor_base + tor
        for offset in range(servers_per_tor):
            links.append((tor * servers_per_tor + offset, tor_id, 25, 1000))

    for tor in range(n_tor):
        tor_id = tor_base + tor
        for agg in range(n_agg):
            links.append((tor_id, agg_base + agg, 400, 1000))

    for agg in range(n_agg):
        agg_id = agg_base + agg
        for core in range(n_core):
            links.append((agg_id, core_base + core, 400, 1000))

    switch_ids = list(range(tor_base, core_base + n_core))
    return "bcc_fat_320_25G_400G_OS1", core_base + n_core, switch_ids, links


def main():
    parser = argparse.ArgumentParser(description="Generate BCC reproduction topologies.")
    parser.add_argument("--out-dir", default="config")
    args = parser.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    for topo in (single_switch_5(), bcc_fat_320()):
        name, node_count, switch_ids, links = topo
        path = os.path.join(args.out_dir, name + ".txt")
        write_topology(path, node_count, switch_ids, links)
        print("{}: nodes={} switches={} links={} -> {}".format(
            name, node_count, len(switch_ids), len(links), path))


if __name__ == "__main__":
    main()
