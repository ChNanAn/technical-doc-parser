import { ARTIFACTS_EXPIRED_MESSAGE, getRun } from "./api";

export function isTerminalStatus(status: string): boolean {
  return ["succeeded", "failed", "cancelled"].includes(status);
}

export function subscribeToRun(
  runId: string,
  onEvent: (event: Record<string, unknown>) => void,
  onStatus: (status: string, error?: string) => void,
  onCancelRequested?: () => void,
): () => void {
  const source = new EventSource(`/api/v1/runs/${runId}/events`);
  const abort = new AbortController();
  const seen = new Set<string>();
  let stopped = false;
  let querying = false;
  let sequence = 0;
  const timer = setInterval(() => void refreshStatus(), 3000);

  function stop() {
    stopped = true;
    clearInterval(timer);
    source.close();
    abort.abort();
  }

  async function refreshStatus() {
    if (stopped || querying) return;
    querying = true;
    const startedSequence = sequence;
    try {
      const run = await getRun(runId, abort.signal);
      if (stopped) return;
      // An older HTTP response must not undo newer SSE progress.
      if (isTerminalStatus(run.status) || startedSequence === sequence) {
        if (run.cancel_requested) onCancelRequested?.();
        onStatus(run.status, run.error || (run.artifacts_expired_at ? ARTIFACTS_EXPIRED_MESSAGE : undefined));
        if (isTerminalStatus(run.status)) stop();
      }
    } catch {
      // EventSource retries with Last-Event-ID; HTTP polling also retries.
    } finally {
      querying = false;
    }
  }

  source.onmessage = (message) => {
    if (stopped) return;
    let event: Record<string, unknown>;
    try {
      event = JSON.parse(message.data);
      if (!event || typeof event !== "object" || typeof event.type !== "string") return;
    } catch {
      return;
    }
    const id = String(event.event_id ?? message.lastEventId);
    if (id && seen.has(id)) return;
    if (id) seen.add(id);
    onEvent(event);
    const eventSequence = Number(event.sequence ?? sequence + 1);
    if (eventSequence < sequence) return;
    sequence = eventSequence;
    const status = event.type === "job_succeeded" ? "succeeded"
      : event.type === "job_failed" ? "failed"
        : event.type === "job_cancelled" ? "cancelled" : "running";
    const error = event.error as { message?: string } | undefined;
    onStatus(status, error?.message);
    if (isTerminalStatus(status)) stop();
  };
  source.onerror = () => void refreshStatus();
  void refreshStatus();
  return stop;
}
