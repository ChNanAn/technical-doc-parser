import { CSSProperties, useState } from "react";
import {
  ChevronLeft,
  ChevronRight,
  Eye,
  EyeOff,
  ImageOff,
  ZoomIn,
  ZoomOut,
} from "lucide-react";
import { Artifact } from "./api";
import { OverlayItem } from "./visualization";

type DocumentViewerProps = {
  runId: string;
  pages: Artifact[];
  pageNumber: number;
  onPageChange: (pageNumber: number) => void;
  overlays: OverlayItem[];
  selectedId?: string;
  onSelect: (item?: OverlayItem) => void;
  showOverlays: boolean;
  onShowOverlaysChange: (visible: boolean) => void;
  zoom: number;
  onZoomChange: (zoom: number) => void;
  loading: boolean;
};

function artifactHref(runId: string, artifact: Artifact): string {
  return `/api/v1/runs/${runId}/artifacts/${artifact.artifact_id}`;
}

function overlayClass(kind: string): string {
  const normalized = kind.toLowerCase().replace(/[^a-z0-9_-]/g, "-");
  return `overlay-box overlay-${normalized}`;
}

export function DocumentViewer({
  runId,
  pages,
  pageNumber,
  onPageChange,
  overlays,
  selectedId,
  onSelect,
  showOverlays,
  onShowOverlaysChange,
  zoom,
  onZoomChange,
  loading,
}: DocumentViewerProps) {
  const [imageSize, setImageSize] = useState({ width: 0, height: 0 });
  const pageIndex = Math.max(0, pages.findIndex((page) => page.page_number === pageNumber));
  const page = pages[pageIndex];
  const canvasStyle = { "--page-zoom": `${zoom}%` } as CSSProperties;

  function changePage(nextIndex: number) {
    const nextPage = pages[nextIndex]?.page_number;
    if (nextPage !== undefined) {
      setImageSize({ width: 0, height: 0 });
      onSelect(undefined);
      onPageChange(nextPage);
    }
  }

  return (
    <section className="viewer" aria-label="文档页面检查器">
      <div className="viewer-toolbar">
        <div className="page-navigation">
          <button
            className="icon-button"
            type="button"
            title="上一页"
            aria-label="上一页"
            disabled={pageIndex <= 0}
            onClick={() => changePage(pageIndex - 1)}
          >
            <ChevronLeft size={18} />
          </button>
          <span>{pages.length > 0 ? `${pageIndex + 1} / ${pages.length}` : "0 / 0"}</span>
          <button
            className="icon-button"
            type="button"
            title="下一页"
            aria-label="下一页"
            disabled={pageIndex >= pages.length - 1}
            onClick={() => changePage(pageIndex + 1)}
          >
            <ChevronRight size={18} />
          </button>
        </div>
        <div className="viewer-tools">
          <button
            className="icon-button"
            type="button"
            title={showOverlays ? "隐藏标注" : "显示标注"}
            aria-label={showOverlays ? "隐藏标注" : "显示标注"}
            onClick={() => onShowOverlaysChange(!showOverlays)}
          >
            {showOverlays ? <Eye size={18} /> : <EyeOff size={18} />}
          </button>
          <span className="toolbar-divider" />
          <button
            className="icon-button"
            type="button"
            title="缩小"
            aria-label="缩小"
            disabled={zoom <= 60}
            onClick={() => onZoomChange(Math.max(60, zoom - 20))}
          >
            <ZoomOut size={18} />
          </button>
          <span className="zoom-value">{zoom}%</span>
          <button
            className="icon-button"
            type="button"
            title="放大"
            aria-label="放大"
            disabled={zoom >= 200}
            onClick={() => onZoomChange(Math.min(200, zoom + 20))}
          >
            <ZoomIn size={18} />
          </button>
        </div>
      </div>

      {pages.length > 1 && (
        <div className="page-strip" aria-label="页面缩略图">
          {pages.map((candidate, index) => {
            const candidatePage = candidate.page_number ?? index + 1;
            return (
              <button
                className={candidatePage === pageNumber ? "page-thumb active" : "page-thumb"}
                type="button"
                key={candidate.artifact_id}
                onClick={() => changePage(index)}
              >
                <img src={artifactHref(runId, candidate)} alt={`第 ${candidatePage} 页`} />
                <span>{candidatePage}</span>
              </button>
            );
          })}
        </div>
      )}

      <div className="viewer-scroll" onClick={() => onSelect(undefined)}>
        {!page ? (
          <div className="viewer-empty">
            <ImageOff size={34} />
            <strong>{loading ? "正在生成页面" : "暂无页面图像"}</strong>
          </div>
        ) : (
          <div className="page-canvas" style={canvasStyle}>
            <img
              className="page-image"
              src={artifactHref(runId, page)}
              alt={`第 ${pageNumber} 页`}
              onLoad={(event) => setImageSize({
                width: event.currentTarget.naturalWidth,
                height: event.currentTarget.naturalHeight,
              })}
            />
            {showOverlays && imageSize.width > 0 && (
              <div className="overlay-layer">
                {overlays.map((overlay) => {
                  const [x0, y0, x1, y1] = overlay.bbox;
                  const style = {
                    left: `${(x0 / imageSize.width) * 100}%`,
                    top: `${(y0 / imageSize.height) * 100}%`,
                    width: `${((x1 - x0) / imageSize.width) * 100}%`,
                    height: `${((y1 - y0) / imageSize.height) * 100}%`,
                  };
                  const label = overlay.order === undefined
                    ? overlay.label
                    : String(overlay.order + 1);
                  return (
                    <button
                      type="button"
                      key={overlay.id}
                      className={`${overlayClass(overlay.kind)}${selectedId === overlay.id ? " selected" : ""}`}
                      style={style}
                      title={overlay.text || overlay.label}
                      aria-label={overlay.text || overlay.label}
                      onClick={(event) => {
                        event.stopPropagation();
                        onSelect(overlay);
                      }}
                    >
                      <span>{label}</span>
                    </button>
                  );
                })}
              </div>
            )}
          </div>
        )}
      </div>
    </section>
  );
}
