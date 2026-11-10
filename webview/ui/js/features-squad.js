"use strict";

(() => {
  const pages = window.BetterBattlePages || (window.BetterBattlePages = {});
  const {
    normalizeSkillLevels,
    skillLevelsFor,
    skillLevelFor,
    skillLevelLabel,
  } = pages.config || {};

  function stageLabel(stage) {
    if (!stage) return "";
    const code = String(stage.code || "").trim();
    const name = String(stage.name || "").trim();
    return code && name ? `${code} · ${name}` : (name || code || String(stage.id));
  }

  function activeRogueTopic() {
    return ROGUE_BY_TOPIC.get(String(state.rogueTopic || "")) ||
      ROGUE_TOPICS[0] || null;
  }

  function activeStageCatalog() {
    if (state.stageMode === "rogue") {
      return activeRogueTopic()?.stages || [];
    }
    return STAGE_CATALOG;
  }

  function stageById(id) {
    const key = String(id || "");
    if (state.stageMode === "rogue") {
      return ROGUE_STAGE_BY_ID.get(key) || null;
    }
    return STAGE_BY_ID.get(key) || null;
  }

  function stageTopicName(stage) {
    if (state.stageMode !== "rogue" || !stage) return "";
    return ROGUE_BY_TOPIC.get(String(stage.topicId || state.rogueTopic))?.name || "";
  }

  function filterStages(query) {
    const catalog = activeStageCatalog();
    const normalized = String(query || "").trim().toLowerCase();
    if (!normalized) {
      return catalog.slice(0, STAGE_OPTION_LIMIT);
    }
    const matches = [];
    for (let index = 0; index < catalog.length; index += 1) {
      const stage = catalog[index];
      const id = String(stage.id || "").toLowerCase();
      const code = String(stage.code || "").toLowerCase();
      const name = String(stage.name || "").trim().toLowerCase();
      let score = 0;
      if (id === normalized || code === normalized || name === normalized) score = 120;
      else if (id.startsWith(normalized)) score = 100;
      else if (code.startsWith(normalized)) score = 95;
      else if (name.startsWith(normalized)) score = 90;
      else if (id.includes(normalized)) score = 70;
      else if (code.includes(normalized)) score = 65;
      else if (name.includes(normalized)) score = 60;
      else continue;
      if (stage.difficulty === "NORMAL") score += 4;
      if (!String(stage.id || "").includes("#")) score += 2;
      matches.push({ stage, index, score });
    }
    matches.sort((left, right) =>
      right.score - left.score ||
      left.index - right.index ||
      String(left.stage.id).localeCompare(String(right.stage.id)));
    return matches.slice(0, STAGE_OPTION_LIMIT).map((item) => item.stage);
  }

  function resolveStageQuery(query) {
    const value = String(query || "").trim();
    if (!value) return null;
    const catalog = activeStageCatalog();
    const normalized = value.toLowerCase();
    const score = (stage) => {
      const normal = stage.difficulty === "NORMAL" ? 2 : 0;
      const base = String(stage.id || "").includes("#") ? 0 : 1;
      return normal + base;
    };
    const best = (items) =>
      [...items].sort((left, right) => score(right) - score(left))[0] || null;
    const exact = catalog.filter((stage) =>
      String(stage.id || "").toLowerCase() === normalized ||
      String(stage.code || "").toLowerCase() === normalized ||
      String(stage.name || "").trim().toLowerCase() === normalized);
    if (exact.length) return best(exact);
    const partial = catalog.filter((stage) =>
      String(stage.id || "").toLowerCase().includes(normalized) ||
      String(stage.code || "").toLowerCase().includes(normalized) ||
      String(stage.name || "").toLowerCase().includes(normalized));
    const ids = new Set(partial.map((stage) => stage.id));
    return ids.size === 1 ? partial[0] : null;
  }

  function stageFromSnapshot() {
    const snapshot = state.snapshot;
    if (!snapshot) return null;
    const level = String(snapshot.stage || "").trim();
    if (level) {
      const byLevel = STAGE_BY_LEVEL.get(level);
      if (byLevel) return byLevel;
    }
    if (snapshot.stage_input) {
      const direct = stageById(snapshot.stage_input);
      if (direct) return direct;
    }
    const code = String(snapshot.stage_code || "").trim();
    const name = String(snapshot.stage_name || "").trim();
    return activeStageCatalog().find((stage) =>
      (code && stage.code === code) || (name && stage.name === name)) || null;
  }

  function currentStageId() {
    const typed = dom.stageInput.value.trim();
    const resolved = resolveStageQuery(typed);
    if (resolved) return String(resolved.id);
    const stateStage = resolveStageQuery(state.stage);
    return stateStage && typed === stageLabel(stateStage) ? String(stateStage.id) : "";
  }

  function selectedStage() {
    return stageById(state.stage);
  }

  function stageInputShowsSelectedLabel() {
    const selected = selectedStage();
    return selected ? dom.stageInput.value.trim() === stageLabel(selected) : false;
  }

  function positionStageOptions() {
    if (!state.stageDropdownOpen) return;
    const rect = dom.stageCombobox.getBoundingClientRect();
    const width = Math.max(rect.width, 280);
    const spaceBelow = window.innerHeight - rect.bottom;
    const maxHeight = Math.min(330, Math.max(150, spaceBelow - 12));
    dom.stageOptions.style.width = `${Math.min(width, window.innerWidth - 24)}px`;
    dom.stageOptions.style.left = `${Math.max(8, Math.min(rect.left, window.innerWidth - width - 8))}px`;
    if (spaceBelow >= 180) {
      dom.stageOptions.style.top = `${rect.bottom + 4}px`;
      dom.stageOptions.style.bottom = "auto";
      dom.stageOptions.style.maxHeight = `${maxHeight}px`;
    } else {
      const height = Math.min(330, Math.max(150, rect.top - 12));
      dom.stageOptions.style.top = "auto";
      dom.stageOptions.style.bottom = `${window.innerHeight - rect.top + 4}px`;
      dom.stageOptions.style.maxHeight = `${height}px`;
    }
  }

  function renderStageOptions(query, showAll = false) {
    const filter = showAll ? "" : query;
    const stages = filterStages(filter);
    state.stageOptions = stages;
    state.stageActiveIndex = stages.length ? Math.max(0, Math.min(state.stageActiveIndex, stages.length - 1)) : -1;
    if (state.stageActiveIndex < 0 && stages.length) state.stageActiveIndex = 0;
    dom.stageOptions.innerHTML = stages.length
      ? stages.map((stage, index) => `
          <li class="stage-option${index === state.stageActiveIndex ? " active" : ""}"
              id="stage-option-${index}" role="option"
              aria-selected="${String(stage.id) === String(state.stage) ? "true" : "false"}"
              data-stage-id="${esc(stage.id)}">
            <span class="stage-option-title">${esc(stageLabel(stage))}</span>
            <span class="stage-option-meta">${esc([
              stageTopicName(stage),
              stage.id,
              stage.difficulty && stage.difficulty !== "NORMAL" ? stage.difficulty : "",
            ].filter(Boolean).join(" · "))}</span>
          </li>`).join("")
      : '<li class="stage-option empty" role="presentation">没有匹配的关卡</li>';
    dom.stageOptions.hidden = false;
    if (stages.length && state.stageActiveIndex >= 0) {
      dom.stageInput.setAttribute("aria-activedescendant", `stage-option-${state.stageActiveIndex}`);
    } else {
      dom.stageInput.removeAttribute("aria-activedescendant");
    }
    positionStageOptions();
  }

  function openStageDropdown(showAll = false) {
    if (dom.stageInput.disabled) return;
    state.stageDropdownOpen = true;
    state.stageShowAll = showAll || stageInputShowsSelectedLabel();
    dom.stageCombobox.classList.add("open");
    dom.stageOptions.hidden = false;
    dom.stageInput.setAttribute("aria-expanded", "true");
    renderStageOptions(state.stageShowAll ? "" : dom.stageInput.value, state.stageShowAll);
  }

  function closeStageDropdown(restore = false) {
    if (restore) {
      const selected = selectedStage();
      if (selected) {
        dom.stageInput.value = stageLabel(selected);
        dom.stageResolvedName.textContent =
          [stageTopicName(selected), stageLabel(selected)].filter(Boolean).join(" · ");
      }
    }
    state.stageDropdownOpen = false;
    state.stageShowAll = false;
    dom.stageCombobox.classList.remove("open");
    dom.stageOptions.hidden = true;
    dom.stageInput.setAttribute("aria-expanded", "false");
    dom.stageInput.removeAttribute("aria-activedescendant");
  }

  function selectStage(stage) {
    if (!stage) return;
    if (state.stageMode === "rogue" && stage.topicId) {
      state.rogueTopic = String(stage.topicId);
      if (!dom.rogueTopic.hidden) dom.rogueTopic.value = state.rogueTopic;
    }
    state.stage = String(stage.id);
    state.savedStage = String(stage.id);
    state.stageEdited = true;
    dom.stageInput.value = stageLabel(stage);
    dom.stageResolvedName.textContent =
      [stageTopicName(stage), stageLabel(stage)].filter(Boolean).join(" · ");
    dom.stageInput.title = `${stage.id} · ${stage.level || ""}`;
    closeStageDropdown();
    saveUi();
  }

  function moveStageActive(delta) {
    if (!state.stageOptions.length) return;
    const size = state.stageOptions.length;
    state.stageActiveIndex = (state.stageActiveIndex + delta + size) % size;
    dom.stageOptions.querySelectorAll(".stage-option").forEach((item, index) => {
      const active = index === state.stageActiveIndex;
      item.classList.toggle("active", active);
      if (active) item.scrollIntoView({ block: "nearest" });
    });
    dom.stageInput.setAttribute("aria-activedescendant", `stage-option-${state.stageActiveIndex}`);
  }

  function renderStageField(locked = isSquadLocked()) {
    const typedStage = resolveStageQuery(dom.stageInput.value);
    const snapshotStage = stageFromSnapshot();
    const inBattle = battleInProgress();
    let resolvedStage = inBattle
      ? (snapshotStage || resolveStageQuery(state.stage) || typedStage)
      : (typedStage || resolveStageQuery(state.stage));
    if (!resolvedStage) {
      resolvedStage = activeStageCatalog()[0] || null;
    }
    if (inBattle && snapshotStage) {
      state.stage = String(snapshotStage.id || state.stage);
      state.stageSynced = true;
      if (snapshotStage.topicId) {
        state.stageMode = "rogue";
        state.rogueTopic = String(snapshotStage.topicId);
      } else {
        state.stageMode = "normal";
      }
    }
    if (resolvedStage) state.stage = String(resolvedStage.id || state.stage);
    const topic = activeRogueTopic();
    if (topic && state.rogueTopic !== String(topic.id)) {
      state.rogueTopic = String(topic.id);
    }
    if (document.activeElement !== dom.stageMode) {
      dom.stageMode.value = state.stageMode;
    }
    dom.rogueTopic.hidden = state.stageMode !== "rogue" || !ROGUE_TOPICS.length;
    if (!dom.rogueTopic.hidden) {
      const options = ROGUE_TOPICS.map((item) =>
        `<option value="${esc(item.id)}">${esc(item.name || item.id)}</option>`).join("");
      if (dom.rogueTopic.__bbOptions !== options &&
          document.activeElement !== dom.rogueTopic) {
        dom.rogueTopic.innerHTML = options;
        dom.rogueTopic.__bbOptions = options;
      }
      if (topic && document.activeElement !== dom.rogueTopic) {
        dom.rogueTopic.value = String(topic.id);
      }
    }
    if (document.activeElement !== dom.stageInput || !state.stageDropdownOpen) {
      dom.stageInput.value = resolvedStage ? stageLabel(resolvedStage) : state.stage;
    }
    dom.stageInput.title = resolvedStage
      ? `${resolvedStage.id} · ${resolvedStage.level || ""}`
      : (state.stageMode === "rogue" ? "输入肉鸽关卡 ID、代号或名称" : "输入关卡 ID、代号或名称");
    dom.stageInput.placeholder = state.stageMode === "rogue"
      ? "输入肉鸽关卡 ID、代号或名称"
      : "输入关卡 ID、代号或名称";
    dom.stageResolvedName.textContent = resolvedStage
      ? [stageTopicName(resolvedStage), stageLabel(resolvedStage)].filter(Boolean).join(" · ")
      : "未匹配关卡，请检查 ID、代号或名称";
    dom.stageInput.disabled = locked;
    dom.stageToggle.disabled = locked;
    dom.stageMode.disabled = locked;
    dom.rogueTopic.disabled = locked;
    if (locked) closeStageDropdown();
  }

  function squadIndexOfOperator(operatorId) {
    return state.squad.findIndex((slot) => slot?.id === String(operatorId || ""));
  }

  function squadHasOperator(operatorId) {
    return squadIndexOfOperator(operatorId) >= 0;
  }

  function firstEmptySquadIndex() {
    return state.squad
      .slice(0, state.squadLimit)
      .findIndex((slot) => !slot);
  }

  function rosterEntry(id) {
    return ROSTER_BY_ID.get(String(id || "")) || null;
  }

  function eliteCapsFor(id) {
    const caps = rosterEntry(id)?.eliteCaps;
    return Array.isArray(caps) && caps.length ? caps : [50, 80, 90];
  }

  function makeSquadSlot(id) {
    const entry = rosterEntry(id);
    const caps = eliteCapsFor(id);
    const evolve = caps.length - 1;
    const skill = clampInt((entry?.defaultSkill ?? 0) + 1, 1, 3, 1);
    return {
      id: String(id),
      level: caps[evolve],
      evolve,
      potential: 0,
      trust: 100,
      skill,
      skill_levels: [10, 10, 10],
      skill_level: 10,
      module: "",
      module_level: 3,
    };
  }

  function isSquadLocked() {
    const runtime = window.BetterBattleSession;
    return runtime ? runtime.squadLocked() : (!!state.replayActive || battleInProgress());
  }

  function battleInProgress(snapshot = state.snapshot) {
    const runtime = window.BetterBattleSession;
    return runtime ? runtime.battleActive(snapshot) : false;
  }

  function syncSquadFromSnapshot(rawSquad) {
    if (!Array.isArray(rawSquad)) return false;
    const existingById = new Map(
      state.squad.filter(Boolean).map((slot) => [slot.id, slot]),
    );
    const next = Array(MAX_SLOTS).fill(null);
    rawSquad.slice(0, state.squadLimit).forEach((raw, index) => {
      const id = String(raw?.id || "");
      if (!id) return;
      const existing = existingById.get(id) || state.squad[index];
      const caps = eliteCapsFor(id);
      const evolve = clampInt(raw.evolve, 0, caps.length - 1, 0);
      const skill = clampInt(raw.skill, 1, 3, 1);
      const skillLevels = normalizeSkillLevels(
        raw.skill_levels ?? existing?.skill_levels,
        raw.skill_level ?? existing?.skill_level ?? 10,
      );
      if (raw.skill_level !== undefined) {
        skillLevels[skill - 1] = clampInt(raw.skill_level, 1, 10, 10);
      }
      const rawModule = raw.module !== undefined
        ? String(raw.module || "")
        : String(existing?.module || "");
      next[index] = {
        id,
        level: clampInt(raw.level, 1, caps[evolve], caps[evolve]),
        evolve,
        potential: clampInt(raw.potential, 0, 6, 0),
        trust: raw.trust !== undefined
          ? clampInt(raw.trust, 0, 200, 100)
          : clampInt(existing?.trust, 0, 200, 100),
        skill,
        skill_levels: skillLevels,
        skill_level: skillLevels[skill - 1],
        module: moduleFor(rawModule)?.id || rawModule,
        module_level: raw.module_level !== undefined
          ? clampInt(raw.module_level, 1, 3, 3)
          : clampInt(existing?.module_level, 1, 3, 3),
      };
    });

    if (JSON.stringify(next) === JSON.stringify(state.squad)) return false;
    const selectedId = state.squad[state.selectedSquadIndex]?.id || "";
    state.squad = next;
    const selectedIndex = selectedId
      ? next.findIndex((slot) => slot?.id === selectedId)
      : -1;
    state.selectedSquadIndex = selectedIndex >= 0
      ? selectedIndex
      : next.findIndex(Boolean);
    return true;
  }

  function serializeSquad() {
    return state.squad.slice(0, state.squadLimit).filter(Boolean).map((slot) => {
      const caps = eliteCapsFor(slot.id);
      const evolve = clampInt(slot.evolve, 0, caps.length - 1, 0);
      const level = clampInt(slot.level, 1, caps[evolve], caps[evolve]);
      const potential = clampInt(slot.potential, 0, 6, 0);
      const skill = clampInt(slot.skill, 1, 3, 1);
      const skillLevels = skillLevelsFor(slot);
      const skillLevel = skillLevels[skill - 1];
      slot.skill_level = skillLevel;
      const trust = clampInt(slot.trust, 0, 200, 100);
      const moduleId = String(
        moduleFor(slot.module)?.id || slot.module || "",
      ).trim();
      const base = `${slot.id}:${level}:${evolve}:${potential}:${skill}:${skillLevel}`;
      return `${base}:${moduleId}:${clampInt(slot.module_level, 1, 3, 3)}:${trust}`;
    }).join(",");
  }

  function catalogEntry(id) {
    return state.catalogById.get(String(id)) || null;
  }

  function operatorName(id) {
    const local = rosterEntry(id);
    if (local?.name) return local.name;
    const entry = catalogEntry(id);
    return entry?.name || String(id);
  }

  function skillCount(id) {
    const localCount = rosterEntry(id)?.skills?.length;
    if (localCount) return Math.max(1, Math.min(3, localCount));
    return Math.max(1, Math.min(3, Number(catalogEntry(id)?.skill_count) || 3));
  }

  function moduleListFor(id) {
    const local = rosterEntry(id)?.modules;
    if (Array.isArray(local) && local.length) return local;
    return modulesFor(id).map((module) => ({
      id: module.id,
      name: module.name || module.id,
      type: module.typeLabel || module.type || "MOD",
    }));
  }

  function addOperator(id, targetIndex = null) {
    const operatorId = String(id || "");
    if (!operatorId) return;
    if (isSquadLocked()) {
      toast("回放中，编队已锁定", "error");
      return;
    }
    const existingIndex = squadIndexOfOperator(operatorId);
    let target = Number.isInteger(targetIndex) ? targetIndex : firstEmptySquadIndex();
    if (target >= squadCount()) target = firstEmptySquadIndex();
    const replacing = target >= 0 && !!state.squad[target];

    if (existingIndex >= 0) {
      const slotNumber = String(existingIndex + 1).padStart(2, "0");
      state.selectedSquadIndex = existingIndex;
      renderSquad();
      closeSquadPicker();
      toast(`${operatorName(operatorId)} 已在 ${slotNumber} 号位`);
      return;
    }
    if (target < 0) {
      toast(`当前关卡编队上限 ${state.squadLimit} 人`, "error");
      return;
    }
    if (target >= state.squadLimit) {
      toast(`槽位超出当前关卡上限（${state.squadLimit} 人）`, "error");
      return;
    }

    state.squad[target] = makeSquadSlot(operatorId);
    commitSquadEdit();
    state.selectedSquadIndex = target;
    saveUi();
    renderSquad();
    renderQuickResults();
    closeSquadPicker();
    toast(`${replacing ? "已更换为" : "已加入"} ${operatorName(operatorId)}`, "ok");
  }

  function removeOperator(index) {
    if (isSquadLocked()) {
      toast("回放中，编队已锁定", "error");
      return;
    }
    if (index < 0 || index >= MAX_SLOTS) return;
    const removed = state.squad[index];
    if (!removed) return;
    state.squad[index] = null;
    const compact = state.squad.filter(Boolean);
    state.squad = compact.concat(Array(MAX_SLOTS - compact.length).fill(null));
    commitSquadEdit();
    state.selectedSquadIndex = state.squad.findIndex(Boolean);
    saveUi();
    renderSquad();
    renderQuickResults();
    toast(`已移除 ${operatorName(removed.id)}`);
  }

  function squadPortraitId(id) {
    return window.BetterBattleAssets?.portraitIdFor(id) || "";
  }

  function squadSkillRail(slot, index, locked) {
    const skills = rosterEntry(slot.id)?.skills || [];
    const activeIndex = clampInt(slot.skill, 1, Math.max(1, skills.length), 1) - 1;
    if (!skills.length) {
      return Array.from({ length: skillCount(slot.id) }, (_, skillIndex) => {
        const number = skillIndex + 1;
        return `<button class="squad-skill-slot ${activeIndex === skillIndex ? "is-active" : ""}" ` +
          `data-action="squad-skill" data-index="${index}" data-skill="${number}" ` +
          `title="技能 ${number}" ${locked ? "disabled" : ""}>${number}` +
          `<b class="squad-skill-level">${esc(skillLevelLabel(skillLevelFor(slot, number)))}</b>` +
          `</button>`;
      }).join("");
    }
    return skills.map((skill, skillIndex) => {
      const number = skillIndex + 1;
      return `<button class="squad-skill-slot ${activeIndex === skillIndex ? "is-active" : ""}" ` +
        `data-action="squad-skill" data-index="${index}" data-skill="${number}" ` +
        `title="技能 ${number} · ${esc(skill.name)}" ` +
        `aria-label="技能 ${number} ${esc(skill.name)}" ${locked ? "disabled" : ""}>` +
        `<img src="${esc(skill.icon)}" alt="" loading="lazy">` +
        `<b class="squad-skill-level">${esc(skillLevelLabel(skillLevelFor(slot, number)))}</b>` +
        `</button>`;
    }).join("");
  }

  function squadModuleStrip(slot, index, modules, locked) {
    const currentModule = moduleFor(slot.module)?.id || "";
    const noModule = !currentModule;
    const slots = [`<button class="squad-module-slot is-none ${noModule ? "is-active" : ""}" ` +
      `data-action="squad-module" data-index="${index}" data-module="" ` +
      `title="不携带模组" aria-label="不携带模组" ${locked ? "disabled" : ""}>∅</button>`];
    for (const module of modules) {
      const active = currentModule === module.id;
      const type = String(module.type || "MOD").split("-").pop();
      slots.push(`<button class="squad-module-slot ${active ? "is-active" : ""}" ` +
        `data-action="squad-module" data-index="${index}" data-module="${esc(module.id)}" ` +
        `title="${esc(module.name)} · ${esc(module.type)}" ` +
        `aria-label="${esc(module.name)}" ${locked ? "disabled" : ""}>${esc(type)}</button>`);
    }
    return slots.join("");
  }

  function squadLevelControls(slot, index, locked) {
    const caps = eliteCapsFor(slot.id);
    const maxElite = caps.length - 1;
    const evolve = clampInt(slot.evolve, 0, maxElite, 0);
    const maxLevel = caps[evolve];
    const level = clampInt(slot.level, 1, maxLevel, maxLevel);
    const potential = clampInt(slot.potential, 0, 6, 0);
    const eliteOptions = caps.map((cap, value) =>
      `<option value="${value}" ${evolve === value ? "selected" : ""}>E${value}</option>`).join("");
    const potentialOptions = Array.from({ length: 7 }, (_, value) =>
      `<option value="${value}" ${potential === value ? "selected" : ""}>${value}</option>`).join("");
    return `<div class="squad-card-stats">` +
      `<label title="精英阶段"><span>精英</span>` +
      `<select data-squad-stat="evolve" data-index="${index}" aria-label="精英阶段" ` +
      `${locked ? "disabled" : ""}>` +
      `${eliteOptions}</select></label>` +
      `<label title="当前阶段等级上限 ${maxLevel}"><span>等级</span>` +
      `<input type="number" min="1" max="${maxLevel}" value="${level}" ` +
      `data-squad-stat="level" data-index="${index}" aria-label="等级" ` +
      `${locked ? "disabled" : ""}></label>` +
      `<label title="潜能"><span>潜能</span>` +
      `<select data-squad-stat="potential" data-index="${index}" aria-label="潜能" ` +
      `${locked ? "disabled" : ""}>` +
      `${potentialOptions}</select></label></div>`;
  }

  function renderSquadSlot(slot, index) {
    const number = String(index + 1).padStart(2, "0");
    const locked = isSquadLocked();
    if (index >= state.squadLimit) {
      return `<article class="squad-slot is-locked" data-squad-index="${index}" ` +
        `aria-label="${number} 号位超出关卡编队上限">` +
        `<span class="squad-lock-index">${number}</span>` +
        `<span class="squad-lock-cross"></span>` +
        `<span class="squad-lock-label">关卡上限</span></article>`;
    }

    if (!slot) {
      return `<article class="squad-slot is-empty ${locked ? "is-replay-locked" : ""}" ` +
        `data-squad-index="${index}">` +
        `<button class="squad-empty-button" type="button" data-action="squad-open-slot" ` +
        `data-index="${index}" aria-label="为 ${number} 号位选择干员" ` +
        `${locked ? "disabled" : ""}>` +
        `<span class="squad-empty-index">${number}</span>` +
        `<span class="squad-empty-plus">+</span>` +
        `<span class="squad-empty-label">${locked ? "REPLAY LOCK" : "EMPTY SLOT"}</span>` +
        `</button></article>`;
    }

    const entry = rosterEntry(slot.id);
    const name = entry?.name || operatorName(slot.id);
    const profession = entry?.profession || "";
    const rarity = Number(entry?.rarity) || 0;
    const portraitId = squadPortraitId(slot.id);
    const modules = moduleListFor(slot.id);
    const selected = state.selectedSquadIndex === index;
    const artHtml = portraitId
      ? `<img data-asset-kind="portrait" data-asset-id="${esc(portraitId)}" ` +
        `alt="${esc(name)}半身立绘" loading="lazy">`
      : `<div class="squad-card-fallback">${esc(Array.from(name)[0] || "?")}</div>`;

    return `<article class="squad-slot is-filled ${selected ? "is-selected" : ""} ` +
      `${locked ? "is-replay-locked" : ""}" ` +
      `data-squad-index="${index}" data-operator-id="${esc(slot.id)}" ` +
      `role="${locked ? "group" : "button"}" ` +
      `${locked ? "aria-disabled=\"true\"" : "tabindex=\"0\""} ` +
      `aria-label="${locked ? "" : "更换 "}${number} 号位 ${esc(name)}">` +
      `<div class="squad-card-art">${artHtml}</div>` +
      `<div class="squad-card-shade"></div>` +
      `<div class="squad-card-topline">` +
      `<span class="squad-slot-index">${number}</span>` +
      `${profession ? `<span class="squad-class-tag">${esc(profession)}</span>` : ""}</div>` +
      `<button class="squad-card-remove" type="button" data-action="squad-remove" ` +
      `data-index="${index}" aria-label="移除 ${esc(name)}" ` +
      `${locked ? "disabled" : ""}>×</button>` +
      `<div class="squad-card-name"><strong>${esc(name)}</strong>` +
      `${rarity ? `<small>${rarity}★</small>` : ""}</div>` +
      `<div class="squad-skill-rail">${squadSkillRail(slot, index, locked)}</div>` +
      `${squadLevelControls(slot, index, locked)}` +
      `<div class="squad-module-strip">${squadModuleStrip(slot, index, modules, locked)}</div>` +
      `</article>`;
  }

  function renderSquadSelection() {
    const slot = state.squad[state.selectedSquadIndex] || null;
    if (!slot) {
      dom.squadSelectionName.textContent = "未选择干员";
      dom.squadSelectionDetail.textContent = "点击干员卡槽查看当前技能与模组";
      setHtml(dom.squadSelectionEditor, "");
      return;
    }
    const entry = rosterEntry(slot.id);
    const skills = entry?.skills || [];
    const skillIndex = clampInt(slot.skill, 1, Math.max(1, skills.length), 1) - 1;
    const skill = skills[skillIndex];
    const module = slot.module
      ? moduleListFor(slot.id).find((item) => item.id === slot.module)
      : null;
    const caps = eliteCapsFor(slot.id);
    const evolve = clampInt(slot.evolve, 0, caps.length - 1, 0);
    const level = clampInt(slot.level, 1, caps[evolve], caps[evolve]);
    const potential = clampInt(slot.potential, 0, 6, 0);
    const trust = clampInt(slot.trust, 0, 200, 100);
    const levels = skillLevelsFor(slot);
    const locked = isSquadLocked();
    dom.squadSelectionName.textContent = entry?.name || operatorName(slot.id);
    dom.squadSelectionDetail.textContent = [
      `E${evolve} Lv.${level} 潜 ${potential} 信赖 ${trust}%`,
      skill
        ? `技能 ${skillIndex + 1} · ${skill.name} · ${skillLevelLabel(levels[skillIndex])}`
        : `技能 ${slot.skill}`,
      module ? `模组 ${module.type} · ${module.name}` : "不携带模组",
    ].join("    /    ");
    const skillButtons = skills.map((item, index) => {
      const number = index + 1;
      return `<button class="squad-selection-skill ${skillIndex === index ? "is-active" : ""}" ` +
        `type="button" data-action="squad-skill" data-index="${state.selectedSquadIndex}" ` +
        `data-skill="${number}" title="${esc(item.name)}" ` +
        `aria-label="技能 ${number} ${esc(item.name)}" ${locked ? "disabled" : ""}>` +
        `<img src="${esc(item.icon)}" alt="" loading="lazy"></button>`;
    }).join("");
    const skillLevelOptions = Array.from({ length: 10 }, (_, index) => {
      const value = index + 1;
      return `<option value="${value}" ${levels[skillIndex] === value ? "selected" : ""}>` +
        `${esc(skillLevelLabel(value))}</option>`;
    }).join("");
    const moduleLevelOptions = Array.from({ length: 3 }, (_, index) => {
      const value = index + 1;
      return `<option value="${value}" ${clampInt(slot.module_level, 1, 3, 3) === value ? "selected" : ""}>` +
        `${value}</option>`;
    }).join("");
    const skillRow = skills.length
      ? `<span class="squad-selection-skills-row">` +
        `<span class="squad-selection-control-label">技能</span>` +
        `<span class="squad-selection-skills">${skillButtons}</span></span>`
      : "";
    const tuners = `<span class="squad-selection-tuners">` +
      `<label class="squad-selection-tuner">` +
      `<span>技能等级</span>` +
      `<select data-squad-tuner="skill-level" data-index="${state.selectedSquadIndex}" ` +
      `aria-label="技能等级" ${locked ? "disabled" : ""}>${skillLevelOptions}</select></label>` +
      `<label class="squad-selection-tuner ${module ? "" : "is-disabled"}">` +
      `<span>模组等级</span>` +
      `<select data-squad-tuner="module-level" data-index="${state.selectedSquadIndex}" ` +
      `aria-label="模组等级" ${locked || !module ? "disabled" : ""}>` +
      `${moduleLevelOptions}</select></label>` +
      `<label class="squad-selection-tuner">` +
      `<span>信赖</span>` +
      `<span class="squad-selection-trust">` +
      `<input type="number" min="0" max="200" step="1" value="${trust}" ` +
      `data-squad-tuner="trust" data-index="${state.selectedSquadIndex}" ` +
      `aria-label="信赖百分比" ${locked ? "disabled" : ""}><b>%</b></span></label>` +
      `</span>`;
    setHtml(
      dom.squadSelectionEditor,
      skillRow + tuners,
    );
  }

  function renderSquad() {
    const locked = isSquadLocked();
    renderStageField(locked);
    dom.squadCount.textContent = `${squadCount()} / ${state.squadLimit}`;
    dom.squadLimitHint.textContent =
      `当前关卡上限 ${state.squadLimit} 人，共 ${MAX_SLOTS} 个编队槽位。`;
    dom.squadReplayLock.hidden = !locked;
    const sessionPhase = window.BetterBattleSession?.phase();
    dom.squadReplayLock.textContent = sessionPhase === "replay"
      ? "回放中 · 编队已锁定"
      : (sessionPhase === "starting"
        ? "准备中 · 编队已同步并锁定"
        : "战斗中 · 编队已同步并锁定");
    dom.squadStartButton.textContent =
      state.stageMode === "rogue" ? "进入配置" : "进入战斗";
    dom.squadStartButton.disabled = locked;
    dom.squadClearButton.disabled = locked;
    dom.squadAddButton.disabled = locked;
    dom.squadBoard.classList.toggle("is-replay-locked", locked);
    dom.squadRowTop.innerHTML = state.squad.slice(0, 7)
      .map((slot, index) => renderSquadSlot(slot, index)).join("");
    dom.squadRowBottom.innerHTML = state.squad.slice(7, MAX_SLOTS)
      .map((slot, index) => renderSquadSlot(slot, index + 7)).join("");
    window.BetterBattleAssets?.observe(dom.squadBoard);
    renderSquadSelection();
    renderRosterPreview();
  }

  function renderSquadPicker() {
    if (!dom.squadPickerList) return;
    const query = state.pickerQuery.trim().toLowerCase();
    const list = ROSTER_CATALOG.filter((entry) =>
      !query || `${entry.name || ""} ${entry.id || ""}`.toLowerCase().includes(query));
    dom.squadPickerCount.textContent = `${list.length} 名`;

    const target = Number.isInteger(state.pickerTarget) ? state.pickerTarget : null;
    if (target == null) {
      dom.squadPickerTitle.textContent = "干员目录";
      dom.squadPickerTarget.textContent = firstEmptySquadIndex() >= 0
        ? `选择干员加入 ${String(firstEmptySquadIndex() + 1).padStart(2, "0")} 号空位`
        : `当前关卡编队上限 ${state.squadLimit} 人`;
    } else {
      const current = state.squad[target];
      dom.squadPickerTitle.textContent = current ? "更换干员" : "加入干员";
      dom.squadPickerTarget.textContent =
        `${current ? "更换" : "填入"} ${String(target + 1).padStart(2, "0")} 号位` +
        (current ? ` · 当前 ${operatorName(current.id)}` : "");
    }

    if (!ROSTER_CATALOG.length) {
      dom.squadPickerList.innerHTML =
        `<div class="squad-picker-empty">等待本地干员数据</div>`;
      return;
    }
    if (!list.length) {
      dom.squadPickerList.innerHTML =
        `<div class="squad-picker-empty">没有匹配的干员</div>`;
      return;
    }

    dom.squadPickerList.innerHTML = list.map((entry) => {
      const slotIndex = squadIndexOfOperator(entry.id);
      const rostered = slotIndex >= 0;
      const moduleLabel = entry.modules?.length ? `${entry.modules.length} 个模组` : "无模组";
      const stateLabel = rostered
        ? String(slotIndex + 1).padStart(2, "0")
        : "ADD";
      return `<button class="squad-picker-item ${rostered ? "is-rostered" : ""}" ` +
        `type="button" data-action="squad-picker-select" data-id="${esc(entry.id)}">` +
        `<span class="squad-picker-avatar">` +
        `<img data-asset-kind="avatar" data-asset-id="${esc(entry.id)}" ` +
        `alt="" loading="lazy"></span>` +
        `<span class="squad-picker-copy"><strong>${esc(entry.name)}</strong>` +
        `<span>${esc(entry.rarity)}★ · ${esc(entry.profession)} · ` +
        `${esc(entry.skills?.length || 0)} 技能 · ${esc(moduleLabel)}</span>` +
        `<code>${esc(entry.id)}</code></span>` +
        `<span class="squad-picker-state">${stateLabel}</span></button>`;
    }).join("");
    window.BetterBattleAssets?.observe(dom.squadPickerList);
  }

  function openSquadPicker(targetIndex = null) {
    if (isSquadLocked()) {
      toast("回放中，编队已锁定", "error");
      return;
    }
    state.pickerOpen = true;
    state.pickerTarget = Number.isInteger(targetIndex) ? targetIndex : null;
    state.pickerQuery = "";
    dom.squadPickerSearch.value = "";
    renderSquadPicker();
    dom.squadPicker.hidden = false;
    requestAnimationFrame(() => dom.squadPickerSearch.focus());
  }

  function closeSquadPicker() {
    if (!state.pickerOpen) return;
    state.pickerOpen = false;
    state.pickerTarget = null;
    state.pickerQuery = "";
    dom.squadPicker.hidden = true;
  }

  function renderRosterPreview() {
    const roster = serializeSquad();
    dom.rosterPreview.textContent = roster || "为空";
  }

  function renderQuickResults() {
    if (!dom.quickResults || !dom.quickSearch) return;
    const locked = isSquadLocked();
    if (!state.catalog.length) {
      dom.quickResults.innerHTML = `<div class="empty-small">目录加载后可按名称搜索</div>`;
      return;
    }
    const query = dom.quickSearch.value.trim().toLowerCase();
    const list = query
      ? state.catalog.filter((entry) =>
          `${entry.name || ""} ${entry.id || ""}`.toLowerCase().includes(query)).slice(0, 30)
      : state.catalog.slice(0, 10);
    dom.quickResults.innerHTML = list.length
      ? list.map((entry) => {
          const exists = squadHasOperator(entry.id);
          return `<div class="quick-item"><div><strong>${esc(entry.name || entry.id)}</strong>` +
            `<code>${esc(entry.id)} · ${esc(entry.skill_count)} 技能</code></div>` +
            `<button class="btn tiny ${exists || locked ? "ghost" : "primary"}" data-action="catalog-add" ` +
            `data-id="${esc(entry.id)}" ${exists || locked ? "disabled" : ""}>` +
            `${locked ? "回放锁定" : (exists ? "已编入" : "加入")}</button></div>`;
        }).join("")
      : `<div class="empty-small">没有匹配干员</div>`;
  }

  pages.squad = {
    stageLabel,
    activeRogueTopic,
    activeStageCatalog,
    stageById,
    stageTopicName,
    filterStages,
    resolveStageQuery,
    stageFromSnapshot,
    currentStageId,
    selectedStage,
    stageInputShowsSelectedLabel,
    positionStageOptions,
    renderStageOptions,
    openStageDropdown,
    closeStageDropdown,
    selectStage,
    moveStageActive,
    renderStageField,
    squadIndexOfOperator,
    squadHasOperator,
    firstEmptySquadIndex,
    rosterEntry,
    eliteCapsFor,
    makeSquadSlot,
    isSquadLocked,
    battleInProgress,
    syncSquadFromSnapshot,
    serializeSquad,
    catalogEntry,
    operatorName,
    skillCount,
    moduleListFor,
    addOperator,
    removeOperator,
    squadPortraitId,
    squadSkillRail,
    squadModuleStrip,
    squadLevelControls,
    renderSquadSlot,
    renderSquadSelection,
    render: renderSquad,
    renderSquadPicker,
    openSquadPicker,
    closeSquadPicker,
    renderRosterPreview,
    renderQuickResults,
  };
})();
