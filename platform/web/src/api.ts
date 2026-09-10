export type BackendSelection = {
  document: string;
  ocr: string;
  layout: string;
  table: string;
};

export type Capabilities = {
  registered: Record<keyof BackendSelection, string[]>;
  available: Record<keyof BackendSelection, string[]>;
  workers: Array<Record<string, unknown>>;
};

export type Artifact = {
  artifact_id: string;
  execution_id?: string;
  stage: string;
  kind: string;
  media_type: string;
  page_number?: number;
  size_bytes?: number;
};

export async function uploadDocument(file: File) {
  const form = new FormData();
  form.append("file", file);
  const response = await fetch("/api/v1/documents", { method: "POST", body: form });
  if (!response.ok) throw new Error(await response.text());
  return response.json() as Promise<{ document_id: string; filename: string }>;
}

export async function createRun(documentId: string, backends: BackendSelection, dpi: number) {
  const response = await fetch(`/api/v1/documents/${documentId}/runs`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ dpi, debug: true, backends }),
  });
  if (!response.ok) throw new Error(await response.text());
  return response.json() as Promise<{ run_id: string; status: string }>;
}

export async function getCapabilities(): Promise<Capabilities> {
  const response = await fetch("/api/v1/capabilities");
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}

export const ARTIFACTS_EXPIRED_MESSAGE = "解析产物已过保留期限，请重新运行解析。";

export class ArtifactsExpiredError extends Error {
  constructor() { super(ARTIFACTS_EXPIRED_MESSAGE); }
}

export type RunStatus = {
  status: string;
  error?: string | null;
  cancel_requested?: boolean;
  artifacts_expired_at?: string | null;
};

export async function getRun(runId: string, signal?: AbortSignal): Promise<RunStatus> {
  const response = await fetch(`/api/v1/runs/${runId}`, { signal });
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}

export async function cancelRun(runId: string): Promise<RunStatus> {
  const response = await fetch(`/api/v1/runs/${runId}/cancel`, { method: "POST" });
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}

async function artifactJson<T>(path: string): Promise<T> {
  const response = await fetch(path);
  if (response.status === 410) throw new ArtifactsExpiredError();
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}

export async function getStage(runId: string, stage: string): Promise<unknown> {
  return artifactJson(`/api/v1/runs/${runId}/stages/${stage}`);
}

export async function getArtifacts(runId: string): Promise<Artifact[]> {
  return artifactJson(`/api/v1/runs/${runId}/artifacts`);
}

export async function getArtifactJson(
  runId: string,
  artifactId: string,
  executionId?: string,
): Promise<Record<string, unknown>> {
  return artifactJson(artifactUrl(runId, { artifact_id: artifactId, execution_id: executionId }));
}

export function artifactUrl(runId: string, artifact: Pick<Artifact, "artifact_id" | "execution_id">): string {
  const path = `/api/v1/runs/${runId}/artifacts/${artifact.artifact_id}`;
  return artifact.execution_id ? `${path}?execution_id=${encodeURIComponent(artifact.execution_id)}` : path;
}
