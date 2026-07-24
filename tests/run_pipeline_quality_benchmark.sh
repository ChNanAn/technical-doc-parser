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
MINIMUM_TEXT_COMPLETENESS="${10}"
MINIMUM_READING_ORDER_ANCHOR_RECALL="${11}"
MINIMUM_READING_ORDER_SCORE="${12}"
MAXIMUM_TEXT_DUPLICATION_RATE="${13}"

"$PRODUCER" \
  --manifest "$MANIFEST" \
  --input-root "$INPUT_ROOT" \
  --work-dir "$WORK_DIR" \
  --output "$PREDICTIONS"

"$PYTHON" "$EVALUATOR" \
  --ground-truth "$GROUND_TRUTH" \
  --predictions "$PREDICTIONS" \
  --output "$REPORT" \
  --minimum-text-completeness "$MINIMUM_TEXT_COMPLETENESS" \
  --minimum-reading-order-anchor-recall "$MINIMUM_READING_ORDER_ANCHOR_RECALL" \
  --minimum-reading-order-score "$MINIMUM_READING_ORDER_SCORE" \
  --maximum-text-duplication-rate "$MAXIMUM_TEXT_DUPLICATION_RATE"
