#!/usr/bin/env bash
set -euo pipefail

PRODUCER="$1"
MANIFEST="$2"
INPUT_ROOT="$3"
WORK_DIR="$4"
PREDICTIONS="$5"
PYTHON="$6"
EVALUATOR="$7"
GROUND_TRUTH="$8"
REPORT="$9"
MINIMUM_MICRO_F1="${10}"

"$PRODUCER" \
  --manifest "$MANIFEST" \
  --input-root "$INPUT_ROOT" \
  --work-dir "$WORK_DIR" \
  --output "$PREDICTIONS"

"$PYTHON" "$EVALUATOR" \
  --ground-truth "$GROUND_TRUTH" \
  --predictions "$PREDICTIONS" \
  --output "$REPORT" \
  --minimum-micro-f1 "$MINIMUM_MICRO_F1"
