"use strict";

  function configTopicData(topicId) {
    return CONFIG_BY_TOPIC.get(String(topicId || "")) || CONFIG_TOPICS[0] || null;
  }

  function configEntry(topicId) {
    const topic = configTopicData(topicId);
    if (!topic) {
      return {
        mode: 2,
        grade: 0,
        gold: 0,
        scrap: -1,
        scrapLimit: -1,
        relics: [],
        special: [],
        charBuffs: [],
        masters: [],
        copper: [],
        fragments: [],
        totems: [],
        concepts: [],
        tech: [],
      };
    }
    const all = state.battleConfig && typeof state.battleConfig === "object"
      ? state.battleConfig
      : {};
    const raw = all[topic.id] && typeof all[topic.id] === "object"
      ? all[topic.id]
      : {};
    const difficulty = (topic.difficulties || [])[0] || { mode: 2, grade: 0 };
    return {
      mode: clampInt(raw.mode, 1, 16, Number(difficulty.mode) || 2),
      grade: clampInt(raw.grade, 0, 18, Number(difficulty.grade) || 0),
      gold: clampInt(raw.gold, 0, 9999, 0),
      scrap: raw.scrap === undefined || raw.scrap === null || raw.scrap === ""
        ? -1
        : clampInt(raw.scrap, 0, 9999, -1),
      scrapLimit: raw.scrapLimit === undefined || raw.scrapLimit === null ||
        raw.scrapLimit === ""
        ? -1
        : clampInt(raw.scrapLimit, 0, 9999, 10),
      relics: Array.isArray(raw.relics)
        ? raw.relics
          .filter((entry) => entry && entry.id)
          .map((entry) => ({
            id: String(entry.id),
            layer: clampInt(entry.layer, 1, 999, 1),
          }))
        : [],
      special: Array.isArray(raw.special)
        ? raw.special.filter((entry) => entry && entry.id && entry.kind).slice(0, 32)
        : [],
      charBuffs: Array.isArray(raw.charBuffs)
        ? raw.charBuffs
          .filter((entry) => entry && entry.id)
          .map((entry) => ({
            id: String(entry.id),
            layer: clampInt(entry.layer, 0, 9, 1),
            targets: Array.isArray(entry.targets)
              ? Array.from(new Set(entry.targets.map(String))).slice(0, 14)
              : [],
          }))
          .slice(0, 8)
        : [],
      masters: Array.isArray(raw.masters)
        ? raw.masters
          .filter((item) => item && item.charId && item.masterId)
          .map((item) => ({
            charId: String(item.charId),
            masterId: String(item.masterId),
            level: clampInt(item.level, 1, 3, 1),
            zone: clampInt(item.zone, 0, 4, 0),
          }))
          .slice(0, 8)
        : [],
      copper: Array.isArray(raw.copper)
        ? Array.from(new Set(raw.copper.map(String))).slice(0, 12)
        : [],
      fragments: Array.isArray(raw.fragments)
        ? Array.from(new Set(raw.fragments.map(String))).slice(0, 12)
        : [],
      totems: Array.isArray(raw.totems)
        ? Array.from(new Set(raw.totems.map(String))).slice(0, 12)
        : [],
      concepts: Array.isArray(raw.concepts)
        ? raw.concepts
          .map((item) => {
            if (typeof item === "string") {
              return item ? { id: item, layer: 1 } : null;
            }
            if (!item || !item.id) return null;
            return {
              id: String(item.id),
              layer: clampInt(item.layer, 1, 99, 1),
            };
          })
          .filter(Boolean)
        : [],
      tech: Array.isArray(raw.tech)
        ? Array.from(new Set(raw.tech.map(String)))
        : [],
    };
  }

  function normalizeBattleConfig(raw) {
    const result = {};
    if (!raw || typeof raw !== "object") return result;
    for (const topic of CONFIG_TOPICS) {
      const entry = raw[topic.id];
      if (!entry || typeof entry !== "object") continue;
      const difficulty = (topic.difficulties || [])[0] || { mode: 2, grade: 0 };
      result[topic.id] = {
        mode: clampInt(entry.mode, 1, 16, Number(difficulty.mode) || 2),
        grade: clampInt(entry.grade, 0, 18, Number(difficulty.grade) || 0),
        gold: clampInt(entry.gold, 0, 9999, 0),
        scrap: entry.scrap === undefined || entry.scrap === null ||
          entry.scrap === ""
          ? -1
          : clampInt(entry.scrap, 0, 9999, -1),
        scrapLimit: entry.scrapLimit === undefined || entry.scrapLimit === null ||
          entry.scrapLimit === ""
          ? -1
          : clampInt(entry.scrapLimit, 0, 9999, 10),
        relics: Array.isArray(entry.relics)
          ? entry.relics
            .filter((item) => item && item.id)
            .map((item) => ({
              id: String(item.id),
              layer: clampInt(item.layer, 1, 999, 1),
            }))
            .filter((item) => (topic.relics || [])
              .some((relic) => String(relic.id) === item.id))
          : [],
        special: Array.isArray(entry.special)
          ? entry.special
            .filter((item) => item && item.id && item.kind)
            .map((item) => {
              const next = {
                kind: String(item.kind),
                id: String(item.id),
                layer: clampInt(item.layer, 0, 9, 1),
              };
              if (item.normalId) next.normalId = String(item.normalId);
              return next;
            })
            .slice(0, 32)
          : [],
        charBuffs: Array.isArray(entry.charBuffs)
          ? entry.charBuffs
            .filter((item) => item && item.id)
            .map((item) => ({
              id: String(item.id),
              layer: clampInt(item.layer, 0, 9, 1),
              targets: Array.isArray(item.targets)
                ? Array.from(new Set(item.targets.map(String))).slice(0, 14)
                : [],
            }))
            .slice(0, 8)
          : [],
        masters: Array.isArray(entry.masters)
          ? entry.masters
            .filter((item) => item && item.charId && item.masterId)
            .map((item) => ({
              charId: String(item.charId),
              masterId: String(item.masterId),
              level: clampInt(item.level, 1, 3, 1),
              zone: clampInt(item.zone, 0, 4, 0),
            }))
          : [],
        copper: Array.isArray(entry.copper)
          ? Array.from(new Set(entry.copper.map(String)))
            .filter((id) => (topic.copper || [])
              .some((item) => String(item.id) === id))
            .slice(0, 12)
          : [],
        fragments: Array.isArray(entry.fragments)
          ? Array.from(new Set(entry.fragments.map(String))).slice(0, 12)
          : [],
        totems: Array.isArray(entry.totems)
          ? Array.from(new Set(entry.totems.map(String))).slice(0, 12)
          : [],
        concepts: Array.isArray(entry.concepts)
          ? entry.concepts
            .map((item) => {
              if (typeof item === "string") {
                return item ? { id: item, layer: 1 } : null;
              }
              if (!item || !item.id) return null;
              return {
                id: String(item.id),
                layer: clampInt(item.layer, 1, 99, 1),
              };
            })
            .filter(Boolean)
          : [],
        tech: Array.isArray(entry.tech)
          ? Array.from(new Set(entry.tech.map(String)))
          : [],
      };
    }
    return result;
  }

  function normalizeSkillLevels(raw, fallback = 10) {
    const fallbackLevel = clampInt(fallback, 1, 10, 10);
    const levels = Array.isArray(raw) ? raw : [];
    return Array.from({ length: 3 }, (_, index) =>
      clampInt(levels[index], 1, 10, fallbackLevel));
  }

  function skillLevelsFor(slot) {
    return normalizeSkillLevels(slot?.skill_levels, slot?.skill_level);
  }

  function skillLevelFor(slot, skillNumber) {
    const level = clampInt(skillNumber, 1, 3, 1) - 1;
    return skillLevelsFor(slot)[level];
  }

  function skillLevelLabel(level) {
    const value = clampInt(level, 1, 10, 10);
    return value >= 8 ? ["专一", "专二", "专三"][value - 8] : String(value);
  }

  function truncate(text, limit) {
    return text.length > limit ? text.slice(0, limit) + "\n…（已截断）" : text;
  }

  function configIconMarkup(entry, size = "") {
    const fallback = String(entry?.name || entry?.id || "?")
      .trim()
      .slice(0, 1)
      .toUpperCase();
    const image = entry?.icon
      ? `<img src="assets/game/roguelike/${esc(entry.icon)}" alt="" loading="lazy"
          onerror="this.remove()">`
      : "";
    const rarity = Number(entry?.rarity || 0);
    const bar = rarity >= 1 && rarity <= 6
      ? `<span class="config-rarity r${rarity}"></span>`
      : "";
    return `<span class="config-icon ${size}"><span>${esc(fallback)}</span>${image}${bar}</span>`;
  }

  function configDifficultyLabel(difficulty) {
    const name = String(difficulty?.title || difficulty?.name || "难度").trim();
    const grade = Number(difficulty?.grade || 0);
    return grade > 0 ? `${name} · ${grade}` : name;
  }

  function renderConfigTopicList(topicId) {
    const node = document.getElementById("configTopicList");
    if (!node) return;
    setHtml(node, CONFIG_TOPICS.map((topic, index) => `
      <button class="config-topic-button ${topic.id === topicId ? "is-active" : ""}"
        data-action="config-topic" data-topic="${esc(topic.id)}" type="button">
        <i>${String(index + 1).padStart(2, "0")}</i>
        <span>${esc(topic.name)}</span>
      </button>
    `).join(""));
  }

  function renderConfigDifficulty(topic, entry) {
    const select = document.getElementById("configDifficulty");
    const note = document.getElementById("configDifficultyDesc");
    if (!select || !note) return;
    const difficulties = Array.isArray(topic.difficulties) ? topic.difficulties : [];
    const key = `${entry.mode}:${entry.grade}`;
    const options = difficulties.map((difficulty) => {
      const value = `${difficulty.mode}:${difficulty.grade}`;
      return `<option value="${value}">${esc(configDifficultyLabel(difficulty))}</option>`;
    }).join("");
    if (select.__bbOptions !== options) {
      select.innerHTML = options;
      select.__bbOptions = options;
    }
    const known = difficulties.some(
      (difficulty) => `${difficulty.mode}:${difficulty.grade}` === key);
    select.value = known ? key : (difficulties[0]
      ? `${difficulties[0].mode}:${difficulties[0].grade}`
      : "");
    const selected = difficulties.find(
      (difficulty) => `${difficulty.mode}:${difficulty.grade}` === select.value);
    const details = []
      .concat(selected?.desc || [])
      .concat(selected?.ruleDesc || [])
      .concat(selected?.addDesc || [])
      .filter(Boolean);
    note.textContent = details.length
      ? details.join(" · ")
      : "该难度没有额外的战斗规则说明。";
  }

  function renderConfigSummary(topic, entry) {
    const summary = document.getElementById("configSummary");
    const badge = document.getElementById("configSummaryBadge");
    if (summary) {
      setHtml(summary, `
        <div class="config-summary-cell"><strong>${entry.relics.length}</strong><span>藏品</span></div>
        <div class="config-summary-cell"><strong>${selectedSpecialCount(entry)}</strong><span>机制</span></div>
        <div class="config-summary-cell"><strong>${entry.tech.length}</strong><span>科技</span></div>
      `);
    }
    if (badge) {
      const difficulty = (topic.difficulties || []).find(
        (item) => item.mode === entry.mode && item.grade === entry.grade);
      badge.textContent = `${topic.name} · ${configDifficultyLabel(difficulty || entry)}`;
    }
  }

  function renderConfigResources(topic, entry) {
    const badge = document.getElementById("configResourceBadge");
    const title = document.getElementById("configResourceTitle");
    const grid = document.getElementById("configResourceGrid");
    const keys = Array.isArray(topic.resourceKeys) && topic.resourceKeys.length
      ? topic.resourceKeys
      : ["gold"];
    if (title) title.textContent = "局外信息";
    for (const field of document.querySelectorAll("[data-resource-field]")) {
      field.hidden = !keys.includes(field.dataset.resourceField);
    }
    if (grid) {
      grid.style.gridTemplateColumns =
        `repeat(${Math.max(1, keys.length)}, minmax(0, 1fr))`;
    }
    const fields = [
      ["gold", document.getElementById("configResourceGold"), entry.gold, ""],
      ["scrap", document.getElementById("configResourceScrap"), entry.scrap,
        "按藏品推导"],
      ["scrapLimit", document.getElementById("configResourceScrapLimit"),
        entry.scrapLimit, "默认 10"],
    ];
    for (const [, input, value, placeholder] of fields) {
      if (!input) continue;
      const next = value === -1 ? "" : String(value);
      if (document.activeElement !== input && input.value !== next) {
        input.value = value === -1 ? "" : next;
      }
      input.placeholder = placeholder || "";
    }
    if (badge) {
      const parts = [];
      if (keys.includes("gold") && entry.gold > 0) {
        parts.push(`源石锭 ${entry.gold}`);
      }
      if (keys.includes("scrap") && entry.scrap >= 0) {
        parts.push(`零件 ${entry.scrap}`);
      }
      if (keys.includes("scrapLimit") && entry.scrapLimit >= 0) {
        parts.push(`上限 ${entry.scrapLimit}`);
      }
      badge.textContent = parts.length ? parts.join(" · ") : "默认值";
    }
  }

  function renderConfigMasteries(topic, entry) {
    const panel = document.getElementById("configMasteryPanel");
    const list = document.getElementById("configMasteryList");
    const badge = document.getElementById("configMasteryBadge");
    if (!panel || !list || !badge) return;
    const operators = Array.isArray(topic.specialOperators)
      ? topic.specialOperators
      : [];
    panel.hidden = operators.length === 0;
    if (!operators.length) return;
    const squad = new Set(
      state.squad
        .slice(0, state.squadLimit)
        .filter(Boolean)
        .map((slot) => String(slot.id)));
    let count = 0;
    setHtml(list, operators.map((operator) => {
      const inSquad = squad.has(String(operator.charId));
      const rows = (operator.masteries || []).map((mastery) => {
        const selected = entry.masters.find((item) =>
          item.charId === operator.charId &&
          item.masterId === mastery.id);
        if (selected && inSquad) ++count;
        const level = selected?.level || 1;
        const levelInfo = mastery.levels.find(
          (item) => item.level === level) || mastery.levels[0];
        const chain = mastery.mechanism
          ? (mastery.chains?.[level] || [])
          : [];
        const zoneCount = chain.length;
        const zone = clampInt(selected?.zone, 0, zoneCount || 4, 0);
        const disabled = !inSquad ||
          (!mastery.injectable && !mastery.mechanism);
        const tag = mastery.mechanism
          ? ' <em>机制 · 按层注入</em>'
          : (mastery.injectable ? "" : ' <em>未接入</em>');
        const zoneMark = mastery.mechanism && selected && zone > 0
          ? ` · 已注入 ${zone} 层`
          : "";
        return `
          <label class="config-mastery-row ${disabled ? "is-disabled" : ""}">
            <input type="checkbox" data-config-mastery-toggle
              data-char="${esc(operator.charId)}"
              data-master="${esc(mastery.id)}"
              ${selected ? "checked" : ""} ${disabled ? "disabled" : ""}>
            <span class="config-mastery-copy">
              <strong>${esc(mastery.name)}${tag}</strong>
              <span>${esc(levelInfo?.description || "")}${esc(zoneMark)}</span>
            </span>
            <span class="config-mastery-controls">
              <select data-config-mastery-level
                data-char="${esc(operator.charId)}"
                data-master="${esc(mastery.id)}"
                ${!selected || disabled ? "disabled" : ""}>
                ${mastery.levels.map((item) => `
                  <option value="${item.level}"
                    ${item.level === level ? "selected" : ""}>${item.level} 级</option>
                `).join("")}
              </select>
              ${mastery.mechanism ? `
                <select data-config-mastery-zone
                  data-char="${esc(operator.charId)}"
                  data-master="${esc(mastery.id)}"
                  ${!selected || disabled ? "disabled" : ""}>
                  ${Array.from({ length: zoneCount + 1 }, (_, index) => index)
                    .map((value) => `
                      <option value="${value}"
                        ${value === zone ? "selected" : ""}>${value} 层</option>
                    `).join("")}
                </select>` : ""}
            </span>
          </label>`;
      }).join("");
      return `
        <div class="config-mastery-op ${inSquad ? "" : "is-locked"}">
          <div class="config-mastery-op-head">
            ${operator.charId
              ? `<img class="config-mastery-avatar"
                  data-asset-kind="avatar"
                  data-asset-id="${esc(operator.charId)}" alt="">`
              : ""}
            <div>
              <strong>${esc(operator.name)}</strong>
              <span>${inSquad ? "编队中" : "编队中未编入"}</span>
            </div>
          </div>
          <div class="config-mastery-rows">${rows}</div>
        </div>`;
    }).join(""));
    window.BetterBattleAssets?.observe(list);
    badge.textContent = `${count} 项`;
  }

  function renderConfigRelics(topic, entry) {
    const grid = document.getElementById("configRelicGrid");
    const empty = document.getElementById("configRelicEmpty");
    const count = document.getElementById("configRelicCount");
    const chips = document.getElementById("configSelectedRelics");
    if (!grid || !empty || !count || !chips) return;
    count.textContent = String(entry.relics.length);
    const selected = new Map(entry.relics.map((item) => [String(item.id), item]));
    const relicById = CONFIG_RELIC_BY_TOPIC.get(topic.id) || new Map();
    setHtml(chips, entry.relics.map((item) => {
      const relic = relicById.get(String(item.id));
      if (!relic) return "";
      const stack = relic.stack;
      const layerControl = stack
        ? `<span class="config-stack-control">
            <button type="button" data-action="config-relic-layer"
              data-id="${esc(item.id)}" data-delta="-1"
              aria-label="减少 ${esc(relic.name)} ${esc(stack.label)}">−</button>
            <input type="number" data-config-relic-layer data-id="${esc(item.id)}"
              min="1" max="${Number(stack.max) || 999}" step="1"
              value="${clampInt(item.layer, 1, Number(stack.max) || 999, 1)}"
              aria-label="${esc(relic.name)} ${esc(stack.label)}">
            <button type="button" data-action="config-relic-layer"
              data-id="${esc(item.id)}" data-delta="1"
              aria-label="增加 ${esc(relic.name)} ${esc(stack.label)}">+</button>
          </span>`
        : "";
      return `
        <span class="config-selected-chip ${stack ? "is-stacked" : ""}">
          ${esc(relic.name)}
          ${layerControl}
          <button type="button" data-action="config-relic-remove"
            data-id="${esc(item.id)}" aria-label="移除 ${esc(relic.name)}">×</button>
        </span>`;
    }).join(""));
    const query = state.configRelicQuery.trim().toLowerCase();
    const rarity = state.configRarity;
    const rows = [];
    for (const relic of topic.relics || []) {
      if (rarity !== "all" && Number(relic.rarity) !== Number(rarity)) continue;
      if (query) {
        const haystack = `${relic.name} ${relic.usage} ${relic.desc} ${relic.id}`.toLowerCase();
        if (!haystack.includes(query)) continue;
      }
      rows.push(relic);
      if (rows.length >= 1000) break;
    }
    empty.hidden = rows.length > 0;
    setHtml(grid, rows.map((relic) => `
      <button type="button" class="config-relic-card ${selected.has(String(relic.id)) ? "is-selected" : ""}"
        data-action="config-relic-toggle" data-id="${esc(relic.id)}"
        title="${esc(relic.usage || relic.desc || relic.id)}">
        ${configIconMarkup(relic)}
        <span class="config-relic-copy">
          <strong>${esc(relic.name)}</strong>
          <span>${esc(relic.usage || relic.desc || "无效果说明")}</span>
          <code>${esc(relic.id)}${relic.stack
            ? ` · ${esc(relic.stack.label)} 1-${Number(relic.stack.max) || 999}`
            : ""}</code>
        </span>
        ${selected.has(String(relic.id)) ? '<span class="config-selected-mark">✓</span>' : ""}
      </button>
    `).join(""));
  }

  function activeConfigSpecialGroup(topic) {
    const groups = Array.isArray(topic.special) ? topic.special : [];
    if (!groups.length) return null;
    return groups.find((group) => group.kind === state.configSpecialKind) || groups[0];
  }

  function specialFamilyId(kind, id) {
    const text = String(id || "");
    if (!MERGED_SPECIAL_KINDS.has(kind)) return text;
    return text.replace(/_([abcd])(?:_\d+)?$/, "");
  }

  function specialLevelIndex(id) {
    const text = String(id || "");
    const match = text.match(/_([abcd])(?:_\d+)?$/);
    return match ? { a: 0, b: 1, c: 2, d: 3 }[match[1]] : 0;
  }

  function specialVariantSlot(id) {
    const match = String(id || "").match(/_([abcd])(_\d+)?$/);
    return match ? (match[2] || "") : "";
  }

  function specialVariantLabel(item) {
    const text = String(item?.detail || "").trim();
    const cut = text.split("干员")[0]
      .replace(/[，,、]$/, "")
      .replace(/的$/, "")
      .trim();
    if (cut && cut !== text && cut.length <= 18) return cut;
    if (item?.awakened) return "醒觉";
    return item?.levelName || item?.name || item?.id || "";
  }

  function specialFamilyKey(group, item) {
    if (item?.group) return String(item.group);
    return specialFamilyId(group.kind, item?.id);
  }

  function specialSelectionFamily(group, selectionId) {
    const text = String(selectionId || "");
    const family = specialFamilies(group).find((candidate) =>
      candidate.id === text ||
      candidate.items.some((item) => String(item.id) === text));
    return family?.id || specialFamilyId(group.kind, text);
  }

  function specialTierForGrade(grade) {
    const value = Number(grade) || 0;
    if (value >= 12) return 2;
    if (value >= 6) return 1;
    return 0;
  }

  function specialFamilies(group) {
    const families = [];
    const byId = new Map();
    for (const item of group?.items || []) {
      const id = specialFamilyKey(group, item);
      let family = byId.get(id);
      if (!family) {
        family = {
          id,
          kind: group.kind,
          name: item.name,
          detail: item.detail,
          desc: item.desc,
          icon: item.icon,
          iconId: item.iconId,
          items: [],
        };
        byId.set(id, family);
        families.push(family);
      }
      family.items.push(item);
      if (!family.detail && item.detail) family.detail = item.detail;
      if (!family.icon && item.icon) {
        family.icon = item.icon;
        family.iconId = item.iconId;
      }
    }
    for (const family of families) {
      family.injectable = family.items.some((item) => item.injectable !== false);
    }
    return families;
  }

  function resolveSpecialItem(group, selectionId, grade) {
    const familyId = specialSelectionFamily(group, selectionId);
    const family = specialFamilies(group).find((item) => item.id === familyId);
    if (!family) return null;
    const direct = family.items.find(
      (item) => String(item.id) === String(selectionId));
    if (direct?.awakened) return direct;
    if (!MERGED_SPECIAL_KINDS.has(group.kind) || family.items.length <= 1) {
      return direct || family.items[0] || null;
    }
    const tier = specialTierForGrade(grade);
    const tierItems = family.items.filter(
      (item) => specialLevelIndex(item.id) === tier);
    const concreteItems = tierItems.filter(
      (item) => specialVariantSlot(item.id) !== "");
    const pool = concreteItems.length ? concreteItems : tierItems;
    const exact = tierItems.find(
      (item) => String(item.id) === String(selectionId));
    if (exact && (concreteItems.length === 0 ||
      specialVariantSlot(exact.id) !== "")) {
      return exact;
    }
    const slot = specialVariantSlot(selectionId);
    if (!slot) return pool[0] || tierItems[0] || family.items[0] || null;
    return pool.find((item) => specialVariantSlot(item.id) === slot) ||
      pool[0] ||
      tierItems[0] ||
      family.items[0] ||
      null;
  }

  function selectedSpecialCount(entry) {
    const seen = new Set();
    for (const selection of entry?.special || []) {
      seen.add(`${selection.kind}:${specialFamilyId(selection.kind, selection.id)}`);
    }
    return seen.size;
  }

  function renderConfigSpecials(topic, entry) {
    const tabs = document.getElementById("configSpecialTabs");
    const grid = document.getElementById("configSpecialGrid");
    const badge = document.getElementById("configSpecialBadge");
    const title = document.getElementById("configSpecialTitle");
    if (!tabs || !grid || !badge || !title) return;
    const groups = Array.isArray(topic.special) ? topic.special : [];
    title.textContent = topic.specialLabel || "主题特殊机制";
    badge.textContent = `${selectedSpecialCount(entry)} 项`;
    if (!groups.length) {
      setHtml(tabs, "");
      setHtml(grid, '<div class="empty-small">该主题没有可配置的特殊机制。</div>');
      return;
    }
    const group = activeConfigSpecialGroup(topic);
    if (state.configSpecialKind !== group.kind) {
      state.configSpecialKind = group.kind;
    }
    const groupInjectable = group.injectable !== false;
    setHtml(tabs, groups.map((item) => `
      <button type="button" data-action="config-special-tab"
        data-kind="${esc(item.kind)}"
        class="${item.kind === group.kind ? "is-active" : ""}">
        ${esc(item.label)} · ${specialFamilies(item).length}${item.injectable === false ? " · 仅展示" : ""}
      </button>
    `).join(""));
    const selections = entry.special.filter(
      (selection) => selection.kind === group.kind);
    setHtml(grid, specialFamilies(group).map((family) => {
      const familySelection = selections.find((selection) =>
        specialSelectionFamily(group, selection.id) === family.id) || null;
      const isSelected = !!familySelection;
      const awakeItem = family.items.find((candidate) => candidate.awakened) ||
        null;
      const selectedItem = familySelection
        ? family.items.find(
          (candidate) => String(candidate.id) === String(familySelection.id))
        : null;
      const awakeOn = !!selectedItem?.awakened;
      const item = (awakeOn
        ? awakeItem
        : resolveSpecialItem(
          group,
          isSelected ? familySelection.id : family.id,
          entry.grade)) || family.items[0];
      const tierItems = MERGED_SPECIAL_KINDS.has(group.kind)
        ? family.items.filter((candidate) =>
          specialLevelIndex(candidate.id) === specialTierForGrade(entry.grade))
        : [];
      const concreteItems = tierItems.filter(
        (candidate) => specialVariantSlot(candidate.id) !== "");
      const pickerItems = concreteItems.length ? concreteItems : tierItems;
      const showVariants = pickerItems.length > 1;
      const showOptions = showVariants || !!awakeItem;
      const normalId = awakeOn
        ? (pickerItems.some((candidate) =>
          String(candidate.id) === String(familySelection?.normalId))
          ? familySelection.normalId
          : pickerItems[0]?.id || "")
        : (isSelected ? item?.id : "");
      const isInjectable = groupInjectable && family.injectable &&
        item?.injectable !== false;
      const meta = [
        item?.positive ? "正面" : "",
      ].filter(Boolean).join(" · ");
      const displayName = family.items.length > 1 && item?.levelName
        ? `${family.name}·${item.levelName}`
        : family.name;
      const statusText = isInjectable
        ? (item?.mechanic?.battle === false
          ? "非战斗机制"
          : item?.derived
            ? "静态方案 · 自动"
            : (family.items.length > 1 ? "自动" : group.label))
        : "仅展示";
      const tooltip = isInjectable
        ? [item?.detail || item?.desc || family.id, item?.note]
          .filter(Boolean).join("\n")
        : (item?.unavailableReason || group.unavailableReason ||
          "该选项当前仅展示，不写入战斗启动参数");
      const selectedMark = isSelected
        ? (isInjectable ? " · 已启用" : " · 已选择")
        : "";
      return `
        <div class="config-special-entry">
          <button type="button" class="config-special-card ${isSelected ? "is-selected" : ""} ${isInjectable ? "" : "is-unavailable"}"
            data-action="config-special-toggle" data-kind="${esc(group.kind)}"
            data-id="${esc(family.id)}" data-layer="1"
            title="${esc(tooltip)}"
            ${isInjectable ? "" : "disabled"}>
            ${configIconMarkup(item, "is-small")}
            <span class="config-special-copy">
              <strong>${esc(displayName)}</strong>
              <span>${esc(item?.detail || item?.desc || family.detail || "无效果说明")}</span>
              <em>${esc(statusText)}${meta && isInjectable ? ` · ${esc(meta)}` : ""}${selectedMark}</em>
            </span>
          </button>
          ${showOptions ? `
            <div class="config-special-options">
              ${showVariants ? `
                <div class="config-special-variant">
                  <span>变体</span>
                  <div class="config-special-chips">
                    ${pickerItems.map((variant) => `
                      <button type="button"
                        class="config-special-chip ${String(variant.id) === String(normalId) ? "is-active" : ""}"
                        data-action="config-special-variant"
                        data-kind="${esc(group.kind)}"
                        data-family="${esc(family.id)}"
                        data-id="${esc(variant.id)}"
                        title="${esc(variant.detail || variant.desc || "")}">
                        ${esc(specialVariantLabel(variant))}
                      </button>`).join("")}
                  </div>
                </div>` : ""}
              ${awakeItem ? `
                <label class="config-special-awake">
                  <input type="checkbox" data-config-special-awake
                    data-kind="${esc(group.kind)}"
                    data-family="${esc(family.id)}"
                    ${awakeOn ? "checked" : ""}>
                  <span>醒觉</span>
                </label>` : ""}
            </div>` : ""}
        </div>`;
    }).join(""));
  }

  function renderConfigCopper(topic, entry) {
    const panel = document.getElementById("configCopperPanel");
    const grid = document.getElementById("configCopperGrid");
    const badge = document.getElementById("configCopperBadge");
    if (!panel || !grid || !badge) return;
    const items = Array.isArray(topic.copper) ? topic.copper : [];
    panel.hidden = items.length === 0;
    if (!items.length) return;
    const selected = new Set(entry.copper.map(String));
    badge.textContent = `${entry.copper.length} 项`;
    const query = String(state.configCopperQuery || "").trim().toLowerCase();
    const rows = [];
    for (const item of items) {
      if (query) {
        const haystack = `${item.name} ${item.variantLabel || ""} ${item.usage} ${item.desc} ${item.id}`.toLowerCase();
        if (!haystack.includes(query)) continue;
      }
      rows.push(item);
      if (rows.length >= 240) break;
    }
    setHtml(grid, rows.map((item) => {
      const isSelected = selected.has(String(item.id));
      return `
        <button type="button" class="config-special-card ${isSelected ? "is-selected" : ""}"
          data-action="config-copper-toggle" data-id="${esc(item.id)}"
          title="${esc(item.usage || item.desc || item.id)}">
          ${configIconMarkup(item, "is-small")}
          <span class="config-special-copy">
            <strong>${esc(item.variantLabel ? `${item.name} · ${item.variantLabel}` : item.name)}</strong>
            <span>${esc(item.usage || item.desc || "无效果说明")}</span>
            <em>${item.buffItems.length} buff${isSelected ? " · 已启用" : ""}</em>
          </span>
        </button>`;
    }).join(""));
  }

  function renderConfigCharBuffs(topic, entry) {
    const panel = document.getElementById("configCharBuffPanel");
    const grid = document.getElementById("configCharBuffGrid");
    const badge = document.getElementById("configCharBuffBadge");
    const selectedNode = document.getElementById("configCharBuffSelected");
    if (!panel || !grid || !badge || !selectedNode) return;
    const title = document.getElementById("configCharBuffTitle");
    const note = document.getElementById("configCharBuffNote");
    if (title) title.textContent = topic.charBuffTitle || "干员 Buff";
    if (note) {
      note.textContent = topic.charBuffNote ||
        "选中后只作用于下方勾选的编队干员；不选目标则保持空目标列表。";
    }
    const items = Array.isArray(topic.charBuffs) ? topic.charBuffs : [];
    panel.hidden = items.length === 0;
    if (!items.length) return;
    badge.textContent = `${entry.charBuffs.length} 项`;
    const selected = new Map(
      entry.charBuffs.map((item) => [String(item.id), item]));
    setHtml(grid, items.map((item) => {
      const isSelected = selected.has(String(item.id));
      const buffText = (item.buffKeys || []).join(" / ");
      return `
        <button type="button" class="config-special-card ${isSelected ? "is-selected" : ""}"
          data-action="config-charbuff-toggle" data-id="${esc(item.id)}"
          title="${esc(item.detail || item.desc || item.id)}">
          ${configIconMarkup(item, "is-small")}
          <span class="config-special-copy">
            <strong>${esc(item.name)}</strong>
            <span>${esc(item.detail || item.desc || "无效果说明")}</span>
            <em>${esc(buffText || "无 buff key")}${isSelected ? " · 已启用" : ""}</em>
          </span>
        </button>`;
    }).join(""));

    const squad = state.squad
      .slice(0, state.squadLimit)
      .filter(Boolean)
      .filter((slot, index, rows) =>
        rows.findIndex((item) => item.id === slot.id) === index);
    if (!entry.charBuffs.length) {
      setHtml(selectedNode,
        '<div class="empty-small">先从上方选择一个干员 Buff。</div>');
      return;
    }
    if (!squad.length) {
      setHtml(selectedNode,
        '<div class="empty-small">当前编队为空，请先在编队页选择干员。</div>');
      return;
    }
    setHtml(selectedNode, entry.charBuffs.map((selection) => {
      const item = items.find(
        (candidate) => String(candidate.id) === String(selection.id));
      const targets = new Set(selection.targets.map(String));
      return `
        <div class="config-charbuff-row">
          <div class="config-charbuff-row-head">
            <strong>${esc(item?.name || selection.id)}</strong>
            <button type="button" data-action="config-charbuff-toggle"
              data-id="${esc(selection.id)}" aria-label="移除 ${esc(item?.name || selection.id)}">×</button>
          </div>
          <div class="config-target-grid">
            ${squad.map((slot) => `
              <button type="button"
                class="config-target-chip ${targets.has(String(slot.id)) ? "is-selected" : ""}"
                data-action="config-charbuff-target"
                data-id="${esc(selection.id)}" data-target="${esc(slot.id)}">
                ${esc(operatorName(slot.id))}
              </button>
            `).join("")}
          </div>
          <span class="config-note">${selection.targets.length
            ? `作用于 ${selection.targets.length} 名干员`
            : "未选择目标：charUniqueIds 为空"}</span>
        </div>`;
    }).join(""));
  }

  function renderRunBuffCatalog(topic, entry, options) {
    const panel = document.getElementById(options.panelId);
    const grid = document.getElementById(options.gridId);
    const badge = document.getElementById(options.badgeId);
    if (!panel || !grid || !badge) return;
    const items = Array.isArray(topic[options.sourceKey])
      ? topic[options.sourceKey]
      : [];
    panel.hidden = items.length === 0;
    if (!items.length) return;
    const selected = new Set(entry[options.entryKey].map(String));
    badge.textContent = `${selected.size} 项`;
    const query = String(state[options.queryKey] || "").trim().toLowerCase();
    const rows = [];
    for (const item of items) {
      if (query) {
        const haystack =
          `${item.name} ${item.usage} ${item.desc} ${item.id}`.toLowerCase();
        if (!haystack.includes(query)) continue;
      }
      rows.push(item);
      if (rows.length >= 240) break;
    }
    setHtml(grid, rows.map((item) => {
      const isSelected = selected.has(String(item.id));
      const injectable = item.injectable !== false;
      const status = injectable
        ? `${item.usage || item.desc || item.id}${isSelected ? " · 已启用" : ""}`
        : (item.unavailableReason || "仅展示");
      return `
        <button type="button"
          class="config-special-card ${isSelected ? "is-selected" : ""} ${injectable ? "" : "is-unavailable"}"
          data-action="${options.action}" data-id="${esc(item.id)}"
          title="${esc(injectable ? (item.usage || item.desc || item.id) : status)}"
          ${injectable ? "" : "disabled"}>
          ${configIconMarkup(item, "is-small")}
          <span class="config-special-copy">
            <strong>${esc(item.name)}</strong>
            <span>${esc(item.usage || item.desc || "无效果说明")}</span>
            <em>${esc(status)}</em>
          </span>
        </button>`;
    }).join(""));
  }

  function renderConfigRunBuffs(topic, entry) {
    renderRunBuffCatalog(topic, entry, {
      panelId: "configFragmentPanel",
      gridId: "configFragmentGrid",
      badgeId: "configFragmentBadge",
      sourceKey: "fragments",
      entryKey: "fragments",
      queryKey: "configFragmentQuery",
      action: "config-fragment-toggle",
    });
    renderRunBuffCatalog(topic, entry, {
      panelId: "configTotemPanel",
      gridId: "configTotemGrid",
      badgeId: "configTotemBadge",
      sourceKey: "totems",
      entryKey: "totems",
      queryKey: "configTotemQuery",
      action: "config-totem-toggle",
    });
  }

  function renderConfigConcepts(topic, entry) {
    const panel = document.getElementById("configConceptPanel");
    const grid = document.getElementById("configConceptGrid");
    const badge = document.getElementById("configConceptBadge");
    const chips = document.getElementById("configSelectedConcepts");
    if (!panel || !grid || !badge || !chips) return;
    const items = Array.isArray(topic.concepts) ? topic.concepts : [];
    panel.hidden = items.length === 0;
    if (!items.length) return;
    const selected = new Map(
      entry.concepts.map((item) => [String(item.id), item]));
    const totalLayers = entry.concepts.reduce(
      (sum, item) => sum + Math.max(1, Number(item.layer) || 1), 0);
    const conceptById = new Map(items.map((item) => [String(item.id), item]));
    badge.textContent = `${totalLayers} 层 / ${items.length} 项`;
    setHtml(chips, entry.concepts.map((item) => {
      const concept = conceptById.get(String(item.id));
      if (!concept) return "";
      return `
        <span class="config-selected-chip is-stacked">
          ${esc(concept.name)}
          <span class="config-stack-control">
            <button type="button" data-action="config-concept-layer"
              data-id="${esc(item.id)}" data-delta="-1"
              aria-label="减少 ${esc(concept.name)} 层数">−</button>
            <input type="number" data-config-concept-layer data-id="${esc(item.id)}"
              min="1" max="99" step="1" value="${clampInt(item.layer, 1, 99, 1)}"
              aria-label="${esc(concept.name)} 层数">
            <button type="button" data-action="config-concept-layer"
              data-id="${esc(item.id)}" data-delta="1"
              aria-label="增加 ${esc(concept.name)} 层数">+</button>
          </span>
          <button type="button" data-action="config-concept-remove"
            data-id="${esc(item.id)}" aria-label="移除 ${esc(concept.name)}">×</button>
        </span>`;
    }).join(""));
    setHtml(grid, items.map((item) => {
      const isSelected = selected.has(String(item.id));
      const layer = selected.get(String(item.id))?.layer || 0;
      const battle = item.battleEffect === true;
      const buffText = (item.outerBuffs || [])
        .map((buff) =>
          `${buff.field || buff.key} ${Number(buff.value) > 0 ? "+" : ""}${buff.value}`)
        .join(" · ");
      const status = battle ? `战斗 · ${buffText}` : "局外";
      return `
        <button type="button"
          class="config-special-card ${isSelected ? "is-selected" : ""}"
          data-action="config-concept-toggle" data-id="${esc(item.id)}"
          title="${esc(item.usage || item.desc || item.id)}">
          ${configIconMarkup(item, "is-small")}
          <span class="config-special-copy">
            <strong>${esc(item.name)}</strong>
            <span>${esc(item.usage || item.desc || "无效果说明")}</span>
            <em>${esc(status)}${isSelected ? ` · ${layer} 层` : ""}</em>
          </span>
        </button>`;
    }).join(""));
  }

  function renderConfigTech(topic, entry) {
    const tree = document.getElementById("configTechTree");
    const badge = document.getElementById("configTechBadge");
    if (!tree || !badge) return;
    badge.textContent = `${entry.tech.length} 项`;
    const selected = new Set(entry.tech.map(String));
    const rows = new Map();
    for (const node of topic.tech || []) {
      const row = Number(node.row || 0) || 1;
      if (!rows.has(row)) rows.set(row, []);
      rows.get(row).push(node);
    }
    if (!rows.size) {
      setHtml(tree, '<div class="empty-small">该主题没有科技树数据。</div>');
      return;
    }
    const html = Array.from(rows.entries())
      .sort(([left], [right]) => left - right)
      .map(([row, nodes]) => `
        <div class="config-tech-tier">
          <div class="config-tech-tier-label">阶 ${String(row).padStart(2, "0")}</div>
          <div class="config-tech-nodes">
            ${nodes.map((node) => {
              const battle = (node.display || []).some(
                (display) => CONFIG_BATTLE_DISPLAY_TYPES.has(display.displayType));
              const desc = (node.rawDesc || []).join(" · ") ||
                (node.display || []).map((display) => display.tokenDesc).filter(Boolean).join(" · ");
              return `
                <button type="button"
                  class="config-tech-node ${selected.has(String(node.id)) ? "is-selected" : ""} ${battle ? "" : "is-meta"}"
                  data-action="config-tech-toggle" data-id="${esc(node.id)}"
                  title="${esc(desc || node.id)}">
                  ${configIconMarkup(node, "is-small")}
                  <span class="config-tech-copy">
                    <strong>${esc(node.name)}</strong>
                    <span>${esc(desc || "无描述")}</span>
                    <em class="${battle ? "config-tech-battle-tag" : ""}">
                      ${battle ? "战斗属性" : "局外成长"} · ${node.cost || 0} 点
                    </em>
                  </span>
                </button>`;
            }).join("")}
          </div>
        </div>
      `).join("");
    setHtml(tree, html);
  }

  function renderConfig() {
    const page = document.getElementById("page-config");
    if (!page) return;
    const topic = configTopicData(state.configTopic);
    if (!topic) {
      document.getElementById("configSummaryBadge").textContent = "配置数据未加载";
      return;
    }
    state.configTopic = topic.id;
    if (!state.configSpecialKind && topic.special?.[0]) {
      state.configSpecialKind = topic.special[0].kind;
    }
    const entry = configEntry(topic.id);
    renderConfigTopicList(topic.id);
    renderConfigDifficulty(topic, entry);
    renderConfigSummary(topic, entry);
    renderConfigResources(topic, entry);
    renderConfigMasteries(topic, entry);
    renderConfigRelics(topic, entry);
    renderConfigSpecials(topic, entry);
    renderConfigCharBuffs(topic, entry);
    renderConfigCopper(topic, entry);
    renderConfigRunBuffs(topic, entry);
    renderConfigConcepts(topic, entry);
    renderConfigTech(topic, entry);
    const applyBadge = document.getElementById("configApplyBadge");
    if (applyBadge) {
      applyBadge.textContent = `已保存 · ${topic.name} 启动时生效`;
    }
    const applyButton = document.querySelector(".config-apply-button");
    const stage = configStageForTopic(topic.id);
    if (applyButton) {
      applyButton.textContent = stage
        ? `应用并开始战斗 · ${stage.name}`
        : "应用并开始战斗";
      applyButton.title = stage
        ? [stage.code, stage.name, stage.id].filter(Boolean).join(" · ")
        : "";
    }
  }

  function rogueConfigArgs(topicId) {
    const topic = configTopicData(topicId);
    const entry = configEntry(topicId);
    const injectableSpecial = [];
    const seenSpecial = new Set();
    for (const selection of entry.special) {
      const group = (topic?.special || []).find(
        (candidate) => candidate.kind === selection.kind);
      if (!group) continue;
      const family = specialSelectionFamily(group, selection.id);
      const key = `${selection.kind}:${family}`;
      if (seenSpecial.has(key)) continue;
      seenSpecial.add(key);
      const item = resolveSpecialItem(group, selection.id, entry.grade);
      if (!item) continue;
      if (item.injectable === false) continue;
      injectableSpecial.push({ selection, group, item });
    }
    const parts = [];
    const resourceKeys = Array.isArray(topic?.resourceKeys)
      ? topic.resourceKeys
      : ["gold"];
    if (resourceKeys.includes("scrap") && entry.scrap >= 0) {
      parts.push(`--scrap=${entry.scrap}`);
    }
    if (resourceKeys.includes("scrapLimit") && entry.scrapLimit >= 0) {
      parts.push(`--scraplimit=${entry.scrapLimit}`);
    }
    const pushOuterBuff = (buff) => {
      if (Array.isArray(buff?.randomKeyField) && buff.randomKeyField.length) {
        const chosen = buff.randomKeyField[
          Math.floor(Math.random() * buff.randomKeyField.length)
        ];
        parts.push(`--outerbuff=${buff.key}|${chosen}|`);
        return;
      }
      const fields = [];
      for (const pair of buff.blackboard || []) {
        fields.push(`${pair.key}=${encodeURIComponent(String(pair.value))}`);
      }
      parts.push(`--outerbuff=${[
        buff.key,
        buff.keyField || "",
        fields.join(";"),
      ].join("|")}`);
    };
    if (entry.relics.length) {
      parts.push("--relics=" + entry.relics
        .map((item) => `${item.id}@${item.layer}`)
        .join(","));
    }
    const squadIds = new Set(
      state.squad
        .slice(0, state.squadLimit)
        .filter(Boolean)
        .map((slot) => String(slot.id)));
    for (const master of entry.masters) {
      if (!squadIds.has(master.charId)) continue;
      const operator = (topic?.specialOperators || []).find(
        (item) => item.charId === master.charId);
      const mastery = operator?.masteries.find(
        (item) => item.id === master.masterId);
      if (!mastery) continue;
      if (mastery.mechanism) {
        const level = clampInt(master.level, 1, 3, 1);
        const chain = mastery.chains?.[level] || [];
        const zone = clampInt(master.zone, 0, chain.length, 0);
        for (const item of chain) {
          if (item.zone > zone) continue;
          for (const buff of item.buffs || []) {
            pushOuterBuff(buff);
          }
        }
        continue;
      }
      if (!mastery.injectable) continue;
      parts.push(
        `--master=${master.charId}:${master.masterId}@${clampInt(master.level, 1, 3, 1)}`);
    }
    if (entry.charBuffs.length) {
      parts.push("--charbuff=" + entry.charBuffs.map((item) => {
        const targets = item.targets.length
          ? `:${item.targets.join("|")}`
          : "";
        return `${item.id}@${item.layer}${targets}`;
      }).join(","));
    }
    const extraSpecials = [];
    for (const { selection, item } of injectableSpecial) {
      if (item.mechanic?.battle === false) {
        continue;
      }
      const buffs = item.mechanic?.buffs || [];
      if (!buffs.length) {
        extraSpecials.push(
          `${selection.kind}:${item.id}:${selection.layer}`);
        continue;
      }
      for (const buff of buffs) {
        pushOuterBuff(buff);
      }
    }
    if (extraSpecials.length) {
      parts.push("--extra=" + extraSpecials.join(","));
    }
    if (entry.copper.length) {
      const copperById = new Map(
        (topic?.copper || []).map((item) => [String(item.id), item]));
      const buffItems = [];
      for (const id of entry.copper) {
        const item = copperById.get(String(id));
        for (const buffId of item?.buffItems || []) {
          buffItems.push(`${buffId}@1`);
        }
      }
      if (buffItems.length) {
        parts.push("--buffitem=" + buffItems.join(","));
      }
    }
    for (const key of ["fragments", "totems"]) {
      const byId = new Map(
        (topic?.[key] || []).map((item) => [String(item.id), item]));
      for (const id of entry[key]) {
        const item = byId.get(String(id));
        for (const buff of item?.mechanic?.buffs || []) {
          pushOuterBuff(buff);
        }
      }
    }
    if (entry.tech.length) {
      parts.push("--tech=" + entry.tech.join(","));
    }
    const outer = [];
    let hpBonus = 0;
    let maxHpBonus = 0;
    let shieldBonus = 0;
    let goldBonus = resourceKeys.includes("gold")
      ? Math.max(0, Number(entry.gold) || 0)
      : 0;
    const techById = CONFIG_TECH_BY_TOPIC.get(String(topic?.id || "")) || new Map();
    for (const techId of entry.tech) {
      const node = techById.get(String(techId));
      for (const display of node?.display || []) {
        const type = String(display.displayType || "");
        const value = Number(display.displayNum || 0);
        if (!Number.isFinite(value) || !value) continue;
        if (type === "display_bat_attack") {
          outer.push(`char_attribute_mul:atk:${(value / 100).toFixed(6)}`);
        } else if (type === "display_bat_def") {
          outer.push(`char_attribute_mul:def:${(value / 100).toFixed(6)}`);
        } else if (type === "display_bat_max_hp") {
          outer.push(`char_attribute_mul:max_hp:${(value / 100).toFixed(6)}`);
        } else if (type === "display_hp") {
          hpBonus += value;
        } else if (type === "display_temp_hp") {
          maxHpBonus += value;
        } else if (type === "display_pro") {
          shieldBonus += value;
        } else if (type === "display_gold") {
          goldBonus += value;
        }
      }
    }
    const conceptById = new Map(
      (topic?.concepts || []).map((item) => [String(item.id), item]));
    for (const selection of entry.concepts) {
      const concept = conceptById.get(String(selection.id));
      const layer = clampInt(selection.layer, 1, 99, 1);
      for (let index = 0; index < layer; ++index) {
        for (const buff of concept?.outerBuffs || []) {
          if (!buff?.key || !buff?.field) continue;
          outer.push(`${buff.key}:${buff.field}:${buff.value}`);
        }
      }
    }
    if (outer.length) {
      parts.push("--outer=" + outer.join(","));
    }
    if (hpBonus) parts.push(`--hp=${hpBonus}`);
    if (maxHpBonus) parts.push(`--maxhp=${maxHpBonus}`);
    if (shieldBonus) parts.push(`--shield=${shieldBonus}`);
    if (goldBonus) parts.push(`--gold=${goldBonus}`);
    return parts.join(" ");
  }

  function configStageForTopic(topicId) {
    const topic = ROGUE_BY_TOPIC.get(String(topicId || ""));
    if (!topic?.stages?.length) return null;
    const stage = topic.stages.find(
      (item) => String(item.id) === String(state.stage)) || topic.stages[0];
    return ROGUE_STAGE_BY_ID.get(String(stage.id)) || stage;
  }
