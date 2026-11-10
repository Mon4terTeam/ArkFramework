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
    buffHtml,
    collectAbnormalChips,
    abnormalChipsHtml,
  } = battle;
  const { enemyProfileFor, enemyStatRows } = enemyPage;
  const listState = {
    mode: "operators",
    active: false,
    hostRequested: false,
    hostState: "detached",
    hostMessage: "未吸附",
    layoutQueued: false,
    initialized: false,
    leaveTimer: 0,
  };

  let nodes = null;
  let listComponent = null;
  let detailComponent = null;
  let timelineDock = null;

  function ensureNodes() {
    if (nodes) return nodes;
    const ids = [
      "gameviewListTabs", "gameviewListCount", "gameviewRailList",
      "gameviewGameHole", "gameviewGamePlaceholder", "gameviewHostState",
      "gameviewHostHint", "gameviewAttachButton", "gameviewDetachButton",
      "gameviewDetailKind", "gameviewDetailName", "gameviewDetailMeta",
      "gameviewDetailBadge", "gameviewDetailBody", "gameviewTimelineHost",
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

  function postLayout(type = "gameview-layout") {
    if (!active() || !listState.hostRequested) return;
    refreshGameHoleSize();
    const hole = ensureNodes().gameviewGameHole;
    if (!hole) return;
    const rect = hole.getBoundingClientRect();
    post({
      type,
      rect: {
        x: rect.left,
        y: rect.top,
        w: rect.width,
        h: rect.height,
      },
      dpr: dpr(),
    });
  }

  function scheduleLayout(type = "gameview-layout") {
    if (!listState.hostRequested || listState.layoutQueued) return;
    listState.layoutQueued = true;
    requestAnimationFrame(() => {
      listState.layoutQueued = false;
      postLayout(type);
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
          `<button type="button" class="enemy-row${selected ? " is-selected" : ""}" ` +
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
    if (view.gameviewListCount) {
      view.gameviewListCount.textContent = String(
        operatorMode
          ? cardsAndUnits().operatorCards.length
          : aliveEnemies().length,
      );
    }
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
    const buffs = Array.isArray(enemy.buffs) ? enemy.buffs : [];
    const abnormal = collectAbnormalChips(buffs);
    return `<div class="gameview-detail-section">` +
      `<div class="section-label">当前属性</div>` +
      `<div class="enemy-stat-grid">${stats.map(([label, value]) =>
        `<div class="enemy-stat"><span>${esc(label)}</span><strong>${esc(value)}</strong></div>`).join("")}</div>` +
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

  function renderHostState() {
    const view = ensureNodes();
    const attached = listState.hostState === "attached";
    const requested = listState.hostRequested;
    view.gameviewGameHole?.classList.toggle("is-attached", attached);
    if (view.gameviewHostState) {
      view.gameviewHostState.textContent = attached
        ? "已吸附"
        : requested ? "等待游戏窗口" : "未吸附";
      view.gameviewHostState.className = "badge " +
        (attached ? "green" : requested ? "amber" : "");
    }
    if (view.gameviewHostHint) {
      view.gameviewHostHint.textContent = attached
        ? "游戏窗口已吸附；点击“解除吸附”恢复独立窗口"
        : requested
          ? "正在查找游戏窗口，找到后自动排版"
          : "游戏窗口保持独立，点击“吸附游戏”后进入布局";
    }
    if (view.gameviewAttachButton) {
      view.gameviewAttachButton.disabled = requested;
      view.gameviewAttachButton.textContent = attached ? "已吸附" : "吸附游戏";
    }
    if (view.gameviewDetachButton) {
      view.gameviewDetachButton.disabled = !requested;
    }
  }

  function render() {
    if (!active()) return;
    renderRail();
    renderDetail();
    renderHostState();
  }

  function attachGameWindow() {
    if (!active() || listState.hostRequested) return;
    listState.hostRequested = true;
    listState.hostState = "waiting";
    listState.hostMessage = "等待游戏窗口";
    renderHostState();
    refreshGameHoleSize();
    postLayout("gameview-enter");
  }

  function detachGameWindow() {
    if (!listState.hostRequested) return;
    listState.hostRequested = false;
    listState.hostState = "detached";
    listState.hostMessage = "未吸附";
    post({ type: "gameview-detach" });
    renderHostState();
  }

  function enter() {
    if (listState.leaveTimer) {
      clearTimeout(listState.leaveTimer);
      listState.leaveTimer = 0;
    }
    if (listState.active) return;
    const wasRequested = listState.hostRequested;
    listState.active = true;
    if (!wasRequested) {
      listState.hostState = "detached";
      listState.hostMessage = "未吸附";
    }
    timelineDock?.mount();
    refreshGameHoleSize();
    render();
    window.BetterBattleTimeline?.onTabActivated?.();
    if (wasRequested) postLayout("gameview-enter");
  }

  function leave() {
    if (!listState.active) return;
    if (listState.leaveTimer) {
      clearTimeout(listState.leaveTimer);
      listState.leaveTimer = 0;
    }
    const hostRequested = listState.hostRequested;
    timelineDock?.unmount();
    const snap = state.snapshot;
    const battleActive = window.BetterBattleSession?.shouldPauseOnLeave(snap) || false;
    const finish = () => {
      listState.leaveTimer = 0;
      if (hostRequested) {
        post({ type: "gameview-leave" });
      }
      listState.hostRequested = false;
      listState.hostState = "detached";
      listState.hostMessage = "未吸附";
    };
    if (battleActive && !snap.paused) {
      sendCommand(protocol.commands.pause());
      if (hostRequested) {
        listState.leaveTimer = window.setTimeout(finish, 650);
      } else {
        finish();
      }
    } else {
      finish();
    }
    listState.active = false;
  }

  function onListSelect(item) {
    if (!item) return;
    const session = window.BetterBattleSession;
    if (item.kind === "summon") {
      session?.selectSummon(item.row?.unit?.uid);
    } else if (item.kind === "card") {
      session?.selectCard(item.row?.card?.uid);
    } else if (item.kind === "enemy") {
      session?.selectEnemy(item.enemy?.uid);
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
    if (!listState.hostRequested && message.state !== "attached") {
      listState.hostState = "detached";
      listState.hostMessage = "未吸附";
      renderHostState();
      return;
    }
    listState.hostState = String(message.state || "waiting");
    listState.hostMessage = String(message.message || "等待游戏窗口");
    if (listState.hostState === "attached") {
      listState.hostRequested = true;
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
    view.gameviewAttachButton?.addEventListener("click", attachGameWindow);
    view.gameviewDetachButton?.addEventListener("click", detachGameWindow);
    window.addEventListener("resize", () => {
      if (active() && listState.hostRequested) scheduleLayout();
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
    active,
    postLayout: () => scheduleLayout(),
    attach: attachGameWindow,
    detach: detachGameWindow,
  };
})();
