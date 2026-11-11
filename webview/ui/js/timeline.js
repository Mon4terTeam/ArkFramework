(() => {
  "use strict";

  const FPS = 30;
  const protocol = window.BetterBattleProtocol;
  const session = window.BetterBattleSession;
  const NO_SOURCE_ID = "__no_source__";
  const NODE_EVENT_INSET = 9;
  const FULL_RENDER_INTERVAL_MS = 100;
  const CATEGORY = {
    operation: { label: "玩家操作", color: "#8ff0c0" },
    combat: { label: "战斗事件", color: "#e6c15c" },
    world: { label: "世界事件", color: "#72d5e6" },
  };
  // 节点内三条轴从上到下排列。
  // operation 操作轴：玩家入口操作。
  // event 事件轴：伤害等战斗事件。
  // world 世界轴：波次、费用等世界事件，暂未接入。
  const AXES = ["operation", "event", "world"];
  const AXIS_OF_CATEGORY = {
    operation: "operation",
    combat: "event",
    world: "world",
  };
  const TYPE_META = {
    deploy: { label: "部署", color: "#8ff0c0", source: "BattleController.PlayerOp_Spawn" },
    skill: { label: "技能", color: "#b69cff", source: "BattleController.PlayerOp_TrigSkill" },
    withdraw: { label: "撤退", color: "#ff7b72", source: "BattleController.PlayerOp_Withdraw" },
    damage: { label: "伤害变化", color: "#e6c15c", source: "damage.series" },
    heal: { label: "治疗", color: "#83d6a5", source: "damage.series" },
    regen: { label: "回血", color: "#5fd0ff", source: "Entity._UpdateHpRecovery" },
    taken: { label: "承伤", color: "#ff7b72", source: "unit.hp" },
    cost: { label: "费用回复", color: "#72d5e6", source: "BattleController.GetCost" },
    wave: { label: "波次", color: "#72d5e6", source: "Scheduler.get_spawnedWavesCnt" },
  };
  const DAMAGE_TYPES = [
    { key: "physical", label: "物理" },
    { key: "magical", label: "法术" },
    { key: "true", label: "真实" },
    { key: "elemental", label: "元素" },
    { key: "heal", label: "治疗" },
    { key: "regen", label: "回血" },
    { key: "taken", label: "承伤" },
  ];
  const BRANCH_COLORS = [
    "#8ff0c0", "#e6c15c", "#b69cff", "#72d5e6",
    "#9fc9ff", "#f09a73", "#f08fd4", "#c6d36b",
  ];
  const STAGE_NAME_BY_ID = new Map();
  const UNIT_NAME_BY_ID = new Map();
  for (const operator of Array.isArray(window.ROSTER_CATALOG)
    ? window.ROSTER_CATALOG
    : []) {
    const id = String(operator?.id || "").trim();
    const name = String(operator?.name || "").trim();
    if (id && name && !UNIT_NAME_BY_ID.has(id)) UNIT_NAME_BY_ID.set(id, name);
  }
  const indexStageNames = (stages) => {
    for (const stage of Array.isArray(stages) ? stages : []) {
      const id = String(stage?.id || "").trim();
      const name = String(stage?.name || "").trim();
      if (id && name && !STAGE_NAME_BY_ID.has(id)) {
        STAGE_NAME_BY_ID.set(id, name);
      }
    }
  };
  indexStageNames(window.BETTERBATTLE_STAGES);
  for (const topic of Array.isArray(window.BETTERBATTLE_ROGUELIKE)
    ? window.BETTERBATTLE_ROGUELIKE
    : []) {
    indexStageNames(topic?.stages);
  }

  const dom = {};
  const ids = [
    "timelinePauseBadge", "timelineStageBadge",
    "timelineState", "timelinePauseButton", "timelineResumeButton",
    "treeViewToggle", "frameReadout", "totalReadout", "frameInput",
    "frameGo", "playButton", "followPlayback", "splitBranch", "rewindPlay",
    "jumpModeNormal", "jumpModeFast",
    "jumpStart", "jumpEnd",
    "treeScroll", "treeCanvas", "tree", "connectors", "treeNodes",
    "cursor", "cursorFlag", "gameCursor", "gameCursorFlag",
    "treeHint", "renameSegment", "deleteSegment",
    "rewindView",
    "renameModal", "segmentNameInput", "confirmRename", "deleteModal",
    "deleteSegmentName", "deleteSegmentRange", "deleteDescendantCount",
    "confirmDelete", "filters", "eventSummary", "activeSegmentBadge",
    "eventList", "detailFrameBadge", "eventDetail",
    "saveTimeline", "loadTimeline", "saveTimelineModal", "saveDirectory",
    "saveBrowse", "saveName", "saveEvents", "confirmSaveTimeline", "loadTimelineModal",
    "loadDirectory", "loadBrowse", "loadFilter", "loadTimelineFile",
    "loadTimelineMeta", "confirmLoadTimeline",
  ];
  for (const id of ids) dom[id] = document.getElementById(id);

  const state = {
    get snapshot() {
      return session?.snapshot || null;
    },
    get timeline() {
      return session?.timeline || null;
    },
    get unitNames() {
      return session?.unitNames || new Map();
    },
    nodeRender: new Map(),
    frame: 0,
    selectedNodeId: "",
    branchTargetNodeId: "",
    selectedEventId: "",
    filter: "all",
    viewMode: "detail",
    followLive: true,
    autoPan: true,
    lastFrameAdvanceAt: 0,
    jumpMode: "normal",
    resultRevisionSeen: 0,
    forceResult: false,
    timelineRequested: false,
    rewinding: false,
    rewindEpoch: 0,
    rewindStartedAt: 0,
    recordings: {
      directory: "",
      requestPath: "",
      files: [],
    },
  };

  function setTimelinePauseButtonState(paused) {
    const known = typeof paused === "boolean";
    dom.timelinePauseButton?.classList.toggle("primary", known && !paused);
    dom.timelineResumeButton?.classList.toggle("primary", known && paused);
  }

  let pendingFullRender = 0;
  let lastFullRenderAt = 0;
  let lastRenderSignature = "";
  let pointerHeld = false;
  let renderAfterPointer = false;
  const stepButtons = [...document.querySelectorAll("[data-step]")];

  const esc = (value) => String(value ?? "").replace(/[&<>"']/g, (ch) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[ch]));
  const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
  const formatFrame = (frame) => String(Math.max(0, Math.round(frame))).padStart(5, "0");
  const formatTime = (frame) => {
    const seconds = Math.max(0, frame) / FPS;
    const minutes = Math.floor(seconds / 60);
    return `${String(minutes).padStart(2, "0")}:${(seconds % 60).toFixed(2).padStart(5, "0")}`;
  };
  const formatNumber = (value) => Math.round(Number(value) || 0).toLocaleString("zh-CN");

  function post(message) {
    protocol.post(message);
  }

  function sendCommand(line) {
    protocol.send(line);
  }

  function toast(text, kind = "") {
    const container = document.getElementById("toasts");
    if (!container) return;
    const node = document.createElement("div");
    node.className = `toast ${kind}`.trim();
    node.textContent = text;
    container.appendChild(node);
    setTimeout(() => node.remove(), 3400);
  }

  function setHtml(element, html) {
    if (!element || element.__tasHtml === html) return false;
    const scrollTop = element.scrollTop;
    element.__tasHtml = html;
    element.innerHTML = html;
    if (scrollTop) element.scrollTop = scrollTop;
    return true;
  }

  function isTabActive() {
    const timelinePage = document.getElementById("page-timeline");
    if (timelinePage?.classList.contains("active")) return true;
    return !!document.getElementById("page-gameview")?.classList.contains("active") &&
      !!document.querySelector("#gameviewTimelineHost .tas-timeline-panel");
  }

  function activeTimeline() {
    return state.timeline?.active ? state.timeline : null;
  }

  function clearTimelineState() {
    session?.clearTimeline();
    state.timelineRequested = false;
    state.frame = 0;
    state.selectedNodeId = "";
    state.branchTargetNodeId = "";
    state.selectedEventId = "";
    state.nodeRender.clear();
    state.followLive = true;
    state.autoPan = true;
    state.rewinding = false;
    state.rewindStartedAt = 0;
  }

  function isIdleSnapshot(snapshot) {
    if (!snapshot) return true;
    return window.BetterBattleSession?.idle(snapshot) || false;
  }

  function replayInfo() {
    const replay = state.snapshot?.replay;
    return replay && String(replay.file || "").trim() ? replay : null;
  }

  function currentPlaybackFrame() {
    return Number(state.snapshot?.frame) || 0;
  }

  function actionTargetFrame() {
    const nodes = timelineNodes();
    const maxFrame = Math.max(currentPlaybackFrame(), totalFrames(nodes));
    const raw = Number(dom.frameInput?.value);
    const value = Number.isFinite(raw) ? raw : state.frame;
    return clamp(Math.round(value), 0, maxFrame);
  }

  function isTimelinePlayback() {
    const live = activeTimeline();
    const advancing = state.lastFrameAdvanceAt > 0 &&
      performance.now() - state.lastFrameAdvanceAt < 900;
    const phase = window.BetterBattleSession?.phase(state.snapshot);
    return !!(phase === "replay" || replayInfo() || live?.jumping ||
      live?.advancing || advancing);
  }

  function operationEvents(node) {
    const events = [];
    let sequence = 0;
    for (const [frameText, operations] of Object.entries(node.operations || {})) {
      const frame = Number(frameText);
      if (!Number.isFinite(frame) || !Array.isArray(operations)) continue;
      for (const operation of operations) {
        if (!Array.isArray(operation) || operation.length < 2) continue;
        const type = String(operation[0] || "");
        if (!TYPE_META[type] || type === "damage") continue;
        events.push({
          id: `op:${node.id}:${frame}:${sequence++}`,
          nodeId: node.id,
          frame,
          category: "operation",
          type,
          unit: String(operation[1] || ""),
          direction: Number(operation[2]),
          tile: Array.isArray(operation[3]) ? operation[3].map(Number) : null,
        });
      }
    }
    return events;
  }

  function damageEvents() {
    const hits = Array.isArray(activeTimeline()?.damage_hits)
      ? activeTimeline().damage_hits : [];
    return hits.map((hit, index) => {
      const typeIndex = Number(hit?.type);
      const type = DAMAGE_TYPES[typeIndex] || DAMAGE_TYPES[2];
      const healing = typeIndex === 4;
      const taken = typeIndex === 5;
      const regen = typeIndex === 6;
      return {
        id: `damage:${Number(hit?.frame) || 0}:${index}`,
        nodeId: String(hit?.node_id || ""),
        frame: Number(hit?.frame) || 0,
        category: "combat",
        type: healing ? "heal" : (regen ? "regen" : (taken ? "taken" : "damage")),
        actor: String(hit?.actor || ""),
        target: String(hit?.target || ""),
        actorUid: Number(hit?.actor_uid) || 0,
        targetUid: Number(hit?.target_uid) || 0,
        damageType: type.key,
        damageLabel: type.label,
        amount: Math.abs(Number(hit?.value) || 0),
      };
    }).filter((event) => event.actor || event.target);
  }

  function worldEvents() {
    const events = Array.isArray(activeTimeline()?.world_events)
      ? activeTimeline().world_events : [];
    return events
      .filter((event) => Number(event?.kind) !== 2)
      .map((event, index) => ({
        id: `world:${Number(event?.frame) || 0}:${index}`,
        nodeId: String(event?.node_id || ""),
        frame: Number(event?.frame) || 0,
        category: "world",
        type: "cost",
        value: Number(event?.value) || 0,
        total: Number(event?.total) || 0,
      }));
  }

  function timelineNodes() {
    const live = activeTimeline();
    if (live) {
      return (live.nodes || [])
        .filter((node) => node && node.id)
        .map((node) => ({
          ...node,
          parentId: String(node.parent_id || ""),
          start: Number(node.fork_frame) || 0,
          end: Math.max(Number(node.fork_frame) || 0, Number(node.end_frame) || 0),
          branchId: Number(node.branch) || 1,
          operations: node.operations && typeof node.operations === "object"
            ? node.operations : {},
        }))
        .sort((left, right) => left.start - right.start || String(left.id).localeCompare(String(right.id)));
    }
    return [];
  }

  function totalFrames(nodes) {
    return Math.max(
      Number(state.snapshot?.frame) || 0,
      Number(activeTimeline()?.max_frame) || 0,
      ...nodes.map((node) => node.end || 0),
    );
  }

  function timelineCurrentNodeId(nodes) {
    const live = activeTimeline();
    if (live?.current_id && nodes.some((node) => node.id === live.current_id)) {
      return live.current_id;
    }
    return nodes[0]?.id || "";
  }

  function timelinePathNodes(nodes) {
    const live = activeTimeline();
    const byId = new Map(nodes.map((node) => [node.id, node]));
    if (Array.isArray(live?.active_path) && live.active_path.length) {
      const path = live.active_path
        .map((id) => byId.get(String(id)))
        .filter(Boolean);
      if (path.length) return path;
    }
    const path = [];
    let node = byId.get(timelineCurrentNodeId(nodes));
    const seen = new Set();
    while (node && !seen.has(node.id)) {
      path.push(node);
      seen.add(node.id);
      node = byId.get(node.parentId);
    }
    return path.reverse();
  }

  function nodePath(nodes, nodeId) {
    const byId = new Map(nodes.map((node) => [node.id, node]));
    const path = [];
    const seen = new Set();
    let node = byId.get(String(nodeId || ""));
    while (node && !seen.has(node.id)) {
      path.push(node);
      seen.add(node.id);
      node = byId.get(node.parentId);
    }
    return path.reverse();
  }

  function descendantConnectorIds(nodes, targetId) {
    const baseId =
      playbackNodeAtFrame(nodes, currentPlaybackFrame())?.id || "";
    const base = nodePath(nodes, baseId).map((node) => node.id);
    const target = nodePath(nodes, targetId).map((node) => node.id);
    if (!base.length || !target.length || base.length > target.length) {
      return new Set();
    }
    for (let index = 0; index < base.length; index += 1) {
      if (base[index] !== target[index]) return new Set();
    }
    return new Set(target.slice(base.length));
  }

  function isPlaybackPathDescendant(nodes, targetId) {
    const baseId =
      playbackNodeAtFrame(nodes, currentPlaybackFrame())?.id || "";
    const base = nodePath(nodes, baseId).map((node) => node.id);
    const target = nodePath(nodes, targetId).map((node) => node.id);
    if (!base.length || !target.length || base.length > target.length) {
      return false;
    }
    return base.every((id, index) => id === target[index]);
  }

  function isNodeOnTimelinePath(nodes, targetId) {
    const active = timelinePathNodes(nodes).map((node) => node.id);
    const target = nodePath(nodes, targetId).map((node) => node.id);
    return target.length > 0 && target.length <= active.length &&
      target.every((id, index) => id === active[index]);
  }

  function selectedBranchTarget(nodes) {
    const target = String(state.branchTargetNodeId || "");
    return target &&
      (isPlaybackPathDescendant(nodes, target) ||
       isNodeOnTimelinePath(nodes, target))
      ? target
      : "";
  }

  function actionNodeId(nodes, frame) {
    const target = Number(frame) || 0;
    const selected = nodes.find(
      (node) => node.id === String(state.selectedNodeId || ""),
    );
    return selectedBranchTarget(nodes) ||
      (isNodeOnTimelinePath(nodes, state.selectedNodeId)
        ? state.selectedNodeId
        : "") ||
      (selected && target >= selected.start && target <= selected.end
        ? selected.id
        : "") ||
      playbackNodeAtFrame(nodes, currentPlaybackFrame())?.id ||
      playbackNodeAtFrame(nodes, target)?.id ||
      timelineCurrentNodeId(nodes);
  }

  function switchPlaybackPath(nodes, targetId) {
    const target = String(targetId || "");
    if (!target || !isPlaybackPathDescendant(nodes, target)) {
      return;
    }
    const active = timelinePathNodes(nodes).map((node) => node.id);
    const targetPath = nodePath(nodes, target).map((node) => node.id);
    const alreadyOnPath = targetPath.every(
      (id, index) => active[index] === id,
    );
    if (alreadyOnPath || !replayInfo()) {
      return;
    }
    sendCommand(protocol.commands.timelineBranch(target));
  }

  function playbackNodeAtFrame(nodes, frame) {
    const path = timelinePathNodes(nodes);
    if (!path.length) return null;
    let current = path[0];
    for (const node of path) {
      const start = node.parentId ? node.start + 1 : 0;
      if (frame >= start) current = node;
      else break;
    }
    return current;
  }

  function laneNodeIdFor(nodes) {
    const currentId = timelineCurrentNodeId(nodes);
    return state.followLive
      ? (playbackNodeAtFrame(nodes, Number(state.frame) || 0)?.id || currentId)
      : (state.selectedNodeId || currentId);
  }

  function nodeEventGeometry(node) {
    const width = Math.max(1, Number(node.render?.width) || 0);
    const laneWidth = Math.max(1, width - NODE_EVENT_INSET * 2);
    return {
      left: (Number(node.render?.x) || 0) + NODE_EVENT_INSET,
      width: laneWidth,
    };
  }

  function nodeFrameRatio(node, frame) {
    return clamp(
      (Number(frame) - node.start) / Math.max(1, node.end - node.start),
      0,
      1,
    );
  }

  function nodeFrameX(node, frame) {
    const lane = nodeEventGeometry(node);
    return lane.left + nodeFrameRatio(node, frame) * lane.width;
  }

  function selectedNode(nodes) {
    return nodes.find((node) => node.id === state.selectedNodeId)
      || nodes.find((node) => node.id === timelineCurrentNodeId(nodes))
      || nodes[0]
      || null;
  }

  function displayedNode(nodes) {
    if (state.followLive) {
      return playbackNodeAtFrame(nodes, Number(state.frame) || 0)
        || selectedNode(nodes);
    }
    return selectedNode(nodes);
  }

  function childNodes(nodes, parentId) {
    return nodes
      .filter((node) => node.parentId === parentId)
      .sort((left, right) => left.start - right.start || String(left.id).localeCompare(String(right.id)));
  }

  function nodeHasEvent(node, event) {
    if (event.nodeId) return event.nodeId === node.id;
    if (!node.parentId) {
      return event.frame >= node.start && event.frame <= node.end;
    }
    return event.frame > node.start && event.frame <= node.end;
  }

  function allEvents(nodes) {
    const operations = nodes.flatMap(operationEvents);
    return [...operations, ...damageEvents(), ...worldEvents()]
      .sort((left, right) => left.frame - right.frame
        || String(left.id).localeCompare(String(right.id)));
  }

  function eventsForNode(nodes, node) {
    if (!node) return [];
    return allEvents(nodes).filter((event) => nodeHasEvent(node, event));
  }

  function eventColor(event) {
    return TYPE_META[event.type]?.color || CATEGORY[event.category]?.color || "#74808a";
  }

  function unitName(unitId, unitUid = 0) {
    const id = String(unitId || "");
    if (id === NO_SOURCE_ID) return "无来源";
    if (id.startsWith("enemy_") && window.BetterBattleEnemyDisplay) {
      const snapshot = state.snapshot;
      const profiles = session
        ? session.enemyProfiles(snapshot)
        : new Map(
          (Array.isArray(snapshot?.enemy_profiles)
            ? snapshot.enemy_profiles
            : []).map((profile) => [String(profile?.id || ""), profile]),
        );
      const enemies = Array.isArray(snapshot?.enemies) ? snapshot.enemies : [];
      const exact = window.BetterBattleEnemyDisplay.labelByUid(
        unitUid,
        Array.isArray(snapshot?.enemy_identities)
          ? snapshot.enemy_identities
          : [],
        profiles,
        enemies,
      );
      if (exact) return exact;
      return window.BetterBattleEnemyDisplay.label(
        id,
        profiles,
        enemies,
      );
    }
    return UNIT_NAME_BY_ID.get(id)
      || state.unitNames.get(id)
      || id;
  }

  function eventTitle(event) {
    if (event.category === "operation") {
      return `${TYPE_META[event.type]?.label || event.type} · ${unitName(event.unit)}`;
    }
    if (event.type === "damage") {
      return event.actor
        ? `${unitName(event.actor, event.actorUid)} 对 ${unitName(event.target, event.targetUid)} 造成 ${formatNumber(event.amount)} 点${event.damageLabel}伤害`
        : `${unitName(event.target, event.targetUid)} 受到 ${formatNumber(event.amount)} 点${event.damageLabel}伤害`;
    }
    if (event.type === "heal") {
      return event.actor && event.actor !== event.target
        ? `${unitName(event.actor, event.actorUid)} 为 ${unitName(event.target, event.targetUid)} 回复 ${formatNumber(event.amount)} 点生命`
        : `${unitName(event.target, event.targetUid)} 回复 ${formatNumber(event.amount)} 点生命`;
    }
    if (event.type === "taken") {
      return `${unitName(event.target, event.targetUid)} 受到 ${formatNumber(event.amount)} 点伤害`;
    }
    if (event.type === "regen") {
      return `${unitName(event.target, event.targetUid)} 被动回复 ${formatNumber(event.amount)} 点生命`;
    }
    if (event.type === "cost") {
      return `费用 +${formatNumber(event.value)} · 当前 ${formatNumber(event.total)}`;
    }
    if (event.type === "wave") {
      return event.total > 0
        ? `第 ${event.value}/${event.total} 波`
        : `第 ${event.value} 波`;
    }
    return event.type;
  }

  function eventMeta(event) {
    if (event.category === "operation") {
      if (event.type === "deploy") {
        return `[${event.tile?.[0] ?? "?"}, ${event.tile?.[1] ?? "?"}] · 朝向 ${event.direction}`;
      }
      return "";
    }
    if (event.category === "combat") return "";
    if (event.category === "world") {
      return event.type === "wave" ? "波次开始" : "自然回复或返费";
    }
    return "";
  }

  function eventById(nodes, id) {
    return allEvents(nodes).find((event) => event.id === id) || null;
  }

  function eventAtFrame(nodes, frame) {
    const events = allEvents(nodes);
    return events.reduce((best, event) => {
      if (!best) return event;
      return Math.abs(event.frame - frame) < Math.abs(best.frame - frame) ? event : best;
    }, null);
  }

  function branchColor(branchId) {
    return BRANCH_COLORS[(Math.max(1, Number(branchId) || 1) - 1) % BRANCH_COLORS.length];
  }

  function nodeLabel(node) {
    const branch = `TL${Number(node.branchId) || 1}`;
    const name = String(node.name || "").trim();
    if (!name) return branch;
    if (name === branch || name === `L${Number(node.branchId) || 1}`) {
      return branch;
    }
    return name;
  }

  function layoutRows(nodes) {
    let leafRow = 0;
    const roots = nodes.filter((node) => !node.parentId);
    const visit = (node) => {
      const children = childNodes(nodes, node.id);
      if (!children.length) {
        node.row = leafRow++;
        return node.row;
      }
      const rows = children.map(visit);
      node.row = rows.reduce((sum, row) => sum + row, 0) / rows.length;
      return node.row;
    };
    roots.forEach(visit);
    return Math.max(0, ...nodes.map((node) => node.row || 0));
  }

  function buildGeometry(nodes, viewportWidth, viewportHeight, overview) {
    state.nodeRender.clear();
    const maxRow = layoutRows(nodes);
    const timeScale = overview
      ? Math.max(0.08, Math.min(0.72, viewportWidth / Math.max(800, totalFrames(nodes) * 1.2)))
      : 0.56;
    const rowStep = overview ? 44 : 70;
    const nodeHeight = overview ? 30 : 42;
    const activeHeight = overview ? 30 : 88;
    const maxHeight = Math.max(nodeHeight, activeHeight);
    const contentHeight = maxRow * rowStep + maxHeight;
    const treeHeight = Math.max(180, viewportHeight, contentHeight + 24);
    const verticalOffset = Math.max(12, (treeHeight - contentHeight) / 2);
    const gutter = overview ? 28 : 70;
    const padX = overview ? 18 : 34;
    const activeId = laneNodeIdFor(nodes);
    let maxX = 0;
    const visit = (node, x) => {
      const active = node.id === activeId;
      const duration = Math.max(1, node.end - node.start);
      const minimumWidth = overview ? (active ? 92 : 72) : (active ? 200 : 140);
      const width = Math.max(minimumWidth, duration * timeScale + (active ? 18 : 4));
      const height = active ? activeHeight : nodeHeight;
      const centerY = verticalOffset + node.row * rowStep + maxHeight / 2;
      node.render = {
        x,
        width,
        height,
        y: centerY - height / 2,
        centerY,
      };
      state.nodeRender.set(node.id, node.render);
      maxX = Math.max(maxX, x + width);
      for (const child of childNodes(nodes, node.id)) visit(child, x + width + gutter);
    };
    nodes.filter((node) => !node.parentId).forEach((node) => visit(node, padX));
    return {
      treeHeight,
      contentWidth: maxX + padX * 2,
      rowStep,
      nodeHeight,
      activeHeight,
      overview,
    };
  }

  function renderTree(nodes) {
    const viewportWidth = Math.max(1, dom.treeScroll.clientWidth || 1200);
    const viewportHeight = Math.max(180, dom.treeScroll.clientHeight || 260);
    const overview = state.viewMode === "overview";
    const geometry = buildGeometry(nodes, viewportWidth, viewportHeight, overview);
    const width = Math.max(viewportWidth, geometry.contentWidth);
    const widthStyle = `${width}px`;
    const heightStyle = `${geometry.treeHeight}px`;
    if (dom.treeCanvas.style.width !== widthStyle) dom.treeCanvas.style.width = widthStyle;
    if (dom.treeCanvas.style.height !== heightStyle) dom.treeCanvas.style.height = heightStyle;
    if (dom.tree.style.height !== heightStyle) dom.tree.style.height = heightStyle;
    dom.tree.classList.toggle("is-overview", overview);
    dom.treeViewToggle.textContent = overview ? "总览" : "聚焦";
    dom.treeViewToggle.classList.toggle("is-overview", overview);

    if (!nodes.length) {
      setHtml(dom.treeNodes, `<div class="tas-tree-empty">
        进入战斗后会自动建立根节点；帧轴移动不会影响游戏状态。
      </div>`);
      setHtml(dom.connectors, "");
      dom.cursor.hidden = true;
      return;
    }

    const live = activeTimeline();
    const laneNodeId = laneNodeIdFor(nodes);
    const html = nodes.map((node) => {
      const color = branchColor(node.branchId);
      const active = node.id === laneNodeId;
      const selected = node.id === state.selectedNodeId;
      const compact = node.render.width < (active ? 248 : 196);
      const events = eventsForNode(nodes, node);
      const lanes = overview || !active ? "" : AXES.map((axis) => {
        const matched = events.filter(
          (event) => AXIS_OF_CATEGORY[event.category] === axis,
        );
        const stride = Math.max(1, Math.ceil(matched.length / (overview ? 40 : 120)));
        const lane = nodeEventGeometry(node);
        const laneWidth = lane.width;
        const markers = matched
          .filter((event, index) => index % stride === 0)
          .map((event) => {
            const ratio = clamp(
              (event.frame - node.start) / Math.max(1, node.end - node.start),
              0,
              1,
            );
            return `<i class="tas-node-event" data-event-id="${esc(event.id)}"
              title="${esc(eventTitle(event))} · F${event.frame}"
              style="left:${Math.min(ratio * laneWidth, Math.max(0, laneWidth - 3))}px;--event-color:${eventColor(event)}"></i>`;
          }).join("");
        return `<span class="tas-node-lane is-${axis}">${markers}</span>`;
      }).join("");
      return `<button class="tas-node${active ? " is-active" : ""}${selected ? " is-selected" : ""}${compact ? " is-compact" : ""}"
        type="button" data-node-id="${esc(node.id)}"
        style="left:${node.render.x}px;top:${node.render.y}px;height:${node.render.height}px;
          width:${node.render.width}px;--branch-color:${color}"
        title="${esc(nodeLabel(node))} · F${node.start} → F${node.end}">
        <span class="tas-node-edge is-start"><b>F${formatFrame(node.start)}</b><time>${formatTime(node.start)}</time></span>
        <span class="tas-node-edge is-end"><b>F${formatFrame(node.end)}</b><time>${formatTime(node.end)}</time></span>
        <span class="tas-node-head">${
          String(node.name || "").trim()
            ? `<code>TL${node.branchId}</code>${
                nodeLabel(node) === `TL${node.branchId}`
                  ? ""
                  : `<strong>${esc(nodeLabel(node))}</strong>`
              }`
            : `<code>TL${node.branchId}</code>`
        }</span>
        <span class="tas-node-events">${lanes}</span>
      </button>`;
    }).join("");
    const splitDots = nodes
      .filter((node) => childNodes(nodes, node.id).length)
      .map((node) => `<span class="tas-node-split"
        style="left:${node.render.x + node.render.width}px;top:${node.render.centerY}px"></span>`)
      .join("");
    setHtml(dom.treeNodes, html + splitDots);
    renderConnectors(nodes);
    renderCursor(nodes, live);
    renderGameCursor(nodes, live);
  }

  function renderConnectors(nodes) {
    const width = dom.tree.clientWidth;
    const height = dom.tree.clientHeight;
    if (!width || !height) {
      setHtml(dom.connectors, "");
      return;
    }
    dom.connectors.setAttribute("viewBox", `0 0 ${width} ${height}`);
    const paths = [];
    const selectedLinks = descendantConnectorIds(
      nodes,
      selectedBranchTarget(nodes),
    );
    for (const child of nodes) {
      if (!child.parentId) continue;
      const parent = nodes.find((node) => node.id === child.parentId);
      if (!parent?.render || !child.render) continue;
      const x1 = parent.render.x + parent.render.width + 1;
      const y1 = parent.render.centerY;
      const x2 = child.render.x - 1;
      const y2 = child.render.centerY;
      const color = branchColor(child.branchId);
      const selected = selectedLinks.has(child.id);
      const d = Math.abs(y1 - y2) < 1
        ? `M ${x1} ${y1} L ${x2} ${y2}`
        : `M ${x1} ${y1} C ${(x1 + x2) / 2} ${y1}, ${(x1 + x2) / 2} ${y2}, ${x2} ${y2}`;
      const marker = Math.abs(y1 - y2) < 1
        ? ""
        : `<circle cx="${x1}" cy="${y1}" r="1.8" fill="${color}" fill-opacity=".9"></circle>`;
      paths.push(`<g class="tas-connector${selected ? " is-selected" : ""}"
        data-child-id="${esc(child.id)}" style="--branch-color:${color}">
        <path class="tas-connector-hit" d="${d}"></path>
        <path class="tas-connector-line${Math.abs(y1 - y2) < 1 ? " is-straight" : ""}"
          d="${d}"></path>${marker}
      </g>`);
    }
    setHtml(dom.connectors, paths.join(""));
  }

  function renderCursor(nodes, live) {
    const nodeId = state.followLive
      ? (playbackNodeAtFrame(nodes, state.frame)?.id || timelineCurrentNodeId(nodes))
      : (state.selectedNodeId || timelineCurrentNodeId(nodes));
    const node = nodes.find((entry) => entry.id === nodeId);
    if (!node?.render || !live) {
      dom.cursor.hidden = true;
      return;
    }
    dom.cursor.hidden = false;
    dom.cursor.style.left = `${nodeFrameX(node, state.frame)}px`;
    dom.cursor.style.top = `${node.render.y - 4}px`;
    dom.cursor.style.height = `${node.render.height + 8}px`;
    dom.cursorFlag.textContent = `F${state.frame}`;
    ensureCursorVisible(nodes, live);
  }

  function renderGameCursor(nodes, live) {
    const frame = currentPlaybackFrame();
    const node = playbackNodeAtFrame(nodes, frame);
    if (!node?.render || !live) {
      dom.gameCursor.hidden = true;
      return;
    }
    dom.gameCursor.hidden = false;
    dom.gameCursor.style.left = `${nodeFrameX(node, frame)}px`;
    dom.gameCursor.style.top = `${node.render.y - 4}px`;
    dom.gameCursor.style.height = `${node.render.height + 8}px`;
    dom.gameCursorFlag.textContent = `实际 F${frame}`;
  }

  function renderCursorsNow() {
    const nodes = timelineNodes();
    const live = activeTimeline();
    if (!live) return;
    for (const node of nodes) {
      node.render = state.nodeRender.get(node.id) || null;
    }
    renderCursor(nodes, live);
    renderGameCursor(nodes, live);
  }

  function filteredEvents(nodes, node) {
    const events = eventsForNode(nodes, node);
    return state.filter === "all"
      ? events
      : events.filter((event) => event.category === state.filter);
  }

  function renderFilters(nodes, node) {
    const events = eventsForNode(nodes, node);
    const counts = {
      all: events.length,
      operation: events.filter((event) => event.category === "operation").length,
      combat: events.filter((event) => event.category === "combat").length,
      world: events.filter((event) => event.category === "world").length,
    };
    for (const button of dom.filters.querySelectorAll("[data-filter]")) {
      button.classList.toggle("hot", button.dataset.filter === state.filter);
      const count = button.querySelector("b");
      if (count) count.textContent = String(counts[button.dataset.filter] || 0);
    }
  }

  function ensureSelection(nodes, node) {
    const events = filteredEvents(nodes, node);
    if (!events.length) {
      state.selectedEventId = "";
      return;
    }
    if (!events.some((event) => event.id === state.selectedEventId)) {
      const nearest = eventAtFrame(nodes, state.frame);
      state.selectedEventId = nearest && events.some((event) => event.id === nearest.id)
        ? nearest.id : events[0].id;
    }
  }

  function renderEventList(nodes, node) {
    const events = filteredEvents(nodes, node);
    dom.eventSummary.textContent = node
      ? `${CATEGORY[state.filter]?.label || "全部"} ${events.length} 条 · F${node.start} → F${node.end}`
      : "等待时间轴节点";
    const rows = events.map((event) => `
      <button class="tas-event-row${event.id === state.selectedEventId ? " is-selected" : ""}"
        type="button" data-event-id="${esc(event.id)}" style="--event-color:${eventColor(event)}">
        <span class="tas-event-frame">F${String(event.frame).padStart(4, "0")}</span>
        <span class="tas-event-swatch"></span>
        <span class="tas-event-name">${esc(eventTitle(event))}</span>
        <span class="tas-event-meta">${esc(eventMeta(event))}</span>
      </button>
    `).join("");
    setHtml(
      dom.eventList,
      rows || '<div class="empty-small">当前节点没有玩家操作或采集事件</div>',
    );
  }

  function renderDetail(nodes, node) {
    const event = eventById(nodes, state.selectedEventId);
    dom.detailFrameBadge.textContent = event ? `F${event.frame}` : "—";
    if (!event) {
      setHtml(dom.eventDetail, `<div class="tas-detail-empty">
        选择玩家操作、战斗事件或世界事件。
      </div>`);
      return;
    }
    const color = eventColor(event);
    const operation = event.category === "operation";
    const lines = [
      ["分类", CATEGORY[event.category]?.label || event.category],
      ["类型", TYPE_META[event.type]?.label || event.type],
      ["逻辑帧", String(event.frame)],
      ["战斗时间", formatTime(event.frame)],
    ];
    if (operation) {
      lines.push(["干员", unitName(event.unit)], ["干员 ID", event.unit]);
      if (event.type === "deploy") {
        lines.push(["方向", String(event.direction)]);
        lines.push(["坐标", `[${event.tile?.[0] ?? "?"}, ${event.tile?.[1] ?? "?"}]`]);
      }
    } else if (event.category === "combat") {
      if (event.actor) {
        lines.push(["来源单位", unitName(event.actor, event.actorUid)]);
      }
      lines.push(["目标单位", unitName(event.target, event.targetUid)]);
      lines.push(["类型", event.damageLabel]);
      if (event.type === "heal") {
        lines.push(["治疗量", formatNumber(event.amount)]);
      } else if (event.type === "regen") {
        lines.push(["回血量", formatNumber(event.amount)]);
      } else if (event.type === "taken") {
        lines.push(["承伤值", formatNumber(event.amount)]);
      } else {
        lines.push(["伤害值", formatNumber(event.amount)]);
        lines.push(["目标承伤", formatNumber(event.amount)]);
      }
    } else {
      if (event.type === "cost") {
        lines.push(["回复量", formatNumber(event.value)]);
        lines.push(["当前费用", formatNumber(event.total)]);
        lines.push(["来源", "BattleController.GetCost"]);
      } else {
        lines.push(["波次", event.total > 0
          ? `${event.value} / ${event.total}`
          : String(event.value)]);
        lines.push(["来源", "Scheduler.get_spawnedWavesCnt"]);
      }
    }
    const playbackFrame = Number(state.snapshot?.frame) || 0;
    const replayMode = !!replayInfo();
    const canReplaySeek = replayMode && event.frame !== playbackFrame;
    const canJump = !!activeTimeline() && event.frame <= playbackFrame;
    const canSeek = canReplaySeek || canJump;
    const seekLabel = canReplaySeek
      ? (event.frame < playbackFrame ? "回退到此帧" : "快进到此帧")
      : "回退到此帧并分叉";
    setHtml(dom.eventDetail, `
      <div class="tas-detail-body">
        <div class="tas-detail-top">
          <span class="tas-detail-frame">
            <strong>F${event.frame}</strong>
            <button class="btn tiny primary" id="seekSelected" type="button" ${canSeek ? "" : "disabled"}>
              ${seekLabel}
            </button>
          </span>
          <span>${formatTime(event.frame)}</span>
        </div>
        <div class="tas-detail-category" style="--event-color:${color}">
          <i></i>${CATEGORY[event.category]?.label || event.category} / ${TYPE_META[event.type]?.label || event.type}
        </div>
        <h4 class="tas-detail-title">${esc(eventTitle(event))}</h4>
        <div class="tas-detail-lines">
          ${lines.map(([label, value]) => `<div class="tas-detail-line"><span>${esc(label)}</span><strong>${esc(value)}</strong></div>`).join("")}
        </div>
      </div>`);
  }

  function renderHeader(nodes, node, selection = node) {
    const live = activeTimeline();
    const snapshot = state.snapshot;
    const displayNode = state.followLive
      ? (playbackNodeAtFrame(nodes, state.frame) || node)
      : node;
    const advancing = !!live?.advancing;
    const idle = isIdleSnapshot(snapshot);
    const hasTimeline = !!live && Array.isArray(live.nodes) && live.nodes.length > 0;
    dom.saveTimeline.disabled = !hasTimeline;
    dom.saveTimeline.title = hasTimeline ? "保存完整时间树" : "当前没有可保存的时间轴";
    dom.loadTimeline.disabled = false;
    const rewinding = !!state.rewinding || !!live?.jumping;
    document.getElementById("page-timeline")?.classList.toggle(
      "is-rewinding", rewinding);
    dom.rewindView.hidden = !rewinding;
    dom.timelinePauseBadge.textContent = rewinding
      ? "世界线回溯中..."
      : (idle
        ? "未战斗"
        : (advancing
          ? `快进至 F${Number(live?.advance_target) || 0}`
          : (window.BetterBattleSession?.phaseLabel(snapshot) || "战斗中")));
    const headerPhase = window.BetterBattleSession?.phase(snapshot);
    dom.timelinePauseBadge.classList.toggle(
      "amber",
      headerPhase === "paused" || headerPhase === "ended" ||
        headerPhase === "starting",
    );
    const paused = idle ? null : (typeof snapshot?.paused === "boolean"
      ? snapshot.paused
      : null);
    setTimelinePauseButtonState(paused);
    const stageId = idle && !live
      ? ""
      : String(live?.stage || snapshot?.stage_input || snapshot?.stage || "").trim();
    const stageName = STAGE_NAME_BY_ID.get(stageId) ||
      String(live?.stage_name || snapshot?.stage_name || "").trim();
    dom.timelineStageBadge.textContent = stageName || stageId || "无关卡";
    dom.timelineStageBadge.title =
      stageId && stageName && stageId !== stageName ? stageId : "";
    const warning = String(snapshot?.result || "").trim();
    const eventWarning = warning.startsWith("Warning") ? warning : "";
    dom.timelineState.classList.toggle(
      "is-worldline", rewinding || !!eventWarning);
    dom.timelineState.textContent = rewinding
      ? "世界线回溯中..."
      : (eventWarning || (displayNode
        ? (advancing
            ? `快进中 · 目标 F${Number(live?.advance_target) || 0} · ${nodeLabel(displayNode)}`
            : `第 ${state.frame} 帧 · ${formatTime(state.frame)} · ${nodeLabel(displayNode)}`)
        : "等待战斗录制"));
    dom.activeSegmentBadge.textContent = displayNode
      ? nodeLabel(displayNode) : "无活动节点";
    dom.frameInput.max = String(Math.max(0, totalFrames(nodes)));
    if (document.activeElement !== dom.frameInput) {
      dom.frameInput.value = String(state.frame);
    }
    dom.jumpModeNormal.classList.toggle("is-active", state.jumpMode === "normal");
    dom.jumpModeFast.classList.toggle("is-active", state.jumpMode === "fast");
    dom.jumpModeNormal.setAttribute(
      "aria-pressed", state.jumpMode === "normal" ? "true" : "false");
    dom.jumpModeFast.setAttribute(
      "aria-pressed", state.jumpMode === "fast" ? "true" : "false");
    const following = state.followLive && state.autoPan;
    dom.followPlayback.disabled = !live || following;
    dom.followPlayback.textContent = state.followLive && !state.autoPan
      ? "恢复跟随"
      : "跟随";
    dom.followPlayback.title = following
      ? "已经在跟随当前位置"
      : "回到当前播放位置并持续跟随";
    dom.frameReadout.textContent = formatFrame(state.frame);
    dom.totalReadout.textContent = formatFrame(totalFrames(nodes));
    dom.deleteSegment.disabled = !live || !selection || !selection.parentId;
    dom.renameSegment.disabled = !live || !selection;
    const snapshotFrame = Number(snapshot?.frame) || 0;
    const replayMode = !!replayInfo();
    const targetFrame = actionTargetFrame();
    const targetBefore = targetFrame < snapshotFrame;
    dom.splitBranch.textContent = replayMode
      ? (targetBefore ? "回退到指针" : "快进到指针")
      : "回退到指针";
    dom.splitBranch.disabled = !live || !!live?.jumping || rewinding;
    dom.splitBranch.title = dom.splitBranch.disabled
      ? (replayMode
          ? "需要活动的时间轴节点"
          : "需要活动的时间轴节点")
      : (replayMode
          ? (targetBefore
              ? `重新开局并回退到 F${targetFrame}`
              : `继续播放当前录像到 F${targetFrame}`)
          : `重新开局并回退到 F${targetFrame}，不执行原录像操作`);
    dom.rewindPlay.disabled = !live || !!live?.jumping || rewinding ||
      snapshotFrame <= 0 || !targetBefore;
    dom.rewindPlay.title = dom.rewindPlay.disabled
      ? "需要活动时间轴，且目标帧早于当前帧"
      : `重新开局并回退到 F${targetFrame}，随后继续播放原录像`;
    dom.frameGo.textContent = replayMode
      ? (targetBefore ? "回退" : "快进")
      : "定位";
    dom.frameGo.title = replayMode
      ? (targetBefore
          ? "重新开局并回退到输入帧"
          : "继续播放当前录像到输入帧")
      : "只移动指针";
    const atSegmentEnd = !!live && !!node &&
      node.id === live.current_id && snapshotFrame >= (Number(node.end) || 0);
    const ffDisabled = !live || !snapshot?.paused ||
      !!live?.jumping || advancing || !!snapshot?.replay?.active ||
      !atSegmentEnd;
    for (const button of document.querySelectorAll("[data-ff]")) {
      button.disabled = ffDisabled;
      button.title = ffDisabled
        ? "需要暂停并位于当前时间段末尾"
        : `快进 ${button.dataset.ff} 帧`;
    }
  }

  function render(force = false) {
    const nodes = timelineNodes();
    const live = activeTimeline();
    const laneNodeId = laneNodeIdFor(nodes);
    const signature = [
      state.selectedNodeId, state.branchTargetNodeId,
      state.selectedEventId, state.filter, state.viewMode,
      state.timeline?.revision, state.timeline?.current_id,
      state.timeline?.jumping, state.snapshot?.paused,
      state.rewinding,
      state.snapshot?.result,
      state.jumpMode, state.autoPan, laneNodeId,
      state.timeline?.damage_hits?.length || 0,
      state.unitNames.size, nodes.map((node) => `${node.id}:${node.end}`).join(","),
    ].join("|");
    if (!force && signature === lastRenderSignature) return;
    lastRenderSignature = signature;
    if (live?.current_id && !nodes.some((node) => node.id === state.selectedNodeId)) {
      state.selectedNodeId = live.current_id;
    }
    if (!state.selectedNodeId && nodes.length) {
      state.selectedNodeId = nodes[0].id;
    }
    const selection = selectedNode(nodes);
    const node = displayedNode(nodes);
    const maxFrame = totalFrames(nodes);
    state.frame = clamp(
      Number(state.frame) || 0,
      0,
      Math.max(0, maxFrame),
    );
    ensureSelection(nodes, node);
    renderTree(nodes);
    renderFilters(nodes, node);
    renderEventList(nodes, node);
    renderDetail(nodes, node);
    renderHeader(nodes, node, selection);
    dom.treeHint.textContent = state.viewMode === "overview"
      ? "总览模式 · 点击节点返回聚焦"
      : "点击节点选帧；双击重命名";
    lastFullRenderAt = performance.now();
  }

  function updateLiveView() {
    if (!isTabActive()) return;
    const nodes = timelineNodes();
    if (!nodes.length) return;
    for (const node of nodes) {
      node.render = state.nodeRender.get(node.id) || null;
    }
    if (state.followLive) {
      state.frame = Number(state.snapshot?.frame) || 0;
    }
    const maxFrame = totalFrames(nodes);
    state.frame = clamp(
      Number(state.frame) || 0,
      0,
      Math.max(0, maxFrame),
    );
    const selection = selectedNode(nodes);
    const node = displayedNode(nodes);
    renderCursor(nodes, activeTimeline());
    renderHeader(nodes, node, selection);
  }

  function scheduleFullRender(force = false) {
    updateLiveView();
    if (!isTabActive()) return;
    if (pointerHeld && !force) {
      renderAfterPointer = true;
      return;
    }
    if (force) {
      if (pendingFullRender) {
        clearTimeout(pendingFullRender);
        pendingFullRender = 0;
      }
      requestAnimationFrame(() => {
        if (isTabActive()) render(true);
      });
      return;
    }
    if (pendingFullRender) return;
    const delay = Math.max(
      0,
      FULL_RENDER_INTERVAL_MS - (performance.now() - lastFullRenderAt),
    );
    pendingFullRender = setTimeout(() => {
      pendingFullRender = 0;
      requestAnimationFrame(() => {
        if (isTabActive()) render();
      });
    }, delay);
  }

  function requestJump(frame, nodeId = "", autoPlay = false) {
    const live = activeTimeline();
    const snapshotFrame = Number(state.snapshot?.frame) || 0;
    if (!live) {
      toast("当前没有实时时间轴节点；请先进入战斗", "error");
      return;
    }
    if (live.jumping) {
      toast("已有回退正在进行");
      return;
    }
    if (snapshotFrame <= 0) {
      toast("还没有可回退的逻辑帧", "error");
      return;
    }
    const nodes = timelineNodes();
    if (nodeId && !nodes.some((node) => node.id === nodeId)) {
      nodeId = "";
    }
    const target = clamp(Math.round(Number(frame) || 0), 0, snapshotFrame);
    if (!nodeId) {
      nodeId = actionNodeId(nodes, target);
    }
    const fast = state.jumpMode === "fast";
    state.followLive = true;
    state.autoPan = true;
    if (target < snapshotFrame) {
      state.rewinding = true;
      state.rewindEpoch = Number(state.snapshot?.epoch) || 0;
      state.rewindStartedAt = performance.now();
    }
    sendCommand(protocol.commands.timelineJump(target, nodeId, fast, autoPlay));
    toast(`已请求${fast ? "快速(beta) " : ""}${autoPlay ? "回退并播放" : "回退到指针"} F${target}`);
  }

  function requestReplaySeek(frame, nodeId = "") {
    const replay = replayInfo();
    if (!replay) {
      toast("当前没有已装载的录像", "error");
      return;
    }
    const current = currentPlaybackFrame();
    const nodes = timelineNodes();
    const maxFrame = Math.max(current, totalFrames(nodes));
    const target = clamp(Math.round(Number(frame) || 0), 0, maxFrame);
    if (target === current && !nodeId) {
      toast("指针已经在当前帧");
      return;
    }
    if (nodeId && !nodes.some((node) => node.id === nodeId)) {
      nodeId = "";
    }
    if (!nodeId) {
      nodeId = actionNodeId(nodes, target);
    }
    if (target < current) {
      requestJump(target, nodeId, false);
      return;
    }
    const switchesBranch = !!nodeId &&
      !isPlaybackPathDescendant(nodes, nodeId) &&
      !isNodeOnTimelinePath(nodes, nodeId);
    const fast = state.jumpMode === "fast";
    state.followLive = true;
    state.autoPan = true;
    sendCommand(protocol.commands.replaySeek(target, nodeId, fast));
    const action = switchesBranch
      ? "切换到指针（重新开局）"
      : (nodeId ? "快进（按选中节点）" : "快进");
    toast(`已请求${action}${fast ? "（快速）" : ""}到 F${target}`);
  }

  function quotePath(value) {
    return protocol.quotePath(value);
  }

  function joinPath(directory, name) {
    const base = String(directory || "").trim().replace(/[\\/]+$/, "");
    const file = String(name || "").trim().replace(/^[\\/]+/, "");
    return `${base}\\${file}`;
  }

  function ensureJsonName(name) {
    const value = String(name || "").trim() || "timeline.json";
    return /\.json$/i.test(value) ? value : `${value}.json`;
  }

  function safeFilePart(value, fallback = "stage") {
    const text = String(value || "").trim().replace(/[\\/:*?"<>|]+/g, "_")
      .replace(/\s+/g, "_").replace(/^_+|_+$/g, "");
    return text || fallback;
  }

  function defaultRecordingName() {
    const live = activeTimeline();
    const stage = live?.stage_code || live?.stage_name || live?.stage || "stage";
    const now = new Date();
    const pad = (value) => String(value).padStart(2, "0");
    const stamp = `${now.getFullYear()}${pad(now.getMonth() + 1)}${pad(now.getDate())}-` +
      `${pad(now.getHours())}${pad(now.getMinutes())}${pad(now.getSeconds())}`;
    return `timeline_${safeFilePart(stage)}_${stamp}.json`;
  }

  function requestRecordingFiles(directory = state.recordings.directory) {
    post({
      type: protocol.Message.TimelineFiles,
      directory: String(directory || ""),
    });
  }

  function openSaveTimeline() {
    if (!activeTimeline()) {
      toast("当前没有可保存的时间轴", "error");
      return;
    }
    dom.saveDirectory.value = state.recordings.directory || "";
    dom.saveName.value = defaultRecordingName();
    dom.saveEvents.checked = true;
    dom.saveTimelineModal.hidden = false;
    setTimeout(() => {
      dom.saveName.focus();
      dom.saveName.select();
    }, 0);
  }

  function confirmSaveTimeline() {
    const directory = dom.saveDirectory.value.trim();
    const name = ensureJsonName(dom.saveName.value);
    if (!directory) {
      toast("请选择保存目录", "error");
      return;
    }
    const path = joinPath(directory, name);
    state.forceResult = true;
    sendCommand(protocol.commands.replaySave(path, dom.saveEvents.checked));
    dom.saveTimelineModal.hidden = true;
    toast(`正在保存到 ${path}`);
    setTimeout(() => requestRecordingFiles(directory), 900);
  }

  function filteredRecordings() {
    const query = dom.loadFilter.value.trim().toLowerCase();
    if (!query) return state.recordings.files;
    return state.recordings.files.filter((file) =>
      `${file.name || ""} ${file.stageName || ""} ${file.stageCode || ""} ` +
      `${file.stageId || ""} ${file.seed ?? ""}`.toLowerCase().includes(query));
  }

  function renderLoadTimelineMeta() {
    const file = state.recordings.files.find(
      (entry) => String(entry.path) === dom.loadTimelineFile.value);
    if (!file) {
      dom.loadTimelineMeta.textContent = state.recordings.files.length
        ? "没有匹配的录像"
        : "目录中没有录像";
      dom.confirmLoadTimeline.disabled = true;
      return;
    }
    const stage = file.stageName || file.stageCode || file.stageId || "未知关卡";
    const kind = file.rogue ? "肉鸽" : "普通";
    dom.loadTimelineMeta.textContent =
      `${stage} · ${kind} · F${Number(file.operations) || 0} 操作 · ` +
      `${Number(file.nodes) || 0} 节点 · seed ${file.seed ?? "--"}`;
    dom.confirmLoadTimeline.disabled = false;
  }

  function renderRecordingOptions() {
    const files = filteredRecordings();
    const previous = dom.loadTimelineFile.value;
    dom.loadTimelineFile.replaceChildren(...files.map((file) => {
      const option = document.createElement("option");
      option.value = String(file.path || "");
      option.textContent = String(file.name || file.path || "");
      option.title = [
        file.stageName || file.stageCode || file.stageId || "",
        `${Number(file.nodes) || 0} 节点`,
        file.rogue ? "肉鸽" : "",
      ].filter(Boolean).join(" · ");
      return option;
    }));
    if (files.some((file) => String(file.path) === previous)) {
      dom.loadTimelineFile.value = previous;
    } else if (files.length) {
      dom.loadTimelineFile.value = String(files[0].path || "");
    }
    renderLoadTimelineMeta();
  }

  function openLoadTimeline() {
    dom.loadDirectory.value = state.recordings.directory || "";
    dom.loadFilter.value = "";
    renderRecordingOptions();
    dom.loadTimelineModal.hidden = false;
    requestRecordingFiles(dom.loadDirectory.value);
    setTimeout(() => dom.loadFilter.focus(), 0);
  }

  function confirmLoadTimeline() {
    const path = dom.loadTimelineFile.value;
    if (!path) {
      toast("请选择录像", "error");
      return;
    }
    state.followLive = true;
    state.selectedNodeId = "";
    state.branchTargetNodeId = "";
    state.selectedEventId = "";
    state.frame = 0;
    sendCommand(protocol.commands.replayStart(path));
    dom.loadTimelineModal.hidden = true;
    toast("正在加载录像");
  }

  function receiveRecordingFiles(message) {
    state.recordings.directory = String(message.directory || "").trim();
    state.recordings.requestPath = String(message.requestPath || "").trim();
    state.recordings.files = Array.isArray(message.files) ? message.files : [];
    if (!dom.saveDirectory.value && state.recordings.directory) {
      dom.saveDirectory.value = state.recordings.directory;
    }
    if (!dom.loadTimelineModal.hidden) {
      dom.loadDirectory.value = state.recordings.directory;
      renderRecordingOptions();
    }
  }

  function handlePickedPath(message) {
    const path = String(message.path || "").trim();
    if (!path) return;
    if (message.target === "save") {
      dom.saveDirectory.value = path;
    } else if (message.target === "load") {
      dom.loadDirectory.value = path;
      requestRecordingFiles(path);
    }
  }

  function seekPointer(frame, nodeId = "") {
    const nodes = timelineNodes();
    const liveFrame = Number(state.snapshot?.frame) || 0;
    const maxFrame = Math.max(liveFrame, totalFrames(nodes));
    const target = clamp(Math.round(Number(frame) || 0), 0, maxFrame);
    state.frame = target;
    state.followLive = liveFrame > 0 && target === liveFrame;
    if (nodeId) {
      state.selectedNodeId = nodeId;
    } else {
      const containing = nodes.find(
        (node) => target >= node.start && target <= node.end);
      if (containing) state.selectedNodeId = containing.id;
    }
    render(true);
  }

  function collectSubtree(nodes, id) {
    const result = [];
    const visit = (nodeId) => {
      result.push(nodeId);
      for (const child of childNodes(nodes, nodeId)) visit(child.id);
    };
    visit(id);
    return result;
  }

  function openRename() {
    const nodes = timelineNodes();
    const node = selectedNode(nodes);
    if (!activeTimeline() || !node) return;
    dom.segmentNameInput.value = node.name || "";
    dom.segmentNameInput.placeholder = `TL${node.branchId}`;
    dom.renameModal.hidden = false;
    setTimeout(() => {
      dom.segmentNameInput.focus();
      dom.segmentNameInput.select();
    }, 0);
  }

  function commitRename() {
    const nodes = timelineNodes();
    const node = selectedNode(nodes);
    if (!node) return;
    sendCommand(protocol.commands.timelineRename(
      node.id, dom.segmentNameInput.value.trim()));
    dom.renameModal.hidden = true;
  }

  function openDelete() {
    const nodes = timelineNodes();
    const node = selectedNode(nodes);
    if (!activeTimeline() || !node?.parentId) return;
    dom.deleteSegmentName.textContent = nodeLabel(node);
    dom.deleteSegmentRange.textContent = `F${formatFrame(node.start)} → F${formatFrame(node.end)}`;
    dom.deleteDescendantCount.textContent = String(collectSubtree(nodes, node.id).length);
    dom.deleteModal.hidden = false;
  }

  function commitDelete() {
    const nodes = timelineNodes();
    const node = selectedNode(nodes);
    if (!node || node.id === activeTimeline()?.current_id) {
      toast("不能删除当前活动分支", "error");
      dom.deleteModal.hidden = true;
      return;
    }
    sendCommand(protocol.commands.timelineDelete(node.id));
    dom.deleteModal.hidden = true;
  }

  function selectNode(nodeId, frame) {
    const nodes = timelineNodes();
    const node = nodes.find((entry) => entry.id === nodeId);
    if (!node) return;
    state.followLive = false;
    state.autoPan = true;
    state.selectedNodeId = node.id;
    state.branchTargetNodeId =
      (isPlaybackPathDescendant(nodes, node.id) ||
       isNodeOnTimelinePath(nodes, node.id))
        ? node.id
        : "";
    switchPlaybackPath(nodes, state.branchTargetNodeId);
    state.viewMode = "detail";
    state.frame = clamp(
      Math.round(Number(frame) || state.frame),
      node.start,
      Math.max(node.start, node.end),
    );
    const event = eventAtFrame(nodes, state.frame);
    if (event && event.frame >= node.start && event.frame <= node.end) {
      state.selectedEventId = event.id;
    }
    render(true);
    requestAnimationFrame(() => centerNode(node.id, state.frame));
  }

  function selectEvent(eventData) {
    if (!eventData) return;
    const nodes = timelineNodes();
    state.followLive = false;
    state.autoPan = true;
    state.selectedEventId = eventData.id;
    state.branchTargetNodeId = "";
    state.frame = eventData.frame;
    const owner = nodes.find((node) => node.id === eventData.nodeId)
      || nodes.find((node) => nodeHasEvent(node, eventData));
    if (owner) {
      state.selectedNodeId = owner.id;
    }
    render(true);
  }

  function centerNode(nodeId, frame) {
    const nodes = timelineNodes();
    const node = nodes.find((entry) => entry.id === nodeId);
    if (!node?.render || state.viewMode === "overview") return;
    const viewport = dom.treeScroll.getBoundingClientRect();
    const x = nodeFrameX(node, Number(frame) || node.start);
    const maxLeft = Math.max(0, dom.treeScroll.scrollWidth - viewport.width);
    const maxTop = Math.max(0, dom.treeScroll.scrollHeight - viewport.height);
    dom.treeScroll.scrollLeft = clamp(x - viewport.width / 2, 0, maxLeft);
    dom.treeScroll.scrollTop = clamp(node.render.centerY - viewport.height / 2, 0, maxTop);
  }

  function ensureCursorVisible(nodes, live) {
    if (!state.autoPan || pointerHeld) return;
    if (!live || state.viewMode === "overview") return;
    const frame = Number(state.frame) || 0;
    const nodeId = state.followLive
      ? (playbackNodeAtFrame(nodes, frame)?.id || timelineCurrentNodeId(nodes))
      : (state.selectedNodeId || timelineCurrentNodeId(nodes));
    const node = nodes.find((entry) => entry.id === nodeId);
    if (!node?.render) return;
    const view = dom.treeScroll;
    const viewportWidth = view.clientWidth;
    const viewportHeight = view.clientHeight;
    if (viewportWidth < 2 || viewportHeight < 2) return;

    const x = nodeFrameX(node, frame);
    const y = node.render.y;
    const height = node.render.height;
    const marginX = Math.min(120, viewportWidth * 0.16);
    const marginY = Math.min(48, viewportHeight * 0.18);
    let left = view.scrollLeft;
    let top = view.scrollTop;

    if (x < left + marginX) left = x - marginX;
    else if (x > left + viewportWidth - marginX) {
      left = x - viewportWidth + marginX;
    }
    if (y < top + marginY) top = y - marginY;
    else if (y + height > top + viewportHeight - marginY) {
      top = y + height - viewportHeight + marginY;
    }

    const maxLeft = Math.max(0, view.scrollWidth - viewportWidth);
    const maxTop = Math.max(0, view.scrollHeight - viewportHeight);
    left = clamp(left, 0, maxLeft);
    top = clamp(top, 0, maxTop);
    if (Math.abs(left - view.scrollLeft) >= 1) view.scrollLeft = left;
    if (Math.abs(top - view.scrollTop) >= 1) view.scrollTop = top;
  }

  function onTabActivated() {
    state.timelineRequested = true;
    state.followLive = true;
    state.autoPan = true;
    const idle = isIdleSnapshot(state.snapshot);
    if (idle) clearTimelineState();
    const nodes = timelineNodes();
    const currentId = timelineCurrentNodeId(nodes);
    if (currentId && nodes.some((node) => node.id === currentId)) {
      state.selectedNodeId = currentId;
    }
    state.frame = idle ? 0 : (Number(state.snapshot?.frame) || totalFrames(nodes));
    sendCommand(protocol.commands.timelineStatus());
    requestRecordingFiles();
    render(true);
    setTimeout(() => {
      const node = selectedNode(timelineNodes());
      if (node) centerNode(node.id, state.frame);
    }, 0);
  }

  function handleSnapshotMessage(payload, previous) {
    const previousFrame = Number(previous?.frame);
    const nextFrame = Number(payload?.frame);
    if (Number.isFinite(nextFrame) &&
        (!Number.isFinite(previousFrame) || nextFrame > previousFrame)) {
      state.lastFrameAdvanceAt = performance.now();
    }
    setTimelinePauseButtonState(
      isIdleSnapshot(payload)
        ? null
        : (typeof payload.paused === "boolean" ? payload.paused : null),
    );
    if (state.rewinding) {
      const epoch = Number(payload.epoch) || 0;
      const battleState = Number(payload.state);
      const timedOut = state.rewindStartedAt > 0 &&
        performance.now() - state.rewindStartedAt > 120000;
      if (timedOut ||
          (epoch !== state.rewindEpoch && battleState >= 2)) {
        state.rewinding = false;
      }
    }
    const result = String(payload.result || "");
    const resultRevision = session?.resultRevision || 0;
    if (result &&
        (resultRevision !== state.resultRevisionSeen || state.forceResult)) {
      state.resultRevisionSeen = resultRevision;
      state.forceResult = false;
      if (isTabActive() && result.startsWith("录像已保存")) {
        toast("保存成功", "ok");
      } else if (isTabActive() && result.startsWith("保存录像失败")) {
        toast(result, "error");
      } else if (isTabActive() && result.startsWith("Warning")) {
        toast(result, "error");
      }
    }
    if (isIdleSnapshot(payload)) {
      clearTimelineState();
      if (isTabActive()) render(true);
      return;
    }
    if (state.followLive) {
      state.frame = Number(payload.frame) || 0;
    }
    if (isTabActive()) renderCursorsNow();
    if (!state.timelineRequested && !!payload.record?.timeline?.active) {
      state.timelineRequested = true;
      sendCommand(protocol.commands.timelineStatus());
    }
    if (isTabActive()) scheduleFullRender();
  }

  function handleTimelineMessage(payload, previous) {
    if (state.snapshot && isIdleSnapshot(state.snapshot)) {
      clearTimelineState();
      if (isTabActive()) render(true);
      return;
    }
    const previousNodeIds = new Set(
      (Array.isArray(previous?.nodes) ? previous.nodes : [])
        .map((node) => String(node?.id || "")),
    );
    state.timelineRequested = true;
    if (payload?.active === false) {
      clearTimelineState();
      if (isTabActive()) render(true);
      return;
    }
    const currentId = String(payload?.current_id || "");
    const newNode = currentId && !previousNodeIds.has(currentId);
    if (newNode) {
      state.selectedNodeId = currentId;
      state.followLive = true;
      state.autoPan = true;
      state.frame = Number(state.snapshot?.frame) || state.frame;
    } else if (state.followLive && payload?.current_id) {
      state.selectedNodeId = payload.current_id;
    } else if (!state.selectedNodeId && payload?.current_id) {
      state.selectedNodeId = payload.current_id;
    }
    if (isTabActive()) {
      scheduleFullRender();
      if (newNode) {
        requestAnimationFrame(() => {
          if (isTabActive()) centerNode(currentId, state.frame);
        });
      }
    }
  }

  function receive(event) {
    let message = event.data;
    if (typeof message === "string") {
      try {
        message = JSON.parse(message);
      } catch {
        return;
      }
    }
    if (!message || typeof message !== "object") return;
    if (message.type === protocol.Message.TimelineFiles) {
      receiveRecordingFiles(message);
      return;
    }
    if (message.type === protocol.Message.PathPicked) {
      handlePickedPath(message);
      return;
    }
    if (message.type === protocol.Message.Command &&
        (message.state === "failed" || message.state === "rejected" || message.state === "waiting")) {
      if (isTabActive()) {
        toast(message.message || `命令 ${message.state}: ${message.line || ""}`, "error");
      }
    }
  }

  function bindTreePan() {
    const view = dom.treeScroll;
    if (!view) return;
    let pan = null;
    let suppressClick = false;
    const interactive = (target) =>
      target instanceof Element && !!target.closest(
        ".tas-node, .tas-node-event, [data-child-id], " +
        "button, input, select, textarea, label, a",
      );

    view.addEventListener("pointerdown", (event) => {
      if (event.pointerType === "mouse" &&
          event.button !== 0 && event.button !== 1) {
        return;
      }
      if (event.button === 0 && interactive(event.target)) return;
      if (event.button === 1) event.preventDefault();
      pan = {
        id: event.pointerId,
        x: event.clientX,
        y: event.clientY,
        left: view.scrollLeft,
        top: view.scrollTop,
        moved: false,
      };
      suppressClick = false;
      try {
        view.setPointerCapture(event.pointerId);
      } catch {
      }
    });

    view.addEventListener("pointermove", (event) => {
      if (!pan || event.pointerId !== pan.id) return;
      const dx = event.clientX - pan.x;
      const dy = event.clientY - pan.y;
      if (!pan.moved && Math.hypot(dx, dy) < 4) return;
      pan.moved = true;
      event.preventDefault();
      view.classList.add("is-panning");
      view.scrollLeft = pan.left - dx;
      view.scrollTop = pan.top - dy;
    });

    const finish = (event) => {
      if (!pan || (event && event.pointerId !== pan.id)) return;
      const moved = pan.moved;
      const pointerId = pan.id;
      view.classList.remove("is-panning");
      try {
        if (view.hasPointerCapture(pointerId)) {
          view.releasePointerCapture(pointerId);
        }
      } catch {
      }
      pan = null;
      if (moved) {
        suppressClick = true;
        state.autoPan = false;
        scheduleFullRender();
        setTimeout(() => {
          suppressClick = false;
        }, 350);
      }
    };
    view.addEventListener("pointerup", finish);
    view.addEventListener("pointercancel", finish);
    view.addEventListener("click", (event) => {
      if (!suppressClick) return;
      suppressClick = false;
      event.preventDefault();
      event.stopPropagation();
    }, true);
    view.addEventListener("wheel", (event) => {
      if (!event.deltaX && !event.deltaY) return;
      state.autoPan = false;
      scheduleFullRender();
    }, { passive: true });
  }

  function bind() {
    bindTreePan();
    dom.saveTimeline.addEventListener("click", openSaveTimeline);
    dom.loadTimeline.addEventListener("click", openLoadTimeline);
    dom.saveBrowse.addEventListener("click", () => post({
      type: protocol.Message.PickFolder,
      target: "save",
      path: dom.saveDirectory.value,
    }));
    dom.loadBrowse.addEventListener("click", () => post({
      type: protocol.Message.PickFolder,
      target: "load",
      path: dom.loadDirectory.value,
    }));
    dom.confirmSaveTimeline.addEventListener("click", confirmSaveTimeline);
    dom.confirmLoadTimeline.addEventListener("click", confirmLoadTimeline);
    dom.loadFilter.addEventListener("input", renderRecordingOptions);
    dom.loadTimelineFile.addEventListener("change", renderLoadTimelineMeta);
    dom.jumpModeNormal.addEventListener("click", () => {
      state.jumpMode = "normal";
      render(true);
    });
    dom.jumpModeFast.addEventListener("click", () => {
      state.jumpMode = "fast";
      render(true);
    });
    dom.playButton.addEventListener("click", () => {
      sendCommand(protocol.commands.timelineStatus());
    });
    dom.followPlayback.addEventListener("click", () => {
      const nodes = timelineNodes();
      const frame = currentPlaybackFrame();
      const node = playbackNodeAtFrame(nodes, frame);
      state.followLive = true;
      state.autoPan = true;
      state.frame = frame;
      state.selectedNodeId = node?.id || timelineCurrentNodeId(nodes);
      state.branchTargetNodeId = "";
      render(true);
      if (node) requestAnimationFrame(() => centerNode(node.id, frame));
    });
    document.querySelectorAll("[data-ff]").forEach((button) => {
      button.addEventListener("click", () => {
        const frames = Math.max(1, Number(button.dataset.ff) || 0);
        sendCommand(protocol.commands.timelineAdvance(frames));
      });
    });
    dom.jumpStart.addEventListener("click", () => {
      const node = selectedNode(timelineNodes());
      seekPointer(node ? node.start : 0, node?.id);
    });
    stepButtons.forEach((button) => {
      button.addEventListener("click", () => {
        const delta = Number(button.dataset.step) || 0;
        seekPointer(
          state.frame + delta,
          selectedNode(timelineNodes())?.id,
        );
      });
    });
    dom.jumpEnd.addEventListener("click", () => {
      const node = selectedNode(timelineNodes());
      seekPointer(
        node ? node.end : totalFrames(timelineNodes()),
        node?.id,
      );
    });
    dom.splitBranch.addEventListener("click", () => {
      const target = actionTargetFrame();
      const nodeId = actionNodeId(timelineNodes(), target);
      if (replayInfo() && target >= currentPlaybackFrame()) {
        requestReplaySeek(target, nodeId);
        return;
      }
      requestJump(target, nodeId);
    });
    dom.rewindPlay.addEventListener("click", () => {
      const target = actionTargetFrame();
      const nodeId = actionNodeId(timelineNodes(), target);
      if (target >= currentPlaybackFrame()) {
        toast("回退并播放需要选择当前帧之前的帧", "error");
        return;
      }
      requestJump(target, nodeId, true);
    });
    dom.frameGo.addEventListener("click", () => {
      const target = actionTargetFrame();
      if (replayInfo() && target !== currentPlaybackFrame()) {
        requestReplaySeek(target, actionNodeId(timelineNodes(), target));
        return;
      }
      seekPointer(target, selectedNode(timelineNodes())?.id);
    });
    dom.frameInput.addEventListener("input", () => {
      if (!isTabActive()) return;
      const nodes = timelineNodes();
      renderHeader(nodes, displayedNode(nodes), selectedNode(nodes));
    });
    dom.frameInput.addEventListener("keydown", (event) => {
      if (event.key !== "Enter") return;
      event.preventDefault();
      dom.frameGo.click();
    });
    dom.treeViewToggle.addEventListener("click", () => {
      state.viewMode = state.viewMode === "overview" ? "detail" : "overview";
      render();
      if (state.viewMode === "overview") {
        dom.treeScroll.scrollLeft = 0;
        dom.treeScroll.scrollTop = 0;
      } else {
        const node = selectedNode(timelineNodes());
        if (node) setTimeout(() => centerNode(node.id, state.frame), 0);
      }
    });
    const selectNodeAtPointer = (nodeButton, clientX) => {
      const nodes = timelineNodes();
      const node = nodes.find((entry) => entry.id === nodeButton.dataset.nodeId);
      if (!node) return;
      const rect = nodeButton.getBoundingClientRect();
      const laneWidth = Math.max(1, rect.width - NODE_EVENT_INSET * 2);
      const ratio = clamp(
        (clientX - rect.left - NODE_EVENT_INSET) / laneWidth,
        0,
        1,
      );
      selectNode(node.id, node.start + ratio * Math.max(0, node.end - node.start));
    };
    dom.tree.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      const marker = event.target.closest(".tas-node-event");
      const branchLink = event.target.closest("[data-child-id]");
      if (branchLink) {
        const nodes = timelineNodes();
        const child = nodes.find(
          (node) => node.id === branchLink.dataset.childId,
        );
        if (child && isPlaybackPathDescendant(nodes, child.id)) {
          state.branchTargetNodeId = child.id;
          switchPlaybackPath(nodes, child.id);
          render(true);
        }
        return;
      }
      if (marker) {
        selectEvent(eventById(timelineNodes(), marker.dataset.eventId));
        return;
      }
      const nodeButton = event.target.closest("[data-node-id]");
      if (nodeButton) selectNodeAtPointer(nodeButton, event.clientX);
    });
    dom.tree.addEventListener("click", (event) => {
      const marker = event.target.closest(".tas-node-event");
      const nodeButton = event.target.closest("[data-node-id]");
      if (marker) {
        if (event.detail === 0) {
          selectEvent(eventById(timelineNodes(), marker.dataset.eventId));
        }
        return;
      }
      if (nodeButton) {
        if (event.detail === 0) selectNodeAtPointer(nodeButton, event.clientX);
        return;
      }
    });
    dom.tree.addEventListener("dblclick", (event) => {
      const nodeButton = event.target.closest("[data-node-id]");
      if (!nodeButton) return;
      state.selectedNodeId = nodeButton.dataset.nodeId;
      openRename();
    });
    const selectEventRow = (row) => {
      if (!row) return;
      selectEvent(eventById(timelineNodes(), row.dataset.eventId));
    };
    dom.eventList.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      const row = event.target.closest("[data-event-id]");
      if (!row) return;
      event.preventDefault();
      selectEventRow(row);
    });
    dom.eventList.addEventListener("click", (event) => {
      if (event.detail > 0) return;
      const row = event.target.closest("[data-event-id]");
      selectEventRow(row);
    });
    dom.eventDetail.addEventListener("pointerdown", (event) => {
      if (event.button !== 0) return;
      const button = event.target.closest("#seekSelected");
      if (!button || button.disabled) return;
      event.preventDefault();
      const eventData = eventById(timelineNodes(), state.selectedEventId);
      if (!eventData) return;
      const nodeId = eventData.nodeId ||
        actionNodeId(timelineNodes(), eventData.frame);
      if (replayInfo()) {
        requestReplaySeek(eventData.frame, nodeId);
      } else {
        requestJump(eventData.frame, nodeId);
      }
    });
    dom.filters.addEventListener("click", (event) => {
      const button = event.target.closest("[data-filter]");
      if (!button) return;
      state.filter = button.dataset.filter;
      render();
    });
    dom.renameSegment.addEventListener("click", openRename);
    dom.deleteSegment.addEventListener("click", openDelete);
    dom.confirmRename.addEventListener("click", commitRename);
    dom.confirmDelete.addEventListener("click", commitDelete);
    dom.segmentNameInput.addEventListener("keydown", (event) => {
      if (event.key !== "Enter") return;
      event.preventDefault();
      commitRename();
    });
    document.querySelectorAll("[data-modal-close]").forEach((button) => {
      button.addEventListener("click", () => {
        dom.renameModal.hidden = true;
        dom.deleteModal.hidden = true;
      });
    });
    document.querySelectorAll("[data-timeline-modal-close]").forEach((button) => {
      button.addEventListener("click", () => {
        dom.saveTimelineModal.hidden = true;
        dom.loadTimelineModal.hidden = true;
      });
    });
    window.addEventListener("keydown", (event) => {
      if (event.key !== "Escape") return;
      dom.renameModal.hidden = true;
      dom.deleteModal.hidden = true;
      dom.saveTimelineModal.hidden = true;
      dom.loadTimelineModal.hidden = true;
    });
    window.addEventListener("pointerdown", () => {
      pointerHeld = true;
    }, true);
    window.addEventListener("pointerup", () => {
      pointerHeld = false;
      if (renderAfterPointer) {
        renderAfterPointer = false;
        scheduleFullRender();
      }
    }, true);
    window.addEventListener("pointercancel", () => {
      pointerHeld = false;
      if (renderAfterPointer) {
        renderAfterPointer = false;
        scheduleFullRender();
      }
    }, true);
    window.addEventListener("resize", () => {
      if (isTabActive()) scheduleFullRender(true);
    });
    session?.onSnapshot(handleSnapshotMessage);
    session?.onTimeline(handleTimelineMessage);
    if (window.chrome?.webview) {
      window.chrome.webview.addEventListener("message", receive);
    }
    requestRecordingFiles();
  }

  window.BetterBattleTimeline = {
    onTabActivated,
    render: () => scheduleFullRender(),
  };

  bind();
  render();
  if (isTabActive()) onTabActivated();
})();
