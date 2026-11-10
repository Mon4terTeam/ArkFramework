(() => {
  "use strict";

  const CATALOG_URL = "data/buff-catalog.tsv";
  const EFFECTS_URL = "data/relic-effects.tsv";
  const ABNORMAL = window.BuffAbnormalNames || {};
  const COMBO = window.BuffAbnormalComboNames || {};
  const ROSTER = Array.isArray(window.ROSTER_CATALOG)
    ? window.ROSTER_CATALOG
    : [];
  const ROSTER_BY_ID = new Map(
    ROSTER.map((entry) => [String(entry?.id || ""), entry]),
  );
  const SKILL_BY_KEY = new Map();
  for (const operator of ROSTER) {
    for (const skill of Array.isArray(operator.skills) ? operator.skills : []) {
      if (!skill?.id) continue;
      SKILL_BY_KEY.set(canonical(skill.id), {
        id: skill.id,
        name: skill.name || skill.id,
        owner: operator.id,
      });
    }
  }

  const state = {
    loading: null,
    loaded: false,
    error: "",
    buffs: new Map(),
    skills: new Map(),
    relics: new Map(),
    anchors: new Map(),
  };

  function baseKey(raw) {
    const text = String(raw ?? "").trim();
    if (!text) return "";
    const start = text.startsWith("[g]") ? 3 : 0;
    const cut = text.indexOf("[", start);
    return text.slice(start, cut < 0 ? undefined : cut);
  }

  function canonical(raw) {
    let key = baseKey(raw);
    if (!key) return "";
    let match = /^skchr_(.+)_(\d+)$/.exec(key);
    if (match) return `${match[1]}_s_${match[2]}`;
    match = /^sktok_(.+)_(\d+)$/.exec(key);
    if (match) return `${match[1]}_s_${match[2]}`;
    match = /^(.*)_([st])(\d+)$/.exec(key);
    if (match) return `${match[1]}_${match[2]}_${match[3]}`;
    return key;
  }

  function keysFor(raw) {
    const keys = [];
    const add = (value) => {
      const key = String(value ?? "").trim();
      if (key && !keys.includes(key)) keys.push(key);
    };
    add(raw);
    add(baseKey(raw));
    add(canonical(raw));
    add(canonical(baseKey(raw)));
    return keys;
  }

  function parseTsv(text) {
    const lines = String(text ?? "").replace(/^\uFEFF/, "").split(/\r?\n/);
    if (!lines.length) return [];
    const header = lines.shift().split("\t");
    const rows = [];
    for (const line of lines) {
      if (!line) continue;
      const cells = line.split("\t");
      const row = {};
      for (let index = 0; index < header.length; index += 1) {
        row[header[index]] = cells[index] ?? "";
      }
      rows.push(row);
    }
    return rows;
  }

  async function fetchText(url) {
    const response = await fetch(url, { cache: "no-cache" });
    if (!response.ok) {
      throw new Error(`${url}: HTTP ${response.status}`);
    }
    return response.text();
  }

  function load() {
    if (state.loading) return state.loading;
    state.loading = (async () => {
      try {
        const [catalogText, effectsText] = await Promise.all([
          fetchText(CATALOG_URL),
          fetchText(EFFECTS_URL),
        ]);
        for (const row of parseTsv(catalogText)) {
          if (row.kind === "buff") state.buffs.set(row.key, row);
          else if (row.kind === "skill") state.skills.set(row.key, row);
          else if (row.kind === "relic") state.relics.set(row.key, row);
        }
        for (const row of parseTsv(effectsText)) {
          const anchor = String(row.anchor || "").trim();
          const relic = String(row.relic_id || "").trim();
          if (!anchor || !relic) continue;
          const list = state.anchors.get(anchor) || [];
          if (!list.includes(relic)) list.push(relic);
          state.anchors.set(anchor, list);
        }
        state.loaded = true;
        state.error = "";
        window.BetterBattleProtocol?.post({
          type: window.BetterBattleProtocol?.Message?.Log,
          message: `Buff 静态目录已加载：${state.buffs.size} 个 Buff，` +
            `${state.skills.size} 个技能，${state.relics.size} 件藏品`,
        });
        if (typeof window.renderSnapshotPage === "function") {
          window.renderSnapshotPage();
        }
        return true;
      } catch (error) {
        state.error = String(error?.message || error);
        console.warn("[BuffCatalog] 静态目录加载失败", error);
        window.BetterBattleProtocol?.post({
          type: window.BetterBattleProtocol?.Message?.Log,
          message: `Buff 静态目录加载失败：${state.error}`,
        });
        return false;
      }
    })();
    return state.loading;
  }

  function find(raw) {
    for (const key of keysFor(raw)) {
      const hit = state.buffs.get(key) || state.skills.get(key);
      if (hit) return hit;
    }
    return null;
  }

  function resolve(buff) {
    if (!buff) return null;
    const entry = find(buff.key) || find(buff.template) ||
      find(buff.override) || find(buff.effect);
    if (!entry) {
      return { entry: null, name: "", description: "", owner: "", relic: [] };
    }
    const skill = SKILL_BY_KEY.get(canonical(entry.source));
    const owner = ROSTER_BY_ID.get(String(entry.owner || skill?.owner || ""));
    return {
      entry,
      skill,
      owner,
      name: entry.name,
      description: entry.description,
    };
  }

  function sourceLabel(info) {
    const entry = info?.entry;
    if (!entry) return "";
    if (entry.kind === "relic") {
      return `${entry.topic ? entry.topic + " · " : ""}藏品`;
    }
    const parts = [];
    if (info.owner?.name) parts.push(info.owner.name);
    if (info.skill?.name) parts.push(info.skill.name);
    if (!parts.length && entry.owner) parts.push(entry.owner);
    if (entry.subtype) {
      parts.push(entry.subtype === "talent" ? "天赋" : "技能");
    }
    return parts.join(" · ");
  }

  function hidesDescription(entry) {
    if (!entry) return false;
    if (entry.kind === "skill") return true;
    const subtype = String(entry.subtype || "").toLowerCase();
    return subtype === "skill" || subtype === "talent" ||
      subtype.endsWith("_skill");
  }

  function blackboardEntries(buff) {
    if (Array.isArray(buff?.blackboard_entries) &&
        buff.blackboard_entries.length) {
      return buff.blackboard_entries;
    }
    const entries = [];
    for (const part of String(buff?.blackboard || "").split(",")) {
      const text = part.trim();
      const at = text.indexOf("=");
      if (at <= 0) continue;
      const raw = text.slice(at + 1);
      const value = Number(raw);
      entries.push({
        key: text.slice(0, at),
        value: Number.isFinite(value) ? value : 0,
        value_str: Number.isFinite(value) ? "" : raw,
      });
    }
    return entries;
  }

  function boardValue(entry) {
    if (entry?.value_str) return entry.value_str;
    const value = Number(entry?.value);
    return Number.isFinite(value) ? String(Number(value.toFixed(4))) : "";
  }

  function lookupBlackboard(entries, name) {
    const needle = String(name || "").trim();
    if (!needle) return undefined;
    const variants = [needle];
    const dot = needle.lastIndexOf(".");
    if (dot >= 0) variants.push(needle.slice(dot + 1));
    const at = needle.lastIndexOf("@");
    if (at >= 0) variants.push(needle.slice(at + 1));
    for (const variant of variants) {
      const hit = entries.find((entry) =>
        String(entry?.key || "") === variant);
      if (hit) return hit;
    }
    for (const variant of variants) {
      const hit = entries.find((entry) =>
        String(entry?.key || "").endsWith("@" + variant) ||
        String(entry?.key || "").endsWith("." + variant));
      if (hit) return hit;
    }
    return undefined;
  }

  function formatBlackboardValue(entry, format) {
    if (entry.value_str) return entry.value_str;
    const value = Number(entry.value);
    if (!Number.isFinite(value)) return String(entry.value_str || "");
    const spec = String(format || "");
    if (!spec) return String(Number(value.toFixed(4)));
    if (spec.includes("%")) {
      const decimals = Math.min(4, Math.max(0, (spec.split(".")[1] || "").length));
      return `${(value * 100).toFixed(decimals)}%`;
    }
    const decimals = Math.min(4, Math.max(0, (spec.split(".")[1] || "").length));
    return value.toFixed(decimals);
  }

  function substituteDescription(text, entries) {
    const source = String(text ?? "");
    if (!source) return "";
    return source.replace(/\{([^{}]+)\}/g, (whole, expression) => {
      const colon = expression.indexOf(":");
      const name = colon < 0 ? expression : expression.slice(0, colon);
      const format = colon < 0 ? "" : expression.slice(colon + 1);
      const entry = lookupBlackboard(entries, name);
      if (!entry) return whole;
      return formatBlackboardValue(entry, format);
    });
  }

  function abnormalLabel(name) {
    const key = String(name || "").trim();
    if (!key) return "";
    const chinese = ABNORMAL[key] || COMBO[key];
    return chinese && chinese !== key ? `${chinese}（${key}）` : key;
  }

  function abnormalName(name) {
    const key = String(name || "").trim();
    if (!key) return "";
    return ABNORMAL[key] || COMBO[key] || key;
  }

  function abnormalLabels(value) {
    return String(value || "")
      .split(",")
      .map((part) => abnormalLabel(part.trim()))
      .filter(Boolean);
  }

  function modifierGroups(entries) {
    const definitions = [
      { kind: 0, label: "基础加值" },
      { kind: 1, label: "倍率" },
      { kind: 2, label: "最终加值" },
      { kind: 3, label: "最终倍率" },
    ];
    const groups = definitions.map((definition) => ({
      ...definition,
      items: [],
    }));
    let positive = 0;
    let negative = 0;
    for (const entry of Array.isArray(entries) ? entries : []) {
      const group = groups[Number(entry.kind)];
      if (!group) continue;
      const value = Number(entry.value);
      if (!Number.isFinite(value)) continue;
      if (value > 0) positive += 1;
      else if (value < 0) negative += 1;
      group.items.push({ ...entry, value });
    }
    let category = "未分类";
    if (positive && negative) category = "混合";
    else if (positive) category = "加成";
    else if (negative) category = "减益";
    return { category, groups: groups.filter((group) => group.items.length) };
  }

  function modifierText(entry) {
    const value = Number(entry.value);
    if (entry.kind === 1) return `${entry.attribute} ${(value * 100).toFixed(2)}%`;
    if (entry.kind === 3) return `${entry.attribute} ×${value.toFixed(4)}`;
    return `${entry.attribute} ${value >= 0 ? "+" : ""}${value.toFixed(4)}`;
  }

  function relicMatches(buff) {
    const keys = new Set(keysFor(buff?.key));
    if (buff?.template) keysFor(buff.template).forEach((key) => keys.add(key));
    if (buff?.effect) keysFor(buff.effect).forEach((key) => keys.add(key));
    const ids = new Set();
    for (const key of keys) {
      for (const relic of state.anchors.get(key) || []) ids.add(relic);
    }
    return [...ids];
  }

  function relicCandidates(buff, rogue) {
    const active = new Map();
    for (const relic of Array.isArray(rogue?.relics) ? rogue.relics : []) {
      active.set(String(relic.id || ""), relic);
    }
    const keys = new Set(keysFor(buff?.key));
    if (buff?.template) keysFor(buff.template).forEach((key) => keys.add(key));
    if (buff?.effect) keysFor(buff.effect).forEach((key) => keys.add(key));
    const exact = [];
    for (const relic of active.values()) {
      const buffKeys = Array.isArray(relic.buff_keys) ? relic.buff_keys : [];
      if (buffKeys.some((key) => keys.has(key))) exact.push(relic.id);
    }
    const candidates = [];
    const seen = new Set(exact);
    for (const id of relicMatches(buff)) {
      if (seen.has(id)) continue;
      seen.add(id);
      candidates.push(id);
    }
    const firstRelic = (Array.isArray(rogue?.relics) ? rogue.relics : [])
      .map((relic) => String(relic?.id || ""))
      .find(Boolean) || "";
    const topic = String(rogue?.topic || "") ||
      (firstRelic.match(/^rogue_\d+/) || [""])[0];
    const resolved = candidates.map((id) => ({
      id,
      entry: state.relics.get(id) || null,
      active: active.has(id),
      layer: active.get(id)?.layer ?? null,
      exact: false,
    })).filter((item) => item.entry || item.active);
    const exact_items = exact.map((id) => ({
      id,
      entry: state.relics.get(id) || null,
      active: true,
      layer: active.get(id)?.layer ?? null,
      exact: true,
    }));
    const all = [...exact_items, ...resolved];
    if (topic) {
      const same_topic = all.filter((item) => item.id.startsWith(topic + "_"));
      return same_topic.length ? same_topic : all;
    }
    return all;
  }

  window.BuffCatalog = {
    load,
    resolve,
    find,
    sourceLabel,
    keysFor,
    canonical,
    baseKey,
    blackboardEntries,
    boardValue,
    substituteDescription,
    abnormalLabel,
    abnormalName,
    abnormalLabels,
    modifierGroups,
    modifierText,
    relicCandidates,
    hidesDescription,
    relic(id) {
      return state.relics.get(String(id || "")) || null;
    },
    operator(id) {
      return ROSTER_BY_ID.get(String(id || "")) || null;
    },
    get ready() {
      return state.loaded;
    },
    get error() {
      return state.error;
    },
    get buffCount() {
      return state.buffs.size;
    },
  };

  load();
})();
