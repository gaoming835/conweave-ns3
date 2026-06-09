#!/bin/bash

cecho(){
    RED="\033[0;31m"
    GREEN="\033[0;32m"
    YELLOW="\033[0;33m"
    NC="\033[0m"

    printf "${!1}${2} ${NC}\n"
}

cecho "GREEN" "Running ConWeave + BCC simulations (leaf-spine topology)"

TOPOLOGY="${TOPOLOGY:-leaf_spine_128_100G_OS2}"
NETLOAD="${NETLOAD:-50}"
RUNTIME="${RUNTIME:-0.1}"

cecho "YELLOW" "\n----------------------------------"
cecho "YELLOW" "TOPOLOGY: ${TOPOLOGY}"
cecho "YELLOW" "NETWORK LOAD: ${NETLOAD}"
cecho "YELLOW" "TIME: ${RUNTIME}"
cecho "YELLOW" "CC: BCC"
cecho "YELLOW" "LB: ConWeave"
cecho "YELLOW" "----------------------------------\n"

cecho "GREEN" "Run Lossless RDMA: BCC congestion control with ConWeave load balancing..."
python3 run.py --cc bcc --lb conweave --pfc 1 --irn 0 --simul_time "${RUNTIME}" --netload "${NETLOAD}" --topo "${TOPOLOGY}" 2>&1 > /dev/null &
sleep 5

cecho "GREEN" "Run IRN RDMA: BCC congestion control with ConWeave load balancing..."
python3 run.py --cc bcc --lb conweave --pfc 0 --irn 1 --simul_time "${RUNTIME}" --netload "${NETLOAD}" --topo "${TOPOLOGY}" 2>&1 > /dev/null &
sleep 0.1

cecho "GREEN" "Running all ConWeave + BCC experiments in parallel. Check the processors running in background!"
