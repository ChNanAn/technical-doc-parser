FROM python:3.12.8-slim

WORKDIR /workspace
COPY scripts/package_model_pack.py scripts/setup_model_pack.sh scripts/
COPY packaging/model-pack.v1.json packaging/MODEL-LICENSES.md packaging/
COPY platform/deploy/run-model-init.sh /usr/local/bin/run-model-init

VOLUME ["/models"]
HEALTHCHECK --interval=2s --timeout=2s --retries=900 \
    CMD test -f /tmp/model-pack-ready || exit 1
CMD ["run-model-init"]
