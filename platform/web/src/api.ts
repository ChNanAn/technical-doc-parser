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

export async function getStage(runId: string, stage: string): Promise<unknown> {
  const response = await fetch(`/api/v1/runs/${runId}/stages/${stage}`);
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}

export async function getArtifacts(runId: string): Promise<Artifact[]> {
  const response = await fetch(`/api/v1/runs/${runId}/artifacts`);
  if (!response.ok) throw new Error(await response.text());
  return response.json();
}
