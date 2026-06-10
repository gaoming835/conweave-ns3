#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
IMAGE="${DCP_DOCKER_IMAGE:-cw-sim:sigcomm23ae}"

cd "${REPO_ROOT}"

output="$(docker run --rm -v "${REPO_ROOT}:/root" "${IMAGE}" bash -lc \
  "cd /root && ./waf configure --build-profile=optimized >/tmp/dcp_waf_configure.log 2>&1 && ./waf >/tmp/dcp_waf_build.log 2>&1 && ./waf --run dcp-ooo-receiver-test")"

get_stat() {
  awk -F= -v field="$1" '$1 == field {print $2}' <<<"${output}"
}

dcp_ooo_packets="$(get_stat dcp_ooo_packets)"
dcp_completed_messages="$(get_stat dcp_completed_messages)"
dcp_ack_packets="$(get_stat dcp_ack_packets)"
dcp_spurious_retx="$(get_stat dcp_spurious_retx)"

for value in dcp_ooo_packets dcp_completed_messages dcp_ack_packets dcp_spurious_retx; do
  if [[ -z "${!value}" ]]; then
    echo "missing ${value} in dcp-ooo-receiver-test output" >&2
    echo "${output}" >&2
    exit 1
  fi
done

if (( dcp_ooo_packets <= 0 )); then
  echo "expected dcp_ooo_packets > 0, got ${dcp_ooo_packets}" >&2
  exit 1
fi
if (( dcp_spurious_retx != 0 )); then
  echo "expected dcp_spurious_retx == 0, got ${dcp_spurious_retx}" >&2
  exit 1
fi
if (( dcp_completed_messages <= 0 )); then
  echo "expected dcp_completed_messages > 0, got ${dcp_completed_messages}" >&2
  exit 1
fi

echo "dcp_ooo_smoke=pass"
echo "dcp_ooo_packets=${dcp_ooo_packets}"
echo "dcp_completed_messages=${dcp_completed_messages}"
echo "dcp_ack_packets=${dcp_ack_packets}"
echo "dcp_spurious_retx=${dcp_spurious_retx}"
