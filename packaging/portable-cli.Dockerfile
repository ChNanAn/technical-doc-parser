# syntax=docker/dockerfile:1.7

FROM ubuntu:20.04 AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG REVISION=unknown
ARG SOURCE_DATE_EPOCH=0
ARG VCPKG_COMMIT=b781af668027bbf77f2f827f47b5c6cd8d825c08

RUN apt-get update && apt-get install -y --no-install-recommends \
      binutils \
      build-essential \
      ca-certificates \
      curl \
      git \
      ninja-build \
      pkg-config \
      python3 \
      tar \
      unzip \
      zip && \
    rm -rf /var/lib/apt/lists/*

RUN git init /opt/vcpkg && \
    git -C /opt/vcpkg remote add origin https://github.com/microsoft/vcpkg.git && \
    git -C /opt/vcpkg fetch --depth=1 origin "${VCPKG_COMMIT}" && \
    git -C /opt/vcpkg checkout --detach FETCH_HEAD && \
    /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics && \
    cmake_binary="$(/opt/vcpkg/vcpkg fetch cmake)" && \
    ln -s "${cmake_binary}" /usr/local/bin/cmake

ENV VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1 \
    VCPKG_BINARY_SOURCES="clear;files,/root/.cache/vcpkg/archives,readwrite"

COPY vcpkg.json /tmp/technical-doc-parser-manifest/vcpkg.json
RUN --mount=type=cache,target=/root/.cache/vcpkg/archives \
    /opt/vcpkg/vcpkg install \
      --x-manifest-root=/tmp/technical-doc-parser-manifest \
      --x-install-root=/opt/vcpkg-installed \
      --triplet x64-linux \
      --clean-after-build

WORKDIR /workspace
COPY . .
RUN cmake -S . -B build/portable-cli -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_INSTALLED_DIR=/opt/vcpkg-installed \
      -DVCPKG_TARGET_TRIPLET=x64-linux \
      -DBUILD_SHARED_LIBS=OFF \
      -DBUILD_TESTING=OFF \
      -DDOCUMENT_INTELLIGENCE_ENGINE_GIT_REVISION="${REVISION}" \
      -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLEOCR_BASELINE=OFF \
      -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_DOCLAYNET_LAYOUT=OFF \
      -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_PADDLE_LAYOUT=OFF \
      -DDOCUMENT_INTELLIGENCE_ENGINE_AUTO_SETUP_TABLE_TRANSFORMER=OFF && \
    cmake --build build/portable-cli \
      --target document_intelligence_engine \
      --parallel 2 && \
    bash scripts/package_release.sh \
      --kind cli \
      --build-dir build/portable-cli \
      --output-dir dist \
      --revision "${REVISION}" \
      --source-date-epoch "${SOURCE_DATE_EPOCH}" \
      --require-portable-linux && \
    (cd dist && sha256sum --check SHA256SUMS)

FROM scratch AS artifacts
COPY --from=build /workspace/dist/ /
