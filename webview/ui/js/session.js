(() => {
  "use strict";

  const protocol = window.BetterBattleProtocol;
  const snapshotListeners = new Set();
  const timelineListeners = new Set();

  const PHASE_LABELS = {
    idle: "未战斗",
    starting: "准备中",
    playing: "战斗中",
    paused: "已暂停",
    ended: "已结束",
    replay: "回放中",
  };

  const session = {
    snapshot: null,
    timeline: null,
    lastSnapshotAt: 0,

    onSnapshot(listener) {
      if (typeof listener !== "function") return () => {};
      snapshotListeners.add(listener);
      return () => snapshotListeners.delete(listener);
    },

    onTimeline(listener) {
      if (typeof listener !== "function") return () => {};
      timelineListeners.add(listener);
      return () => timelineListeners.delete(listener);
    },

    replayActive(snapshot = session.snapshot) {
      return !!snapshot?.replay?.active;
    },

    battleActive(snapshot = session.snapshot) {
      if (!snapshot) return false;
      if (snapshot.playing === false) return false;
      const state = Number(snapshot.state);
      return state === 1 || state === 2;
    },

    squadLocked(snapshot = session.snapshot) {
      return session.replayActive(snapshot) || session.battleActive(snapshot);
    },

    phase(snapshot = session.snapshot) {
      if (!snapshot || snapshot.available === false) return "idle";
      if (session.replayActive(snapshot)) return "replay";
      if (snapshot.playing === false) return "idle";
      const state = Number(snapshot.state);
      if (state === 3) return "ended";
      if (state === 1) return "starting";
      if (state === 2) return snapshot.paused ? "paused" : "playing";
      return state >= 0 ? "playing" : "idle";
    },

    phaseLabel(snapshot = session.snapshot) {
      return PHASE_LABELS[session.phase(snapshot)] || "未战斗";
    },
  };

  function ingest(message) {
    if (!message || typeof message !== "object") return false;
    if (message.topic === protocol.Topic.Snapshot && message.payload) {
      session.snapshot = message.payload;
      session.lastSnapshotAt = Date.now();
      for (const listener of snapshotListeners) listener(session.snapshot);
      return true;
    }
    if (message.topic === protocol.Topic.Timeline && message.payload) {
      session.timeline = message.payload;
      for (const listener of timelineListeners) listener(session.timeline);
      return true;
    }
    return false;
  }

  session.ingest = ingest;

  if (window.chrome?.webview) {
    window.chrome.webview.addEventListener("message", (event) => {
      let message = event.data;
      if (typeof message === "string") {
        try {
          message = JSON.parse(message);
        } catch {
          return;
        }
      }
      ingest(message);
    });
  }

  window.BetterBattleSession = session;
})();
