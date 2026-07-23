FROM python:3.12.8-slim

WORKDIR /app
COPY platform/api/pyproject.toml ./pyproject.toml
COPY platform/api/app ./app
RUN pip install --no-cache-dir .

RUN useradd --create-home --uid 10001 api && mkdir -p /runtime && chown api:api /runtime
USER api
EXPOSE 8000
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000"]
