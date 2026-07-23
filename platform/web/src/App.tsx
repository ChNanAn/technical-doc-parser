import { useEffect, useMemo, useState } from "react";
import {
  Artifact,
  BackendSelection,
  Capabilities,
  createRun,
  getArtifacts,
  getCapabilities,
  getStage,
  uploadDocument,
} from "./api";

const stages = ["render", "text", "layout", "table", "reading_order", "assembly", "export"];
const fallbackCapabilities: Capabilities = {
  registered: {
    document: ["pdf"],
    ocr: ["auto", "paddle", "tesseract", "noop"],
    layout: ["auto", "doclaynet", "paddle-layout", "text"],
    table: ["auto", "table-transformer", "text"],
  },
  available: {
    document: ["auto", "pdf"],
    ocr: ["auto", "paddle", "tesseract", "noop"],
    layout: ["auto", "doclaynet", "paddle-layout", "text"],
    table: ["auto", "table-transformer", "text"],
  },
  workers: [],
};

export function App() {
  const [capabilities, setCapabilities] = useState(fallbackCapabilities);
  const [file, setFile] = useState<File>();
  const [documentId, setDocumentId] = useState("");
  const [runId, setRunId] = useState("");
  const [status, setStatus] = useState("未开始");
  const [activeStage, setActiveStage] = useState("layout");
  const [stageOutput, setStageOutput] = useState<unknown>();
  const [events, setEvents] = useState<Array<Record<string, unknown>>>([]);
  const [artifacts, setArtifacts] = useState<Artifact[]>([]);
  const [error, setError] = useState("");
  const [dpi, setDpi] = useState(200);
  const [backends, setBackends] = useState<BackendSelection>({
    document: "pdf",
    ocr: "auto",
    layout: "auto",
    table: "auto",
  });

  useEffect(() => {
    getCapabilities().then(setCapabilities).catch(() => undefined);
  }, []);

  useEffect(() => {
    if (!runId) return;
    const source = new EventSource(`/api/v1/runs/${runId}/events`);
    source.onmessage = (message) => {
      const event = JSON.parse(message.data) as Record<string, unknown>;
      setEvents((current) => [...current, event]);
      setStatus(String(event.type));
      if (event.type === "job_succeeded") setActiveStage("export");
    };
    source.onerror = () => source.close();
    return () => source.close();
  }, [runId]);

  useEffect(() => {
    if (!runId) return;
    getStage(runId, activeStage).then(setStageOutput).catch((reason) => setStageOutput({ message: String(reason) }));
  }, [runId, activeStage, status]);

  useEffect(() => {
    if (!runId) return;
    getArtifacts(runId).then(setArtifacts).catch(() => undefined);
  }, [runId, status]);

  const latestProgress = useMemo(
    () => [...events].reverse().find((event) => event.type === "stage_progress"),
    [events],
  );
  const selectableBackends = useMemo(() => capabilities.available, [capabilities]);
  const stageArtifacts = useMemo(
    () => artifacts.filter((artifact) => artifact.stage === activeStage),
    [activeStage, artifacts],
  );

  useEffect(() => {
    setBackends((current) => {
      const next = { ...current };
      for (const stage of ["document", "ocr", "layout", "table"] as const) {
        if (!selectableBackends[stage].includes(next[stage])) next[stage] = selectableBackends[stage][0] ?? "auto";
      }
      return next;
    });
  }, [selectableBackends]);

  async function submit() {
    if (!file) return;
    setError("");
    try {
      const document = documentId ? { document_id: documentId } : await uploadDocument(file);
      setDocumentId(document.document_id);
      const run = await createRun(document.document_id, backends, dpi);
      setRunId(run.run_id);
      setEvents([]);
      setArtifacts([]);
      setStatus(run.status);
    } catch (reason) {
      setError(String(reason));
    }
  }

  return (
    <main>
      <header>
        <div>
          <p className="eyebrow">DOCUMENT INTELLIGENCE ENGINE</p>
          <h1>文档解析工作台</h1>
        </div>
        <span className="status">{status}</span>
      </header>

      <section className="controls panel">
        <label className="upload">
          <span>上传 PDF</span>
          <input type="file" accept="application/pdf" onChange={(event) => setFile(event.target.files?.[0])} />
          <strong>{file?.name ?? "选择文件"}</strong>
        </label>
        {(["document", "ocr", "layout", "table"] as const).map((stage) => (
          <label key={stage}>
            <span>{stage.toUpperCase()}</span>
            <select
              value={backends[stage]}
              onChange={(event) => setBackends({ ...backends, [stage]: event.target.value })}
            >
              {selectableBackends[stage].map((backend) => <option key={backend}>{backend}</option>)}
            </select>
          </label>
        ))}
        <label>
          <span>DPI</span>
          <input type="number" min={36} max={600} value={dpi} onChange={(event) => setDpi(Number(event.target.value))} />
        </label>
        <button disabled={!file} onClick={submit}>开始新 Run</button>
      </section>

      {error && <p className="error">{error}</p>}
      <section className="workspace">
        <aside className="panel stages">
          <h2>Stage</h2>
          {stages.map((stage) => (
            <button className={activeStage === stage ? "active" : ""} key={stage} onClick={() => setActiveStage(stage)}>
              {stage}
            </button>
          ))}
        </aside>
        <article className="panel output">
          <div className="output-header">
            <div><span>RUN</span><code>{runId || "尚未创建"}</code></div>
            <div><span>PROGRESS</span><code>{latestProgress ? JSON.stringify(latestProgress.progress) : "-"}</code></div>
          </div>
          <h2>{activeStage}</h2>
          {stageArtifacts.length > 0 && (
            <div className="artifacts">
              {stageArtifacts.map((artifact) => {
                const href = `/api/v1/runs/${runId}/artifacts/${artifact.artifact_id}`;
                return (
                  <a href={href} key={artifact.artifact_id} target="_blank" rel="noreferrer">
                    {artifact.media_type.startsWith("image/") && <img src={href} alt={artifact.kind} />}
                    <span>{artifact.kind}{artifact.page_number ? ` · page ${artifact.page_number}` : ""}</span>
                  </a>
                );
              })}
            </div>
          )}
          <pre>{stageOutput ? JSON.stringify(stageOutput, null, 2) : "等待 Stage 输出"}</pre>
        </article>
        <aside className="panel event-log">
          <h2>Events</h2>
          {events.slice(-30).map((event, index) => (
            <div className="event" key={`${String(event.event_id)}-${index}`}>
              <strong>{String(event.type)}</strong>
              <span>{String(event.stage ?? "job")}</span>
            </div>
          ))}
        </aside>
      </section>
    </main>
  );
}
