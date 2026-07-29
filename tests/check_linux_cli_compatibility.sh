#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:?usage: check_linux_cli_compatibility.sh ROOT_DIR}"
CHECKER="${ROOT_DIR}/scripts/check_linux_cli_compatibility.sh"
TEMPORARY_DIR="$(mktemp -d)"
trap 'rm -rf "$TEMPORARY_DIR"' EXIT

require_failure() {
  local expected_message="$1"
  shift
  local output
  if output="$("$@" 2>&1)"; then
    echo "Command unexpectedly succeeded: $*" >&2
    exit 1
  fi
  if ! grep -Fq "$expected_message" <<<"$output"; then
    echo "Failure did not contain '${expected_message}':" >&2
    printf '%s\n' "$output" >&2
    exit 1
  fi
}

mkdir -p "${TEMPORARY_DIR}/portable/bin" "${TEMPORARY_DIR}/portable/lib"
cp /bin/true "${TEMPORARY_DIR}/portable/bin/document_intelligence_engine"
bash "$CHECKER" "${TEMPORARY_DIR}/portable" 999 999

require_failure \
  "maximum allowed is GLIBC_0" \
  bash "$CHECKER" "${TEMPORARY_DIR}/portable" 0 999

mkdir -p "${TEMPORARY_DIR}/dynamic/bin" "${TEMPORARY_DIR}/dynamic/lib"
cc -shared -fPIC \
  -Wl,-soname,libopencv_core.so.406 \
  -x c -o "${TEMPORARY_DIR}/dynamic/lib/libopencv_core.so.406" \
  - <<'EOF'
int opencv_fixture(void) {
    return 0;
}
EOF
cc -x c - \
  -L"${TEMPORARY_DIR}/dynamic/lib" \
  -Wl,-rpath,'$ORIGIN/../lib' \
  -Wl,--no-as-needed \
  -lopencv_core \
  -o "${TEMPORARY_DIR}/dynamic/bin/document_intelligence_engine" \
  <<'EOF'
int main(void) {
    return 0;
}
EOF

require_failure \
  "must not depend on an OpenCV shared library" \
  bash "$CHECKER" "${TEMPORARY_DIR}/dynamic" 999 999
