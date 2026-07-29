#!/usr/bin/env bash
set -euo pipefail

READY_FILE="/tmp/model-pack-ready"
rm -f "$READY_FILE"

bash /workspace/scripts/setup_model_pack.sh --models-dir /models
touch "$READY_FILE"
echo "Model initialization completed; model pack is verified and ready"

exec sleep infinity
