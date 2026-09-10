import { afterEach, expect, it, vi } from "vitest";
import { artifactUrl, cancelRun, getArtifactJson } from "./api";

afterEach(() => vi.unstubAllGlobals());

it("pins image/download and JSON requests to the execution that supplied the manifest", async () => {
  const artifact = { artifact_id: "artifact_export_document_json", execution_id: "execution_2" };
  const path = "/api/v1/runs/run_1/artifacts/artifact_export_document_json?execution_id=execution_2";
  expect(artifactUrl("run_1", artifact)).toBe(path);
  const fetch = vi.fn().mockResolvedValue({ ok: true, json: async () => ({ blocks: [] }) });
  vi.stubGlobal("fetch", fetch);
  await getArtifactJson("run_1", artifact.artifact_id, artifact.execution_id);
  expect(fetch).toHaveBeenCalledWith(path);
  expect(artifactUrl("run_1", { artifact_id: "old" })).toBe("/api/v1/runs/run_1/artifacts/old");
});

it("keeps cancellation pending until the server confirms a terminal status", async () => {
  const fetch = vi.fn().mockResolvedValue({ ok: true, json: async () => ({ status: "running", cancel_requested: true }) });
  vi.stubGlobal("fetch", fetch);
  expect(await cancelRun("run_1")).toEqual({ status: "running", cancel_requested: true });
  expect(fetch).toHaveBeenCalledWith("/api/v1/runs/run_1/cancel", { method: "POST" });
  fetch.mockResolvedValue({ ok: false, text: async () => "temporarily unavailable" });
  await expect(cancelRun("run_1")).rejects.toThrow("temporarily unavailable");
});
