#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="${1:-${ROOT_DIR}/data/raw/funsd}"
ARCHIVE="${DATA_DIR}/dataset.zip"
URL="https://guillaumejaume.github.io/FUNSD/dataset.zip"
SHA256="${FUNSD_SHA256:-c31735649e4f441bcbb4fd0f379574f7520b42286e80b01d80b445649d54761f}"

mkdir -p "${DATA_DIR}"

if [[ -f "${ARCHIVE}" ]] && echo "${SHA256}  ${ARCHIVE}" | sha256sum --check --status; then
    echo "Using verified archive: ${ARCHIVE}"
else
    echo "Downloading FUNSD to ${ARCHIVE}"
    curl -L --fail --retry 3 "${URL}" -o "${ARCHIVE}.tmp"
    echo "${SHA256}  ${ARCHIVE}.tmp" | sha256sum --check --status || {
        rm -f "${ARCHIVE}.tmp"
        echo "FUNSD archive SHA256 verification failed" >&2
        exit 1
    }
    mv "${ARCHIVE}.tmp" "${ARCHIVE}"
fi

echo "Extracting FUNSD under ${DATA_DIR}"
unzip -q -o "${ARCHIVE}" -d "${DATA_DIR}"

echo "FUNSD ready:"
find "${DATA_DIR}" -maxdepth 3 -type d | sort
