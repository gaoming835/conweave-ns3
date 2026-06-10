#!/usr/bin/python3
from genericpath import exists
import subprocess
import os
import time
from xmlrpc.client import boolean
import copy
import shutil
import random
from datetime import datetime
import sys
import os
import argparse
from datetime import date

# randomID
random.seed(datetime.now().isoformat())
MAX_RAND_RANGE = 1000000000

# config template
config_template = """TOPOLOGY_FILE config/{topo}.txt
FLOW_FILE {flow_file_path}

FLOW_INPUT_FILE mix/output/{id}/{id}_in.txt
CNP_OUTPUT_FILE mix/output/{id}/{id}_out_cnp.txt
FCT_OUTPUT_FILE mix/output/{id}/{id}_out_fct.txt
PFC_OUTPUT_FILE mix/output/{id}/{id}_out_pfc.txt
QLEN_MON_FILE mix/output/{id}/{id}_out_qlen.txt
RATE_MON_FILE mix/output/{id}/{id}_out_rate.txt
SOURCE_RATE_MON_FILE mix/output/{id}/{id}_out_source_rate.txt
BCC_STATE_MON_FILE mix/output/{id}/{id}_out_bcc_state.txt
BCC_TCM_MON_FILE mix/output/{id}/{id}_out_bcc_tcm.txt
VOQ_MON_FILE mix/output/{id}/{id}_out_voq.txt
VOQ_MON_DETAIL_FILE mix/output/{id}/{id}_out_voq_per_dst.txt
UPLINK_MON_FILE mix/output/{id}/{id}_out_uplink.txt
CONN_MON_FILE mix/output/{id}/{id}_out_conn.txt
EST_ERROR_MON_FILE mix/output/{id}/{id}_out_est_error.txt

QLEN_MON_START {qlen_mon_start}
QLEN_MON_END {qlen_mon_end}
SW_MONITORING_INTERVAL {sw_monitoring_interval}

FLOWGEN_START_TIME {flowgen_start_time}
FLOWGEN_STOP_TIME {flowgen_stop_time}
BUFFER_SIZE {buffer_size}

CC_MODE {cc_mode}
LB_MODE {lb_mode}
ENABLE_PFC {enabled_pfc}
ENABLE_IRN {enabled_irn}

CONWEAVE_TX_EXPIRY_TIME {cwh_tx_expiry_time}
CONWEAVE_REPLY_TIMEOUT_EXTRA {cwh_extra_reply_deadline}
CONWEAVE_PATH_PAUSE_TIME {cwh_path_pause_time}
CONWEAVE_EXTRA_VOQ_FLUSH_TIME {cwh_extra_voq_flush_time}
CONWEAVE_DEFAULT_VOQ_WAITING_TIME {cwh_default_voq_waiting_time}

ALPHA_RESUME_INTERVAL {alpha_resume_interval}
RATE_DECREASE_INTERVAL {rate_decrease_interval}
CLAMP_TARGET_RATE 0
RP_TIMER 300 
FAST_RECOVERY_TIMES 1
EWMA_GAIN {ewma_gain}
RATE_AI {ai}Mb/s
RATE_HAI {hai}Mb/s
MIN_RATE 100Mb/s
DCTCP_RATE_AI {dctcp_ai}Mb/s

ERROR_RATE_PER_LINK 0.0000
L2_CHUNK_SIZE 4000
L2_ACK_INTERVAL 1
L2_BACK_TO_ZERO 0

RATE_BOUND 1
HAS_WIN {has_win}
VAR_WIN {var_win}
FAST_REACT {fast_react}
MI_THRESH {mi}
INT_MULTI {int_multi}
GLOBAL_T 1
U_TARGET 0.95
MULTI_RATE 0
SAMPLE_FEEDBACK 0

ENABLE_QCN 1
ENABLE_BCC {enable_bcc}
{dcp_config_block}ACK_HIGH_PRIO {ack_high_prio}
BCC_U {bcc_u}
BCC_S {bcc_s}
BCC_CONTROL_PERIOD {bcc_control_period}
BCC_MD_FACTOR {bcc_md_factor}
USE_DYNAMIC_PFC_THRESHOLD 1
PACKET_PAYLOAD_SIZE 1000


LINK_DOWN 0 0 0
KMAX_MAP {kmax_map}
KMIN_MAP {kmin_map}
PMAX_MAP {pmax_map}
LOAD {load}
RANDOM_SEED 1
"""


# LB/CC mode matching
cc_modes = {
    "dcqcn": 1,
    "hpcc": 3,
    "timely": 7,
    "dctcp": 8,
    "bcc": 10,
}

lb_modes = {
    "fecmp": 0,
    "drill": 2,
    "conga": 3,
    "letflow": 6,
    "conweave": 9,
}

topo2bdp = {
    "leaf_spine_128_100G_OS2": 104000,  # 2-tier -> all 100Gbps
    "fat_k8_100G_OS2": 156000,  # 3-tier -> all 100Gbps
    "bcc_single_switch_5_25G_OS1": 14500,  # 5 hosts, one 25Gbps bottleneck
    "bcc_stage4_single_switch_5_10G_OS1": 5800,
    "bcc_stage4_single_switch_5_25G_OS1": 14500,
    "bcc_fat_320_25G_400G_OS1": 27125,  # 320 hosts, 25Gbps access, 400Gbps fabric
}

FLOWGEN_DEFAULT_TIME = 2.0  # see /traffic_gen/traffic_gen.py::base_t


def get_topology_link_rates_bps(topo):
    rates = set()
    with open("config/{topo}.txt".format(topo=topo), 'r') as f_topo:
        first_line = f_topo.readline().split(" ")
        n_link = int(first_line[2])
        f_topo.readline()  # switch IDs
        for i, line in enumerate(f_topo.readlines()):
            if i >= n_link:
                break
            parsed = line.split(" ")
            if len(parsed) <= 2:
                continue
            rate = parsed[2]
            if rate.endswith("Gbps"):
                rates.add(int(rate.replace("Gbps", "")) * 1000000000)
            else:
                raise Exception("Unsupported topology link rate format: {}".format(rate))
    return sorted(rates)


def build_threshold_map(rates, value):
    fields = [str(len(rates))]
    for rate in rates:
        fields += [str(rate), str(value)]
    return " ".join(fields)


def build_pmax_map(rates, value):
    fields = [str(len(rates))]
    for rate in rates:
        fields += [str(rate), "{:.2f}".format(value)]
    return " ".join(fields)


def validate_bcc_config(cc_mode, enable_bcc, ack_high_prio):
    if enable_bcc and cc_mode != 10:
        raise Exception(
            "CONFIG ERROR : ENABLE_BCC=1 requires CC_MODE=10 (--cc bcc).")
    if cc_mode == 10 and not enable_bcc:
        raise Exception(
            "CONFIG ERROR : CC_MODE=10 requires ENABLE_BCC=1 for switch-side BCC marking.")
    if enable_bcc and not ack_high_prio:
        raise Exception(
            "CONFIG ERROR : BCC feedback is carried by ACKs, so ACK_HIGH_PRIO must be 1.")


def build_dcp_config_block(enable_dcp, config_id, trim_threshold):
    if not enable_dcp:
        return ""
    return (
        "ENABLE_DCP 1\n"
        "TRANSPORT_MODE dcp\n"
        "DCP_STATS_FILE mix/output/{id}/{id}_out_dcp_stats.txt\n"
        "DCP_TRIM_THRESHOLD {trim_threshold}\n"
    ).format(id=config_id, trim_threshold=trim_threshold)


def main():
    # make directory if not exists
    isExist = os.path.exists(os.getcwd() + "/mix/output/")
    if not isExist:
        os.makedirs(os.getcwd() + "/mix/output/")
        print("The new directory is created - {}".format(os.getcwd() + "/mix/output/"))

    parser = argparse.ArgumentParser(description='run simulation')
    parser.add_argument('--cc', dest='cc', action='store',
                        default='dcqcn', help="hpcc/dcqcn/timely/dctcp (default: dcqcn)")
    parser.add_argument('--lb', dest='lb', action='store',
                        default='fecmp', help="fecmp/pecmp/drill/conga (default: fecmp)")
    parser.add_argument('--pfc', dest='pfc', action='store',
                        type=int, default=1, help="enable PFC (default: 1)")
    parser.add_argument('--irn', dest='irn', action='store',
                        type=int, default=0, help="enable IRN (default: 0)")
    parser.add_argument('--simul_time', dest='simul_time', action='store',
                        default='0.1', help="traffic time to simulate (up to 3 seconds) (default: 0.1)")
    parser.add_argument('--buffer', dest="buffer", action='store',
                        default='9', help="the switch buffer size (MB) (default: 9)")
    parser.add_argument('--netload', dest='netload', action='store', type=int,
                        default=40, help="Network load at NIC to generate traffic (default: 40.0)")
    parser.add_argument('--bw', dest="bw", action='store',
                        default='100', help="the NIC bandwidth (Gbps) (default: 100)")
    parser.add_argument('--topo', dest='topo', action='store',
                        default='leaf_spine_128_100G', help="the name of the topology file (default: leaf_spine_128_100G_OS2)")
    parser.add_argument('--cdf', dest='cdf', action='store',
                        default='AliStorage2019', help="the name of the cdf file (default: AliStorage2019)")
    parser.add_argument('--enforce_win', dest='enforce_win', action='store',
                        type=int, default=0, help="enforce to use window scheme (default: 0)")
    parser.add_argument('--sw_monitoring_interval', dest='sw_monitoring_interval', action='store',
                        type=int, default=10000, help="interval of sampling statistics for queue status (default: 10000ns)")
    parser.add_argument('--flow_file', dest='flow_file', action='store',
                        default=None, help="use an existing flow trace instead of generating one")
    parser.add_argument('--ecn_kmin_kb', dest='ecn_kmin_kb', action='store',
                        type=int, default=100, help="ECN Kmin in KB before SwitchMmu byte conversion (default: 100)")
    parser.add_argument('--ecn_kmax_kb', dest='ecn_kmax_kb', action='store',
                        type=int, default=400, help="ECN Kmax in KB before SwitchMmu byte conversion (default: 400)")
    parser.add_argument('--ecn_pmax', dest='ecn_pmax', action='store',
                        type=float, default=0.2, help="ECN max marking probability (default: 0.2)")
    parser.add_argument('--dcqcn_ti_us', dest='dcqcn_ti_us', action='store',
                        type=float, default=1, help="DCQCN alpha resume interval Ti in us (default preserves existing config: 1)")
    parser.add_argument('--dcqcn_td_us', dest='dcqcn_td_us', action='store',
                        type=float, default=4, help="DCQCN rate decrease interval Td in us (default preserves existing config: 4)")
    parser.add_argument('--enable_bcc', dest='enable_bcc', action='store',
                        type=int, default=0, help="enable BCC switch-side packet tagging (default: 0)")
    parser.add_argument('--transport', dest='transport', action='store',
                        default='rdma', help="transport mode: rdma/dcp (default: rdma)")
    parser.add_argument('--dcp_trim_threshold', dest='dcp_trim_threshold', action='store',
                        type=int, default=0xffffffff,
                        help="DCP egress data queue trim threshold in bytes (default: disabled)")
    parser.add_argument('--ack_high_prio', dest='ack_high_prio', action='store',
                        type=int, default=1, help="set high priority for ACK/NACK packets (default: 1)")
    parser.add_argument('--bcc_u', dest='bcc_u', action='store',
                        type=float, default=0.9, help="BCC TU utilization threshold (default: 0.9)")
    parser.add_argument('--bcc_s', dest='bcc_s', action='store',
                        type=float, default=1.0, help="BCC TC queue-slope threshold (default: 1.0)")
    parser.add_argument('--bcc_control_period_us', dest='bcc_control_period_us', action='store',
                        type=float, default=55.0, help="BCC source control period in us (default: 55)")
    parser.add_argument('--bcc_md_factor', dest='bcc_md_factor', action='store',
                        type=float, default=0.1, help="BCC PCM gentle multiplicative decrease factor (default: 0.1)")
    parser.add_argument('--skip_fct_analysis', dest='skip_fct_analysis', action='store',
                        type=int, default=0, help="skip fctAnalysis.py after simulation (default: 0)")
    parser.add_argument('--validate_only', dest='validate_only', action='store',
                        type=int, default=0, help="validate config combinations and exit before creating a run (default: 0)")

    # #### CONWEAVE PARAMETERS ####
    # parser.add_argument('--cwh_extra_reply_deadline', dest='cwh_extra_reply_deadline', action='store',
    #                     type=int, default=4, help="extra-timeout, where reply_deadline = base-RTT + extra-timeout (default: 4us)")
    # parser.add_argument('--cwh_path_pause_time', dest='cwh_path_pause_time', action='store',
    #                     type=int, default=16, help="Time to pause the path with ECN feedback (default: 8us")
    # parser.add_argument('--cwh_extra_voq_flush_time', dest='cwh_extra_voq_flush_time', action='store',
    #                     type=int, default=16, help="Extra VOQ Flush Time (default: 8us for IRN)")
    # parser.add_argument('--cwh_default_voq_waiting_time', dest='cwh_default_voq_waiting_time', action='store',
    #                     type=int, default=400, help="Default VOQ Waiting Time (default: 400us)")
    # parser.add_argument('--cwh_tx_expiry_time', dest='cwh_tx_expiry_time', action='store',
    #                     type=int, default=1000, help="timeout value of ConWeave Tx for CLEAR signal (default: 1000us)")

    args = parser.parse_args()

    # make running ID of this config
    # need to check directory exists or not
    isExist = True
    config_ID = 0
    while (isExist):
        config_ID = str(random.randrange(MAX_RAND_RANGE))
        isExist = os.path.exists(os.getcwd() + "/mix/output/" + config_ID)

    if args.transport not in ("rdma", "dcp"):
        raise Exception("CONFIG ERROR : --transport must be rdma or dcp.")
    enable_dcp = int(args.transport == "dcp")
    if enable_dcp:
        args.transport = "dcp"
        if args.cc == "dcqcn":
            print("CONFIG INFO : DCP skeleton currently reuses DCQCN transport behavior.")
        elif args.cc != "bcc":
            raise Exception("CONFIG ERROR : DCP skeleton currently supports --cc dcqcn or --cc bcc.")

    # input parameters
    cc_mode = cc_modes[args.cc]
    lb_mode = lb_modes[args.lb]
    enabled_pfc = int(args.pfc)
    enabled_irn = int(args.irn)
    enable_bcc = int(args.enable_bcc)
    if cc_mode == 10 and enable_bcc == 0:
        print("CONFIG WARNING : --cc bcc requires switch-side BCC marking; auto-setting ENABLE_BCC=1.",
              file=sys.stderr)
        enable_bcc = 1
    ack_high_prio = int(args.ack_high_prio)
    try:
        validate_bcc_config(cc_mode, enable_bcc, ack_high_prio)
    except Exception as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)
    if args.validate_only:
        print("config_validation=pass")
        return
    bw = int(args.bw)
    buffer = args.buffer
    topo = args.topo
    enforce_win = args.enforce_win
    cdf = args.cdf
    flowgen_start_time = FLOWGEN_DEFAULT_TIME  # default: 2.0
    flowgen_stop_time = flowgen_start_time + \
        float(args.simul_time)  # default: 2.0
    sw_monitoring_interval = int(args.sw_monitoring_interval)

    # get over-subscription ratio from topoogy name

    netload = args.netload
    if "OS" in topo:
        oversub = int(topo.replace("\n", "").split("OS")[-1].replace(".txt", ""))
    else:
        oversub = 1
    assert (int(args.netload) % oversub == 0)
    hostload = int(args.netload) / oversub
    assert (hostload > 0)

    # Sanity checks
    if (args.cc == "timely" or args.cc == "hpcc") and args.lb == "conweave":
        raise Exception(
            "CONFIG ERROR : ConWeave currently does not support RTT-based protocols. Plz modify its logic accordingly.")
    if enabled_irn == 1 and enabled_pfc == 1:
        raise Exception(
            "CONFIG ERROR : If IRN is turn-on, then you should turn off PFC (for better perforamnce).")
    if enabled_irn == 0 and enabled_pfc == 0:
        raise Exception(
            "CONFIG ERROR : Either IRN or PFC should be true (at least one).")
    if float(args.simul_time) < 0.005:
        raise Exception("CONFIG ERROR : Runtime must be larger than 5ms (= warmup interval).")

    # sniff number of servers
    with open("config/{topo}.txt".format(topo=args.topo), 'r') as f_topo:
        line = f_topo.readline().split(" ")
        n_host = int(line[0]) - int(line[1])

    assert (hostload >= 0 and hostload < 100)
    flow = "L_{load:.2f}_CDF_{cdf}_N_{n_host}_T_{time}ms_B_{bw}_flow".format(
        load=hostload, cdf=args.cdf, n_host=n_host, time=int(float(args.simul_time)*1000), bw=bw)
    if args.flow_file:
        flow_file_path = args.flow_file
        flow = os.path.splitext(os.path.basename(args.flow_file))[0]
        print("Use existing input traffic file: {}".format(flow_file_path))
    else:
        flow_file_path = "config/{flow}.txt".format(flow=flow)

    # check the file exists
    if args.flow_file:
        if not exists(flow_file_path):
            raise Exception("Input traffic file does not exist: {}".format(flow_file_path))
    elif (exists(os.getcwd() + "/config/" + flow + ".txt")):
        print("Input traffic file with load:{load:.2f}, cdf:{cdf}, n_host:{n_host} already exists".format(
            load=hostload, cdf=cdf, n_host=n_host))
    else:  # make the input traffic file
        print("Generate a input traffic file...")
        print("python ./traffic_gen/traffic_gen.py -c {cdf} -n {n_host} -l {load} -b {bw} -t {time} -o {output}".format(
            cdf=os.getcwd() + "/../traffic_gen/" + args.cdf + ".txt",
            n_host=n_host,
            load=hostload / 100.0,
            bw=args.bw + "G",
            time=args.simul_time,
            output=os.getcwd() + "/config/" + flow + ".txt"))

        os.system("python ./traffic_gen/traffic_gen.py -c {cdf} -n {n_host} -l {load} -b {bw} -t {time} -o {output}".format(
            cdf=os.getcwd() + "/traffic_gen/" + args.cdf + ".txt",
            n_host=n_host,
            load=hostload / 100.0,
            bw=args.bw + "G",
            time=args.simul_time,
            output=os.getcwd() + "/config/" + flow + ".txt"))

    # sanity check - bandwidth
    with open("config/{topo}.txt".format(topo=args.topo), 'r') as f_topo:
        first_line = f_topo.readline().split(" ")
        n_host = int(first_line[0]) - int(first_line[1])
        n_link = int(first_line[2])
        i = 0
        for line in f_topo.readlines()[1:]:
            i += 1
            if (i > n_link):
                break
            parsed = line.split(" ")
            if len(parsed) > 2 and (int(parsed[0]) < n_host or int(parsed[1]) < n_host):
                assert (int(parsed[2].replace("Gbps", "")) == int(bw))
    print("All NIC bandwidth is {bw}Gbps".format(bw=bw))

    ##################################################################
    ##########              ConWeave parameters             ##########
    ##################################################################
    if (lb_mode == 9):
        cwh_extra_reply_deadline = 4  # 4us, NOTE: this is "extra" term to base RTT
        cwh_path_pause_time = 16  # 8us (K_min) or 16us

        if "leaf_spine" in topo:  # 2-tier
            cwh_extra_voq_flush_time = 16
            cwh_default_voq_waiting_time = 200
            cwh_tx_expiry_time = 300  # 300us
        elif "fat" in topo and enabled_pfc == 0 and enabled_irn == 1:  # 3-tier, IRN
            cwh_extra_voq_flush_time = 16
            cwh_default_voq_waiting_time = 300
            cwh_tx_expiry_time = 1000  # 1ms
        elif "fat" in topo and enabled_pfc == 1 and enabled_irn == 0:  # 3-tier, Lossless
            cwh_extra_voq_flush_time = 64
            cwh_default_voq_waiting_time = 600
            cwh_tx_expiry_time = 1000  # 1ms
        else:
            raise Exception(
                "Unsupported ConWeave Parameter Setup")
    else:
        #### CONWEAVE PARAMETERS (DUMMY) ####
        cwh_extra_reply_deadline = 4
        cwh_path_pause_time = 16
        cwh_extra_voq_flush_time = 64
        cwh_default_voq_waiting_time = 400
        cwh_tx_expiry_time = 1000

    ##################################################################

    # make directory if not exists
    isExist = os.path.exists(os.getcwd() + "/mix/output/" + config_ID + "/")
    assert (not isExist)
    # if not isExist:
    os.makedirs(os.getcwd() + "/mix/output/" + config_ID + "/")
    print("The new directory is created  - {}".format(os.getcwd() +
          "/mix/output/" + config_ID + "/"))

    config_name = os.getcwd() + "/mix/output/" + config_ID + "/config.txt"
    print("Config filename:{}".format(config_name))

    # By default, DCQCN uses no window (rate-based).
    has_win = 0
    var_win = 0
    if (cc_mode == 3 or cc_mode == 8 or enforce_win == 1):  # HPCC or DCTCP or enforcement
        has_win = 1
        var_win = 1
        if enforce_win == 1:
            print("### INFO: Enforced to use window scheme! ###")

    # record to history
    simulday = datetime.now().strftime("%m/%d/%y")
    with open("./mix/.history", "a") as history:
        history.write("{simulday},{config_ID},{cc_mode},{lb_mode},{cwh_tx_expiry_time},{cwh_extra_reply_deadline},{cwh_path_pause_time},{cwh_extra_voq_flush_time},{cwh_default_voq_waiting_time},{pfc},{irn},{has_win},{var_win},{topo},{bw},{cdf},{load},{time}\n".format(
            simulday=simulday,
            config_ID=config_ID,
            cc_mode=cc_mode,
            lb_mode=lb_mode,
            cwh_tx_expiry_time=cwh_tx_expiry_time,
            cwh_extra_reply_deadline=cwh_extra_reply_deadline,
            cwh_path_pause_time=cwh_path_pause_time,
            cwh_extra_voq_flush_time=cwh_extra_voq_flush_time,
            cwh_default_voq_waiting_time=cwh_default_voq_waiting_time,
            pfc=enabled_pfc,
            irn=enabled_irn,
            has_win=has_win,
            var_win=var_win,
            topo=topo,
            bw=bw,
            cdf=cdf,
            load=netload,
            time=args.simul_time,
        ))

    # 1 BDP calculation
    if topo2bdp.get(topo) == None:
        print("ERROR - topology is not registered in run.py!!", flush=True)
        return
    bdp = int(topo2bdp[topo])
    print("1BDP = {}".format(bdp))

    # DCQCN parameters (NOTE: HPCC's 400KB/1600KB is too large, although used in Microsoft)
    topology_rates = get_topology_link_rates_bps(topo)
    kmax_map = build_threshold_map(topology_rates, args.ecn_kmax_kb)
    kmin_map = build_threshold_map(topology_rates, args.ecn_kmin_kb)
    pmax_map = build_pmax_map(topology_rates, args.ecn_pmax)

    # queue monitoring
    qlen_mon_start = flowgen_start_time
    qlen_mon_end = flowgen_stop_time

    if (cc_mode == 1 or cc_mode == 10):  # DCQCN or BCC-with-DCQCN PCM
        ai = 10 * bw / 25
        hai = 25 * bw / 25
        dctcp_ai = 1000
        fast_react = 0
        mi = 0
        int_multi = 1
        ewma_gain = 0.00390625

        config = config_template.format(id=config_ID, topo=topo, flow=flow,
                                        flow_file_path=flow_file_path,
                                        qlen_mon_start=qlen_mon_start, qlen_mon_end=qlen_mon_end, flowgen_start_time=flowgen_start_time,
                                        flowgen_stop_time=flowgen_stop_time, sw_monitoring_interval=sw_monitoring_interval,
                                        load=netload, buffer_size=buffer, lb_mode=lb_mode, cwh_tx_expiry_time=cwh_tx_expiry_time,
                                        cwh_extra_reply_deadline=cwh_extra_reply_deadline, cwh_default_voq_waiting_time=cwh_default_voq_waiting_time,
                                        cwh_path_pause_time=cwh_path_pause_time, cwh_extra_voq_flush_time=cwh_extra_voq_flush_time,
                                        enabled_pfc=enabled_pfc, enabled_irn=enabled_irn,
                                        cc_mode=cc_mode,
                                        ai=ai, hai=hai, dctcp_ai=dctcp_ai,
                                        alpha_resume_interval=args.dcqcn_ti_us,
                                        rate_decrease_interval=args.dcqcn_td_us,
                                        enable_bcc=enable_bcc,
                                        dcp_config_block=build_dcp_config_block(
                                            enable_dcp, config_ID, args.dcp_trim_threshold),
                                        ack_high_prio=ack_high_prio,
                                        bcc_u=args.bcc_u,
                                        bcc_s=args.bcc_s,
                                        bcc_control_period=args.bcc_control_period_us,
                                        bcc_md_factor=args.bcc_md_factor,
                                        has_win=has_win, var_win=var_win,
                                        fast_react=fast_react, mi=mi, int_multi=int_multi, ewma_gain=ewma_gain,
                                        kmax_map=kmax_map, kmin_map=kmin_map, pmax_map=pmax_map)
    else:
        print("unknown cc:{}".format(args.cc))

    with open(config_name, "w") as file:
        file.write(config)

    # run program
    print("Running simulation...")
    output_log = config_name.replace(".txt", ".log")
    run_command = "./waf --run 'scratch/network-load-balance {config_name}' > {output_log} 2>&1".format(
        config_name=config_name, output_log=output_log)
    with open("./mix/.history", "a") as history:
        history.write(run_command + "\n")
        history.write(
            "./waf --run 'scratch/network-load-balance' --command-template='gdb --args %s {config_name}'\n".format(
                config_name=config_name)
        )
        history.write("\n")

    print(run_command)
    ret = os.system("./waf --run 'scratch/network-load-balance {config_name}' > {output_log} 2>&1".format(
        config_name=config_name, output_log=output_log))
    if ret != 0:
        raise RuntimeError("simulation failed; inspect {}".format(output_log))
    if args.skip_fct_analysis:
        return

    ####################################################
    #                 Analyze the output FCT           #
    ####################################################
    # NOTE: collect data except warm-up and cold-finish period
    fct_analysis_time_limit_begin = int(
        flowgen_start_time * 1e9) + int(0.005 * 1e9)  # warmup
    fct_analysistime_limit_end = int(
        flowgen_stop_time * 1e9) + int(0.05 * 1e9)  # extra term

    print("Analyzing output FCT...")
    print("python3 fctAnalysis.py -id {config_ID} -dir {dir} -bdp {bdp} -sT {fct_analysis_time_limit_begin} -fT {fct_analysistime_limit_end} > /dev/null 2>&1".format(
        config_ID=config_ID, dir=os.getcwd(), bdp=bdp, fct_analysis_time_limit_begin=fct_analysis_time_limit_begin, fct_analysistime_limit_end=fct_analysistime_limit_end))
    ret = os.system("python3 fctAnalysis.py -id {config_ID} -dir {dir} -bdp {bdp} -sT {fct_analysis_time_limit_begin} -fT {fct_analysistime_limit_end} > /dev/null 2>&1".format(
        config_ID=config_ID, dir=os.getcwd(), bdp=bdp, fct_analysis_time_limit_begin=fct_analysis_time_limit_begin, fct_analysistime_limit_end=fct_analysistime_limit_end))
    if ret != 0:
        raise RuntimeError("FCT analysis failed for run {}".format(config_ID))

    if lb_mode == 9: # ConWeave Logging
        ################################################################
        #             Analyze hardware resource of ConWeave            #
        ################################################################
        # NOTE: collect data except warm-up and cold-finish period
        queue_analysis_time_limit_begin = int(
            flowgen_start_time * 1e9) + int(0.005 * 1e9)  # warmup
        queue_analysistime_limit_end = int(flowgen_stop_time * 1e9)
        print("Analyzing output Queue...")
        print("python3 queueAnalysis.py -id {config_ID} -dir {dir} -sT {queue_analysis_time_limit_begin} -fT {queue_analysistime_limit_end} > /dev/null 2>&1".format(
            config_ID=config_ID, dir=os.getcwd(), queue_analysis_time_limit_begin=queue_analysis_time_limit_begin, queue_analysistime_limit_end=queue_analysistime_limit_end))
        os.system("python3 queueAnalysis.py -id {config_ID} -dir {dir} -sT {queue_analysis_time_limit_begin} -fT {queue_analysistime_limit_end} > /dev/null 2>&1".format(
            config_ID=config_ID, dir=os.getcwd(), queue_analysis_time_limit_begin=queue_analysis_time_limit_begin, queue_analysistime_limit_end=queue_analysistime_limit_end,
            monitoringInterval=sw_monitoring_interval))  # TODO: parameterize

    print("\n\n============== Done ============== ")


if __name__ == "__main__":
    main()
