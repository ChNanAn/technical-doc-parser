#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:?usage: check_worker_container_model_boundary.sh ROOT_DIR}"
DOCKERFILE="${ROOT_DIR}/platform/deploy/worker.Dockerfile"
COMPOSE_FILE="${ROOT_DIR}/platform/deploy/docker-compose.yml"

for option in \
  DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLEOCR_BASELINE \
  DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_DOCLAYNET_LAYOUT \
  DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLE_LAYOUT \
  DOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_TABLE_TRANSFORMER
do
  grep -Fq -- "-D${option}=OFF" "$DOCKERFILE"
done

if grep -Fq 'COPY --from=build /workspace/models' "$DOCKERFILE"; then
  echo "Worker image must not copy model weights from the build stage" >&2
  exit 1
fi

for model_path in \
  /models/paddleocr/baseline \
  /models/layout/doclaynet/model.onnx \
  /models/layout/paddle/pp-doclayout-v3.onnx \
  /models/table/table-transformer/detection.onnx \
  /models/table/table-transformer/structure.onnx
do
  grep -Fq "$model_path" "$DOCKERFILE"
done

grep -Fq '${DIE_MODEL_DIR:-../../models}:/models:ro' "$COMPOSE_FILE"
