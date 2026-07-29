#!/usr/bin/env bash
set -euo pipefail

BUNDLE_ROOT="${1:?usage: check_linux_cli_compatibility.sh BUNDLE_ROOT [MAX_GLIBC] [MAX_GLIBCXX]}"
MAX_GLIBC="${2:-2.31}"
MAX_GLIBCXX="${3:-3.4.28}"
EXECUTABLE="${BUNDLE_ROOT}/bin/document_intelligence_engine"

for command in find grep ldd readelf sort; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Missing required command: $command" >&2
    exit 1
  fi
done

if [[ ! -x "$EXECUTABLE" ]]; then
  echo "CLI executable not found: $EXECUTABLE" >&2
  exit 1
fi

if [[ ! -d "${BUNDLE_ROOT}/lib" ]]; then
  echo "CLI library directory not found: ${BUNDLE_ROOT}/lib" >&2
  exit 1
fi

version_at_most() {
  local actual="$1"
  local maximum="$2"
  [[ "$(printf '%s\n%s\n' "$actual" "$maximum" | sort -V | tail -n 1)" == "$maximum" ]]
}

maximum_required_version() {
  local prefix="$1"
  shift
  local version
  version="$(
    readelf --version-info "$@" 2>/dev/null |
      grep -oE "${prefix}_[0-9]+([.][0-9]+)*" |
      sed "s/^${prefix}_//" |
      sort -Vu |
      tail -n 1
  )"
  printf '%s' "${version:-0}"
}

elf_files=()
while IFS= read -r -d '' candidate; do
  if readelf -h "$candidate" >/dev/null 2>&1; then
    elf_files+=("$candidate")
  fi
done < <(find "${BUNDLE_ROOT}/bin" "${BUNDLE_ROOT}/lib" -type f -print0)

if [[ "${#elf_files[@]}" -eq 0 ]]; then
  echo "Portable CLI bundle contains no ELF files" >&2
  exit 1
fi

for elf_file in "${elf_files[@]}"; do
  dynamic_section="$(readelf -d "$elf_file" 2>/dev/null || true)"
  if grep -Eq 'NEEDED.*libopencv[^]]*[.]so' <<<"$dynamic_section"; then
    echo "Portable CLI must not depend on an OpenCV shared library: ${elf_file}" >&2
    grep 'NEEDED' <<<"$dynamic_section" >&2
    exit 1
  fi

  ldd_output="$(
    LD_LIBRARY_PATH="${BUNDLE_ROOT}/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" \
      ldd "$elf_file" 2>&1 || true
  )"
  if grep -q 'not found' <<<"$ldd_output"; then
    echo "Portable CLI has unresolved runtime dependencies: ${elf_file}" >&2
    printf '%s\n' "$ldd_output" >&2
    exit 1
  fi
done

required_glibc="$(maximum_required_version GLIBC "${elf_files[@]}")"
required_glibcxx="$(maximum_required_version GLIBCXX "${elf_files[@]}")"

if ! version_at_most "$required_glibc" "$MAX_GLIBC"; then
  echo "Portable CLI requires GLIBC_${required_glibc}; maximum allowed is GLIBC_${MAX_GLIBC}" >&2
  exit 1
fi
if ! version_at_most "$required_glibcxx" "$MAX_GLIBCXX"; then
  echo "Portable CLI requires GLIBCXX_${required_glibcxx}; maximum allowed is GLIBCXX_${MAX_GLIBCXX}" >&2
  exit 1
fi

echo "Portable CLI compatibility verified: GLIBC_${required_glibc}, GLIBCXX_${required_glibcxx}, static OpenCV"
