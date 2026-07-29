import {
  AlertTriangle,
  Code2,
  Download,
  FileCode2,
  FileJson2,
  FileText,
} from "lucide-react";
import { Artifact } from "./api";
import { asArray, asRecord, OverlayItem, StageName } from "./visualization";

type InspectorProps = {
  runId: string;
  stage: StageName;
  overlays: OverlayItem[];
  selected?: OverlayItem;
  artifacts: Artifact[];
  warnings: Record<string, unknown>[];
  onOpenRaw: () => void;
};

const stageNames: Record<StageName, string> = {
  render: "页面渲染",
  text: "文本提取",
  layout: "版面分析",
  table: "表格识别",
  reading_order: "阅读顺序",
  assembly: "文档组装",
  export: "结果导出",
};

function artifactHref(runId: string, artifact: Artifact): string {
  return `/api/v1/runs/${runId}/artifacts/${artifact.artifact_id}`;
}

function kindLabel(kind: string): string {
  const labels: Record<string, string> = {
    text: "文本",
    title: "标题",
    paragraph: "段落",
    table: "表格",
    cell: "单元格",
    figure: "图片",
    list: "列表",
    header: "页眉",
    footer: "页脚",
    unknown: "未知",
  };
  return labels[kind] ?? kind;
}

function tableRows(selected?: OverlayItem): Record<string, unknown>[] {
  const raw = asRecord(selected?.raw);
  const table = asRecord(raw?.table) ?? raw;
  return asArray(table?.rows).map(asRecord).filter(
    (row): row is Record<string, unknown> => row !== undefined,
  );
}

function exportIcon(kind: string) {
  if (kind === "document_json") return <FileJson2 size={17} />;
  if (kind === "document_html") return <FileCode2 size={17} />;
  return <FileText size={17} />;
}

export function Inspector({
  runId,
  stage,
  overlays,
  selected,
  artifacts,
  warnings,
  onOpenRaw,
}: InspectorProps) {
  const counts = overlays.reduce<Record<string, number>>((current, overlay) => {
    current[overlay.kind] = (current[overlay.kind] ?? 0) + 1;
    return current;
  }, {});
  const rows = tableRows(selected);
  const exportArtifacts = artifacts.filter((artifact) => artifact.stage === "export");

  return (
    <aside className="inspector">
      <section className="inspector-section stage-summary">
        <div className="section-heading">
          <div>
            <span className="section-kicker">当前阶段</span>
            <h2>{stageNames[stage]}</h2>
          </div>
          <button className="icon-button" type="button" title="查看原始数据" aria-label="查看原始数据" onClick={onOpenRaw}>
            <Code2 size={18} />
          </button>
        </div>
        <div className="summary-counts">
          <strong>{overlays.length}</strong>
          <span>个可视对象</span>
        </div>
        {Object.keys(counts).length > 0 && (
          <div className="kind-legend">
            {Object.entries(counts).map(([kind, count]) => (
              <span key={kind}><i className={`legend-dot legend-${kind}`} />{kindLabel(kind)} {count}</span>
            ))}
          </div>
        )}
      </section>

      <section className="inspector-section selection-details">
        <span className="section-kicker">选中对象</span>
        {!selected ? (
          <p className="muted">未选择页面区域</p>
        ) : (
          <>
            <div className="selection-title">
              <strong>{kindLabel(selected.kind)}</strong>
              {selected.order !== undefined && <span>顺序 {selected.order + 1}</span>}
            </div>
            {selected.text && <p className="selected-text">{selected.text}</p>}
            {selected.confidence !== undefined && (
              <div className="confidence">
                <div><span>置信度</span><strong>{Math.round(selected.confidence * 100)}%</strong></div>
                <progress max={1} value={selected.confidence} />
              </div>
            )}
            <dl className="coordinate-list">
              <div><dt>X</dt><dd>{selected.bbox[0].toFixed(1)} - {selected.bbox[2].toFixed(1)}</dd></div>
              <div><dt>Y</dt><dd>{selected.bbox[1].toFixed(1)} - {selected.bbox[3].toFixed(1)}</dd></div>
            </dl>
          </>
        )}
      </section>

      {rows.length > 0 && (
        <section className="inspector-section table-preview">
          <span className="section-kicker">表格内容</span>
          <div className="table-scroll">
            <table>
              <tbody>
                {rows.map((row, rowIndex) => (
                  <tr key={rowIndex}>
                    {asArray(row.cells).map(asRecord).map((cell, cellIndex) => (
                      <td
                        key={cellIndex}
                        rowSpan={typeof cell?.row_span === "number" ? cell.row_span : 1}
                        colSpan={typeof cell?.column_span === "number" ? cell.column_span : 1}
                      >
                        {String(cell?.text ?? "")}
                      </td>
                    ))}
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        </section>
      )}

      {warnings.length > 0 && (
        <section className="inspector-section warning-list">
          <span className="section-kicker">运行告警</span>
          {warnings.slice(-5).map((warning, index) => (
            <div className="warning-item" key={`${String(warning.code)}-${index}`}>
              <AlertTriangle size={16} />
              <div>
                <strong>{String(warning.code ?? "WARNING")}</strong>
                <p>{String(warning.message ?? "")}</p>
              </div>
            </div>
          ))}
        </section>
      )}

      {exportArtifacts.length > 0 && (
        <section className="inspector-section exports">
          <span className="section-kicker">导出文件</span>
          {exportArtifacts.map((artifact) => (
            <a
              href={artifactHref(runId, artifact)}
              key={artifact.artifact_id}
              download
            >
              {exportIcon(artifact.kind)}
              <span>{artifact.kind.replace("document_", "").toUpperCase()}</span>
              <Download size={15} />
            </a>
          ))}
        </section>
      )}
    </aside>
  );
}
