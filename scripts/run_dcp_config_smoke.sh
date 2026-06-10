#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${DCP_DOCKER_IMAGE:-cw-sim:sigcomm23ae}"

cd "${REPO_ROOT}"

before="$(mktemp)"
after="$(mktemp)"
history_backup="$(mktemp)"
flow_file="$(mktemp "${REPO_ROOT}/config/dcp_config_smoke_flow.XXXXXX.txt")"
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
  rm -f "${flow_file}"
  rm -f "${history_backup}"
}
trap cleanup EXIT

cat > "${flow_file}" <<'EOF'
1
0 1 3 1024 2.000001
EOF
flow_file_in_repo="${flow_file#${REPO_ROOT}/}"

find mix/output -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort > "${before}"

docker run --rm -v "${REPO_ROOT}:/root" "${IMAGE}" bash -lc \
  "cd /root && ./waf configure --build-profile=optimized >/tmp/dcp_waf_configure.log 2>&1 && ./waf >/tmp/dcp_waf_build.log 2>&1 && python3 ./run.py --transport dcp --cc dcqcn --lb fecmp --pfc 1 --irn 0 --simul_time 0.01 --netload 10 --topo bcc_stage4_single_switch_5_25G_OS1 --bw 25 --flow_file ${flow_file_in_repo} --skip_fct_analysis 1"

find mix/output -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort > "${after}"
run_id="$(comm -13 "${before}" "${after}" | tail -n 1)"
rm -f "${before}" "${after}"

if [[ -z "${run_id}" ]]; then
  echo "failed to locate smoke run output directory" >&2
  exit 1
fi

run_dir="mix/output/${run_id}"
stats_file="${run_dir}/${run_id}_out_dcp_stats.txt"

grep -q '^ENABLE_DCP 1$' "${run_dir}/config.txt"
grep -q '^TRANSPORT_MODE dcp$' "${run_dir}/config.txt"
grep -q '^DCP_STATS_FILE ' "${run_dir}/config.txt"
test -f "${stats_file}"

expected_fields=(
  dcp_data_packets
  dcp_ack_packets
  dcp_ho_packets
  dcp_trim_events
  dcp_ho_generated
  dcp_ho_returned
  dcp_ho_rx_at_receiver
  dcp_ho_rx_at_sender
  dcp_retransq_enqueue
  dcp_retransq_dequeue
  dcp_precise_retx
  dcp_spurious_retx
  dcp_timeout_retx
  dcp_ooo_packets
  dcp_completed_messages
  dcp_ho_dropped
  dcp_data_dropped
  control_queue_len
  data_queue_len
)

for field in "${expected_fields[@]}"; do
  grep -q "^${field}," "${stats_file}"
done

grep -q '^dcp_ho_packets,0$' "${stats_file}"
grep -q '^dcp_trim_events,0$' "${stats_file}"
grep -q '^dcp_ho_generated,0$' "${stats_file}"
grep -q '^dcp_ho_rx_at_receiver,0$' "${stats_file}"
grep -q '^dcp_ho_rx_at_sender,0$' "${stats_file}"
grep -q '^dcp_ho_dropped,0$' "${stats_file}"
grep -q '^dcp_data_dropped,0$' "${stats_file}"

echo "dcp_config_smoke=pass"
echo "run_id=${run_id}"
echo "stats_file=${stats_file}"
