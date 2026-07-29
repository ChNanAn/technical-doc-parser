#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:?usage: check_worker_container_model_boundary.sh ROOT_DIR}"
DOCKERFILE="${ROOT_DIR}/platform/deploy/worker.Dockerfile"
MODEL_INIT_DOCKERFILE="${ROOT_DIR}/platform/deploy/model-init.Dockerfile"
MODEL_INIT_ENTRYPOINT="${ROOT_DIR}/platform/deploy/run-model-init.sh"
MODEL_SETUP_SCRIPT="${ROOT_DIR}/scripts/setup_model_pack.sh"
COMPOSE_FILE="${ROOT_DIR}/platform/deploy/docker-compose.yml"

bash -n "$MODEL_SETUP_SCRIPT"
bash -n "$MODEL_INIT_ENTRYPOINT"

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

if grep -Fq 'COPY models' "$MODEL_INIT_DOCKERFILE"; then
  echo "Model initializer must download into the mounted model directory" >&2
  exit 1
fi

grep -Fq 'scripts/setup_model_pack.sh' "$MODEL_INIT_DOCKERFILE"
grep -Fq 'CMD ["run-model-init"]' "$MODEL_INIT_DOCKERFILE"
grep -Fq 'setup_model_pack.sh --models-dir /models' "$MODEL_INIT_ENTRYPOINT"
grep -Fq '/tmp/model-pack-ready' "$MODEL_INIT_ENTRYPOINT"

MODEL_INIT_BLOCK="$(
  awk '
    /^  model-init:$/ { capture = 1; next }
    capture && /^  [a-zA-Z0-9_-]+:$/ { exit }
    capture { print }
  ' "$COMPOSE_FILE"
)"
WORKER_BLOCK="$(
  awk '
    /^  worker:$/ { capture = 1; next }
    capture && /^  [a-zA-Z0-9_-]+:$/ { exit }
    capture { print }
  ' "$COMPOSE_FILE"
)"

grep -Fq 'dockerfile: platform/deploy/model-init.Dockerfile' <<<"$MODEL_INIT_BLOCK"
grep -Fq '${DIE_MODEL_DIR:-../../models}:/models' <<<"$MODEL_INIT_BLOCK"
if grep -Fq ':/models:ro' <<<"$MODEL_INIT_BLOCK"; then
  echo "Model initializer requires a writable /models mount" >&2
  exit 1
fi

grep -Fq 'model-init:' <<<"$WORKER_BLOCK"
grep -A1 -F 'model-init:' <<<"$WORKER_BLOCK" | grep -Fq 'condition: service_healthy'
grep -Fq '${DIE_MODEL_DIR:-../../models}:/models:ro' <<<"$WORKER_BLOCK"
