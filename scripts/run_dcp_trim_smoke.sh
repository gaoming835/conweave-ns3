#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${DCP_DOCKER_IMAGE:-cw-sim:sigcomm23ae}"

cd "${REPO_ROOT}"

before="$(mktemp)"
after="$(mktemp)"
history_backup="$(mktemp)"
flow_file="$(mktemp "${REPO_ROOT}/config/dcp_trim_smoke_flow.XXXXXX.txt")"
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
4
0 1 3 32768 2.000001
0 2 3 32768 2.000001
0 3 3 32768 2.000001
0 4 3 32768 2.000001
EOF
flow_file_in_repo="${flow_file#${REPO_ROOT}/}"

find mix/output -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort > "${before}"

docker run --rm -v "${REPO_ROOT}:/root" "${IMAGE}" bash -lc \
  "cd /root && ./waf configure --build-profile=optimized >/tmp/dcp_waf_configure.log 2>&1 && ./waf >/tmp/dcp_waf_build.log 2>&1 && ./waf --run dcp-trim-semantics-test && python3 ./run.py --transport dcp --cc dcqcn --lb fecmp --pfc 1 --irn 0 --simul_time 0.01 --netload 10 --topo bcc_stage4_single_switch_5_25G_OS1 --bw 25 --flow_file ${flow_file_in_repo} --dcp_trim_threshold 1000 --skip_fct_analysis 1"

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
grep -q '^DCP_TRIM_THRESHOLD 1000$' "${run_dir}/config.txt"
grep -q '^DCP_HO_SIZE 0$' "${run_dir}/config.txt"
test -f "${stats_file}"

get_stat() {
  awk -F, -v field="$1" '$1 == field {print $2}' "${stats_file}"
}

dcp_trim_events="$(get_stat dcp_trim_events)"
dcp_ho_generated="$(get_stat dcp_ho_generated)"
dcp_ho_dropped="$(get_stat dcp_ho_dropped)"
dcp_ho_bytes="$(get_stat dcp_ho_bytes)"
dcp_data_bytes_trimmed="$(get_stat dcp_data_bytes_trimmed)"
dcp_non_dropped="$(get_stat dcp_non_dropped)"
dcp_ack_dropped="$(get_stat dcp_ack_dropped)"

if [[ -z "${dcp_trim_events}" || -z "${dcp_ho_generated}" || -z "${dcp_ho_dropped}" ||
      -z "${dcp_ho_bytes}" || -z "${dcp_data_bytes_trimmed}" ||
      -z "${dcp_non_dropped}" || -z "${dcp_ack_dropped}" ]]; then
  echo "missing DCP trim counters in ${stats_file}" >&2
  exit 1
fi
if (( dcp_trim_events <= 0 )); then
  echo "expected dcp_trim_events > 0, got ${dcp_trim_events}" >&2
  exit 1
fi
if (( dcp_ho_generated <= 0 )); then
  echo "expected dcp_ho_generated > 0, got ${dcp_ho_generated}" >&2
  exit 1
fi
if (( dcp_ho_dropped > 1 )); then
  echo "expected dcp_ho_dropped == 0 or close to 0, got ${dcp_ho_dropped}" >&2
  exit 1
fi
if (( dcp_ho_bytes <= 0 )); then
  echo "expected dcp_ho_bytes > 0, got ${dcp_ho_bytes}" >&2
  exit 1
fi
if (( dcp_data_bytes_trimmed <= 0 )); then
  echo "expected dcp_data_bytes_trimmed > 0, got ${dcp_data_bytes_trimmed}" >&2
  exit 1
fi

echo "dcp_trim_smoke=pass"
echo "run_id=${run_id}"
echo "stats_file=${stats_file}"
echo "dcp_trim_events=${dcp_trim_events}"
echo "dcp_ho_generated=${dcp_ho_generated}"
echo "dcp_ho_dropped=${dcp_ho_dropped}"
echo "dcp_ho_bytes=${dcp_ho_bytes}"
echo "dcp_data_bytes_trimmed=${dcp_data_bytes_trimmed}"
echo "dcp_non_dropped=${dcp_non_dropped}"
echo "dcp_ack_dropped=${dcp_ack_dropped}"
