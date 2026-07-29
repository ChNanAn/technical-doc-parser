import { useEffect, useMemo, useState } from "react";
import { Check, Copy, X } from "lucide-react";

type RawDataDrawerProps = {
  open: boolean;
  title: string;
  value: unknown;
  onClose: () => void;
};

export function RawDataDrawer({ open, title, value, onClose }: RawDataDrawerProps) {
  const [copied, setCopied] = useState(false);
  const serialized = useMemo(
    () => value === undefined ? "{}" : JSON.stringify(value, null, 2),
    [value],
  );

  useEffect(() => {
    if (!open) return;
    function closeOnEscape(event: KeyboardEvent) {
      if (event.key === "Escape") onClose();
    }
    window.addEventListener("keydown", closeOnEscape);
    return () => window.removeEventListener("keydown", closeOnEscape);
  }, [onClose, open]);

  async function copy() {
    if (navigator.clipboard) {
      await navigator.clipboard.writeText(serialized);
    } else {
      const textarea = document.createElement("textarea");
      textarea.value = serialized;
      textarea.style.position = "fixed";
      textarea.style.opacity = "0";
      document.body.appendChild(textarea);
      textarea.select();
      document.execCommand("copy");
      textarea.remove();
    }
    setCopied(true);
    window.setTimeout(() => setCopied(false), 1500);
  }

  if (!open) return null;
  return (
    <div className="drawer-backdrop" role="presentation" onMouseDown={onClose}>
      <aside className="raw-drawer" role="dialog" aria-modal="true" aria-label={`${title} 原始数据`} onMouseDown={(event) => event.stopPropagation()}>
        <header>
          <div>
            <span className="section-kicker">原始数据</span>
            <h2>{title}</h2>
          </div>
          <div className="drawer-actions">
            <button className="icon-button" type="button" title="复制 JSON" aria-label="复制 JSON" onClick={copy}>
              {copied ? <Check size={18} /> : <Copy size={18} />}
            </button>
            <button className="icon-button" type="button" title="关闭" aria-label="关闭" onClick={onClose}>
              <X size={19} />
            </button>
          </div>
        </header>
        <pre>{serialized}</pre>
      </aside>
    </div>
  );
}
