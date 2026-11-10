(() => {
  "use strict";

  // 前端只在这里生成命令文本；插件侧的 process_request_file 仍是解析权威。
  const Message = Object.freeze({
    Command: "command",
    Log: "log",
    Host: "host",
    RequestPath: "requestPath",
    TimelineFiles: "timeline-files",
    TimelineLoad: "timeline-load",
    TimelineCatalog: "timeline-catalog",
    TimelinePlan: "timeline-plan",
    PickFolder: "pick-folder",
    PathPicked: "path-picked",
    FixtureBootstrap: "fixture-bootstrap",
  });

  const Topic = Object.freeze({
    Snapshot: "betterbattle.snapshot",
    Catalog: "betterbattle.catalog",
    Profiles: "betterbattle.profiles",
    Timeline: "betterbattle.timeline",
  });

  const quotePath = (value) => `"${String(value || "").replace(/"/g, "")}"`;

  const commands = Object.freeze({
    start: (stage, roster) => `start ${stage} ${roster}`,
    rogueStart: (topicId, stage, roster, mode, grade, growth, extra) =>
      `rogue-start ${topicId} ${stage} ${roster} ${mode} ${grade} ${growth} ${extra}`.trim(),
    pause: () => "pause",
    resume: () => "resume",
    refresh: () => "refresh",
    leave: () => "leave",
    skill: (uid) => `skill ${uid}`,
    withdraw: (uid) => `withdraw ${uid}`,
    enemyFocus: (uid, durationMs = 6000) => `enemy-focus ${uid} ${durationMs}`,
    traceStart: (fields) => `trace start ${fields}`,
    traceStop: () => "trace stop",
    timelineStatus: () => "timeline status",
    timelineJump: (frame, nodeId = "", fast = false, play = false) => {
      const mode = fast ? "fast" : "normal";
      return `timeline jump ${frame}${nodeId ? ` ${nodeId}` : ""} ${mode}${play ? " play" : ""}`;
    },
    timelineAdvance: (frames) => `timeline advance ${frames}`,
    timelineBranch: (nodeId) => `timeline branch ${nodeId}`,
    timelineRename: (nodeId, name) => `timeline rename ${nodeId} ${name}`,
    timelineDelete: (nodeId) => `timeline delete ${nodeId}`,
    replayStart: (path) => `replay start ${quotePath(path)}`,
    replaySeek: (frame, nodeId = "", fast = false) =>
      `replay seek ${frame}${nodeId ? ` ${nodeId}` : ""}${fast ? " fast" : ""}`,
    replaySave: (path, includeEvents = false) =>
      `replay save ${quotePath(path)}${includeEvents ? " events" : ""}`,
  });

  const normalize = (line) => String(line || "").replace(/[\r\n]+/g, " ").trim();

  const post = (message) => {
    window.chrome?.webview?.postMessage(message);
  };

  const send = (line) => {
    const command = normalize(line);
    if (!command) return false;
    post({ type: Message.Command, line: command });
    return true;
  };

  window.BetterBattleProtocol = Object.freeze({
    Message,
    Topic,
    commands,
    quotePath,
    normalize,
    post,
    send,
  });
})();
