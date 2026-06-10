#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${DCP_DOCKER_IMAGE:-cw-sim:sigcomm23ae}"

cd "${REPO_ROOT}"

before="$(mktemp)"
after="$(mktemp)"
history_backup="$(mktemp)"
flow_file="$(mktemp "${REPO_ROOT}/config/dcp_ar_smoke_flow.XXXXXX.txt")"
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
  rm -f "${flow_file}" "${history_backup}"
}
trap cleanup EXIT

cat > "${flow_file}" <<'EOF'
2
0 1 3 64000 2.000001
0 1 3 64000 2.000001
EOF

flow_in_repo="${flow_file#${REPO_ROOT}/}"

find mix/output -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort > "${before}"

docker run --rm -v "${REPO_ROOT}:/root" "${IMAGE}" bash -lc \
  "cd /root && ./waf configure --build-profile=optimized >/tmp/dcp_waf_configure.log 2>&1 && ./waf >/tmp/dcp_waf_build.log 2>&1 && \
   python3 ./run.py --transport rdma --cc dcqcn --lb ar --pfc 0 --irn 1 --simul_time 0.01 --netload 10 --topo dcp_ar_2path_100G_OS1 --bw 100 --flow_file ${flow_in_repo} --skip_fct_analysis 1 && \
   python3 ./run.py --transport dcp --cc dcqcn --lb ar --pfc 1 --irn 0 --simul_time 0.01 --netload 10 --topo dcp_ar_2path_100G_OS1 --bw 100 --flow_file ${flow_in_repo} --skip_fct_analysis 1"

find mix/output -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort > "${after}"
mapfile -t run_ids < <(comm -13 "${before}" "${after}")
rm -f "${before}" "${after}"

if (( ${#run_ids[@]} < 2 )); then
  echo "failed to locate both AR smoke output directories" >&2
  exit 1
fi

irn_run_id=""
dcp_run_id=""
for run_id in "${run_ids[@]}"; do
  run_dir="mix/output/${run_id}"
  if grep -q '^LB_MODE 11$' "${run_dir}/config.txt" &&
     grep -q '^ENABLE_IRN 1$' "${run_dir}/config.txt"; then
    irn_run_id="${run_id}"
  elif grep -q '^LB_MODE 11$' "${run_dir}/config.txt" &&
       grep -q '^ENABLE_DCP 1$' "${run_dir}/config.txt"; then
    dcp_run_id="${run_id}"
  fi
done

if [[ -z "${irn_run_id}" || -z "${dcp_run_id}" ]]; then
  echo "failed to map AR smoke run IDs" >&2
  printf 'new_run_ids=%s\n' "${run_ids[*]}" >&2
  exit 1
fi

dcp_run_dir="mix/output/${dcp_run_id}"
dcp_stats_file="${dcp_run_dir}/${dcp_run_id}_out_dcp_stats.txt"
dcp_ar_stats_file="${dcp_run_dir}/${dcp_run_id}_out_ar_stats.txt"
test -f "${dcp_stats_file}"
test -f "${dcp_ar_stats_file}"

get_dcp_stat() {
  awk -F, -v field="$1" '$1 == field {print $2}' "${dcp_stats_file}"
}

ar_packets="$(get_dcp_stat ar_packets)"
ar_path_switches="$(get_dcp_stat ar_path_switches)"
ar_used_next_hops="$(get_dcp_stat ar_used_next_hops)"
dcp_spurious_retx="$(get_dcp_stat dcp_spurious_retx)"
dcp_data_packets="$(get_dcp_stat dcp_data_packets)"

for value in ar_packets ar_path_switches ar_used_next_hops dcp_spurious_retx dcp_data_packets; do
  if [[ -z "${!value}" ]]; then
    echo "missing ${value} in ${dcp_stats_file}" >&2
    exit 1
  fi
done

if (( dcp_data_packets <= 0 || ar_packets <= 0 )); then
  echo "expected DCP+AR data packets to make progress, got data=${dcp_data_packets}, ar=${ar_packets}" >&2
  exit 1
fi
if (( ar_path_switches <= 0 )); then
  echo "expected DCP+AR packet-level path switches, got ${ar_path_switches}" >&2
  exit 1
fi
if (( ar_used_next_hops < 2 )); then
  echo "expected DCP+AR to use multiple next-hops, got ${ar_used_next_hops}" >&2
  exit 1
fi
if (( dcp_spurious_retx != 0 )); then
  echo "expected DCP+AR spurious retransmissions to stay zero, got ${dcp_spurious_retx}" >&2
  exit 1
fi

irn_run_dir="mix/output/${irn_run_id}"
irn_ar_stats_file="${irn_run_dir}/${irn_run_id}_out_ar_stats.txt"
test -f "${irn_ar_stats_file}"
get_irn_stat() {
  awk -F, -v field="$1" '$1 == field {print $2}' "${irn_ar_stats_file}"
}
irn_ar_packets="$(get_irn_stat ar_packets)"
irn_ar_path_switches="$(get_irn_stat ar_path_switches)"
irn_ar_used_next_hops="$(get_irn_stat ar_used_next_hops)"
irn_ooo_packets="$(get_irn_stat irn_ooo_packets)"
irn_nack_packets="$(get_irn_stat irn_nack_packets)"

for value in irn_ar_packets irn_ar_path_switches irn_ar_used_next_hops irn_ooo_packets irn_nack_packets; do
  if [[ -z "${!value}" ]]; then
    echo "missing ${value} in ${irn_ar_stats_file}" >&2
    exit 1
  fi
done

if (( irn_ar_packets <= 0 || irn_ar_path_switches <= 0 || irn_ar_used_next_hops < 2 )); then
  echo "expected IRN+AR to use packet-level multipath, got packets=${irn_ar_packets}, switches=${irn_ar_path_switches}, hops=${irn_ar_used_next_hops}" >&2
  exit 1
fi
if (( irn_ooo_packets <= 0 || irn_nack_packets <= 0 )); then
  echo "expected IRN+AR to emit OOO/NACK signal, got ooo=${irn_ooo_packets}, nack=${irn_nack_packets}" >&2
  exit 1
fi

echo "dcp_ar_irn_run_id=${irn_run_id}"
echo "dcp_ar_dcp_run_id=${dcp_run_id}"
echo "dcp_ar_stats_file=${dcp_stats_file}"
echo "irn_ar_stats_file=${irn_ar_stats_file}"
echo "dcp_ar_packets=${ar_packets}"
echo "dcp_ar_path_switches=${ar_path_switches}"
echo "dcp_ar_used_next_hops=${ar_used_next_hops}"
echo "irn_ar_ooo_packets=${irn_ooo_packets}"
echo "irn_ar_nack_packets=${irn_nack_packets}"
echo "dcp_ar_smoke=pass"
