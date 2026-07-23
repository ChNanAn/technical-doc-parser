FROM ubuntu:24.04 AS build

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates cmake curl g++ git libopencv-dev make pkg-config && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .
RUN cmake --preset platform-release \
    -DBUILD_TESTING=OFF \
    -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_TABLE_TRANSFORMER=ON && \
    cmake --build --preset platform-release --target document_intelligence_worker --parallel 2

FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends libopencv-core406t64 libopencv-imgcodecs406t64 libopencv-imgproc406t64 && \
    rm -rf /var/lib/apt/lists/* && useradd --create-home --uid 10001 worker

WORKDIR /workspace
COPY --from=build /workspace/build/platform-release/platform/worker/document_intelligence_worker /usr/local/bin/
COPY --from=build /workspace/third_party /workspace/third_party
COPY --from=build /workspace/models /workspace/models
RUN mkdir -p /runtime && chown -R worker:worker /runtime /workspace/models
ENV LD_LIBRARY_PATH=/workspace/third_party/pdfium/lib:/workspace/third_party/onnxruntime-linux-x64-1.18.1/lib
USER worker
CMD ["document_intelligence_worker"]
