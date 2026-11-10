"use strict";

(() => {
  const pages = window.BetterBattlePages || (window.BetterBattlePages = {});
  const { truncate } = pages.config || {};

  function renderEvents() {
    dom.eventLog.innerHTML = state.events.length
      ? state.events.map((event) =>
          `<div class="event ${event.kind === "error" ? "error" : ""}">` +
          `<time>${esc(event.time)}</time><span>${esc(event.text)}</span></div>`).join("")
      : `<div class="event"><time>--:--:--</time><span>等待事件</span></div>`;
  }

  function renderTrace() {
    const trace = state.snapshot?.trace || {};
    setActiveBadge(dom.traceBadge, !!trace.active, "采集中", "空闲");
    dom.traceChanges.textContent = String(trace.changes ?? 0);
    dom.traceDropped.textContent = String(trace.dropped ?? 0);
    dom.traceFrame.textContent = String(trace.frame ?? 0);
  }

  function renderTraceFields() {
    dom.traceFields.innerHTML = TRACE_FIELDS.map(([value, label]) =>
      `<label><input type="checkbox" value="${value}" ` +
      `${DEFAULT_TRACE_FIELDS.has(value) ? "checked" : ""}>${label}</label>`).join("");
  }

  function renderDiagnostics() {
    const snapshot = state.snapshot;
    dom.diagConnected.textContent = snapshot?.connected ? "已连接" : "未连接";
    dom.diagQueue.textContent = String(snapshot?.pending ?? 0);
    dom.diagCatalog.textContent = String(snapshot?.catalog?.loaded ?? state.catalog.length);
    dom.diagEpoch.textContent = String(snapshot?.epoch ?? 0);
    dom.diagFrame.textContent = String(snapshot?.frame ?? 0);
    dom.diagAge.textContent = state.lastSnapshotAt
      ? `${((Date.now() - state.lastSnapshotAt) / 1000).toFixed(1)}s`
      : "--";
    dom.diagRequestPath.textContent = state.requestPath;
    dom.diagStage.textContent = snapshot?.stage || "--";
    dom.diagTraceFields.textContent = snapshot?.trace?.fields || "--";
    dom.rawJson.textContent = snapshot
      ? truncate(JSON.stringify(snapshot, null, 2), 30000)
      : "暂无数据";
    renderEvents();
  }

  pages.diagnostics = {
    render: renderDiagnostics,
    renderTrace,
    renderTraceFields,
    renderEvents,
  };
})();
