#!/usr/bin/env bash
set -euo pipefail

PRODUCER="$1"
GROUND_TRUTH="$2"
PREDICTIONS="$3"
PYTHON="$4"
METRIC_SCRIPT="$5"
REPORT="$6"
MAXIMUM_CER="$7"

"$PRODUCER" --ground-truth "$GROUND_TRUTH" --output "$PREDICTIONS"

"$PYTHON" "$METRIC_SCRIPT" \
  --ground-truth "$GROUND_TRUTH" \
  --predictions "$PREDICTIONS" \
  --output "$REPORT" \
  --ignore-case \
  --maximum-cer "$MAXIMUM_CER"
