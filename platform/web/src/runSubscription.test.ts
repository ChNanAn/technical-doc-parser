import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { getRun } from "./api";
import { subscribeToRun } from "./runSubscription";

vi.mock("./api", () => ({ getRun: vi.fn() }));

class FakeEventSource {
  static current: FakeEventSource;
  onmessage?: (message: { data: string; lastEventId: string }) => void;
  onerror?: () => void;
  close = vi.fn();
  constructor() { FakeEventSource.current = this; }
  emit(event: Record<string, unknown>) {
    this.onmessage?.({ data: JSON.stringify(event), lastEventId: String(event.sequence) });
  }
}

describe("run subscription recovery", () => {
  let stop: (() => void) | undefined;
  beforeEach(() => {
    vi.useFakeTimers();
    vi.stubGlobal("EventSource", FakeEventSource);
    vi.mocked(getRun).mockResolvedValue({ status: "running" });
  });
  afterEach(() => {
    stop?.();
    vi.useRealTimers();
    vi.unstubAllGlobals();
    vi.resetAllMocks();
  });

  it("keeps SSE reconnectable and deduplicates replayed events", async () => {
    const event = vi.fn();
    stop = subscribeToRun("run_1", event, vi.fn());
    await vi.advanceTimersByTimeAsync(0);
    FakeEventSource.current.onerror?.();
    expect(FakeEventSource.current.close).not.toHaveBeenCalled();
    const message = { type: "stage_progress", event_id: "evt_1", sequence: 1 };
    FakeEventSource.current.emit(message);
    FakeEventSource.current.emit(message);
    expect(event).toHaveBeenCalledTimes(1);
  });

  it("discovers terminal state even when its SSE event was lost", async () => {
    const status = vi.fn();
    stop = subscribeToRun("run_1", vi.fn(), status);
    await vi.advanceTimersByTimeAsync(0);
    vi.mocked(getRun).mockResolvedValue({ status: "failed", error: "invalid PDF" });
    await vi.advanceTimersByTimeAsync(3000);
    expect(status).toHaveBeenLastCalledWith("failed", "invalid PDF");
    expect(FakeEventSource.current.close).toHaveBeenCalledTimes(1);
    const calls = vi.mocked(getRun).mock.calls.length;
    await vi.advanceTimersByTimeAsync(10000);
    expect(getRun).toHaveBeenCalledTimes(calls);
  });

  it("does not apply a stale HTTP response over a newer event", async () => {
    let resolve!: (run: { status: string }) => void;
    vi.mocked(getRun).mockReturnValue(new Promise((done) => { resolve = done; }));
    const status = vi.fn();
    stop = subscribeToRun("run_1", vi.fn(), status);
    FakeEventSource.current.emit({ type: "stage_started", sequence: 1 });
    resolve({ status: "queued" });
    await vi.advanceTimersByTimeAsync(0);
    expect(status).toHaveBeenLastCalledWith("running", undefined);
    expect(status).toHaveBeenCalledTimes(1);
  });

  it("stops at terminal events and ignores responses after cleanup", async () => {
    let resolve!: (run: { status: string }) => void;
    vi.mocked(getRun).mockReturnValue(new Promise((done) => { resolve = done; }));
    const status = vi.fn();
    stop = subscribeToRun("run_1", vi.fn(), status);
    FakeEventSource.current.emit({ type: "job_succeeded", sequence: 5 });
    resolve({ status: "running" });
    await vi.advanceTimersByTimeAsync(0);
    expect(status).toHaveBeenCalledTimes(1);
    expect(status).toHaveBeenLastCalledWith("succeeded", undefined);
    expect(FakeEventSource.current.close).toHaveBeenCalledTimes(1);
  });

  it("restores pending cancellation from polling and keeps listening until cancelled", async () => {
    vi.mocked(getRun).mockResolvedValue({ status: "running", cancel_requested: true });
    const pending = vi.fn();
    const status = vi.fn();
    stop = subscribeToRun("run_1", vi.fn(), status, pending);
    await vi.advanceTimersByTimeAsync(0);
    expect(pending).toHaveBeenCalledOnce();
    expect(status).toHaveBeenLastCalledWith("running", undefined);
    expect(FakeEventSource.current.close).not.toHaveBeenCalled();
    FakeEventSource.current.emit({ type: "job_cancelled", sequence: 12 });
    expect(status).toHaveBeenLastCalledWith("cancelled", undefined);
    expect(FakeEventSource.current.close).toHaveBeenCalledOnce();
  });
});
