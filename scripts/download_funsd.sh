#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DATA_DIR="${1:-${ROOT_DIR}/data/raw/funsd}"
ARCHIVE="${DATA_DIR}/dataset.zip"
URL="https://guillaumejaume.github.io/FUNSD/dataset.zip"

mkdir -p "${DATA_DIR}"

if [[ ! -f "${ARCHIVE}" ]]; then
    echo "Downloading FUNSD to ${ARCHIVE}"
    curl -L "${URL}" -o "${ARCHIVE}"
else
    echo "Using existing archive: ${ARCHIVE}"
fi

echo "Extracting FUNSD under ${DATA_DIR}"
unzip -q -o "${ARCHIVE}" -d "${DATA_DIR}"

echo "FUNSD ready:"
find "${DATA_DIR}" -maxdepth 3 -type d | sort
