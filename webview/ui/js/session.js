(() => {
  "use strict";

  const protocol = window.BetterBattleProtocol;
  const snapshotListeners = new Set();
  const timelineListeners = new Set();
  const catalogListeners = new Set();
  const profilesListeners = new Set();
  let enemyProfilesSnapshot;
  let enemyProfilesCache = new Map();
  let aliveEnemiesSnapshot;
  let aliveEnemiesCache = [];

  function publish(listeners, ...args) {
    for (const listener of listeners) {
      try {
        listener(...args);
      } catch (error) {
        setTimeout(() => { throw error; }, 0);
      }
    }
  }

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
    catalog: [],
    catalogById: new Map(),
    profiles: new Map(),
    unitNames: new Map(),
    lastResult: "",
    resultRevision: 0,
    selection: {
      cardUid: null,
      summonUid: null,
      enemyUid: null,
    },
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

    onCatalog(listener) {
      if (typeof listener !== "function") return () => {};
      catalogListeners.add(listener);
      return () => catalogListeners.delete(listener);
    },

    onProfiles(listener) {
      if (typeof listener !== "function") return () => {};
      profilesListeners.add(listener);
      return () => profilesListeners.delete(listener);
    },

    clearTimeline() {
      session.timeline = null;
    },

    selectCard(uid) {
      const value = Number(uid);
      if (!Number.isFinite(value) || value <= 0) return false;
      session.selection.cardUid = value;
      session.selection.summonUid = null;
      return true;
    },

    selectSummon(uid) {
      const value = Number(uid);
      if (!Number.isFinite(value) || value <= 0) return false;
      session.selection.cardUid = null;
      session.selection.summonUid = value;
      return true;
    },

    selectEnemy(uid) {
      const value = Number(uid);
      if (!Number.isFinite(value) || value <= 0) return false;
      session.selection.enemyUid = value;
      return true;
    },

    idle(snapshot = session.snapshot) {
      return session.phase(snapshot) === "idle";
    },

    requiresRestartConfirmation(snapshot = session.snapshot) {
      return !!snapshot?.available && !!snapshot?.playing;
    },

    shouldPauseOnLeave(snapshot = session.snapshot) {
      if (!snapshot) return false;
      return snapshot.playing !== false &&
        Number(snapshot.state) >= 1 &&
        Number(snapshot.state) <= 2;
    },

    enemyProfiles(snapshot = session.snapshot) {
      if (snapshot === enemyProfilesSnapshot) return enemyProfilesCache;
      enemyProfilesSnapshot = snapshot;
      enemyProfilesCache = new Map(
        (Array.isArray(snapshot?.enemy_profiles) ? snapshot.enemy_profiles : [])
          .map((profile) => [String(profile?.id || ""), profile]),
      );
      return enemyProfilesCache;
    },

    aliveEnemies(snapshot = session.snapshot) {
      if (snapshot === aliveEnemiesSnapshot) return aliveEnemiesCache;
      aliveEnemiesSnapshot = snapshot;
      aliveEnemiesCache = (Array.isArray(snapshot?.enemies)
        ? snapshot.enemies
        : []).filter((enemy) => enemy && enemy.alive !== false);
      return aliveEnemiesCache;
    },

    replayActive(snapshot = session.snapshot) {
      if (!snapshot || snapshot.available === false) return false;
      if (snapshot.phase) return String(snapshot.phase) === "replay";
      return !!snapshot.replay?.active;
    },

    battleActive(snapshot = session.snapshot) {
      const phase = session.phase(snapshot);
      return phase === "starting" || phase === "playing" ||
        phase === "paused";
    },

    squadLocked(snapshot = session.snapshot) {
      const phase = session.phase(snapshot);
      return phase === "replay" || phase === "starting" ||
        phase === "playing" || phase === "paused";
    },

    phase(snapshot = session.snapshot) {
      if (!snapshot) return "idle";
      if (snapshot.phase) return String(snapshot.phase);
      if (snapshot.available === false) return "idle";
      const state = Number(snapshot.state);
      if (state === 3) return "ended";
      if (session.replayActive(snapshot)) return "replay";
      if (snapshot.playing === false) return "idle";
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
      const previous = session.snapshot;
      session.snapshot = message.payload;
      session.lastSnapshotAt = Date.now();
      const result = String(message.payload.result || "");
      if (result && result !== session.lastResult) {
        session.lastResult = result;
        session.resultRevision += 1;
      }
      if (Array.isArray(message.payload.units)) {
        const names = new Map();
        for (const unit of message.payload.units) {
          const id = String(unit?.id || "");
          if (!id) continue;
          names.set(id, String(unit.name || unit.tmpl || id));
        }
        session.unitNames = names;
      }
      publish(snapshotListeners, session.snapshot, previous);
      return true;
    }
    if (message.topic === protocol.Topic.Timeline && message.payload) {
      const previous = session.timeline;
      session.timeline = message.payload;
      publish(timelineListeners, session.timeline, previous);
      return true;
    }
    if (message.topic === protocol.Topic.Catalog && message.payload) {
      session.catalog = (Array.isArray(message.payload.operators)
        ? message.payload.operators
        : []).filter((entry) => String(entry?.id || "").startsWith("char_"));
      session.catalogById = new Map(
        session.catalog.map((entry) => [entry.id, entry]),
      );
      publish(catalogListeners, session.catalog);
      return true;
    }
    if (message.topic === protocol.Topic.Profiles && message.payload) {
      const profiles = new Map();
      for (const profile of message.payload.profiles || []) {
        if (profile && profile.key !== undefined) {
          profiles.set(String(profile.key), profile);
        }
      }
      session.profiles = profiles;
      publish(profilesListeners, session.profiles);
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
