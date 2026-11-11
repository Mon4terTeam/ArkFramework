"use strict";

(() => {
  const protocol = window.BetterBattleProtocol;
  const PANEL_ID = "page-gameview";
  const components = window.BetterBattleComponents || {};
  const battle = window.BetterBattlePages?.battle || {};
  const enemyPage = window.BetterBattlePages?.enemies || {};
  const {
    battleEntities,
    unitForCard,
    profileForCard,
    staticArtFor,
    buildUnitRailRows,
    operatorDetailView,
    globalBuffsView,
    buffHtml,
    collectAbnormalChips,
    abnormalChipsHtml,
  } = battle;
  const {
    profileFor: enemyProfileFor,
    statRows: enemyStatRows,
  } = enemyPage;
  const listState = {
    mode: "operators",
    detailMode: "detail",
    tabActive: false,
    attach: false,
    hostState: "unavailable",
    hostMessage: "",
    hostOwnerOk: false,
    hostVisible: false,
    requestId: 0,
    pending: "",
    layoutQueued: false,
    initialized: false,
  };

  let nodes = null;
  let listComponent = null;
  let detailComponent = null;
  let timelineDock = null;

  function ensureNodes() {
    if (nodes) return nodes;
    const ids = [
      "gameviewListTabs", "gameviewRailList",
      "gameviewGameHole", "gameviewGamePlaceholder", "gameviewHostState",
      "gameviewHostHint", "gameviewAttachButton", "gameviewDetachButton",
      "gameviewStageLabel",
      "gameviewDetailTabs", "gameviewDetailHeader",
      "gameviewDetailKind", "gameviewDetailName", "gameviewDetailMeta",
      "gameviewDetailBadge", "gameviewDetailBody",
      "gameviewGlobalHeader", "gameviewGlobalBody",
      "gameviewGlobalCount", "gameviewGlobalMeta",
      "gameviewTimelineHost",
    ];
    nodes = {};
    for (const id of ids) nodes[id] = document.getElementById(id);
    return nodes;
  }

  function active() {
    return document.getElementById(PANEL_ID)?.classList.contains("active");
  }

  function esc(value) {
    return String(value ?? "").replace(/[&<>"']/g, (ch) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      '"': "&quot;",
      "'": "&#39;",
    }[ch]));
  }

  function post(message) {
    window.chrome?.webview?.postMessage(message);
  }

  function dpr() {
    return Math.max(1, Number(window.devicePixelRatio) || 1);
  }

  function refreshGameHoleSize() {
    const hole = ensureNodes().gameviewGameHole;
    if (!hole) return;
    const scale = dpr();
    document.getElementById(PANEL_ID)?.style.setProperty(
      "--gameview-game-height", `${900 / scale}px`);
    hole.style.width = `${1600 / scale}px`;
    hole.style.height = `${900 / scale}px`;
  }

  function layoutRect() {
    refreshGameHoleSize();
    const hole = ensureNodes().gameviewGameHole;
    if (!hole) return null;
    const rect = hole.getBoundingClientRect();
    return {
      x: rect.left,
      y: rect.top,
      w: rect.width,
      h: rect.height,
    };
  }

  function nextRequest(kind) {
    listState.requestId += 1;
    listState.pending = kind;
    return listState.requestId;
  }

  function postAttach() {
    if (!listState.tabActive || !listState.attach) return;
    const rect = layoutRect();
    if (!rect) return;
    const requestId = nextRequest("attach");
    post({
      type: "gameview-attach",
      requestId,
      rect,
      dpr: dpr(),
    });
    if (rect.w <= 1 || rect.h <= 1) {
      requestAnimationFrame(() => postLayout());
    }
    renderHostState();
  }

  function postHide() {
    const requestId = nextRequest("hide");
    post({ type: "gameview-hide", requestId });
  }

  function postDetach() {
    const requestId = nextRequest("detach");
    post({ type: "gameview-detach", requestId });
  }

  function canLayout() {
    return listState.tabActive &&
      listState.attach &&
      (listState.hostState === "attached" ||
       listState.hostState === "attaching" ||
       listState.pending === "attach");
  }

  function postLayout() {
    if (!canLayout()) return;
    if (!listState.requestId) return;
    const rect = layoutRect();
    if (!rect) return;
    post({
      type: "gameview-layout",
      requestId: listState.requestId,
      rect,
      dpr: dpr(),
    });
  }

  function scheduleLayout() {
    if (!canLayout() || listState.layoutQueued) return;
    listState.layoutQueued = true;
    requestAnimationFrame(() => {
      listState.layoutQueued = false;
      postLayout();
    });
  }

  function snapshot() {
    return state.snapshot || null;
  }

  function enemyProfileMap(snapshotValue = snapshot()) {
    const session = window.BetterBattleSession;
    if (session) return session.enemyProfiles(snapshotValue);
    return new Map(
      (Array.isArray(snapshotValue?.enemy_profiles)
        ? snapshotValue.enemy_profiles
        : []).map((profile) => [String(profile?.id || ""), profile]),
    );
  }

  function aliveEnemies(snapshotValue = snapshot()) {
    return window.BetterBattleSession?.aliveEnemies(snapshotValue) || [];
  }

  function cardsAndUnits() {
    return battleEntities(snapshot());
  }

  function selectedOperatorKey() {
    if (state.selectedSummonUid !== null) {
      return `summon:${state.selectedSummonUid}`;
    }
    if (state.selectedCardUid !== null) {
      return `card:${state.selectedCardUid}`;
    }
    return "";
  }

  function selectedOperator() {
    const { cards, units } = cardsAndUnits();
    const summon = units.find((unit) =>
      Number(unit.uid) === Number(state.selectedSummonUid)) || null;
    if (summon) {
      return {
        kind: "summon",
        card: cards.find((card) => Number(card.uid) === Number(summon.card_uid)) || null,
        unit: summon,
        name: summon.name || summon.id || "召唤物",
      };
    }
    const card = cards.find((item) =>
      Number(item.uid) === Number(state.selectedCardUid)) || null;
    if (!card) return null;
    return {
      kind: "card",
      card,
      unit: unitForCard(card, units),
      name: card.name || profileForCard(card, null)?.name || card.id || "未知干员",
    };
  }

  function selectedEnemy() {
    const enemies = aliveEnemies();
    return enemies.find((enemy) =>
      Number(enemy.uid) === Number(state.selectedEnemyUid)) || null;
  }

  function chooseDefaultOperator() {
    const { operatorCards, units } = cardsAndUnits();
    if (!operatorCards.length) {
      state.selectedCardUid = null;
      state.selectedSummonUid = null;
      return;
    }
    const selectedCardValid = operatorCards.some((card) =>
      Number(card.uid) === Number(state.selectedCardUid));
    const selectedSummonValid = units.some((unit) =>
      Number(unit.uid) === Number(state.selectedSummonUid));
    if (selectedCardValid || selectedSummonValid) return;
    const deployed = operatorCards.find((card) => unitForCard(card, units));
    state.selectedCardUid =
      Number((deployed || operatorCards[0])?.uid ?? 0) || null;
    state.selectedSummonUid = null;
  }

  function chooseDefaultEnemy() {
    const enemies = aliveEnemies();
    if (!enemies.length) {
      state.selectedEnemyUid = null;
      return;
    }
    if (!enemies.some((enemy) =>
      Number(enemy.uid) === Number(state.selectedEnemyUid))) {
      state.selectedEnemyUid = Number(enemies[0].uid) || null;
    }
  }

  function operatorItems() {
    const { cards, operatorCards, units, summonNames } = cardsAndUnits();
    const rows = buildUnitRailRows(operatorCards, cards, units, summonNames);
    return rows.map((row) => {
      if (row.kind === "group") {
        return {
          key: `group:${row.key}`,
          kind: "group",
          render: () => `<div class="unit-rail-group">${esc(row.label)}</div>`,
        };
      }
      const profile = profileForCard(row.card, row.unit);
      const art = staticArtFor(row.card?.id || row.unit?.id);
      const avatarId = art?.avatarId || "";
      const name = row.kind === "summon"
        ? row.label
        : (row.card?.name || profile?.name || row.card?.id || "未知干员");
      return {
        key: row.key,
        kind: row.kind,
        row,
        render: (selected) => {
          const avatar = avatarId
            ? `<img class="unit-avatar" data-asset-kind="avatar" data-asset-id="${esc(avatarId)}" alt="">`
            : `<span class="unit-avatar avatar-fallback">${esc(Array.from(name)[0] || "?")}</span>`;
          return `<button type="button" class="unit-tab${row.kind === "summon" ? " is-summon" : ""}${selected ? " active" : ""}" ` +
            `data-ui-key="${esc(row.key)}">` +
            `${avatar}<span class="unit-tab-body"><span class="unit-tab-name"><strong>${esc(name)}</strong></span></span>` +
            `</button>`;
        },
      };
    });
  }

  function enemyItems() {
    const snap = snapshot() || {};
    const enemies = aliveEnemies(snap);
    const profiles = enemyProfileMap(snap);
    const focusUid = Number(snap?.enemy_focus?.uid) || 0;
    return enemies.map((enemy) => {
      const uid = Number(enemy.uid);
      const profile = enemyProfileFor(profiles, enemy.id);
      const name = (profile?.name && profile.name !== enemy.id ? profile.name : "") ||
        "未知敌人";
      const label = window.BetterBattleEnemyDisplay?.label(
        enemy.id,
        profiles,
        enemies,
        enemy.serial,
      ) || name;
      return {
        key: `enemy:${uid}`,
        kind: "enemy",
        enemy,
        render: (selected) =>
          `<button type="button" class="enemy-row${selected ? " is-selected" : ""}` +
          `${uid === focusUid ? " is-focused" : ""}" ` +
          `data-ui-key="enemy:${uid}">` +
          `<span class="enemy-row-avatar"><img data-asset-kind="enemy-icon" data-asset-id="${esc(enemy.id)}" alt="" loading="lazy"></span>` +
          `<span class="enemy-row-main"><span class="enemy-row-name"><span class="enemy-row-name-text">${esc(label)}</span></span></span>` +
          `</button>`,
      };
    });
  }

  function renderRail() {
    const view = ensureNodes();
    const operatorMode = listState.mode !== "enemies";
    if (operatorMode) chooseDefaultOperator();
    else chooseDefaultEnemy();
    const items = operatorMode ? operatorItems() : enemyItems();
    const selected = operatorMode
      ? selectedOperatorKey()
      : `enemy:${state.selectedEnemyUid}`;
    listComponent?.render(items, selected);
    view.gameviewListTabs?.querySelectorAll("button[data-gameview-list]").forEach((button) => {
      const isSelected = button.dataset.gameviewList === listState.mode;
      button.classList.toggle("is-active", isSelected);
      button.setAttribute("aria-selected", isSelected ? "true" : "false");
    });
    window.BetterBattleAssets?.observe(view.gameviewRailList);
  }

  function operatorDetailHtml(selected) {
    if (!selected) {
      return `<div class="empty-small">选择左侧干员查看数值和 Buff</div>`;
    }
    const detail = operatorDetailView(selected.card, selected.unit, selected.name);
    const buffs = detail.buffs;
    const abnormal = collectAbnormalChips(detail.unitBuffs);
    return `<div class="gameview-detail-section">` +
      `<div class="section-label">当前属性</div>` +
      `<div class="stats-grid">${detail.statsHtml}</div>` +
      `<div class="stats-abnormal"><div class="stats-abnormal-head">` +
      `<span>异常状态</span><span>${abnormal.length}</span></div>` +
      `<div class="stats-abnormal-chips">${abnormalChipsHtml(abnormal) || `<span class="stats-abnormal-none">无异常</span>`}</div></div>` +
      `</div><div class="gameview-detail-section">` +
      `<div class="section-label">Buff <span class="badge">${buffs.length}</span></div>` +
      `<div class="buff-list">${buffs.length
        ? buffs.map((buff) => buffHtml(buff, true)).join("")
        : `<div class="empty-small">该单位当前没有 Buff</div>`}</div>` +
      `</div>`;
  }

  function enemyDetailHtml(enemy) {
    if (!enemy) {
      return `<div class="empty-small">选择左侧敌人查看属性</div>`;
    }
    const snap = snapshot() || {};
    const profiles = enemyProfileMap(snap);
    const stats = enemyStatRows(enemy);
    const panelOpen = enemyPage.panelOpenState?.() === true;
    const panelRows = new Map(
      (Array.isArray(enemy.panel) ? enemy.panel : [])
        .map((row) => [row.attribute, row]),
    );
    const buffs = Array.isArray(enemy.buffs) ? enemy.buffs : [];
    const abnormal = collectAbnormalChips(buffs);
    return `<div class="gameview-detail-section">` +
      `<div class="enemy-stat-head"><div class="section-label">当前属性</div>` +
      `<label class="enemy-panel-toggle"><input type="checkbox" data-enemy-panel-toggle${panelOpen ? " checked" : ""}><span>显示计算过程</span></label></div>` +
      `<div class="enemy-stat-grid">${stats.map(([label, value, attribute]) => {
        const calc = panelOpen && attribute
          ? enemyPage.panelCalcHtml?.(panelRows.get(attribute)) || ""
          : "";
        return `<div class="enemy-stat${calc ? " has-calc" : ""}"><span>${esc(label)}</span>` +
          `<strong>${esc(value)}</strong>${calc}</div>`;
      }).join("")}</div>` +
      `<div class="stats-abnormal"><div class="stats-abnormal-head">` +
      `<span>异常状态</span><span>${abnormal.length}</span></div>` +
      `<div class="stats-abnormal-chips">${abnormalChipsHtml(abnormal) || `<span class="stats-abnormal-none">无异常</span>`}</div></div>` +
      `</div><div class="gameview-detail-section">` +
      `<div class="section-label">Buff <span class="badge">${buffs.length}</span></div>` +
      `<div class="buff-list">${buffs.length
        ? buffs.map((buff) => buffHtml(buff, true)).join("")
        : `<div class="empty-small">当前没有 Buff</div>`}</div>` +
      `</div>`;
  }

  function renderDetail() {
    const operatorMode = listState.mode !== "enemies";
    const selected = operatorMode ? selectedOperator() : selectedEnemy();
    const name = operatorMode
      ? (selected?.name || "未选择干员")
      : (selected ? (enemyProfileFor(
        enemyProfileMap(), selected.id)?.name || "未知敌人") : "未选择敌人");
    const meta = operatorMode
      ? (selected ? (selected.unit
        ? `HP ${fmt(Number(selected.unit.hp) || 0, 0)} / ${fmt(Number(selected.unit.max_hp) || 0, 0)}`
        : "未部署")
        : "")
      : (selected ? `出现于 ${formatDurationValue(Number(selected.created_time) || 0)}` : "");
    const badge = operatorMode
      ? (selected?.unit?.skill_affecting ? "技能中" : selected?.unit ? "在场" : "未部署")
      : (selected ? `#${selected.serial ?? "?"}` : "--");
    detailComponent?.render({
      kind: operatorMode ? "干员详情" : "敌方详情",
      name,
      meta,
      badge,
      html: operatorMode ? operatorDetailHtml(selected) : enemyDetailHtml(selected),
    });
  }

  function refreshDetail() {
    renderDetail();
  }

  function renderGlobal() {
    const view = ensureNodes();
    if (!view.gameviewGlobalBody || typeof globalBuffsView !== "function") return;
    const global = globalBuffsView(snapshot());
    if (!global) return;
    const html = `<div class="buff-list global-buff-list">${global.html}</div>`;
    if (view.gameviewGlobalBody.__tasHtml !== html) {
      view.gameviewGlobalBody.__tasHtml = html;
      view.gameviewGlobalBody.innerHTML = html;
      window.BetterBattleAssets?.observe(view.gameviewGlobalBody);
    }
    if (view.gameviewGlobalCount) {
      view.gameviewGlobalCount.textContent = String(global.total);
    }
    if (view.gameviewGlobalMeta) {
      const parts = [];
      if (global.groups.length) parts.push(`${global.groups.length} 组`);
      if (global.relicTotal) parts.push(`${global.relicTotal} 藏品`);
      view.gameviewGlobalMeta.textContent =
        parts.join(" · ") || "当前没有全局效果";
    }
  }

  function renderGlobalStats() {
    const view = ensureNodes();
    const snap = snapshot() || {};
    const groups = Array.isArray(snap.global_buffs) ? snap.global_buffs : [];
    const total = groups.reduce((sum, group) =>
      sum + (Array.isArray(group?.buffs) ? group.buffs.length : 0), 0);
    const relicTotal = Array.isArray(snap.rogue?.relics)
      ? snap.rogue.relics.length
      : 0;
    if (view.gameviewGlobalCount) {
      view.gameviewGlobalCount.textContent = String(total);
    }
    if (view.gameviewGlobalMeta) {
      const parts = [];
      if (groups.length) parts.push(`${groups.length} 组`);
      if (relicTotal) parts.push(`${relicTotal} 藏品`);
      view.gameviewGlobalMeta.textContent =
        parts.join(" · ") || "当前没有全局效果";
    }
  }

  function renderDetailTabs() {
    const view = ensureNodes();
    const globalMode = listState.detailMode === "global";
    view.gameviewDetailTabs?.querySelectorAll(
      "button[data-gameview-detail]").forEach((button) => {
      const selected = button.dataset.gameviewDetail === listState.detailMode;
      button.classList.toggle("is-active", selected);
      button.setAttribute("aria-selected", selected ? "true" : "false");
    });
    if (view.gameviewDetailHeader) view.gameviewDetailHeader.hidden = globalMode;
    if (view.gameviewDetailBody) view.gameviewDetailBody.hidden = globalMode;
    if (view.gameviewGlobalHeader) view.gameviewGlobalHeader.hidden = !globalMode;
    if (view.gameviewGlobalBody) view.gameviewGlobalBody.hidden = !globalMode;
    renderGlobalStats();
    if (globalMode) renderGlobal();
  }

  function derivedView() {
    if (!listState.tabActive) return "inactive";
    if (!listState.attach) return "detached";
    if (listState.pending === "detach") return "detaching";
    if (listState.pending === "hide") return "hiding";
    if (listState.pending === "attach") {
      if (listState.hostState === "unavailable") return "waiting";
      if (listState.hostState === "error") return "error";
      if (listState.hostState === "attached") return "attached";
      return "attaching";
    }
    if (listState.hostState === "attached") return "attached";
    if (listState.hostState === "attaching") return "attaching";
    if (listState.hostState === "hidden") return "hidden";
    if (listState.hostState === "error") return "error";
    if (listState.hostState === "detached") return "detached";
    return "waiting";
  }

  function hostHint(viewState) {
    switch (viewState) {
    case "attached":
      return "游戏窗口已吸附；点击“解除吸附”恢复独立窗口";
    case "attaching":
      return "正在设置窗口 owner 并应用布局";
    case "waiting":
      return listState.hostMessage || "正在查找游戏窗口，找到后自动排版";
    case "detaching":
      return "正在恢复独立窗口";
    case "hiding":
      return "正在隐藏游戏窗口";
    case "hidden":
      return "游戏窗口已隐藏，返回本页会自动恢复";
    case "error":
      return listState.hostMessage || "吸附失败，可重试";
    default:
      return "游戏窗口保持独立，点击“吸附游戏”后进入布局";
    }
  }

  function renderHostState() {
    const view = ensureNodes();
    const viewState = derivedView();
    const attached = viewState === "attached";
    const labels = {
      attached: "已吸附",
      attaching: "吸附中",
      waiting: "等待游戏窗口",
      detaching: "解除中",
      hiding: "隐藏中",
      hidden: "已隐藏",
      error: "吸附失败",
      detached: "未吸附",
      inactive: "未吸附",
    };
    const tones = {
      attached: "green",
      attaching: "amber",
      waiting: "amber",
      detaching: "amber",
      hiding: "amber",
      hidden: "amber",
      error: "bad",
    };
    view.gameviewGameHole?.classList.toggle("is-attached", attached);
    if (view.gameviewHostState) {
      view.gameviewHostState.textContent = labels[viewState] || "未吸附";
      view.gameviewHostState.className =
        "badge " + (tones[viewState] || "");
    }
    if (view.gameviewHostHint) {
      view.gameviewHostHint.textContent = hostHint(viewState);
    }
    if (view.gameviewAttachButton) {
      const retry = listState.attach && listState.hostState === "error";
      view.gameviewAttachButton.disabled =
        !listState.tabActive || (listState.attach && !retry);
      view.gameviewAttachButton.textContent = listState.attach
        ? (attached ? "已吸附" : (retry ? "重试" : "吸附中"))
        : "吸附游戏";
    }
    if (view.gameviewDetachButton) {
      view.gameviewDetachButton.disabled = !listState.attach;
    }
  }

  function renderStageLabel() {
    const view = ensureNodes();
    if (!view.gameviewStageLabel) return;
    const snap = snapshot() || {};
    const timeline = window.BetterBattleSession?.timeline || {};
    const id = String(
      snap.stage_input || timeline.stage || snap.stage_id || snap.stage || "",
    ).trim();
    const name = String(timeline.stage_name || snap.stage_name || "").trim();
    const label = id && name && id !== name
      ? `${id} · ${name}`
      : (name || id);
    const text = label || "关卡 --";
    if (view.gameviewStageLabel.textContent !== text) {
      view.gameviewStageLabel.textContent = text;
      view.gameviewStageLabel.title = label;
    }
  }

  function render() {
    if (!active()) return;
    renderRail();
    renderDetail();
    renderDetailTabs();
    renderHostState();
    renderStageLabel();
  }

  function attachGameWindow() {
    if (!listState.tabActive) return;
    if (listState.attach && listState.hostState !== "error") return;
    listState.attach = true;
    postAttach();
  }

  function detachGameWindow() {
    if (!listState.attach &&
        !["attached", "attaching", "hidden"].includes(listState.hostState)) {
      return;
    }
    listState.attach = false;
    postDetach();
    renderHostState();
  }

  function enter() {
    if (listState.tabActive) return;
    listState.tabActive = true;
    timelineDock?.mount();
    refreshGameHoleSize();
    if (listState.attach) postAttach();
    render();
    window.BetterBattleTimeline?.onTabActivated?.();
  }

  function leave() {
    if (!listState.tabActive) return;
    listState.tabActive = false;
    timelineDock?.unmount();
    const snap = state.snapshot;
    const battleActive =
      window.BetterBattleSession?.shouldPauseOnLeave(snap) || false;
    if (battleActive) {
      sendCommand(protocol.commands.pause());
    }
    if (listState.attach ||
        listState.hostState === "attached" ||
        listState.hostState === "attaching") {
      postHide();
    }
    renderHostState();
  }

  function onListSelect(item) {
    if (!item) return;
    const session = window.BetterBattleSession;
    if (item.kind === "summon") {
      session?.selectSummon(item.row?.unit?.uid);
    } else if (item.kind === "card") {
      session?.selectCard(item.row?.card?.uid);
    } else if (item.kind === "enemy") {
      const select = window.BetterBattlePages?.enemies?.select;
      if (select) {
        select(item.enemy?.uid, { focusInGame: true });
      } else {
        session?.selectEnemy(item.enemy?.uid);
      }
    } else {
      return;
    }
    render();
  }

  function receive(event) {
    let message = event.data;
    if (typeof message === "string") {
      try { message = JSON.parse(message); } catch { return; }
    }
    if (!message || message.type !== "gameview-state") return;
    const incomingId = Number(message.requestId) || 0;
    if (incomingId > 0 &&
        incomingId < listState.requestId &&
        listState.pending) {
      return;
    }
    listState.hostState = String(message.state || "unavailable");
    listState.hostMessage = String(message.message || "");
    listState.hostOwnerOk = !!message.ownerOk;
    listState.hostVisible = !!message.visible;
    if (!listState.pending || incomingId >= listState.requestId) {
      listState.pending = "";
    }
    if (listState.hostState === "hidden" &&
        listState.tabActive && listState.attach) {
      postAttach();
      return;
    }
    renderHostState();
  }

  function bind() {
    const view = ensureNodes();
    view.gameviewListTabs?.addEventListener("click", (event) => {
      const button = event.target.closest("[data-gameview-list]");
      if (!button) return;
      listState.mode = button.dataset.gameviewList === "enemies" ? "enemies" : "operators";
      render();
    });
    view.gameviewDetailTabs?.addEventListener("click", (event) => {
      const button = event.target.closest("[data-gameview-detail]");
      if (!button) return;
      listState.detailMode =
        button.dataset.gameviewDetail === "global" ? "global" : "detail";
      render();
    });
    view.gameviewAttachButton?.addEventListener("click", attachGameWindow);
    view.gameviewDetachButton?.addEventListener("click", detachGameWindow);
    window.addEventListener("resize", () => {
      if (active() && listState.attach) scheduleLayout();
    });
    if (window.chrome?.webview) {
      window.chrome.webview.addEventListener("message", receive);
    }
  }

  function init() {
    if (listState.initialized) return;
    listState.initialized = true;
    const view = ensureNodes();
    listComponent = components.createSelectionList({
      mount: view.gameviewRailList,
      itemSelector: "[data-ui-key]",
      onSelect: onListSelect,
    });
    detailComponent = components.createDetailView({
      kindNode: view.gameviewDetailKind,
      nameNode: view.gameviewDetailName,
      metaNode: view.gameviewDetailMeta,
      badgeNode: view.gameviewDetailBadge,
      bodyNode: view.gameviewDetailBody,
    });
    timelineDock = components.createPanelDock({
      host: view.gameviewTimelineHost,
      panel: document.querySelector("#page-timeline .tas-timeline-panel"),
      className: "gameview-timeline-panel",
    });
    bind();
  }

  window.BetterBattleGameView = {
    init,
    enter,
    leave,
    render,
    refreshDetail,
    active,
    postLayout: () => scheduleLayout(),
    attach: attachGameWindow,
    detach: detachGameWindow,
  };
})();
