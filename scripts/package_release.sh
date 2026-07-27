#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
KIND="all"
BUILD_DIR="${ROOT_DIR}/build/core-release"
OUTPUT_DIR="${ROOT_DIR}/dist"
SOURCE_REF="HEAD"
SOURCE_REVISION_OVERRIDE=""
SOURCE_DATE_EPOCH_OVERRIDE=""

usage() {
  cat <<EOF
Usage:
  bash scripts/package_release.sh [options]

Options:
  --kind source|cli|all  Artifacts to create. Default: all.
  --build-dir PATH       Configured CMake build directory for CLI packaging.
  --output-dir PATH      Artifact output directory. Default: dist.
  --source-ref REF       Git revision for the source archive. Default: HEAD.
  --revision SHA         Recorded revision for a CLI-only source tree.
  --source-date-epoch N  Archive timestamp for a CLI-only source tree.
  -h, --help             Show this help message.

The CLI bundle supports Linux x86-64 builds and intentionally excludes models.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --kind)
      KIND="${2:?--kind requires a value}"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="${2:?--build-dir requires a value}"
      shift 2
      ;;
    --output-dir)
      OUTPUT_DIR="${2:?--output-dir requires a value}"
      shift 2
      ;;
    --source-ref)
      SOURCE_REF="${2:?--source-ref requires a value}"
      shift 2
      ;;
    --revision)
      SOURCE_REVISION_OVERRIDE="${2:?--revision requires a value}"
      shift 2
      ;;
    --source-date-epoch)
      SOURCE_DATE_EPOCH_OVERRIDE="${2:?--source-date-epoch requires a value}"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "$KIND" != "source" && "$KIND" != "cli" && "$KIND" != "all" ]]; then
  echo "Unsupported package kind: $KIND" >&2
  exit 2
fi

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

require_cmd cmake
require_cmd gzip
require_cmd sha256sum
require_cmd tar

PROJECT_VERSION="$(
  awk '$1 == "VERSION" {print $2; exit}' "${ROOT_DIR}/CMakeLists.txt"
)"
if [[ -z "$PROJECT_VERSION" ]]; then
  echo "Unable to read the project version from CMakeLists.txt" >&2
  exit 1
fi

if [[ "$KIND" == "source" || "$KIND" == "all" ]]; then
  require_cmd git
  SOURCE_REVISION="$(git -C "$ROOT_DIR" rev-parse --verify "${SOURCE_REF}^{commit}")"
  SOURCE_DATE_EPOCH="$(git -C "$ROOT_DIR" show -s --format=%ct "$SOURCE_REVISION")"
else
  if [[ -n "$SOURCE_REVISION_OVERRIDE" && -n "$SOURCE_DATE_EPOCH_OVERRIDE" ]]; then
    SOURCE_REVISION="$SOURCE_REVISION_OVERRIDE"
    SOURCE_DATE_EPOCH="$SOURCE_DATE_EPOCH_OVERRIDE"
  else
    require_cmd git
    SOURCE_REVISION="$(git -C "$ROOT_DIR" rev-parse --verify "${SOURCE_REF}^{commit}")"
    SOURCE_DATE_EPOCH="$(git -C "$ROOT_DIR" show -s --format=%ct "$SOURCE_REVISION")"
  fi
fi

if [[ ! "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]]; then
  echo "Source date epoch must be a non-negative integer: ${SOURCE_DATE_EPOCH}" >&2
  exit 2
fi
PACKAGE_BASENAME="technical-doc-parser-${PROJECT_VERSION}"

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(cd "$OUTPUT_DIR" && pwd)"
generated_artifacts=()

package_source() {
  local archive="${OUTPUT_DIR}/${PACKAGE_BASENAME}-source.tar.gz"
  local temporary_tar
  temporary_tar="$(mktemp)"

  git -C "$ROOT_DIR" archive \
    --format=tar \
    --prefix="${PACKAGE_BASENAME}/" \
    "$SOURCE_REVISION" > "$temporary_tar"
  gzip -n -9 < "$temporary_tar" > "$archive"
  rm -f "$temporary_tar"
  generated_artifacts+=("$archive")
}

copy_private_runtime_libraries() {
  local executable="$1"
  local destination="$2"
  local licenses_destination="$3"
  local needed resolved ignored
  local dependency_root
  local copied_pdfium=0
  local copied_onnxruntime=0

  if ldd "$executable" | grep -q 'not found'; then
    echo "The CLI has unresolved runtime dependencies:" >&2
    ldd "$executable" >&2
    return 1
  fi

  while read -r needed ignored resolved _; do
    case "$needed" in
      libpdfium.so*)
        dependency_root="$(cd "$(dirname "$resolved")/.." && pwd)"
        if [[ ! -f "${dependency_root}/LICENSE" || ! -f "${dependency_root}/VERSION" ]]; then
          echo "PDFium license or version metadata is missing under ${dependency_root}" >&2
          return 1
        fi
        cp -L -- "$resolved" "${destination}/${needed}"
        mkdir -p "${licenses_destination}/pdfium"
        cp -- "${dependency_root}/LICENSE" "${licenses_destination}/pdfium/"
        if [[ -d "${dependency_root}/licenses" ]]; then
          cp -R -- "${dependency_root}/licenses" "${licenses_destination}/pdfium/"
        fi
        BUNDLED_PDFIUM_LIBRARY="$needed"
        BUNDLED_PDFIUM_VERSION="$(tr -d '\r\n' < "${dependency_root}/VERSION")"
        copied_pdfium=1
        ;;
      libonnxruntime.so*)
        dependency_root="$(cd "$(dirname "$resolved")/.." && pwd)"
        if [[ ! -f "${dependency_root}/LICENSE" ||
              ! -f "${dependency_root}/ThirdPartyNotices.txt" ]]; then
          echo "ONNX Runtime license metadata is missing under ${dependency_root}" >&2
          return 1
        fi
        cp -L -- "$resolved" "${destination}/${needed}"
        mkdir -p "${licenses_destination}/onnxruntime"
        cp -- \
          "${dependency_root}/LICENSE" \
          "${dependency_root}/ThirdPartyNotices.txt" \
          "${licenses_destination}/onnxruntime/"
        BUNDLED_ONNXRUNTIME_LIBRARY="$needed"
        BUNDLED_ONNXRUNTIME_VERSION="${needed#libonnxruntime.so.}"
        copied_onnxruntime=1
        ;;
    esac
  done < <(ldd "$executable")

  if [[ "$copied_pdfium" != "1" || "$copied_onnxruntime" != "1" ]]; then
    echo "Expected PDFium and ONNX Runtime in the CLI dependency graph" >&2
    return 1
  fi
}

package_cli() {
  if [[ "$(uname -s)" != "Linux" || "$(uname -m)" != "x86_64" ]]; then
    echo "CLI bundle packaging currently supports Linux x86-64 only" >&2
    return 1
  fi
  if [[ ! -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    echo "Configured CMake build directory not found: ${BUILD_DIR}" >&2
    return 1
  fi

  local configured_version
  configured_version="$(
    awk -F= '$1 == "CMAKE_PROJECT_VERSION:STATIC" {print $2; exit}' \
      "${BUILD_DIR}/CMakeCache.txt"
  )"
  if [[ "$configured_version" != "$PROJECT_VERSION" ]]; then
    echo "Build version '${configured_version}' does not match project version '${PROJECT_VERSION}'" >&2
    return 1
  fi

  local temporary_dir stage package_root build_executable executable archive
  temporary_dir="$(mktemp -d)"
  stage="${temporary_dir}/stage"
  package_root="${temporary_dir}/${PACKAGE_BASENAME}-linux-x86_64-cli"
  archive="${OUTPUT_DIR}/${PACKAGE_BASENAME}-linux-x86_64-cli.tar.gz"

  cmake --install "$BUILD_DIR" --prefix "$stage" --component Runtime
  mkdir -p \
    "${package_root}/bin" \
    "${package_root}/lib" \
    "${package_root}/share/licenses"
  cp -- "${stage}/bin/document_intelligence_engine" "${package_root}/bin/"
  cp -R -- "${stage}/share/DocumentIntelligenceEngine/." "${package_root}/share/"

  build_executable="${BUILD_DIR}/cpp/app/document_intelligence_engine"
  executable="${package_root}/bin/document_intelligence_engine"
  if [[ ! -x "$build_executable" ]]; then
    echo "Built CLI executable not found: ${build_executable}" >&2
    return 1
  fi
  BUNDLED_PDFIUM_LIBRARY=""
  BUNDLED_PDFIUM_VERSION=""
  BUNDLED_ONNXRUNTIME_LIBRARY=""
  BUNDLED_ONNXRUNTIME_VERSION=""
  copy_private_runtime_libraries \
    "$build_executable" \
    "${package_root}/lib" \
    "${package_root}/share/licenses"

  local engine_sha256 pdfium_sha256 onnxruntime_sha256
  engine_sha256="$(sha256sum "$executable" | awk '{print $1}')"
  pdfium_sha256="$(
    sha256sum "${package_root}/lib/${BUNDLED_PDFIUM_LIBRARY}" | awk '{print $1}'
  )"
  onnxruntime_sha256="$(
    sha256sum "${package_root}/lib/${BUNDLED_ONNXRUNTIME_LIBRARY}" | awk '{print $1}'
  )"

  cat > "${package_root}/BUILD-INFO" <<EOF
name=technical-doc-parser
version=${PROJECT_VERSION}
revision=${SOURCE_REVISION}
platform=linux-x86_64
license=MIT
models_included=false
EOF

  cat > "${package_root}/PROGRAM-MANIFEST.json" <<EOF
{
  "schema_version": 1,
  "name": "technical-doc-parser",
  "version": "${PROJECT_VERSION}",
  "revision": "${SOURCE_REVISION}",
  "platform": "linux-x86_64",
  "models_included": false,
  "files": [
    {
      "path": "bin/document_intelligence_engine",
      "sha256": "${engine_sha256}",
      "component": "engine",
      "version": "${PROJECT_VERSION}",
      "license": "MIT"
    },
    {
      "path": "lib/${BUNDLED_PDFIUM_LIBRARY}",
      "sha256": "${pdfium_sha256}",
      "component": "pdfium-binaries",
      "version": "${BUNDLED_PDFIUM_VERSION}",
      "license": "MIT"
    },
    {
      "path": "lib/${BUNDLED_ONNXRUNTIME_LIBRARY}",
      "sha256": "${onnxruntime_sha256}",
      "component": "onnxruntime",
      "version": "${BUNDLED_ONNXRUNTIME_VERSION}",
      "license": "MIT"
    }
  ]
}
EOF

  "$executable" --help >/dev/null
  tar \
    --sort=name \
    --mtime="@${SOURCE_DATE_EPOCH}" \
    --owner=0 \
    --group=0 \
    --numeric-owner \
    --use-compress-program="gzip -n -9" \
    -cf "$archive" \
    -C "$temporary_dir" \
    "$(basename "$package_root")"

  rm -rf "$temporary_dir"
  generated_artifacts+=("$archive")
}

case "$KIND" in
  source)
    package_source
    ;;
  cli)
    package_cli
    ;;
  all)
    package_source
    package_cli
    ;;
esac

checksum_file="${OUTPUT_DIR}/SHA256SUMS"
: > "$checksum_file"
for artifact in "${generated_artifacts[@]}"; do
  (
    cd "$OUTPUT_DIR"
    sha256sum "$(basename "$artifact")"
  ) >> "$checksum_file"
done

echo "Created release artifacts:"
for artifact in "${generated_artifacts[@]}"; do
  echo "  ${artifact}"
done
echo "  ${checksum_file}"
