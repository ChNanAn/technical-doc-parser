FROM ubuntu:24.04 AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG REVISION=unknown
ARG SOURCE_DATE_EPOCH=0
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake curl g++ git libopencv-dev make pkg-config && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .
RUN cmake -S . -B build/container \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF \
      -DDOCUMENT_INTELLIGENCE_ENGINE_GIT_REVISION="${REVISION}" \
      -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLEOCR_BASELINE=OFF \
      -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_DOCLAYNET_LAYOUT=OFF \
      -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLE_LAYOUT=OFF \
      -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_TABLE_TRANSFORMER=OFF && \
    cmake --build build/container --target document_intelligence_engine --parallel 2 && \
    bash scripts/package_release.sh \
      --kind cli \
      --build-dir build/container \
      --output-dir build/release \
      --revision "${REVISION}" \
      --source-date-epoch "${SOURCE_DATE_EPOCH}" && \
    mkdir -p /opt/document-intelligence-engine && \
    tar -xzf build/release/*-linux-x86_64-cli.tar.gz \
      -C /opt/document-intelligence-engine \
      --strip-components=1

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG VERSION=0.1.0
ARG REVISION=unknown
LABEL org.opencontainers.image.title="Document Intelligence Engine CLI" \
      org.opencontainers.image.version="${VERSION}" \
      org.opencontainers.image.revision="${REVISION}" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.source="https://github.com/ChNanAn/technical-doc-parser"

RUN apt-get update && apt-get install -y --no-install-recommends \
    libopencv-core406t64 \
    libopencv-imgcodecs406t64 \
    libopencv-imgproc406t64 \
    tesseract-ocr && \
    rm -rf /var/lib/apt/lists/* && \
    useradd --create-home --uid 10001 engine

COPY --from=build /opt/document-intelligence-engine /opt/document-intelligence-engine

ENV PATH="/opt/document-intelligence-engine/bin:${PATH}" \
    DOCUMENT_INTELLIGENCE_ENGINE_PADDLEOCR_MODEL_DIR="/models/paddleocr/baseline" \
    DOCUMENT_INTELLIGENCE_ENGINE_DOCLAYNET_MODEL="/models/layout/doclaynet/model.onnx" \
    DOCUMENT_INTELLIGENCE_ENGINE_PADDLE_LAYOUT_MODEL="/models/layout/paddle/pp-doclayout-v3.onnx" \
    DOCUMENT_INTELLIGENCE_ENGINE_TABLE_DETECTION_MODEL="/models/table/table-transformer/detection.onnx" \
    DOCUMENT_INTELLIGENCE_ENGINE_TABLE_STRUCTURE_MODEL="/models/table/table-transformer/structure.onnx"

VOLUME ["/models", "/output"]
WORKDIR /work
USER engine
ENTRYPOINT ["document_intelligence_engine"]
