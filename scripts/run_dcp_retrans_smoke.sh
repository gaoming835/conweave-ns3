#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${DCP_DOCKER_IMAGE:-cw-sim:sigcomm23ae}"

cd "${REPO_ROOT}"

before="$(mktemp)"
after="$(mktemp)"
history_backup="$(mktemp)"
flow_file="$(mktemp "${REPO_ROOT}/config/dcp_retrans_smoke_flow.XXXXXX.txt")"
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
2
0 1 3 8192 2.000001
0 2 3 8192 2.000001
EOF
flow_file_in_repo="${flow_file#${REPO_ROOT}/}"

find mix/output -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null | sort > "${before}"

docker run --rm -v "${REPO_ROOT}:/root" "${IMAGE}" bash -lc \
  "cd /root && ./waf configure --build-profile=optimized >/tmp/dcp_waf_configure.log 2>&1 && ./waf >/tmp/dcp_waf_build.log 2>&1 && ./waf --run dcp-retrans-queue-test && python3 ./run.py --transport dcp --cc dcqcn --lb fecmp --pfc 1 --irn 0 --simul_time 0.01 --netload 10 --topo bcc_stage4_single_switch_5_25G_OS1 --bw 25 --flow_file ${flow_file_in_repo} --dcp_trim_threshold 4294967295 --dcp_retrans_batch_size 1 --dcp_retrans_quota_bytes 0 --dcp_retrans_respect_win 0 --dcp_enable_timeout_retx 0 --skip_fct_analysis 1"

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
grep -q '^DCP_TRIM_THRESHOLD 4294967295$' "${run_dir}/config.txt"
grep -q '^DCP_RETRANS_PER_ROUND 1$' "${run_dir}/config.txt"
grep -q '^DCP_RETRANS_BATCH_SIZE 1$' "${run_dir}/config.txt"
grep -q '^DCP_RETRANS_QUOTA_BYTES 0$' "${run_dir}/config.txt"
grep -q '^DCP_RETRANS_RESPECT_WIN 0$' "${run_dir}/config.txt"
grep -q '^DCP_ENABLE_TIMEOUT_RETX 0$' "${run_dir}/config.txt"
test -f "${stats_file}"

get_stat() {
  awk -F, -v field="$1" '$1 == field {print $2}' "${stats_file}"
}

dcp_retransq_enqueue="$(get_stat dcp_retransq_enqueue)"
dcp_retransq_dequeue="$(get_stat dcp_retransq_dequeue)"
dcp_precise_retx="$(get_stat dcp_precise_retx)"
dcp_timeout_retx="$(get_stat dcp_timeout_retx)"
dcp_spurious_retx="$(get_stat dcp_spurious_retx)"
dcp_retrans_bytes="$(get_stat dcp_retrans_bytes)"
dcp_retrans_from_ho="$(get_stat dcp_retrans_from_ho)"
dcp_retrans_from_timeout="$(get_stat dcp_retrans_from_timeout)"
dcp_retrans_retrimmed="$(get_stat dcp_retrans_retrimmed)"

for value in dcp_retransq_enqueue dcp_retransq_dequeue dcp_precise_retx dcp_timeout_retx dcp_spurious_retx dcp_retrans_bytes dcp_retrans_from_ho dcp_retrans_from_timeout dcp_retrans_retrimmed; do
  if [[ -z "${!value}" ]]; then
    echo "missing ${value} in ${stats_file}" >&2
    exit 1
  fi
done

if (( dcp_precise_retx != 0 )); then
  echo "expected no-trim simulation dcp_precise_retx == 0, got ${dcp_precise_retx}" >&2
  exit 1
fi
if (( dcp_spurious_retx != 0 )); then
  echo "expected dcp_spurious_retx == 0, got ${dcp_spurious_retx}" >&2
  exit 1
fi
if (( dcp_timeout_retx != 0 )); then
  echo "expected default dcp_timeout_retx == 0, got ${dcp_timeout_retx}" >&2
  exit 1
fi
if (( dcp_retrans_bytes != 0 )); then
  echo "expected no-trim simulation dcp_retrans_bytes == 0, got ${dcp_retrans_bytes}" >&2
  exit 1
fi
if (( dcp_retrans_from_ho != dcp_precise_retx )); then
  echo "expected dcp_retrans_from_ho == dcp_precise_retx, got ho=${dcp_retrans_from_ho}, precise=${dcp_precise_retx}" >&2
  exit 1
fi
if (( dcp_retrans_from_timeout != 0 )); then
  echo "expected dcp_retrans_from_timeout == 0 with timeout fallback disabled, got ${dcp_retrans_from_timeout}" >&2
  exit 1
fi

echo "dcp_retrans_smoke=pass"
echo "run_id=${run_id}"
echo "stats_file=${stats_file}"
echo "dcp_retransq_enqueue=${dcp_retransq_enqueue}"
echo "dcp_retransq_dequeue=${dcp_retransq_dequeue}"
echo "dcp_precise_retx=${dcp_precise_retx}"
echo "dcp_timeout_retx=${dcp_timeout_retx}"
echo "dcp_retrans_bytes=${dcp_retrans_bytes}"
echo "dcp_retrans_from_ho=${dcp_retrans_from_ho}"
echo "dcp_retrans_from_timeout=${dcp_retrans_from_timeout}"
echo "dcp_retrans_retrimmed=${dcp_retrans_retrimmed}"
echo "dcp_spurious_retx=${dcp_spurious_retx}"
