#!/usr/bin/env bash
set -euo pipefail

if [[ ! -r /etc/os-release ]]; then
  echo "This installer supports Ubuntu/Debian systems only." >&2
  exit 1
fi

# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != "ubuntu" && "${ID:-}" != "debian" && "${ID_LIKE:-}" != *debian* ]]; then
  echo "Unsupported distribution: ${PRETTY_NAME:-unknown}. Install OpenCV 4.2+ with your system package manager." >&2
  exit 1
fi

APT=(apt-get)
if [[ "${EUID}" -ne 0 ]]; then
  if ! command -v sudo >/dev/null 2>&1; then
    echo "sudo is required when this script is not run as root." >&2
    exit 1
  fi
  APT=(sudo apt-get)
fi

"${APT[@]}" update
"${APT[@]}" install -y libopencv-dev

echo "OpenCV development dependencies are installed."
