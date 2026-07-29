import { ChangeEvent, FormEvent, useEffect, useMemo, useRef, useState } from "react";
import {
  Blocks,
  Check,
  CheckCircle2,
  Circle,
  FileStack,
  FileText,
  Layers3,
  ListOrdered,
  LoaderCircle,
  Play,
  ScanText,
  Table2,
  Upload,
  XCircle,
} from "lucide-react";
import {
  Artifact,
  BackendSelection,
  Capabilities,
  createRun,
  getArtifactJson,
  getArtifacts,
  getCapabilities,
  getStage,
  uploadDocument,
} from "./api";
import { DocumentViewer } from "./DocumentViewer";
import { Inspector } from "./Inspector";
import { RawDataDrawer } from "./RawDataDrawer";
import {
  OverlayItem,
  overlaysForStage,
  StageName,
  warningFromEvent,
} from "./visualization";

const stages: Array<{
  id: StageName;
  label: string;
  icon: typeof FileText;
}> = [
  { id: "render", label: "页面渲染", icon: Layers3 },
  { id: "text", label: "文本提取", icon: ScanText },
  { id: "layout", label: "版面分析", icon: Blocks },
  { id: "table", label: "表格识别", icon: Table2 },
  { id: "reading_order", label: "阅读顺序", icon: ListOrdered },
  { id: "assembly", label: "文档组装", icon: FileStack },
  { id: "export", label: "结果导出", icon: FileText },
];

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

type StageState = "pending" | "running" | "completed" | "failed";

function statusLabel(status: string): string {
  const labels: Record<string, string> = {
    idle: "待命",
    uploading: "上传中",
    queued: "排队中",
    running: "处理中",
    succeeded: "已完成",
    failed: "失败",
    cancelled: "已取消",
  };
  return labels[status] ?? status;
}

function eventStatus(event: Record<string, unknown>): string {
  if (event.type === "job_succeeded") return "succeeded";
  if (event.type === "job_failed") return "failed";
  if (event.type === "job_cancelled") return "cancelled";
  return "running";
}

function stageState(events: Array<Record<string, unknown>>, stage: StageName): StageState {
  const stageEvents = events.filter((event) => event.stage === stage);
  const latest = stageEvents[stageEvents.length - 1];
  if (!latest) return "pending";
  if (latest.type === "stage_failed") return "failed";
  if (latest.type === "stage_completed") return "completed";
  return "running";
}

function StageStateIcon({ state }: { state: StageState }) {
  if (state === "completed") return <CheckCircle2 size={16} />;
  if (state === "running") return <LoaderCircle className="spin" size={16} />;
  if (state === "failed") return <XCircle size={16} />;
  return <Circle size={15} />;
}

export function App() {
  const [capabilities, setCapabilities] = useState(fallbackCapabilities);
  const [file, setFile] = useState<File>();
  const [uploadedDocument, setUploadedDocument] = useState<{ file: File; documentId: string }>();
  const [runId, setRunId] = useState("");
  const [status, setStatus] = useState("idle");
  const [activeStage, setActiveStage] = useState<StageName>("layout");
  const [stageOutput, setStageOutput] = useState<unknown>();
  const [layoutOutput, setLayoutOutput] = useState<unknown>();
  const [textOutput, setTextOutput] = useState<unknown>();
  const [stageLoading, setStageLoading] = useState(false);
  const [events, setEvents] = useState<Array<Record<string, unknown>>>([]);
  const [artifacts, setArtifacts] = useState<Artifact[]>([]);
  const [documentOutput, setDocumentOutput] = useState<Record<string, unknown>>();
  const [error, setError] = useState("");
  const [dpi, setDpi] = useState(200);
  const [pageNumber, setPageNumber] = useState(1);
  const [selectedOverlay, setSelectedOverlay] = useState<OverlayItem>();
  const [showOverlays, setShowOverlays] = useState(true);
  const [zoom, setZoom] = useState(100);
  const [rawOpen, setRawOpen] = useState(false);
  const fileSelection = useRef(0);
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
      setEvents((current) => {
        const eventId = String(event.event_id ?? "");
        return eventId && current.some((existing) => existing.event_id === eventId)
          ? current
          : [...current, event];
      });
      setStatus(eventStatus(event));
    };
    source.onerror = () => source.close();
    return () => source.close();
  }, [runId]);

  useEffect(() => {
    if (!runId || status !== "succeeded") {
      setStageOutput(undefined);
      setStageLoading(Boolean(runId) && (status === "running" || status === "queued"));
      return;
    }
    let active = true;
    setStageLoading(true);
    const requests: Promise<unknown>[] = [getStage(runId, activeStage)];
    if (activeStage === "reading_order") {
      requests.push(getStage(runId, "layout"), getStage(runId, "text"));
    } else if (activeStage === "layout") {
      requests.push(getStage(runId, "text"));
    }

    Promise.all(requests)
      .then(([current, layoutOrText, text]) => {
        if (!active) return;
        setStageOutput(current);
        if (activeStage === "reading_order") {
          setLayoutOutput(layoutOrText);
          setTextOutput(text);
        } else if (activeStage === "layout") {
          setTextOutput(layoutOrText);
        }
      })
      .catch(() => {
        if (active) setStageOutput(undefined);
      })
      .finally(() => {
        if (active) setStageLoading(false);
      });
    return () => {
      active = false;
    };
  }, [runId, activeStage, status]);

  useEffect(() => {
    if (!runId) return;
    let active = true;
    getArtifacts(runId)
      .then((output) => {
        if (active) setArtifacts(output);
      })
      .catch(() => undefined);
    return () => {
      active = false;
    };
  }, [runId, events.length]);

  useEffect(() => {
    const artifact = artifacts.find((candidate) => candidate.kind === "document_json");
    if (!runId || !artifact) {
      setDocumentOutput(undefined);
      return;
    }
    let active = true;
    getArtifactJson(runId, artifact.artifact_id)
      .then((output) => {
        if (active) setDocumentOutput(output);
      })
      .catch(() => {
        if (active) setDocumentOutput(undefined);
      });
    return () => {
      active = false;
    };
  }, [artifacts, runId]);

  const selectableBackends = useMemo(() => capabilities.available, [capabilities]);
  const pageArtifacts = useMemo(
    () => artifacts
      .filter((artifact) => artifact.kind === "page_image")
      .sort((left, right) => (left.page_number ?? 0) - (right.page_number ?? 0)),
    [artifacts],
  );
  const overlays = useMemo(
    () => overlaysForStage(activeStage, stageOutput, pageNumber, layoutOutput, textOutput),
    [activeStage, layoutOutput, pageNumber, stageOutput, textOutput],
  );
  const warnings = useMemo(
    () => {
      const eventWarnings = events
        .map(warningFromEvent)
        .filter((warning): warning is Record<string, unknown> => warning !== undefined);
      const documentWarnings = Array.isArray(documentOutput?.warnings)
        ? documentOutput.warnings.filter(
          (warning): warning is Record<string, unknown> => (
            typeof warning === "object" && warning !== null && !Array.isArray(warning)
          ),
        )
        : [];
      const unique = new Map<string, Record<string, unknown>>();
      for (const warning of [...eventWarnings, ...documentWarnings]) {
        unique.set(`${String(warning.code)}:${String(warning.message)}`, warning);
      }
      return [...unique.values()];
    },
    [documentOutput, events],
  );
  const completedStages = useMemo(
    () => stages.filter((stage) => stageState(events, stage.id) === "completed").length,
    [events],
  );
  const progressPercent = runId
    ? status === "succeeded" ? 100 : Math.round((completedStages / stages.length) * 100)
    : 0;

  useEffect(() => {
    const availablePages = new Set(pageArtifacts.map((artifact) => artifact.page_number));
    if (pageArtifacts.length > 0 && !availablePages.has(pageNumber)) {
      setPageNumber(pageArtifacts[0].page_number ?? 1);
    }
  }, [pageArtifacts, pageNumber]);

  useEffect(() => {
    setSelectedOverlay(undefined);
  }, [activeStage, pageNumber]);

  useEffect(() => {
    setBackends((current) => {
      const next = { ...current };
      for (const stage of ["document", "ocr", "layout", "table"] as const) {
        if (!selectableBackends[stage].includes(next[stage])) {
          next[stage] = selectableBackends[stage][0] ?? "auto";
        }
      }
      return next;
    });
  }, [selectableBackends]);

  function selectFile(event: ChangeEvent<HTMLInputElement>) {
    fileSelection.current += 1;
    setFile(event.target.files?.[0]);
    setUploadedDocument(undefined);
    setRunId("");
    setEvents([]);
    setArtifacts([]);
    setDocumentOutput(undefined);
    setStageOutput(undefined);
    setLayoutOutput(undefined);
    setTextOutput(undefined);
    setStatus("idle");
    setError("");
    setPageNumber(1);
    setSelectedOverlay(undefined);
  }

  async function submit(event: FormEvent) {
    event.preventDefault();
    if (!file || status === "uploading") return;
    const selectedFile = file;
    const selection = fileSelection.current;
    setError("");
    setStatus("uploading");
    try {
      const document = uploadedDocument?.file === selectedFile
        ? { document_id: uploadedDocument.documentId }
        : await uploadDocument(selectedFile);
      if (selection !== fileSelection.current) return;
      setUploadedDocument({ file: selectedFile, documentId: document.document_id });
      const run = await createRun(document.document_id, backends, dpi);
      if (selection !== fileSelection.current) return;
      setRunId(run.run_id);
      setEvents([]);
      setArtifacts([]);
      setDocumentOutput(undefined);
      setStageOutput(undefined);
      setStatus(run.status);
      setActiveStage("layout");
    } catch (reason) {
      if (selection === fileSelection.current) {
        setError(String(reason));
        setStatus("failed");
      }
    }
  }

  return (
    <main className="app-shell">
      <header className="app-header">
        <div className="brand">
          <span className="brand-mark"><FileStack size={21} /></span>
          <div>
            <h1>文档解析工作台</h1>
            <span>Document Intelligence Engine</span>
          </div>
        </div>
        <div className={`run-status status-${status}`}>
          {status === "running" || status === "uploading"
            ? <LoaderCircle className="spin" size={16} />
            : status === "succeeded" ? <Check size={16} /> : <Circle size={15} />}
          <span>{statusLabel(status)}</span>
        </div>
      </header>

      <form className="run-controls" onSubmit={submit}>
        <label className="file-picker">
          <Upload size={18} />
          <span>{file?.name ?? "选择 PDF"}</span>
          <input type="file" accept="application/pdf" onChange={selectFile} />
        </label>
        {(["document", "ocr", "layout", "table"] as const).map((stage) => (
          <label className="field" key={stage}>
            <span>{stage.toUpperCase()}</span>
            <select
              value={backends[stage]}
              onChange={(event) => setBackends({ ...backends, [stage]: event.target.value })}
            >
              {selectableBackends[stage].map((backend) => <option key={backend}>{backend}</option>)}
            </select>
          </label>
        ))}
        <label className="field dpi-field">
          <span>DPI</span>
          <input
            type="number"
            min={36}
            max={600}
            value={dpi}
            onChange={(event) => setDpi(Number(event.target.value))}
          />
        </label>
        <button className="primary-button" type="submit" disabled={!file || status === "uploading"}>
          <Play size={17} fill="currentColor" />
          <span>开始解析</span>
        </button>
      </form>

      {error && <div className="error-banner"><XCircle size={17} /><span>{error}</span></div>}

      <div className="run-meta">
        <div>
          <span>RUN</span>
          <code>{runId || "尚未创建"}</code>
        </div>
        <div className="overall-progress">
          <span>{progressPercent}%</span>
          <progress max={100} value={progressPercent} />
        </div>
      </div>

      <section className="workspace">
        <nav className="stage-navigation" aria-label="解析阶段">
          <span className="rail-title">解析阶段</span>
          {stages.map((stage) => {
            const Icon = stage.icon;
            const state = stageState(events, stage.id);
            return (
              <button
                className={activeStage === stage.id ? `active state-${state}` : `state-${state}`}
                type="button"
                key={stage.id}
                onClick={() => setActiveStage(stage.id)}
              >
                <Icon size={18} />
                <span>{stage.label}</span>
                <StageStateIcon state={state} />
              </button>
            );
          })}
        </nav>

        <DocumentViewer
          runId={runId}
          pages={pageArtifacts}
          pageNumber={pageNumber}
          onPageChange={setPageNumber}
          overlays={overlays}
          selectedId={selectedOverlay?.id}
          onSelect={setSelectedOverlay}
          showOverlays={showOverlays}
          onShowOverlaysChange={setShowOverlays}
          zoom={zoom}
          onZoomChange={setZoom}
          loading={stageLoading || status === "running"}
        />

        <Inspector
          runId={runId}
          stage={activeStage}
          overlays={overlays}
          selected={selectedOverlay}
          artifacts={artifacts}
          warnings={warnings}
          onOpenRaw={() => setRawOpen(true)}
        />
      </section>

      <RawDataDrawer
        open={rawOpen}
        title={stages.find((stage) => stage.id === activeStage)?.label ?? activeStage}
        value={stageOutput}
        onClose={() => setRawOpen(false)}
      />
    </main>
  );
}
