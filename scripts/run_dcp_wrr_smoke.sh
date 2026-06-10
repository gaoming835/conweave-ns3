#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${DCP_DOCKER_IMAGE:-cw-sim:sigcomm23ae}"

cd "${REPO_ROOT}"

before="$(mktemp)"
after="$(mktemp)"
history_backup="$(mktemp)"
small_flow_file="$(mktemp "${REPO_ROOT}/config/dcp_wrr_5host_flow.XXXXXX.txt")"
large_flow_file="$(mktemp "${REPO_ROOT}/config/dcp_wrr_128to1_flow.XXXXXX.txt")"
history_existed=0
if [[ -f mix/.history ]]; then
  cp mix/.history "${history_backup}"
  history_existed=1
fi
cleanup() {
  if [[ "${history_existed}" -eq 1 ]]; then
    cp "${history_backup}" mix/.history
  else
    rm -f mix/.history
  fi
  rm -f "${small_flow_file}" "${large_flow_file}" "${history_backup}"
}
trap cleanup EXIT

cat > "${small_flow_file}" <<'EOF'
4
0 1 3 4096 2.000001
0 2 3 4096 2.000001
0 3 3 4096 2.000001
0 4 3 4096 2.000001
EOF

{
  echo 127
  for src in $(seq 1 127); do
    printf '%d 0 3 2048 2.000001\n' "${src}"
  done
} > "${large_flow_file}"

small_flow_in_repo="${small_flow_file#${REPO_ROOT}/}"
large_flow_in_repo="${large_flow_file#${REPO_ROOT}/}"

find mix/output -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort > "${before}"

docker run --rm -v "${REPO_ROOT}:/root" "${IMAGE}" bash -lc \
  "cd /root && ./waf configure --build-profile=optimized >/tmp/dcp_waf_configure.log 2>&1 && ./waf >/tmp/dcp_waf_build.log 2>&1 && \
   python3 ./run.py --transport dcp --cc dcqcn --lb fecmp --pfc 1 --irn 0 --simul_time 0.01 --netload 10 --topo bcc_stage4_single_switch_5_25G_OS1 --bw 25 --flow_file ${small_flow_in_repo} --dcp_trim_threshold 1500 --dcp_enable_wrr 1 --dcp_control_weight 1 --dcp_data_weight 1 --skip_fct_analysis 1 && \
   python3 ./run.py --transport dcp --cc dcqcn --lb fecmp --pfc 1 --irn 0 --simul_time 0.01 --netload 10 --topo leaf_spine_128_100G_OS2 --bw 100 --flow_file ${large_flow_in_repo} --dcp_trim_threshold 4000 --dcp_enable_wrr 1 --dcp_control_weight 1 --dcp_data_weight 1 --skip_fct_analysis 1"

find mix/output -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort > "${after}"
mapfile -t run_ids < <(comm -13 "${before}" "${after}")
rm -f "${before}" "${after}"

if (( ${#run_ids[@]} < 2 )); then
  echo "failed to locate both WRR smoke output directories" >&2
  exit 1
fi

check_run() {
  local label="$1"
  local run_id="$2"
  local run_dir="mix/output/${run_id}"
  local stats_file="${run_dir}/${run_id}_out_dcp_stats.txt"

  grep -q '^ENABLE_DCP 1$' "${run_dir}/config.txt"
  grep -q '^DCP_ENABLE_WRR 1$' "${run_dir}/config.txt"
  grep -q '^DCP_CONTROL_WEIGHT 1$' "${run_dir}/config.txt"
  grep -q '^DCP_DATA_WEIGHT 1$' "${run_dir}/config.txt"
  test -f "${stats_file}"

  get_stat() {
    awk -F, -v field="$1" '$1 == field {print $2}' "${stats_file}"
  }

  local ho_dropped
  local data_packets
  local control_deq
  local data_deq
  local control_bytes
  local data_bytes
  local queue_samples
  ho_dropped="$(get_stat dcp_ho_dropped)"
  data_packets="$(get_stat dcp_data_packets)"
  control_deq="$(get_stat dcp_control_dequeue_packets)"
  data_deq="$(get_stat dcp_data_dequeue_packets)"
  control_bytes="$(get_stat dcp_control_dequeue_bytes)"
  data_bytes="$(get_stat dcp_data_dequeue_bytes)"
  queue_samples="$(get_stat dcp_queue_samples)"

  if [[ -z "${ho_dropped}" || -z "${data_packets}" || -z "${control_deq}" ||
        -z "${data_deq}" || -z "${control_bytes}" || -z "${data_bytes}" ||
        -z "${queue_samples}" ]]; then
    echo "missing DCP WRR counters in ${stats_file}" >&2
    exit 1
  fi
  if (( ho_dropped > 1 )); then
    echo "${label}: expected dcp_ho_dropped == 0 or close to 0, got ${ho_dropped}" >&2
    exit 1
  fi
  if (( data_packets <= 0 )); then
    echo "${label}: expected data packets to make progress, got ${data_packets}" >&2
    exit 1
  fi
  if (( control_deq <= 0 || data_deq <= 0 )); then
    echo "${label}: expected both control and data dequeue progress, got control=${control_deq}, data=${data_deq}" >&2
    exit 1
  fi
  if (( control_bytes <= 0 || data_bytes <= 0 || queue_samples <= 0 )); then
    echo "${label}: expected positive WRR byte/sample counters" >&2
    exit 1
  fi

  echo "${label}_run_id=${run_id}"
  echo "${label}_stats_file=${stats_file}"
  echo "${label}_dcp_ho_dropped=${ho_dropped}"
  echo "${label}_dcp_control_dequeue_packets=${control_deq}"
  echo "${label}_dcp_data_dequeue_packets=${data_deq}"
}

small_run_id=""
large_run_id=""
for run_id in "${run_ids[@]}"; do
  if grep -q '^TOPOLOGY_FILE config/bcc_stage4_single_switch_5_25G_OS1.txt$' "mix/output/${run_id}/config.txt"; then
    small_run_id="${run_id}"
  elif grep -q '^TOPOLOGY_FILE config/leaf_spine_128_100G_OS2.txt$' "mix/output/${run_id}/config.txt"; then
    large_run_id="${run_id}"
  fi
done

if [[ -z "${small_run_id}" || -z "${large_run_id}" ]]; then
  echo "failed to map WRR smoke run IDs to topologies" >&2
  printf 'new_run_ids=%s\n' "${run_ids[*]}" >&2
  exit 1
fi

check_run "dcp_wrr_5host" "${small_run_id}"
check_run "dcp_wrr_128to1" "${large_run_id}"
echo "dcp_wrr_smoke=pass"
