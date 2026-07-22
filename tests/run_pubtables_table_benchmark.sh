#!/usr/bin/env bash
set -euo pipefail

EVALUATOR="$1"
GROUND_TRUTH="$2"
PREDICTIONS="$3"
PYTHON="$4"
METRIC_SCRIPT="$5"
REPORT="$6"
MINIMUM_MICRO_F1="$7"

set +e
"$EVALUATOR" --ground-truth "$GROUND_TRUTH" --output "$PREDICTIONS"
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
