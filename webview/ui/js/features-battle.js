"use strict";

(() => {
  const pages = window.BetterBattlePages || (window.BetterBattlePages = {});
  const renderBattleDamagePanel = (uid) =>
    window.BetterBattlePages?.damage?.renderBattlePanel?.(uid);
  const { rosterEntry } = pages.squad || {};

  let battleEntitiesSnapshot;
  let battleEntitiesCache = null;

  function battleEntities(snapshot = state.snapshot) {
    if (snapshot === battleEntitiesSnapshot && battleEntitiesCache) {
      return battleEntitiesCache;
    }
    const snap = snapshot || {};
    const cards = (Array.isArray(snap.cards) ? snap.cards : [])
      .filter((card) => !!card);
    const operatorCards = cards.filter((card) => !isSummonCard(card));
    const units = (Array.isArray(snap.units) ? snap.units : [])
      .filter((unit) => mine(unit) && unit.alive);
    const summons = units.filter(isSummonUnit);
    battleEntitiesSnapshot = snapshot;
    battleEntitiesCache = {
      cards,
      operatorCards,
      units,
      summons,
      summonNames: summonDisplayNames(summons),
    };
    return battleEntitiesCache;
  }

  function renderBattle() {
    const snapshot = state.snapshot;
    const session = window.BetterBattleSession;
    const phase = session ? session.phase(snapshot) : "";
    if (!snapshot || phase === "idle") {
      setPauseButtonState(null);
      dom.stateBadge.textContent = snapshot ? "未战斗" : "等待";
      dom.stateBadge.className = "badge";
      dom.pauseBadge.textContent = "未暂停";
      dom.pauseBadge.className = "badge";
      dom.deckBadge.textContent = "待部署 --";
      dom.costBadge.textContent = "费用 --";
      dom.resultBox.textContent = "暂无操作";
      dom.resultBox.className = "result compact";
      dom.unitRail.innerHTML = `<div class="empty-small">进入战斗后显示编队卡牌</div>`;
      renderFocusEmpty();
      renderBattleDamagePanel();
      renderGlobalBuffs(null);
      return;
    }

    setPauseButtonState(
      phase === "playing" || phase === "paused" ? !!snapshot.paused : null);
    const {
      cards,
      operatorCards,
      units,
      summons,
      summonNames,
    } = battleEntities(snapshot);
    const selectedSummon = summons.find(
      (unit) => Number(unit.uid) === Number(state.selectedSummonUid)) || null;
    if (!selectedSummon) state.selectedSummonUid = null;
    dom.stateBadge.textContent = session
      ? session.phaseLabel(snapshot)
      : stateName(snapshot.state);
    dom.stateBadge.className = "badge " +
      (phase === "playing" || phase === "paused" || phase === "replay"
        ? "green"
        : (phase === "ended" || phase === "starting" ? "amber" : ""));
    dom.pauseBadge.textContent =
      phase === "paused" ? "已暂停"
        : (phase === "playing" ? "运行中"
          : (phase === "replay" ? "回放中"
            : (phase === "ended" ? "已结束"
              : (phase === "starting" ? "准备中" : "未暂停"))));
    dom.pauseBadge.className = "badge " +
      (phase === "playing" || phase === "replay"
        ? "green"
        : (phase === "paused" || phase === "ended" ||
           phase === "starting" ? "amber" : ""));
    dom.deckBadge.textContent =
      `待部署 ${operatorCards.filter((card) =>
        !units.some((unit) => Number(unit.card_uid) === Number(card.uid))).length}`;
    const cost = Number(snapshot.cost);
    dom.costBadge.textContent =
      Number.isFinite(cost) && cost >= 0 ? `费用 ${cost}` : "费用 --";
    dom.resultBox.textContent = displayResult(snapshot.result) || "暂无操作";
    dom.resultBox.className = "result compact " + (snapshot.ok ? "ok" : "error");

    if (!selectedSummon && (state.selectedCardUid === null ||
        !operatorCards.some((card) =>
          Number(card.uid) === Number(state.selectedCardUid)))) {
      const deployedCard = operatorCards.find((card) => unitForCard(card, units));
      const firstSquadId = String(snapshot.squad?.[0]?.id || "");
      const squadCard = firstSquadId
        ? operatorCards.find((card) => String(card.id) === firstSquadId)
        : null;
      state.selectedCardUid =
        Number((deployedCard || squadCard || operatorCards[0])?.uid ?? 0) || null;
    }
    renderUnitRail(operatorCards, cards, units, summonNames);
    const selectedCard = cards.find((card) => Number(card.uid) === Number(state.selectedCardUid));
    const selectedUnit = selectedCard ? unitForCard(selectedCard, units) : null;
    if (selectedSummon) {
      const summonCard = cards.find((card) =>
        Number(card.uid) === Number(selectedSummon.card_uid)) || null;
      renderBattleDamagePanel(selectedSummon.id);
      renderFocus(
        summonCard,
        selectedSummon,
        summonNames.get(Number(selectedSummon.uid)) || selectedSummon.name);
    } else if (selectedCard) {
      renderBattleDamagePanel(selectedUnit?.id || selectedCard.id || "");
      renderFocus(selectedCard, selectedUnit);
    } else {
      renderBattleDamagePanel("");
      renderFocusEmpty();
    }
    renderGlobalBuffs(snapshot);
  }

  function setPauseButtonState(paused) {
    if (!dom.pauseButton || !dom.resumeButton) return;
    const known = typeof paused === "boolean";
    dom.pauseButton.classList.toggle("primary", known && !paused);
    dom.resumeButton.classList.toggle("primary", known && paused);
  }

  function selectCard(uid) {
    if (!window.BetterBattleSession?.selectCard(uid)) return;
    renderBattle();
  }

  function selectSummon(uid) {
    if (!window.BetterBattleSession?.selectSummon(uid)) return;
    renderBattle();
  }

  function isSummonUnit(unit) {
    return String(unit?.id || "").startsWith("token_");
  }

  function isSummonCard(card) {
    return String(card?.id || "").startsWith("token_");
  }

  function summonDisplayNames(summons) {
    const groups = new Map();
    for (const unit of summons) {
      const name = unit.name || unit.id || "召唤物";
      if (!groups.has(name)) groups.set(name, []);
      groups.get(name).push(unit);
    }
    const labels = new Map();
    for (const [name, list] of groups) {
      list.sort((left, right) => Number(left.uid) - Number(right.uid));
      list.forEach((unit, index) => {
        labels.set(
          Number(unit.uid),
          list.length > 1 ? `${name} #${index + 1}` : name);
      });
    }
    return labels;
  }

  function unitForCard(card, units) {
    return units.find((unit) => Number(unit.card_uid) === Number(card.uid)) || null;
  }

  function summonCardForUnit(unit, cards) {
    return cards.find((card) => Number(card.uid) === Number(unit?.card_uid)) || null;
  }

  function profileForCard(card, unit = null) {
    if (card) {
      const byCard = state.profiles.get(String(card.uid));
      if (byCard) return byCard;
    }
    if (unit) {
      return state.profiles.get(String(unit.card_uid || unit.uid)) ||
        state.profiles.get(String(unit.uid)) || null;
    }
    return null;
  }

  function staticArtFor(id) {
    const operatorId = String(id || "");
    if (!operatorId) return null;
    const roster = rosterEntry(operatorId);
    const portraitId = window.BetterBattleAssets?.portraitIdFor(operatorId) || "";
    return {
      avatar: "",
      portrait: "",
      avatarId: roster ? operatorId : "",
      portraitId: roster ? portraitId : "",
      file: "",
      skill: "",
    };
  }

  function staticSkillIcon(id, skillIndex = 1) {
    const skills = rosterEntry(id)?.skills || [];
    if (!skills.length) return "";
    const index = Math.max(
      0,
      Math.min(skills.length - 1, Number(skillIndex || 1) - 1),
    );
    return skills[index]?.icon || "";
  }

  function deployState(card) {
    if (!card) return { label: "", tone: "" };
    if (card.deployed) return { label: "已部署", tone: "green" };
    if (card.ready) return { label: "可部署", tone: "green" };
    const cost = Number(card.cost);
    const current = Number(state.snapshot?.cost);
    if (Number.isFinite(cost) && Number.isFinite(current) && current < cost) {
      return { label: "费用不足", tone: "amber" };
    }
    return { label: "再部署中", tone: "amber" };
  }

  function cardRespawnRemaining(card) {
    const seconds = Number(card?.respawn_remaining);
    return Number.isFinite(seconds) && seconds > 0 ? seconds : 0;
  }

  function buildUnitRailRows(operatorCards, cards, units, summonLabels) {
    const summons = units.filter(isSummonUnit);
    const ownerCards = new Map();
    for (const card of operatorCards) {
      const unit = unitForCard(card, units);
      for (const ownerUid of [Number(card.uid), Number(unit?.uid)]) {
        if (ownerUid > 0 && !ownerCards.has(ownerUid)) {
          ownerCards.set(ownerUid, card);
        }
      }
    }
    const summonsByCard = new Map();
    const unknown = [];
    for (const summon of summons) {
      const ownerUid = Number(summon.owner_uid) || 0;
      const ownerCard = ownerUid > 0 ? ownerCards.get(ownerUid) : null;
      if (!ownerCard) {
        unknown.push(summon);
        continue;
      }
      if (!summonsByCard.has(ownerCard.uid)) {
        summonsByCard.set(ownerCard.uid, []);
      }
      summonsByCard.get(ownerCard.uid).push(summon);
    }

    const rows = [];
    for (const card of operatorCards) {
      rows.push({
        kind: "card",
        key: `card:${card.uid}`,
        card,
        unit: unitForCard(card, units),
      });
      for (const unit of summonsByCard.get(card.uid) || []) {
        rows.push({
          kind: "summon",
          key: `summon:${unit.uid}`,
          card: summonCardForUnit(unit, cards),
          unit,
          label: summonLabels.get(Number(unit.uid)) || unit.name || unit.id || "召唤物",
        });
      }
    }
    if (unknown.length) {
      rows.push({ kind: "group", key: "group:unknown", label: "召唤物 · 未知来源" });
      for (const unit of unknown) {
        rows.push({
          kind: "summon",
          key: `summon:${unit.uid}`,
          card: summonCardForUnit(unit, cards),
          unit,
          label: summonLabels.get(Number(unit.uid)) || unit.name || unit.id || "召唤物",
        });
      }
    }
    return rows;
  }

  function renderUnitRail(operatorCards, cards, units, summonLabels) {
    const rows = buildUnitRailRows(operatorCards, cards, units, summonLabels);
    if (!rows.length) {
      if (state.unitRailKey !== "empty") {
        dom.unitRail.innerHTML = `<div class="empty-small">暂无编队卡牌</div>`;
        state.unitRailKey = "empty";
      }
      return;
    }
    const structureKey = rows.map((row) => {
      if (row.kind === "group") return row.key;
      const art = staticArtFor(row.card?.id || row.unit?.id);
      return [
        row.key,
        row.card?.name || "",
        row.label || "",
        art?.avatarId || art?.portraitId || "",
      ].join(":");
    }).join("|");
    const existing = dom.unitRail.querySelectorAll(".unit-tab");
    const rowButtons = rows.filter((row) => row.kind !== "group");
    const domMatches = existing.length === rowButtons.length &&
      Array.from(existing).every((button, index) =>
        button.dataset.key === rowButtons[index].key);
    if (state.unitRailKey !== structureKey || !domMatches) {
      dom.unitRail.innerHTML = rows.map((row) => {
        if (row.kind === "group") {
          return `<div class="unit-rail-group">${esc(row.label)}</div>`;
        }
        const { card, unit } = row;
        const profile = profileForCard(card, unit);
        const art = staticArtFor(card?.id || unit?.id);
        const avatarId = art?.avatarId || "";
        const displayName = row.kind === "summon"
          ? row.label
          : (card?.name || profile?.name || card?.id || "未知干员");
        const avatarHtml = avatarId
          ? `<img class="unit-avatar" data-asset-kind="avatar" data-asset-id="${esc(avatarId)}" alt="">`
          : `<span class="unit-avatar avatar-fallback">${esc(Array.from(displayName)[0] || "?")}</span>`;
        const action = row.kind === "summon" ? "select-summon" : "select-card";
        const actionUid = row.kind === "summon" ? unit?.uid : card?.uid;
        return `<button class="unit-tab${row.kind === "summon" ? " is-summon" : ""}" ` +
          `data-action="${action}" data-kind="${row.kind}" ` +
          `data-key="${esc(row.key)}" data-uid="${esc(actionUid ?? 0)}">` +
          avatarHtml +
          `<div class="unit-tab-body">` +
          `<div class="unit-tab-name"><strong>${esc(displayName)}</strong>` +
          `<span></span></div>` +
          `<div class="unit-tab-bars">` +
          `<div class="bar hp"><i></i></div>` +
          `<div class="bar sp"><i></i></div>` +
          `</div><div class="unit-tab-meta"><span></span><span></span></div>` +
          `</div></button>`;
      }).join("");
      state.unitRailKey = structureKey;
      window.BetterBattleAssets?.observe(dom.unitRail);
    }
    const rowsByKey = new Map(rowButtons.map((row) => [row.key, row]));
    dom.unitRail.querySelectorAll(".unit-tab").forEach((button) => {
      const row = rowsByKey.get(button.dataset.key);
      if (!row) return;
      const { card, unit } = row;
      const profile = profileForCard(card, unit);
      const hp = Number(unit?.hp) || 0;
      const maxHp = Number(unit?.max_hp) || 0;
      const shield = Number(unit?.shield) || 0;
      const shieldLayers = Number(unit?.shield_layers) || 0;
      const minusHp = Number(unit?.minus_hp) || 0;
      const maxMinusHp = Number(unit?.max_minus_hp) || 0;
      // TODO: count barrier gain/damage and negative-HP damage as separate stats.
      const negativeHpLabel = maxMinusHp > 0.5
        ? `负血 ${fmt(minusHp, 0)}/${fmt(maxMinusHp, 0)}` : "";
      const sp = Number(unit?.sp) || 0;
      const maxSp = Number(unit?.max_sp) || Number(profile?.skill?.max_sp) || 0;
      const hpRatio = maxHp > 0 ? Math.max(0, Math.min(1, hp / maxHp)) : 0;
      const spRatio = maxSp > 0 ? Math.max(0, Math.min(1, sp / maxSp)) : 0;
      const active = row.kind === "summon"
        ? Number(unit?.uid) === Number(state.selectedSummonUid)
        : Number(card?.uid) === Number(state.selectedCardUid);
      button.classList.toggle("active", active);
      const nameValue = button.querySelector(".unit-tab-name span");
      const hpBar = button.querySelector(".bar.hp > i");
      const spBar = button.querySelector(".bar.sp > i");
      const meta = button.querySelectorAll(".unit-tab-meta span");
      if (nameValue) {
        nameValue.textContent = row.kind === "summon"
          ? (maxHp > 0 ? `${fmt(hp, 0)}/${fmt(maxHp, 0)}` : "召唤物")
          : (unit ? `${fmt(sp, 0)}/${maxSp}` : `${card?.cost ?? "--"} 费`);
      }
      if (hpBar) hpBar.style.width = `${(hpRatio * 100).toFixed(1)}%`;
      if (spBar) spBar.style.width = `${(spRatio * 100).toFixed(1)}%`;
      if (meta[0]) {
        meta[0].textContent = row.kind === "summon"
          ? `攻击 ${fmt(Number(unit?.atk) || 0, 1)}`
          : (unit
              ? `${fmt(hp, 0)} HP${shield > 0.5
                ? ` · 盾 ${fmt(shield, 0)}`
                : shieldLayers > 0 ? ` · 盾 ${shieldLayers}层` : ""}` +
                (negativeHpLabel ? ` · ${negativeHpLabel}` : "")
              : "未部署");
      }
      if (meta[1]) {
        if (row.kind === "summon") {
          meta[1].textContent = shield > 0.5
            ? `盾 ${fmt(shield, 0)}`
            : shieldLayers > 0 ? `盾 ${shieldLayers}层` : "存活";
        } else {
          const respawn = cardRespawnRemaining(card);
          meta[1].textContent = unit
            ? (unit.skill_affecting ? "技能中" : "待机")
            : (card?.ready
                ? "可部署"
                : respawn > 0
                  ? `再部署 ${fmtDuration(respawn)}`
                  : deployState(card).label);
        }
      }
    });
  }

  function renderFocusEmpty() {
    dom.artStage.classList.remove("has-art");
    dom.artCanvas.className = "";
    dom.artEmpty.style.display = "grid";
    dom.artEmpty.innerHTML =
      `<span class="art-empty-name">等待干员</span>` +
      `<span class="art-empty-note">选择左侧干员查看立绘与技能</span>`;
    dom.artError.textContent = "";
    dom.focusName.textContent = "未选择单位";
    dom.focusMeta.textContent = "尚未选择卡牌";
    setHtml(dom.focusBadges, "");
    dom.skillName.textContent = "技能";
    setHtml(dom.skillId, "");
    dom.skillDescription.textContent = "进入战斗后读取编队内干员的技能资料。";
    dom.skillSpLabel.textContent = "-- / --";
    dom.skillSpBar.style.width = "0%";
    dom.skillRemainLabel.textContent = "--";
    dom.skillRemainBar.style.width = "0%";
    dom.skillRemainBar.parentElement.classList.remove("is-infinite");
    setHtml(dom.skillFacts, "");
    setHtml(dom.focusActions, "");
    state.focusActionsKey = "";
    setModuleIcon(null);
    dom.moduleType.textContent = "MOD";
    dom.moduleType.classList.add("is-empty");
    dom.moduleName.textContent = "未携带模组";
    dom.moduleLevel.textContent = "";
    setHtml(dom.moduleDescription, "部署后读取当前携带的模组。");
    dom.moduleDescription.classList.add("is-empty");
    setHtml(dom.moduleStats, "");
    setHtml(dom.talentGrid, "");
    dom.statsHint.textContent = "当前战斗数值";
    setHtml(dom.statsGrid,
      `<div class="stat-cell"><span>生命</span><strong>--</strong></div>` +
      `<div class="stat-cell"><span>攻击</span><strong>--</strong></div>`);
    renderStatsAbnormal(null, "未选择单位");
    dom.focusBuffCount.textContent = "0";
    setHtml(dom.focusBuffs, `<div class="empty-small">无</div>`);
    state.artKey = null;
  }

  function equippedModuleFor(card, unit) {
    if (unit && Array.isArray(unit.modules)) {
      const current = unit.modules.find((module) => module?.id);
      if (current) {
        return { id: current.id, level: Number(current.level) || 3, runtime: true };
      }
    }
    const slot = (state.snapshot?.squad || []).find(
      (entry) => String(entry.id) === String(card?.id || unit?.id || ""));
    return slot?.module
      ? { id: slot.module, level: Number(slot.module_level) || 3, runtime: false }
      : null;
  }

  function setModuleIcon(module) {
    const icon = dom.moduleIcon;
    if (!icon) return;
    const iconKey = module?.icon || "";
    if (icon.dataset.iconKey === iconKey) return;
    icon.dataset.iconKey = iconKey;
    icon.hidden = true;
    icon.removeAttribute("src");
    const available = new Set(MODULE_DATA.icons || []);
    if (!module?.icon || !available.has(module.icon)) return;
    icon.onload = () => {
      icon.hidden = false;
    };
    icon.onerror = () => {
      icon.hidden = true;
      icon.removeAttribute("src");
    };
    // Module art is not shipped with the current resource snapshot. The id-based path is the
    // drop-in contract: putting <icon>.png in this directory makes the real art appear.
    icon.src = `assets/game/module/${encodeURIComponent(module.icon)}.png`;
  }

  function renderModuleTabs() {
    for (const tab of document.querySelectorAll("[data-module-tab]")) {
      tab.classList.toggle("is-active", tab.dataset.moduleTab === state.moduleTab);
    }
    dom.talentPage.classList.toggle("is-active", state.moduleTab === "talent");
    dom.modulePage.classList.toggle("is-active", state.moduleTab === "module");
  }

  function renderTalents(card, unit, equipped, profile) {
    const operatorId = card?.id || unit?.id || "";
    const runtime = (unit?.talents?.length ? unit.talents : card?.talents) || [];
    const module = equipped ? moduleFor(equipped.id) : null;
    const level = equipped ? clampInt(equipped.level, 1, 3, 3) : 0;
    const potential = clampInt(profile?.potential, 0, 6, 0);
    const moduleTalents = module
      ? moduleEffects(module, level, potential).talents
      : [];
    const moduleByName = new Map(
      moduleTalents.map((talent) => [talent.name, talent]),
    );
    const runtimeTalents = runtime.filter((talent) =>
      !talent?.hidden && talent?.name);
    let names = Array.from(new Set(runtimeTalents.map((talent) => talent.name)));
    if (!names.length) {
      names = baseTalentGroupsFor(operatorId).map((group) => group.name);
    }
    if (!names.length) {
      setHtml(dom.talentGrid, `<div class="empty-small">暂无天赋数据</div>`);
      return;
    }
    setHtml(dom.talentGrid, names.map((name, index) => {
      const runtimeTalent = runtimeTalents.find((talent) => talent.name === name);
      const moduleTalent = moduleByName.get(name);
      const description = moduleTalent?.description ||
        runtimeTalent?.description ||
        baseTalentDescription(operatorId, name, profile);
      const modified = !!moduleTalent;
      return `<article class="talent-card ${modified ? "is-module" : ""}">` +
        `<div class="talent-card-head"><span>天赋 ${index + 1}</span>` +
        `${modified ? `<span class="talent-change">模组调整</span>` : ""}</div>` +
        `<div class="talent-name">${esc(name)}</div>` +
        `<div class="talent-desc">${renderRichText(description) || "—"}</div>` +
        `</article>`;
    }).join(""));
  }

  function renderModulePanel(card, unit, profile) {
    const operatorId = card?.id || unit?.id || "";
    const available = modulesFor(operatorId);
    const equipped = equippedModuleFor(card, unit);
    renderModuleTabs();
    renderTalents(card, unit, equipped, profile);
    dom.moduleType.classList.toggle("is-empty", !equipped);
    dom.moduleDescription.classList.remove("is-empty");

    if (!equipped) {
      setModuleIcon(null);
      dom.moduleType.textContent = "—";
      dom.moduleName.textContent = available.length ? "未携带模组" : "无模组";
      dom.moduleLevel.textContent = available.length ? `${available.length} 个可选` : "";
      setHtml(dom.moduleDescription, "");
      dom.moduleDescription.classList.add("is-empty");
      setHtml(dom.moduleStats, "");
      return;
    }

    const module = moduleFor(equipped.id);
    const level = clampInt(equipped.level, 1, 3, 3);
    const potential = clampInt(profile?.potential, 0, 6, 0);
    if (!module) {
      setModuleIcon(null);
      dom.moduleType.textContent = "MOD";
      dom.moduleName.textContent = equipped.id;
      dom.moduleLevel.textContent = `Lv.${level}`;
      setHtml(dom.moduleDescription, "本地模组资料未收录该条目。");
      dom.moduleDescription.classList.add("is-empty");
      setHtml(dom.moduleStats, "");
      return;
    }

    const effects = moduleEffects(module, level, potential);
    setModuleIcon(module);
    dom.moduleType.textContent = module.typeLabel || "MOD";
    dom.moduleName.textContent = module.name || module.id;
    dom.moduleLevel.textContent = `Lv.${level}`;
    const descriptions = [];
    for (const text of effects.traits) {
      descriptions.push(`<div class="module-effect-line">${renderRichText(text)}</div>`);
    }
    for (const talent of effects.talents) {
      descriptions.push(
        `<div class="module-talent">` +
          `<span class="module-talent-name">${esc(talent.name || "模组天赋")}</span>` +
          `<span class="module-talent-text">${renderRichText(talent.description)}</span>` +
        `</div>`,
      );
    }
    const attrs = Object.entries(effects.attributes || {});
    setHtml(dom.moduleDescription, descriptions.length
      ? descriptions.join("")
      : attrs.length
        ? "本等级提供以下属性加成。"
        : "该等级暂无可显示的模组效果。");
    if (!descriptions.length) dom.moduleDescription.classList.add("is-empty");
    setHtml(dom.moduleStats, attrs.length
      ? attrs.map(([key, value]) => {
          const number = Number(value);
          const sign = number > 0 ? "+" : "";
          return `<span class="chip">${esc(MODULE_ATTR_LABELS[key] || key)} ` +
            `${esc(sign + fmt(number, 1))}</span>`;
        }).join("")
      : "");
  }

  function operatorDetailView(card, unit, nameOverride = "") {
    const profile = profileForCard(card, unit);
    const deployed = !!unit;
    const buffs = deployed ? unitBuffs(unit) : [];
    const hp = deployed ? (Number(unit.hp) || 0) : 0;
    const maxHp = deployed ? (Number(unit.max_hp) || 0) : 0;
    const shield = deployed ? (Number(unit.shield) || 0) : 0;
    const shieldLayers = deployed ? (Number(unit.shield_layers) || 0) : 0;
    const minusHp = deployed ? (Number(unit.minus_hp) || 0) : 0;
    const maxMinusHp = deployed ? (Number(unit.max_minus_hp) || 0) : 0;
    const skill = profile?.skill || {};
    const duration = Number(skill.duration) || 0;
    const cardState = deployState(card);
    const cardRespawn = cardRespawnRemaining(card);
    const negativeHpLabel = maxMinusHp > 0.5
      ? `负血 ${fmt(minusHp, 0)}/${fmt(maxMinusHp, 0)}` : "";
    const shieldLabel = shield > 0.5
      ? `屏障 ${fmt(shield, 0)}`
      : shieldLayers > 0 ? `屏障 ${shieldLayers}层` : "";
    const cell = (label, value, wide = false, extraClass = "") =>
      `<div class="stat-cell ${wide ? "wide" : ""} ${extraClass}">` +
      `<span>${label}</span><strong>${value}</strong></div>`;
    const statsHtml = deployed
      ? cell("生命", `${fmt(hp, 0)} / ${fmt(maxHp, 0)}`, true) +
        (shieldLabel
          ? cell("屏障", shield > 0.5 ? fmt(shield, 0) : `${shieldLayers} 层`,
              true, "shield")
          : "") +
        (negativeHpLabel
          ? cell("负生命值", `${fmt(minusHp, 0)} / ${fmt(maxMinusHp, 0)}`,
              true, "shield")
          : "") +
        cell("攻击", fmt(unit.atk, 1)) +
        cell("防御", fmt(unit.def, 1)) +
        cell("法抗", fmt(unit.magic_resistance, 1)) +
        cell("阻挡", esc(unit.block_count)) +
        cell("费用", esc(unit.unit_cost)) +
        cell("攻速", fmt(unit.attack_speed, 2)) +
        cell("状态", unit.alive ? "存活" : "离场")
      : cell("部署费用", esc(card?.cost ?? "--"), true) +
        cell("等级", profile ? `Lv.${esc(profile.level)}` : "--") +
        cell("精英", profile ? `E${esc(profile.evolve)}` : "--") +
        cell("潜能", profile ? esc(profile.potential) : "--") +
        cell("技能消耗", `${esc(skill.sp_cost ?? "--")} SP`) +
        cell("持续时间", duration !== 0 ? fmtDuration(duration) : "--") +
        cell("卡牌状态", cardState.label) +
        (cardRespawn > 0
          ? cell("再部署剩余", fmtDuration(cardRespawn), true)
          : "") +
        cell("剩余", esc(card?.remaining ?? "--")) +
        cell("场上状态", "未部署");
    return {
      profile,
      deployed,
      buffs,
      unitBuffs: buffs,
      statsHtml,
      statsHint: deployed
        ? (profile ? `Lv.${profile.level} · E${profile.evolve} · 潜 ${profile.potential}` : "当前战斗数值")
        : "未部署 · 静态编队数据",
    };
  }

  function renderFocus(card, unit, nameOverride = "") {
    try {
      renderFocusInner(card, unit, nameOverride);
    } catch (error) {
      try {
        window.BetterBattleProtocol?.post({
          type: window.BetterBattleProtocol?.Message.Log,
          message: `renderFocus: ${error?.stack || error}`,
        });
      } catch {
      }
      console.error(error);
    }
  }

  function renderFocusInner(card, unit, nameOverride = "") {
    const profile = profileForCard(card, unit);
    const media = staticArtFor(card?.id || unit?.id);
    const skill = profile?.skill || {};
    const deployed = !!unit;
    const buffs = deployed ? unitBuffs(unit) : [];
    const name = nameOverride || card?.name || unit?.name ||
      profile?.name || card?.id || "未知干员";
    const hp = deployed ? (Number(unit.hp) || 0) : 0;
    const maxHp = deployed ? (Number(unit.max_hp) || 0) : 0;
    const shield = deployed ? (Number(unit.shield) || 0) : 0;
    const shieldLayers = deployed ? (Number(unit.shield_layers) || 0) : 0;
    const minusHp = deployed ? (Number(unit.minus_hp) || 0) : 0;
    const maxMinusHp = deployed ? (Number(unit.max_minus_hp) || 0) : 0;
    // TODO: barrier gain/damage and negative-HP damage are display-only for now.
    const negativeHpLabel = maxMinusHp > 0.5
      ? `负血 ${fmt(minusHp, 0)}/${fmt(maxMinusHp, 0)}` : "";
    const shieldLabel = shield > 0.5
      ? `屏障 ${fmt(shield, 0)}`
      : shieldLayers > 0 ? `屏障 ${shieldLayers}层` : "";
    const maxSp = Number(unit?.max_sp) || Number(skill.max_sp) || 0;
    const sp = deployed ? (Number(unit.sp) || 0) : 0;
    const spRatio = deployed && maxSp > 0 ? Math.max(0, Math.min(1, sp / maxSp)) : 0;
    const duration = Number(skill.duration) || 0;
    const remaining = deployed ? (Number(unit.skill_remaining) || 0) : 0;
    const skillType = Number(skill.type);
    const hasSkillType = Number.isInteger(skillType) && skillType >= 0;
    const passiveSkill = skillType === 0;
    const ammoSkill = Number(skill.duration_type) === SKILL_DURATION_TYPE.AMMO;
    const instantSkill = duration === 0;
    const active = deployed && !!unit.skill_affecting;
    const ammoMax = deployed && Number(unit.skill_ammo_max) > 0
      ? Number(unit.skill_ammo_max) : 0;
    const ammoRemaining = deployed && Number(unit.skill_ammo_remaining) >= 0
      ? Number(unit.skill_ammo_remaining) : 0;
    const infiniteSkill = deployed && !ammoSkill && !passiveSkill && (
      (Number.isFinite(remaining) && remaining >= INFINITE_DURATION_THRESHOLD) ||
      (duration < 0 && active)
    );
    const remainRatio = ammoSkill
      ? (ammoMax > 0 ? Math.max(0, Math.min(1, ammoRemaining / ammoMax)) : 0)
      : infiniteSkill
        ? 1
        : (duration > 0 ? Math.max(0, Math.min(1, remaining / duration)) : 0);
    const uid = deployed ? unitActionUid(unit) : null;
    const cardState = deployState(card);
    const cardRespawn = cardRespawnRemaining(card);

    dom.focusName.textContent = name;
    dom.focusMeta.textContent = profile
      ? `精英 ${esc(profile.evolve)} · Lv.${esc(profile.level)} · 潜能 ${esc(profile.potential)}`
      : (deployed ? "战斗数据已读取" : "尚未读取干员资料");
    setHtml(dom.focusBadges, deployed
      ? `<span class="badge green">存活</span>` +
        (shieldLabel
          ? `<span class="badge shield">${shieldLabel}</span>`
          : "") +
        (negativeHpLabel
          ? `<span class="badge shield">${negativeHpLabel}</span>`
          : "") +
        `<span class="badge ${unit.skill_affecting ? "amber" : ""}">` +
        `${unit.skill_affecting ? "技能持续中" : "技能待机"}</span>`
      : `<span class="badge ${cardState.tone}">${cardState.label}</span>` +
        `<span class="badge">未部署</span>`);

    renderArt(card, profile, unit);
    renderModulePanel(card, unit, profile);

    dom.skillName.textContent = skill.name || "技能数据读取中";
    const skillIcon = staticSkillIcon(card?.id || unit?.id, card?.skill) ||
      media?.skill || "";
    setHtml(dom.skillId, skillIcon
      ? `<img class="skill-icon" src="${esc(skillIcon)}" alt="">`
      : "");
    setHtml(dom.skillDescription,
      renderRichText(skill.description) || "尚未读取技能描述。");
    const spMeter = dom.skillSpBar.closest(".skill-meter");
    const remainMeter = dom.skillRemainBar.closest(".skill-meter");
    spMeter.classList.toggle("hidden", !deployed || (passiveSkill && maxSp <= 0));
    remainMeter.classList.toggle("hidden", passiveSkill || instantSkill);
    dom.skillSpLabel.textContent = deployed
      ? `${fmt(sp, 0)} / ${maxSp || "--"}`
      : `-- / ${maxSp || "--"}`;
    dom.skillSpBar.style.width = `${(spRatio * 100).toFixed(1)}%`;
    remainMeter.querySelector(".meter-label > span").textContent =
      ammoSkill ? "剩余弹药" : "持续时间";
    let remainLabel;
    if (!deployed) {
      if (ammoSkill) {
        remainLabel = "弹药技能";
      } else if (passiveSkill) {
        remainLabel = "被动";
      } else if (duration < 0) {
        remainLabel = "无限";
      } else if (duration > 0) {
        remainLabel = `最多 ${fmtDuration(duration)}`;
      } else {
        remainLabel = "瞬发";
      }
    } else if (ammoSkill) {
      remainLabel = ammoMax > 0
        ? `${ammoRemaining} / ${ammoMax} 发`
        : (active ? "弹药技能" : "待开启");
    } else if (passiveSkill) {
      remainLabel = "被动";
    } else if (infiniteSkill) {
      remainLabel = remaining > 0 ? fmtDuration(remaining) : "无限";
    } else if (active) {
      remainLabel = remaining > 0 ? fmtDuration(remaining) : "计算中";
    } else if (instantSkill) {
      remainLabel = "瞬发";
    } else {
      remainLabel = "待开启";
    }
    dom.skillRemainLabel.textContent = remainLabel;
    dom.skillRemainBar.style.width = `${(remainRatio * 100).toFixed(1)}%`;
    dom.skillRemainBar.parentElement.classList.toggle("is-infinite", infiniteSkill);
    dom.skillRemainBar.parentElement.classList.toggle("is-ammo", ammoSkill);
    const skillFacts = [];
    if (hasSkillType) {
      skillFacts.push(
        `<span class="chip ${skillType === 2 ? "warn" : ""}">` +
        `${skillTriggerLabel(skillType)}</span>`,
      );
    }
    if (ammoSkill) {
      skillFacts.push(`<span class="chip hot">弹药技能</span>`);
    }
    if (deployed) {
      if (unit.skill_overload_skill) {
        skillFacts.push(
          `<span class="chip hot">${unit.skill_overloaded ? "过载中" : "过载技能"}</span>`,
        );
      }
      if (active && unit.skill_retriggerable) {
        skillFacts.push(`<span class="chip hot">可主动关闭</span>`);
      }
      if (Number(skill.sp_cost) > 0) {
        skillFacts.push(`<span class="chip">消耗 ${esc(skill.sp_cost)} SP</span>`);
      }
    } else {
      skillFacts.push(`<span class="chip">部署费用 ${esc(card?.cost ?? "--")}</span>`);
      skillFacts.push(`<span class="chip">消耗 ${esc(skill.sp_cost ?? "--")} SP</span>`);
      skillFacts.push(
        `<span class="chip ${cardState.tone}">${cardState.label}</span>`,
      );
    }
    setHtml(dom.skillFacts, skillFacts.join(""));
    const skillActionLabel = passiveSkill
      ? "被动技能"
      : active
        ? (unit.skill_retriggerable ? "停止技能" : "技能中")
        : "开启技能";
    const actionsHtml = deployed
      ? `<button class="btn tiny primary" data-action="skill" data-uid="${esc(uid)}" ` +
        `${unit.skill_op_triggerable ? "" : "disabled"}>` +
        `${skillActionLabel}</button>` +
        `<button class="btn tiny danger" data-action="withdraw" data-uid="${esc(uid)}" ` +
        `${unit.withdrawable || unit.manually_withdrawable ? "" : "disabled"}>撤退</button>`
      : "";
    const actionsKey = deployed
      ? `${uid}:${skillActionLabel}:${unit.skill_op_triggerable}:` +
        `${unit.withdrawable}:${unit.manually_withdrawable}`
      : "empty";
    if (state.focusActionsKey !== actionsKey) {
      setHtml(dom.focusActions, actionsHtml);
      state.focusActionsKey = actionsKey;
    }

    const detail = operatorDetailView(card, unit, nameOverride);
    dom.statsHint.textContent = detail.statsHint;
    setHtml(dom.statsGrid, detail.statsHtml);

    renderStatsAbnormal(deployed ? unit : null, "未部署");
    dom.focusBuffCount.textContent = String(buffs.length);
    const buffSignature = buffs.map((buff) => String(buff?.key || "")).join("|");
    if (buffSignature !== state.focusBuffSignature) {
      state.focusBuffSignature = buffSignature;
      const catalog = window.BuffCatalog;
      const named = catalog
        ? buffs.filter((buff) => catalog.resolve(buff)?.name).length
        : 0;
      window.BetterBattleProtocol?.post({
        type: window.BetterBattleProtocol?.Message?.Log,
        message: `Buff 面板：${buffs.length} 条，目录命中 ${named} 条`,
      });
    }
    setHtml(dom.focusBuffs, buffs.length
      ? buffs.map((buff) => buffHtml(buff, true)).join("")
      : `<div class="empty-small">该单位当前没有 Buff</div>`);
  }

  function renderArt(card, profile, unit = null) {
    const localArt = staticArtFor(card?.id || unit?.id);
    const portraitId = localArt?.portraitId || "";
    const avatarId = localArt?.avatarId || "";
    const kind = portraitId ? "portrait" : (avatarId ? "avatar" : "");
    const id = portraitId || avatarId;
    const key = `${card?.uid || unit?.card_uid || unit?.uid || 0}:${kind}:${id || "none"}`;
    if (state.artKey === key) return;
    state.artKey = key;
    dom.artStage.classList.remove("has-art");
    dom.artCanvas.className = "";
    dom.artEmpty.style.display = "grid";
    const name = card?.name || unit?.name || profile?.name || card?.id || "未选择单位";
    dom.artEmpty.innerHTML =
      `<span class="art-empty-name">${esc(name)}</span>` +
      `<span class="art-empty-note">正在载入立绘</span>`;
    dom.artError.textContent = "";
    if (!kind || !id) {
      dom.artEmpty.innerHTML =
        `<span class="art-empty-name">${esc(name)}</span>` +
        `<span class="art-empty-note">暂无可用图片资源</span>`;
      return;
    }

    window.BetterBattleAssets?.loadImage(kind, id).then((url) => {
      if (state.artKey !== key) return;
      const image = new Image();
      image.onload = () => {
        if (state.artKey !== key) return;
        const width = image.naturalWidth;
        const height = image.naturalHeight;
        if (width <= 0 || height <= 0) return;
        dom.artCanvas.width = width;
        dom.artCanvas.height = height;
        const context = dom.artCanvas.getContext("2d");
        context.clearRect(0, 0, width, height);
        context.drawImage(image, 0, 0, width, height, 0, 0, width, height);
        dom.artCanvas.className = "ready";
        dom.artStage.classList.add("has-art");
        dom.artEmpty.style.display = "none";
        dom.artError.textContent = "";
      };
      image.onerror = () => {
        dom.artError.textContent = "立绘读取失败";
      };
      image.src = url;
    }).catch(() => {
      if (state.artKey !== key) return;
      dom.artEmpty.innerHTML =
        `<span class="art-empty-name">${esc(name)}</span>` +
        `<span class="art-empty-note">图片资源尚未缓存</span>`;
    });
  }

  function buffTiming(buff) {
    const permanent = isPermanent(buff);
    const ratio = buffRatio(buff);
    const remaining = Number(buff.remaining);
    const life = Number(buff.life);
    const infinite = Number.isFinite(remaining) &&
      remaining >= INFINITE_DURATION_THRESHOLD;
    const timeLabel = infinite
      ? "无限"
      : (permanent ? "永久" : `剩余 ${formatDurationValue(remaining)}`);
    const lifeLabel = !permanent && !infinite && life > 0
      ? `总 ${formatDurationValue(life)}`
      : "";
    return { infinite, ratio, timeLabel, lifeLabel };
  }

  function buffAbnormalRows(buff) {
    const catalog = window.BuffCatalog;
    const rows = [
      ["异常", buff.abnormal],
      ["免疫", buff.abnormal_immune],
      ["反制", buff.abnormal_anti],
      ["组合", buff.abnormal_combo],
      ["组合免疫", buff.abnormal_combo_immune],
    ].filter(([, value]) => value);
    return rows.map(([label, value]) => [
      label,
      catalog ? catalog.abnormalLabels(value).join("、") : value,
    ]);
  }

  function buffBlackboardHtml(buff) {
    const catalog = window.BuffCatalog;
    const entries = catalog ? catalog.blackboardEntries(buff) : [];
    if (!entries.length) return "";
    return `<div class="buff-blackboard">` +
      `<div class="buff-blackboard-head">参数</div>` +
      entries.map((entry) => {
        const value = catalog ? catalog.boardValue(entry) : entry.value_str;
        return `<div class="buff-blackboard-row"><span>${esc(entry.key)}</span>` +
          `<code>${esc(value)}</code></div>`;
      }).join("") + `</div>`;
  }

  function buffModifierHtml(buff) {
    const catalog = window.BuffCatalog;
    const entries = Array.isArray(buff.modifier_entries)
      ? buff.modifier_entries
      : [];
    if (!catalog || !entries.length) {
      return buff.modifiers
        ? `<div class="buff-mods">${esc(buff.modifiers)}</div>`
        : "";
    }
    const { category, groups } = catalog.modifierGroups(entries);
    return `<div class="buff-effects">` +
      `<span class="chip ${category === "减益" ? "bad" : category === "加成" ? "good" : ""}">${esc(category)}</span>` +
      groups.map((group) =>
        `<div class="buff-effect-row"><span class="buff-effect-kind">${esc(group.label)}</span>` +
        group.items.map((item) =>
          `<code class="${item.value > 0 ? "up" : "down"}">${esc(catalog.modifierText(item))}</code>`,
        ).join("") + `</div>`,
      ).join("") + `</div>`;
  }

  function buffSourceHtml(buff, info) {
    const catalog = window.BuffCatalog;
    if (!catalog) return "";
    const parts = [];
    const label = catalog.sourceLabel(info);
    if (label) parts.push(label);
    if (buff.source) {
      const operator = catalog.operator(buff.source);
      const enemy = (state.snapshot?.enemies || []).find(
        (entry) => entry.id === buff.source);
      const unit = (state.snapshot?.units || []).find(
        (entry) => entry.id === buff.source || entry.tmpl === buff.source);
      parts.push(`来源 ${operator?.name || enemy?.name || unit?.name || buff.source}`);
    }
    if (!parts.length) return "";
    return `<div class="buff-source">${esc(parts.join(" · "))}</div>`;
  }

  function buffRelicHtml(buff) {
    const catalog = window.BuffCatalog;
    const rogue = state.snapshot?.rogue;
    if (!catalog || !rogue?.valid) return "";
    const items = catalog.relicCandidates(buff, rogue);
    if (!items.length) return "";
    return `<div class="buff-relics"><span class="buff-relic-head">肉鸽藏品</span>` +
      items.slice(0, 6).map((item) => {
        const name = item.entry?.name || item.id;
        const suffix = item.exact ? "归属" : "候选";
        return `<span class="buff-relic" title="${esc(item.entry?.description || "")}">` +
          `${esc(name)}<em>${suffix}</em></span>`;
      }).join("") + `</div>`;
  }

  function buffDescriptionHtml(buff, info, compact) {
    const catalog = window.BuffCatalog;
    if (!catalog || !info?.description || catalog.hidesDescription(info.entry)) {
      return "";
    }
    const text = catalog.substituteDescription(
      info.description, catalog.blackboardEntries(buff));
    if (!text) return "";
    const html = typeof renderRichText === "function"
      ? renderRichText(text)
      : esc(text).replace(/\n/g, "<br>");
    return `<div class="buff-desc${compact ? " compact" : ""}">${html}</div>`;
  }

  function buffHtml(buff, compact = false, buffId = "") {
    const { infinite, ratio, timeLabel, lifeLabel } = buffTiming(buff);
    const catalog = window.BuffCatalog;
    const info = catalog ? catalog.resolve(buff) : null;
    const displayName = info?.name || buff.key || buff.template || "unknown";
    const chips = [
      `<span class="chip ${buff.enabled ? "good" : "bad"}">${buff.enabled ? "启用" : "禁用"}</span>`,
      `<span class="chip">${esc(buff.stack ?? 0)}/${esc(buff.max_stack || buff.stack || 0)} 层</span>`,
    ];
    if (Number(buff.trigger_interval) > 0 || Number(buff.trigger_count)) {
      chips.push(`<span class="chip">触发 ${esc(buff.trigger_count ?? 0)} · ${formatDurationValue(buff.trigger_interval)}</span>`);
    }
    if (buff.valid === false) chips.push(`<span class="chip bad">无效</span>`);
    if (buff.status_resistable) chips.push(`<span class="chip warn">可抵抗</span>`);
    if (!info?.name && buff.key) chips.push(`<span class="chip ghost">目录未收录</span>`);
    const abnormal = buffAbnormalRows(buff);
    const sub = [];
    if (!compact) {
      if (buff.template && buff.template !== "empty" && buff.template !== buff.key) {
        sub.push("tmpl " + buff.template);
      }
      if (buff.override) sub.push("override " + buff.override);
      if (buff.effect && buff.effect !== "empty") sub.push("effect " + buff.effect);
    }
    return `<div class="buff"${buffId ? ` data-buff-id="${esc(buffId)}"` : ""}>` +
      `<div class="buff-main"><div class="buff-key"><span class="buff-name">${esc(displayName)}</span>` +
      (info?.name && buff.key ? `<code class="buff-keyraw">${esc(buff.key)}</code>` : "") +
      `</div><div class="buff-chips">${chips.join("")}</div></div>` +
      (sub.length ? `<div class="buff-mods">${esc(sub.join(" · "))}</div>` : "") +
      buffSourceHtml(buff, info) +
      buffDescriptionHtml(buff, info, compact) +
      buffModifierHtml(buff) +
      (abnormal.length
        ? `<div class="buff-abnormal">${abnormal.map(([label, value]) =>
          `${esc(label)} ${esc(value)}`).join("<br>")}</div>`
        : "") +
      buffBlackboardHtml(buff) +
      buffRelicHtml(buff) +
      `<div class="buff-time"><span data-buff-remaining>${esc(timeLabel)}</span>` +
      `<span data-buff-life>${esc(lifeLabel)}</span></div>` +
      `<div class="bar${infinite ? " is-infinite" : ""}" data-buff-bar>` +
      `<i style="width:${(ratio * 100).toFixed(1)}%"></i></div></div>`;
  }

  function abnormalTone(name) {
    if (["COLD", "FROZEN"].includes(name)) return "#6fc8ff";
    if (["STUNNED", "STUNNED_NO_AMPLIFY_DAMAGE", "PALSY", "PALSYING",
      "DOZE", "SLEEPING"].includes(name)) return "#f6c85f";
    if (["DISARMED", "FORCE_DISARMED", "SILENCED", "DISABLE_COMBAT",
      "SKILL_NOT_ACTIVATABLE", "SP_RECOVER_STOPPED", "SP_MODIFY_STOPPED",
      "HEAL_FREE", "NOT_WITHDRAWABLE"].includes(name)) return "#ff8a8f";
    if (["FEARED", "FEARED_PRIVATE", "ATTRACTED", "TELEPORTED", "DURANCE",
      "GROUND_BOUND", "LEVITATE", "FORCE_LEVITATE", "OUT_OF_GROUND",
      "UNMOVABLE", "UNMOVABLE_PRIVATE", "HIDDEN", "INVISIBLE",
      "CAMOUFLAGE"].includes(name)) return "#b784ff";
    return "#9fb4ff";
  }

  function collectAbnormalChips(buffs) {
    const fields = [
      { key: "abnormal", tag: "" },
      { key: "abnormal_immune", tag: "免疫", color: "#8fe0b0" },
      { key: "abnormal_anti", tag: "反制", color: "#61d4c7" },
      { key: "abnormal_combo", tag: "组合", color: "#b784ff" },
      { key: "abnormal_combo_immune", tag: "组合免疫", color: "#8fe0b0" },
    ];
    const chips = new Map();
    for (const field of fields) {
      for (const buff of Array.isArray(buffs) ? buffs : []) {
        for (const raw of String(buff?.[field.key] || "").split(",")) {
          const name = raw.trim();
          if (!name) continue;
          let item = chips.get(name);
          if (!item) {
            item = {
              name,
              tags: [],
              sources: 0,
              color: field.color || abnormalTone(name),
            };
            chips.set(name, item);
          }
          if (field.tag && !item.tags.includes(field.tag)) {
            item.tags.push(field.tag);
          }
          item.sources += 1;
        }
      }
    }
    return [...chips.values()];
  }

  function abnormalChipsHtml(list) {
    const catalog = window.BuffCatalog;
    return list.map((item) => {
      const name = catalog ? catalog.abnormalName(item.name) : item.name;
      const title = catalog
        ? `${catalog.abnormalLabel(item.name)} · ${item.sources} 个 Buff 来源`
        : `${item.name} · ${item.sources} 个 Buff 来源`;
      const tags = item.tags.map((tag) => `<em>${esc(tag)}</em>`).join("");
      return `<span class="abnormal-chip" title="${esc(title)}" ` +
        `style="--chip:${item.color};--chip-bg:${item.color}16;` +
        `--chip-border:${item.color}4d">` +
        `<i></i>${tags}<span>${esc(name)}</span></span>`;
    }).join("");
  }

  function renderAbnormalChips(container, countNode, list, emptyText = "无异常") {
    if (!container) return;
    if (countNode) countNode.textContent = String(list.length);
    setHtml(container, list.length
      ? abnormalChipsHtml(list)
      : `<span class="stats-abnormal-none">${esc(emptyText)}</span>`);
  }

  function renderStatsAbnormal(unit, emptyText = "无异常") {
    if (!dom.statsAbnormalChips) return;
    const catalog = window.BuffCatalog;
    const buffs = Array.isArray(unit?.buffs)
      ? unit.buffs.filter((buff) =>
          buff?.enabled !== false && buff?.valid !== false)
      : [];
    const list = collectAbnormalChips(buffs);
    const signature = `${unit?.id || "-"}:` +
      list.map((item) => `${item.name}/${item.tags.join("")}`).join("|");
    if (signature !== state.statsAbnormalSignature) {
      state.statsAbnormalSignature = signature;
      const names = list.map((item) =>
        catalog ? catalog.abnormalName(item.name) : item.name).join("、");
      window.BetterBattleProtocol?.post({
        type: window.BetterBattleProtocol?.Message?.Log,
        message: `异常状态栏：${list.length} 项${names ? " · " + names : ""}`,
      });
    }
    renderAbnormalChips(dom.statsAbnormalChips, dom.statsAbnormalCount, list,
      emptyText);
  }

  function globalBuffsView(snapshot) {
    const catalog = window.BuffCatalog;
    const groups = Array.isArray(snapshot?.global_buffs)
      ? snapshot.global_buffs
      : [];
    const rogue = snapshot?.rogue;
    const total = groups.reduce((sum, group) =>
      sum + (Array.isArray(group?.buffs) ? group.buffs.length : 0), 0);
    const relicTotal = Array.isArray(rogue?.relics) ? rogue.relics.length : 0;
    const parts = [];
    if (rogue?.valid) {
      const relicList = Array.isArray(rogue.relics) ? rogue.relics : [];
      const derivedTopic = String(rogue.topic || "") ||
        (String(relicList[0]?.id || "").match(/^rogue_\d+/) || [""])[0];
      const relics = (Array.isArray(rogue.relics) ? rogue.relics : [])
        .map((relic) => {
          const entry = catalog ? catalog.relic(relic.id) : null;
          const name = entry?.name || relic.id;
          return `<span class="buff-relic" title="${esc(entry?.description || "")}">` +
            `${esc(name)}${Number(relic.layer) > 0 ? `<em>${esc(relic.layer)}</em>` : ""}</span>`;
        }).join("");
      parts.push(`<div class="global-rogue"><span class="global-rogue-topic">` +
        `${esc(derivedTopic || "肉鸽")} · ${relicList.length} 藏品</span>` +
        `<div class="buff-relics">${relics}</div></div>`);
    }
    for (const group of groups) {
      const entries = Array.isArray(group.buffs) ? group.buffs : [];
      const groupBoard = catalog ? catalog.blackboardEntries(group) : [];
      const anchor = groupBoard.find((entry) => entry.key === "key")?.value_str || "";
      const groupRelics = catalog && rogue?.valid && anchor
        ? catalog.relicCandidates({ key: anchor }, rogue)
        : [];
      const relicHtml = groupRelics.length
        ? `<div class="buff-relics"><span class="buff-relic-head">藏品</span>` +
          groupRelics.slice(0, 6).map((item) =>
            `<span class="buff-relic" title="${esc(item.entry?.description || "")}">` +
            `${esc(item.entry?.name || item.id)}<em>${item.exact ? "归属" : "候选"}</em></span>`,
          ).join("") + `</div>`
        : "";
      const boardHtml = groupBoard.length
        ? `<div class="buff-blackboard"><div class="buff-blackboard-head">参数</div>` +
          groupBoard.map((entry) =>
            `<div class="buff-blackboard-row"><span>${esc(entry.key)}</span>` +
            `<code>${esc(catalog?.boardValue(entry) ?? entry.value)}</code></div>`).join("") +
          `</div>`
        : "";
      const rows = entries.map((buff) => {
        const info = catalog ? catalog.resolve(buff) : null;
        const name = info?.name || buff.key || buff.template || "未知效果";
        const board = catalog ? catalog.blackboardEntries(buff) : [];
        const effectiveBoard = board.length ? board : groupBoard;
        const description = catalog && info?.description &&
          !catalog.hidesDescription(info.entry)
          ? catalog.substituteDescription(info.description, effectiveBoard)
          : "";
        const rendered = description
          ? (typeof renderRichText === "function"
              ? renderRichText(description)
              : esc(description))
          : "";
        return `<div class="global-buff-item"><div class="global-buff-title">` +
          `<strong>${esc(name)}</strong>` +
          (name !== buff.key && buff.key ? `<code>${esc(buff.key)}</code>` : "") +
          `</div>` +
          (rendered ? `<div class="buff-desc compact">${rendered}</div>` : "") +
          (board.length
            ? `<div class="buff-blackboard"><div class="buff-blackboard-head">参数</div>` +
              board.map((entry) => `<div class="buff-blackboard-row"><span>${esc(entry.key)}</span>` +
                `<code>${esc(catalog?.boardValue(entry) ?? entry.value)}</code></div>`).join("") +
              `</div>`
            : "") +
          `</div>`;
      }).join("");
      parts.push(`<div class="global-buff-group"><div class="global-buff-head">` +
        `<span>${esc(group.key || "全局效果")}</span>` +
        `<span class="global-buff-meta">${esc(group.source || "")}` +
        `${group.layer_mask ? ` · 层 ${esc(group.layer_mask)}` : ""}` +
        `${group.uid ? ` · #${esc(group.uid)}` : ""}</span></div>` +
        rows + boardHtml + relicHtml + `</div>`);
    }
    return {
      groups,
      total,
      relicTotal,
      html: parts.length
        ? parts.join("")
        : `<div class="empty-small">当前没有读取到全局效果</div>`,
    };
  }

  function renderGlobalBuffs(snapshot) {
    if (!dom.globalBuffs) return;
    const view = globalBuffsView(snapshot);
    const signature = `${view.groups.length}:${view.total}:${view.relicTotal}`;
    if (snapshot && signature !== state.globalBuffSignature) {
      state.globalBuffSignature = signature;
      window.BetterBattleProtocol?.post({
        type: window.BetterBattleProtocol?.Message?.Log,
        message: `全局效果面板：组 ${view.groups.length}，Buff ${view.total}，藏品 ${view.relicTotal}`,
      });
    }
    if (dom.globalBuffCount) {
      dom.globalBuffCount.textContent = String(view.total);
    }
    setHtml(dom.globalBuffs, view.html);
  }

  function renderBattleRightTabs() {
    const tabs = ["stats", "buffs", "global"];
    const active = tabs.includes(state.battleRightTab)
      ? state.battleRightTab
      : "stats";
    dom.battleRightTabs?.querySelectorAll("button[data-tab]").forEach((button) => {
      const selected = button.dataset.tab === active;
      button.classList.toggle("is-active", selected);
      button.setAttribute("aria-selected", selected ? "true" : "false");
    });
    for (const key of tabs) {
      const page = {
        stats: dom.battleRightStats,
        buffs: dom.battleRightBuffs,
        global: dom.battleRightGlobal,
      }[key];
      page?.classList.toggle("is-active", key === active);
    }
  }

  pages.battle = {
    render: renderBattle,
    renderRightTabs: renderBattleRightTabs,
    selectCard,
    selectSummon,
    renderModuleTabs,
    unitForCard,
    profileForCard,
    staticArtFor,
    buildUnitRailRows,
    operatorDetailView,
    globalBuffsView,
    buffHtml,
    collectAbnormalChips,
    abnormalChipsHtml,
    renderAbnormalChips,
    battleEntities,
  };
})();
