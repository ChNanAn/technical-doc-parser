#!/usr/bin/env bash
set -euo pipefail

EVALUATOR="$1"
BACKEND="$2"
GROUND_TRUTH="$3"
PREDICTIONS="$4"
PYTHON="$5"
METRIC_SCRIPT="$6"
REPORT="$7"
MINIMUM_MICRO_F1="$8"

set +e
"$EVALUATOR" --backend "$BACKEND" --ground-truth "$GROUND_TRUTH" --output "$PREDICTIONS"
status=$?
set -e
if [[ "$status" == "77" ]]; then
  exit 77
fi
if [[ "$status" != "0" ]]; then
  exit "$status"
fi

"$PYTHON" "$METRIC_SCRIPT" \
  --ground-truth "$GROUND_TRUTH" \
  --predictions "$PREDICTIONS" \
  --output "$REPORT" \
  --minimum-micro-f1 "$MINIMUM_MICRO_F1"
