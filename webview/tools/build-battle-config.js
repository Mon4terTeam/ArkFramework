const fs = require("fs");
const path = require("path");
const { staticMechanic } = require("./rogue-mechanics-static");

const root = path.resolve(__dirname, "..", "..", "..");
const tablePath = path.join(
  root, "_assets", "ArknightsGameResource", "gamedata", "excel",
  "roguelike_topic_table.json");
const charMetaPath = path.join(
  root, "_assets", "ArknightsGameResource", "gamedata", "excel",
  "char_meta_table.json");
const specialOperatorPath = path.join(
  root, "_assets", "ArknightsGameResource", "gamedata", "excel",
  "special_operator_table.json");
const characterTablePath = path.join(
  root, "_assets", "ArknightsGameResource", "gamedata", "excel",
  "character_table.json");
const outputPath = path.resolve(__dirname, "..", "ui", "data", "battle-config.js");
const iconRoot = path.resolve(__dirname, "..", "ui", "assets", "game", "roguelike");
const avatarRoot = path.resolve(__dirname, "..", "ui", "assets", "game", "avatar");
const table = JSON.parse(fs.readFileSync(tablePath, "utf8"));
const charMeta = JSON.parse(fs.readFileSync(charMetaPath, "utf8"));
const specialOperators = JSON.parse(fs.readFileSync(specialOperatorPath, "utf8"));
const characterTable = JSON.parse(fs.readFileSync(characterTablePath, "utf8"));

const topicMeta = {
  rogue_1: { name: "傀影与猩红孤钻", specialLabel: "幻觉" },
  rogue_2: {
    name: "水月与深蓝之树",
    specialLabel: "启示",
    charBuffTitle: "排异反应 / 干员 Buff",
    charBuffNote: "排异反应作用于指定干员；未选目标时保持空目标列表。",
  },
  rogue_3: { name: "探索者的银凇止境", specialLabel: "坍缩范式" },
  rogue_4: { name: "萨卡兹的无终奇语", specialLabel: "年代" },
  rogue_5: {
    name: "界园志异",
    specialLabel: "岁时",
    charBuffTitle: "伺烛客",
    charBuffNote:
      "将所选编队干员标记为伺烛客（rogue_5_candle_1）；" +
      "部分岁时和界园机制只对伺烛客生效。",
  },
  rogue_6: {
    name: "黑流树海",
    specialLabel: "实托邦 / 乌托邦（理想源）",
    charBuffTitle: "新典训 / 干员 Buff",
    charBuffNote: "新典训会同时出现在藏品列表；这里用于直接指定生效干员。",
    resourceKeys: ["gold", "scrap", "scrapLimit"],
  },
};

function values(object) {
  return object && typeof object === "object" ? Object.values(object) : [];
}

function hasIcon(topicId, iconId) {
  if (!iconId) return false;
  return fs.existsSync(path.join(iconRoot, topicId, `${iconId}.webp`)) ||
    fs.existsSync(path.join(iconRoot, topicId, `${iconId}.png`));
}

function cleanText(value) {
  if (Array.isArray(value)) return value.map(cleanText).filter(Boolean);
  return String(value ?? "").trim();
}

const VERIFIED_SPECIALS = {
  rogue_3: [
    /^rogue_3_chaos_3(_a)?$/,
  ],
  rogue_5: [
    /^rogue_5_wrath_7_[abc]$/,
    /^rogue_5_wrath_8_[abc]$/,
    /^rogue_5_wrath_8_d$/,
    /^rogue_5_wrath_2_[abc]$/,
    /^rogue_5_wrath_2_d$/,
    /^rogue_5_wrath_5_[abc]$/,
    /^rogue_5_wrath_5_d$/,
  ],
  rogue_6: [
    /^rogue_6_weather_1_[abc]$/,
    /^rogue_6_weather_2_[abc]$/,
    /^rogue_6_weather_4_[abc]$/,
    /^rogue_6_weather_7_[abc]$/,
    /^rogue_6_weather_8_[abc]$/,
    /^variation_1$/,
    /^variation_2$/,
    /^variation_3$/,
    /^variation_4$/,
    /^variation_7$/,
  ],
};

function specialIsVerified(topicId, groupKind, itemId) {
  const rules = VERIFIED_SPECIALS[topicId] || [];
  return rules.some((rule) => rule.test(String(itemId || "")));
}

function gateSpecial(topicId, groups) {
  for (const group of groups) {
    for (const item of group.items) {
      if (!item.mechanic) {
        item.injectable = false;
        item.unavailableReason = item.unavailableReason || "暂无战斗配方";
        continue;
      }
      if (item.mechanic.battle === false) {
        item.injectable = false;
        item.unavailableReason = "战斗外效果，已禁用";
        continue;
      }
      if (!specialIsVerified(topicId, group.kind, item.id)) {
        item.injectable = false;
        item.unavailableReason = "未实测，已禁用";
      }
    }
    group.injectable = group.items.some((item) => item.injectable);
    if (!group.injectable) {
      group.unavailableReason = "当前没有已实测且可注入的战斗效果";
    }
  }
  return groups;
}

function relicStack(relic) {
  let best = null;
  for (const buff of Array.isArray(relic?.buffs) ? relic.buffs : []) {
    const key = String(buff?.key || "");
    if (!key.startsWith("layer_")) continue;
    const blackboard = new Map(
      (Array.isArray(buff.blackboard) ? buff.blackboard : [])
        .map((entry) => [String(entry.key || ""), entry.value]),
    );
    const max = Number(blackboard.get("max"));
    if (!Number.isFinite(max) || max <= 1) continue;
    const rawInit = Number(blackboard.get("init"));
    const candidate = {
      max: Math.min(Math.round(max), 999),
      init: Number.isFinite(rawInit) ? Math.max(1, Math.round(rawInit)) : 1,
      buffKey: key,
      label: key === "layer_scrap_owned" ? "零件数" : "层数",
    };
    if (!best || candidate.max > best.max) best = candidate;
  }
  return best;
}

function relicData(topicId, detail) {
  const items = detail.items || {};
  const relics = detail.relics || {};
  return Object.entries(relics)
    .map(([id, relic]) => {
      const item = items[id];
      if (!item || !item.name) return null;
      if (Number(item.type || 0) !== 9) return null;
      return {
        id,
        name: cleanText(item.name),
        usage: cleanText(item.usage || item.description),
        desc: cleanText(item.description),
        iconId: cleanText(item.iconId || id),
        icon: hasIcon(topicId, item.iconId || id) ? `${topicId}/${item.iconId || id}.png` : "",
        rarity: Number(item.rarity || 0),
        type: Number(item.type || 0),
        sortId: Number(item.sortId || 0),
        buffCount: Array.isArray(relic.buffs) ? relic.buffs.length : 0,
        stack: relicStack(relic),
      };
    })
    .filter(Boolean)
    .sort((left, right) =>
      right.rarity - left.rarity ||
      left.sortId - right.sortId ||
      left.name.localeCompare(right.name, "zh-CN"));
}

function plainText(value) {
  return cleanText(value).replace(/<[^>]*>/g, "");
}

function variantBaseName(name) {
  return String(name || "").trim().replace(/[\s\-—_]*[αβγδ]$/u, "").trim();
}

function normalizeVariantIcons(items) {
  const groups = new Map();
  for (const item of items || []) {
    if (!item) continue;
    const key = variantBaseName(item.name);
    if (!key) continue;
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(item);
  }
  for (const [key, group] of groups) {
    const donor = group.find((item) =>
      item.icon && String(item.name || "").trim() === key) ||
      group.find((item) => item.icon) ||
      group.find((item) => item.iconId);
    if (!donor) continue;
    for (const item of group) {
      if (donor.icon) item.icon = donor.icon;
      if (donor.iconId) item.iconId = donor.iconId;
    }
  }
  return items;
}

function normalizeIconExtensions(value) {
  if (Array.isArray(value)) {
    for (const item of value) normalizeIconExtensions(item);
    return;
  }
  if (!value || typeof value !== "object") return;
  if (typeof value.icon === "string" && value.icon.endsWith(".png")) {
    const webp = value.icon.replace(/\.png$/, ".webp");
    if (fs.existsSync(path.join(iconRoot, webp))) {
      value.icon = webp;
    }
  }
  for (const child of Object.values(value)) {
    normalizeIconExtensions(child);
  }
}

const conceptBattleEffects = {
  rogue_6_scrap_P_01: [
    { key: "char_attribute_add", field: "attack_speed", value: 50 },
  ],
  rogue_6_scrap_P_04: [
    { key: "char_attribute_add", field: "attack_speed", value: 35 },
  ],
};

function conceptData(topicId, detail, modules) {
  if (topicId !== "rogue_6") return [];
  const items = detail.items || {};
  return values(modules.scrap && modules.scrap.passiveScrapData)
    .map((entry) => {
      const id = cleanText(entry.scrapId);
      const item = items[id] || {};
      const effects = conceptBattleEffects[id] || [];
      const iconId = cleanText(item.iconId || id);
      return {
        id,
        name: cleanText(item.name || id),
        usage: plainText(item.usage || entry.scrapDesc),
        desc: plainText(item.description),
        iconId,
        icon: hasIcon(topicId, iconId) ? `${topicId}/${iconId}.png` : "",
        rarity: Number(item.rarity || 0),
        battleEffect: effects.length > 0,
        outerBuffs: effects,
        sortId: Number(item.sortId || 0),
      };
    })
    .filter((entry) => entry.id && entry.name)
    .sort((left, right) =>
      left.sortId - right.sortId ||
      left.name.localeCompare(right.name, "zh-CN"));
}

function specialData(topicId, detail, modules) {
  if (topicId === "rogue_1") {
    return [{
      kind: "variation",
      label: "幻觉",
      injectable: false,
      unavailableReason: "当前版本未接入战斗启动参数",
      items: values(detail.variationData).map((entry) => ({
        id: entry.id,
        name: entry.outerName || entry.innerName || entry.id,
        detail: entry.functionDesc || entry.desc || "",
        iconId: entry.iconId || "",
        icon: hasIcon(topicId, entry.iconId) ? `${topicId}/${entry.iconId}.png` : "",
        injectable: false,
        layer: 1,
      })),
    }];
  }
  if (topicId === "rogue_2") {
    return [
      {
        kind: "virtue",
        label: "启示",
        injectable: true,
        items: values(detail.squadBuffData).map((entry) => ({
          id: entry.id,
          name: entry.outerName || entry.innerName || entry.id,
          detail: entry.functionDesc || entry.desc || "",
          iconId: entry.iconId || "",
          icon: hasIcon(topicId, entry.iconId) ? `${topicId}/${entry.iconId}.png` : "",
          injectable: true,
          layer: 1,
        })),
      },
    ];
  }
  if (topicId === "rogue_3") {
    const chaosMechanic = (id) => {
      if (id === "rogue_3_chaos_3") {
        return {
          buffs: [{
            key: "global_buff_normal",
            keyField: "enemy_attr_final[tag]",
            blackboard: [
              { key: "max_hp", value: 1.15 },
              { key: "move_speed", value: 1.15 },
              { key: "attack_speed", value: 0 },
            ],
          }],
        };
      }
      if (id === "rogue_3_chaos_3_a") {
        return {
          buffs: [{
            key: "global_buff_normal",
            keyField: "enemy_attr_final[tag]",
            blackboard: [
              { key: "max_hp", value: 1.2 },
              { key: "move_speed", value: 1.2 },
              { key: "attack_speed", value: 20 },
            ],
          }],
        };
      }
      return null;
    };
    const items = values(modules.chaos && modules.chaos.chaosDatas).map((entry) => {
      const staticMech = staticMechanic(topicId, "chaos", entry);
      const mechanic = staticMech || chaosMechanic(entry.chaosId);
      return {
        id: entry.chaosId,
        name: entry.name || entry.chaosId,
        level: Number(entry.level || 0),
        levelName: `坍缩 ${Number(entry.level || 0)}`,
        detail: entry.functionDesc || entry.desc || "",
        desc: entry.desc || "",
        iconId: entry.iconId || "",
        icon: hasIcon(topicId, entry.iconId) ? `${topicId}/${entry.iconId}.png` : "",
        injectable: !!mechanic,
        unavailableReason: mechanic
          ? ""
          : "该坍缩暂无静态战斗配方",
        mechanic,
        inferred: !!mechanic,
        derived: !!staticMech,
        battleEffect: mechanic ? mechanic.battle !== false : false,
        note: mechanic?.note || "",
        layer: Number(entry.level || 0) + 1,
      };
    });
    return [{
      kind: "chaos",
      label: "坍缩范式",
      injectable: items.some((item) => item.injectable),
      unavailableReason: items.some((item) => item.injectable)
        ? ""
        : "当前没有可用的坍缩战斗 Buff 参数",
      items,
    }];
  }
  if (topicId === "rogue_4") {
    const items = values(modules.disaster && modules.disaster.disasterData).map(
      (entry) => {
        const staticMech = staticMechanic(topicId, "disaster", entry);
        const mechanic = staticMech;
        return {
          id: entry.id,
          name: entry.name || entry.id,
          level: Number(entry.level || 0),
          levelName: entry.levelName || "",
          detail: entry.functionDesc || entry.desc || "",
          desc: entry.desc || "",
          iconId: entry.iconId || "",
          icon: hasIcon(topicId, entry.iconId) ? `${topicId}/${entry.iconId}.png` : "",
          injectable: !!mechanic,
          unavailableReason: mechanic
            ? ""
            : "该年代暂无静态战斗配方",
          mechanic,
          inferred: !!mechanic,
          derived: !!staticMech,
          battleEffect: mechanic ? mechanic.battle !== false : false,
          note: mechanic?.note || "",
          layer: Number(entry.level || 1),
        };
      });
    return [{
      kind: "disaster",
      label: "年代",
      injectable: items.some((item) => item.injectable),
      unavailableReason: items.some((item) => item.injectable)
        ? ""
        : "当前没有可用的年代战斗 Buff 参数",
      items,
    }];
  }
  if (topicId === "rogue_5") {
    const items = values(modules.wrath && modules.wrath.wrathData).map((entry) => {
      const staticMech = staticMechanic(topicId, "wrath", entry);
      const mechanic = staticMech;
      return {
        id: entry.id,
        name: entry.name || entry.id,
        group: entry.group || "",
        level: Number(entry.level || 0),
        levelName: entry.levelName || "",
        detail: entry.functionDesc || entry.desc || "",
        desc: entry.desc || "",
        awakened: !!entry.isPacified,
        injectable: !!mechanic,
        unavailableReason: mechanic
          ? ""
          : "该岁时暂无静态战斗配方",
        mechanic,
        inferred: !!mechanic,
        derived: !!staticMech,
        battleEffect: mechanic ? mechanic.battle !== false : false,
        note: mechanic?.note || "",
        layer: Number(entry.level || 0) + 1,
      };
    });
    return [{
      kind: "wrath",
      label: "岁时",
      injectable: items.some((item) => item.injectable),
      unavailableReason: items.some((item) => item.injectable)
        ? ""
        : "当前没有可用的岁时战斗 Buff 参数",
      items,
    }];
  }
  if (topicId === "rogue_6") {
    const variations = values(detail.variationData).map((entry) => {
      const staticMech = staticMechanic(topicId, "variation", entry);
      const mechanic = staticMech;
      return {
        id: entry.id,
        name: entry.outerName || entry.innerName || entry.id,
        detail: entry.functionDesc || entry.desc || "",
        desc: entry.desc || "",
        iconId: entry.iconId || "",
        icon: hasIcon(topicId, entry.iconId) ? `${topicId}/${entry.iconId}.png` : "",
        injectable: !!mechanic,
        unavailableReason: mechanic
          ? ""
          : "该乌托邦暂无静态战斗配方",
        mechanic,
        inferred: !!mechanic,
        derived: !!staticMech,
        battleEffect: mechanic ? mechanic.battle !== false : false,
        note: mechanic?.note || "",
        layer: 1,
      };
    });
    const main = values(modules.weather && modules.weather.mainWeatherData).map(
      (entry) => {
        const staticMech = staticMechanic(topicId, "weather", entry);
        const mechanic = staticMech;
        return {
          id: entry.id,
          name: entry.name || entry.id,
          level: Number(entry.level || 0),
          levelName: entry.levelName || "",
          detail: entry.functionDesc || entry.desc || "",
          desc: entry.desc || "",
          iconId: entry.iconId || "",
          icon: hasIcon(topicId, entry.iconId) ? `${topicId}/${entry.iconId}.png` : "",
          positive: !!entry.isPositive,
          injectable: !!mechanic,
          unavailableReason: mechanic
            ? ""
            : "该实托邦暂无静态战斗配方",
          mechanic,
          inferred: !!mechanic,
          derived: !!staticMech,
          battleEffect: mechanic ? mechanic.battle !== false : false,
          note: mechanic?.note || "",
          layer: Number(entry.level || 1),
        };
      });
    const sub = values(modules.weather && modules.weather.subWeatherData).map(
      (entry) => {
        const staticMech = staticMechanic(topicId, "weather_sub", entry);
        const mechanic = staticMech;
        return {
          id: entry.id,
          name: entry.name || entry.id,
          level: 1,
          levelName: "实托邦·方针",
          detail: entry.functionDesc || entry.desc || "",
          desc: entry.desc || "",
          iconId: entry.iconId || "",
          icon: hasIcon(topicId, entry.iconId) ? `${topicId}/${entry.iconId}.png` : "",
          positive: !!entry.isPositive,
          injectable: !!mechanic,
          unavailableReason: mechanic
            ? ""
            : "该实托邦方针暂无静态战斗配方",
          mechanic,
          inferred: !!mechanic,
          derived: !!staticMech,
          battleEffect: mechanic ? mechanic.battle !== false : false,
          note: mechanic?.note || "",
          layer: 1,
        };
      });
    return [
      {
        kind: "variation",
        label: "乌托邦",
        injectable: variations.some((item) => item.injectable),
        unavailableReason: variations.some((item) => item.injectable)
          ? ""
          : "当前没有可用的乌托邦战斗配方",
        items: variations,
      },
      {
        kind: "weather",
        label: "实托邦",
        injectable: main.some((item) => item.injectable),
        unavailableReason: main.some((item) => item.injectable)
          ? ""
          : "当前没有可用的实托邦战斗 Buff 参数",
        items: main,
      },
      {
        kind: "weather_sub",
        label: "实托邦·方针",
        injectable: sub.some((item) => item.injectable),
        unavailableReason: sub.some((item) => item.injectable)
          ? ""
          : "当前没有可用的实托邦方针战斗 Buff 参数",
        items: sub,
      },
    ].filter((group) => group.items.length);
  }
  return [];
}

function charBuffData(topicId, detail, modules) {
  const items = detail.items || {};
  const result = values(detail.charBuffData)
    .map((entry) => {
      if (!entry || !entry.id) return null;
      const item = items[entry.id] || {};
      const iconId = cleanText(entry.iconId || "");
      return {
        id: entry.id,
        name: cleanText(entry.outerName || entry.innerName || item.name || entry.id),
        detail: cleanText(entry.functionDesc || entry.desc || item.usage || ""),
        desc: cleanText(item.description || ""),
        iconId,
        icon: hasIcon(topicId, iconId) ? `${topicId}/${iconId}.png` : "",
        buffKeys: values(entry.buffs).map((buff) => cleanText(buff.key)).filter(Boolean),
        injectable: true,
        layer: 1,
      };
    })
    .filter(Boolean);
  if (topicId === "rogue_5") {
    const candleId = cleanText(
      modules?.candle?.moduleConsts?.candleHolderBuffId);
    if (candleId && !result.some((entry) => entry.id === candleId)) {
      result.unshift({
        id: candleId,
        name: "伺烛客",
        detail: "将所选干员标记为伺烛客",
        desc: "部分岁时和界园机制只对伺烛客生效。",
        iconId: "",
        icon: "",
        buffKeys: [],
        injectable: true,
        layer: 1,
      });
    }
  }
  return result;
}

function outerBuff(key, pairs, keyField = "") {
  return {
    key,
    keyField,
    blackboard: pairs.map(([name, value]) => ({ key: name, value })),
  };
}

function numericBattleRecipe(text) {
  const scope = String(text || "");
  const globalScope =
    /所有我方单位|所有友方单位|我方在此处战斗时/.test(scope) &&
    !/随机|受到伤害后|部署后|每选择|经过此处/.test(scope);
  const buffs = [];
  if (globalScope) {
    const attackSpeed = /攻击速度\+(\d+)/.exec(scope);
    if (attackSpeed) {
      buffs.push(outerBuff("char_attribute_add", [
        ["attack_speed", Number(attackSpeed[1])],
      ]));
    }
    const attack = /攻击力\+(\d+)%/.exec(scope);
    if (attack) {
      buffs.push(outerBuff("char_attribute_mul", [
        ["atk", Number(attack[1]) / 100],
      ]));
    }
    const maxHp = /最大生命值?\+(\d+)%/.exec(scope);
    if (maxHp) {
      buffs.push(outerBuff("char_attribute_mul", [
        ["max_hp", Number(maxHp[1]) / 100],
      ]));
    }
    const defense = /防御力\+(\d+)%/.exec(scope);
    if (defense) {
      buffs.push(outerBuff("char_attribute_mul", [
        ["def", Number(defense[1]) / 100],
      ]));
    }
    const redeploy = /再部署时间-(\d+)%/.exec(scope);
    if (redeploy) {
      buffs.push(outerBuff("char_respawntime_mul", [
        ["scale", (100 - Number(redeploy[1])) / 100],
      ]));
    }
    const cost = /费用-(\d+)/.exec(scope);
    if (cost) {
      buffs.push(outerBuff("char_cost_add", [
        ["value", -Number(cost[1])],
      ]));
    }
  }
  const limit = /可同时部署人数\+(\d+)/.exec(scope);
  if (limit) {
    buffs.push(outerBuff("level_char_limit_add", [
      ["value", Number(limit[1])],
    ]));
  }
  return buffs.length ? buffs : null;
}

function fragmentData(topicId, detail, modules) {
  if (topicId !== "rogue_4") return [];
  const source = modules.fragment?.fragmentBuffData || {};
  const items = detail.items || {};
  return Object.entries(source)
    .map(([id, entry]) => {
      if (!entry || !entry.desc) return null;
      const item = items[id] || {};
      const iconId = cleanText(item.iconId || id);
      const buffs = numericBattleRecipe(entry.desc);
      return {
        id,
        name: cleanText(item.name || id),
        usage: cleanText(entry.desc || item.usage || ""),
        desc: cleanText(item.description || ""),
        iconId,
        icon: hasIcon(topicId, iconId) ? `${topicId}/${iconId}.png` : "",
        rarity: Number(item.rarity || 0),
        injectable: !!buffs,
        unavailableReason: buffs ? "" : "该思绪没有可直接映射的战斗数值",
        mechanic: buffs ? { buffs, battle: true } : null,
        layer: 1,
      };
    })
    .filter(Boolean);
}

function totemData(topicId, detail, modules) {
  if (topicId !== "rogue_3") return [];
  const source = modules.totemBuff?.totemBuffDatas || {};
  const items = detail.items || {};
  return Object.entries(source)
    .map(([id, entry]) => {
      const item = items[id] || {};
      const usage = cleanText(item.usage || entry.normalDesc || "");
      const buffs = numericBattleRecipe(usage);
      const iconId = cleanText(item.iconId || id);
      return {
        id,
        name: cleanText(item.name || id),
        usage,
        desc: cleanText(item.description || ""),
        iconId,
        icon: hasIcon(topicId, iconId) ? `${topicId}/${iconId}.png` : "",
        rarity: Number(item.rarity || 0),
        injectable: !!buffs,
        unavailableReason: buffs ? "" : "该密文只影响节点或局外资源",
        mechanic: buffs ? { buffs, battle: true } : null,
        layer: 1,
      };
    })
    .filter((entry) => entry.usage);
}

function copperData(topicId, detail, modules) {
  if (topicId !== "rogue_5") return [];
  const source = values(modules.copper && modules.copper.copperData);
  const changeIds = new Set(
    Object.keys(modules.copper?.changeCopperMap || {}));
  const items = detail.items || {};
  const seen = new Set();
  const result = source
    .filter((entry) => !changeIds.has(entry.id) &&
      !String(entry.id).startsWith("rogue_5_change_copper_"))
    .map((entry) => {
      if (entry.gildTypeId) return null;
      const buffItems = Array.isArray(entry.buffItemIdList)
        ? entry.buffItemIdList.filter(Boolean)
        : [];
      if (!buffItems.length) return null;
      const item = items[entry.id] || {};
      const iconId = cleanText(item.iconId || "");
      return {
        id: entry.id,
        name: cleanText(item.name || entry.id),
        usage: cleanText(item.usage || "")
          .replace(/（下次投钱前变化为[^）]*）/g, ""),
        desc: cleanText(item.description || ""),
        iconId,
        icon: hasIcon(topicId, iconId) ? `${topicId}/${iconId}.png` : "",
        luckyLevel: Number(entry.luckyLevel || 0),
        buffItems,
      };
    })
    .filter((entry) => {
      if (!entry) return false;
      const key = `${entry.name}|${entry.usage}`;
      if (seen.has(key)) return false;
      seen.add(key);
      return true;
    })
    .sort((left, right) =>
      right.luckyLevel - left.luckyLevel ||
      left.name.localeCompare(right.name, "zh-CN"));
  const nameCounts = new Map();
  for (const item of result) {
    nameCounts.set(item.name, (nameCounts.get(item.name) || 0) + 1);
  }
  const merged = result.filter((item) =>
    !(nameCounts.get(item.name) > 1 &&
      String(item.usage).includes("必定会投出")));
  const mergedCounts = new Map();
  for (const item of merged) {
    mergedCounts.set(item.name, (mergedCounts.get(item.name) || 0) + 1);
  }
  for (const item of merged) {
    if (mergedCounts.get(item.name) <= 1) continue;
    const usage = String(item.usage).replace(/<[^>]+>/g, "");
    const direction = usage.match(/方向朝([左右上下])/);
    if (direction) item.variantLabel = `朝${direction[1]}`;
  }
  return merged;
}

function techData(topicId, customize) {
  const common = customize.commonDevelopment || {};
  const developments = common.developments || customize.developments || {};
  return Object.values(developments)
    .filter((entry) => entry && entry.buffId)
    .map((entry) => {
      const iconId = entry.activeIconId || entry.buffIconId || entry.inactiveIconId || "";
      const rawDesc = cleanText(entry.rawDesc);
      return {
        id: entry.buffId,
        name: entry.buffName || entry.buffId,
        iconId,
        icon: hasIcon(topicId, iconId) ? `${topicId}/${iconId}.png` : "",
        inactiveIconId: entry.inactiveIconId || "",
        cost: Number(entry.tokenCost || 0),
        nodeType: Number(entry.nodeType || 0),
        groupId: entry.groupId || "",
        parents: Array.isArray(entry.frontNodeId) ? entry.frontNodeId : [],
        children: Array.isArray(entry.nextNodeId) ? entry.nextNodeId : [],
        row: Number(entry.positionRow || 0),
        order: Number(entry.positionOrder || 0),
        rawDesc: Array.isArray(rawDesc) ? rawDesc : (rawDesc ? [rawDesc] : []),
        display: Array.isArray(entry.buffDisplayInfo) ? entry.buffDisplayInfo : [],
      };
    })
    .sort((left, right) =>
      left.row - right.row ||
      left.order - right.order ||
      left.name.localeCompare(right.name, "zh-CN"));
}

function difficultyData(topicId, detail, customize) {
  const topicDifficulties = values(detail.difficulties)
    .map((entry) => ({
      mode: Number(entry.modeDifficulty || 0),
      grade: Number(entry.grade || 0),
      name: cleanText(entry.name || entry.modeName || ""),
      ruleDesc: cleanText(entry.ruleDesc || ""),
      addDesc: cleanText(entry.addDesc || ""),
    }))
    .filter((entry) => entry.mode > 0);
  const mods = new Map(
    values(customize.difficulties).map((entry) => [
      `${Number(entry.modeDifficulty || 0)}:${Number(entry.grade || 0)}`,
      entry,
    ]),
  );
  return topicDifficulties.map((entry) => {
    const mod = mods.get(`${entry.mode}:${entry.grade}`) || {};
    return {
      ...entry,
      title: entry.name || `难度 ${entry.grade}`,
      desc: cleanText(mod.buffDesc || mod.leftWeatherDesc ||
        mod.leftWrathDesc || mod.leftDisasterDesc || ""),
      buffs: Array.isArray(mod.buffs) ? mod.buffs : [],
      relicDevLevel: cleanText(mod.relicDevLevel || ""),
    };
  });
}

function specialOperatorData(topicId) {
  const zoneBuffRecipe = (buff) => {
    let keyField = "";
    const blackboard = [];
    for (const entry of Array.isArray(buff.blackboard) ? buff.blackboard : []) {
      const value = entry.valueStr !== null && entry.valueStr !== undefined
        ? entry.valueStr
        : entry.value;
      if (entry.key === "key") {
        keyField = String(value ?? "");
      } else {
        blackboard.push({ key: entry.key, value });
      }
    }
    return {
      key: String(buff.key || ""),
      keyField,
      blackboard,
    };
  };
  const radianMechanismChains = () => {
    const chains = {};
    const details = table.details.rogue_5 || {};
    for (const level of [1, 2, 3]) {
      const carrier = details.relics?.[`rogue_5_somaster_${6 + level}`];
      const zones = [];
      for (const buff of Array.isArray(carrier?.buffs) ? carrier.buffs : []) {
        if (buff.key !== "zone_into_reward") continue;
        const board = new Map(
          (buff.blackboard || []).map((entry) => [entry.key, entry]));
        const id = board.get("id")?.valueStr;
        const zone = Number(String(board.get("zone")?.valueStr || "")
          .replace("zone_", ""));
        const effect = details.relics?.[id];
        const buffs = (effect?.buffs || []).map(zoneBuffRecipe);
        if (zone > 0 && buffs.length) zones.push({ zone, id, buffs });
      }
      chains[level] = zones.sort((left, right) => left.zone - right.zone);
    }
    return chains;
  };
  const mechanismChains = topicId === "rogue_5"
    ? radianMechanismChains()
    : null;
  const result = [];
  const basics = specialOperators.operatorBasicData || {};
  for (const [charId, basic] of Object.entries(basics)) {
    if (String(basic?.targetId || "") !== topicId) continue;
    const masterIds = charMeta.charIdMasterListMap?.[charId] || [];
    const masteries = masterIds
      .map((masterId) => charMeta.charMasterDataMap?.[masterId])
      .filter((master) => master &&
        (Number(master.masterType) === 2 ||
          String(master.levelList?.[0]?.name || "").includes("战斗精通")))
      .map((master) => ({
        id: master.masterId,
        name: cleanText(master.levelList?.[0]?.name || master.masterId),
        type: Number(master.masterType || 0),
        injectable: Number(master.masterType) === 2,
        mechanism: master.masterId === "master_radian_5",
        chains: master.masterId === "master_radian_5" ? mechanismChains : null,
        levels: (master.levelList || []).map((level) => ({
          level: Number(level.level || 1),
          description: plainText(level.description || ""),
        })),
      }))
      .filter((master) => master.levels.length);
    if (!masteries.length) continue;
    const avatarFile = `${charId}_2.png`;
    result.push({
      charId,
      name: cleanText(characterTable[charId]?.name || charId),
      avatar: fs.existsSync(path.join(avatarRoot, avatarFile))
        ? `avatar/${avatarFile}`
        : "",
      masteries,
    });
  }
  return result;
}

const topics = Object.entries(topicMeta).map(([topicId, meta]) => {
  const detail = table.details[topicId] || {};
  const modules = table.modules[topicId] || {};
  const customize = table.customizeData[topicId] || {};
  const topic = {
    id: topicId,
    name: meta.name,
    specialLabel: meta.specialLabel,
    difficulties: difficultyData(topicId, detail, customize),
    relics: relicData(topicId, detail),
    special: gateSpecial(topicId, specialData(topicId, detail, modules)),
    charBuffs: charBuffData(topicId, detail, modules),
    charBuffTitle: meta.charBuffTitle || "干员 Buff",
    charBuffNote: meta.charBuffNote || "选中后只作用于下方勾选的编队干员。",
    specialOperators: specialOperatorData(topicId),
    resourceKeys: meta.resourceKeys || ["gold"],
    copper: copperData(topicId, detail, modules),
    fragments: fragmentData(topicId, detail, modules),
    totems: totemData(topicId, detail, modules),
    concepts: conceptData(topicId, detail, modules),
    tech: techData(topicId, customize),
  };
  normalizeVariantIcons(topic.relics);
  normalizeVariantIcons(topic.charBuffs);
  normalizeVariantIcons(topic.copper);
  normalizeVariantIcons(topic.fragments);
  normalizeVariantIcons(topic.totems);
  normalizeVariantIcons(topic.concepts);
  normalizeVariantIcons(topic.tech);
  normalizeIconExtensions(topic);
  for (const group of topic.special) {
    normalizeVariantIcons(group.items);
  }
  return topic;
});

const payload = {
  version: 1,
  generatedAt: new Date().toISOString(),
  topics,
};
const text = `window.BETTERBATTLE_BATTLE_CONFIG=${JSON.stringify(payload)};\n`;
fs.mkdirSync(path.dirname(outputPath), { recursive: true });
fs.writeFileSync(outputPath, text, "utf8");
console.log(
  `wrote ${outputPath}: ${topics.length} topics, ` +
  `${topics.reduce((sum, topic) => sum + topic.relics.length, 0)} relics, ` +
  `${topics.reduce((sum, topic) => sum + topic.special.reduce((n, group) => n + group.items.length, 0), 0)} specials, ` +
  `${topics.reduce((sum, topic) => sum + topic.charBuffs.length, 0)} char buffs, ` +
  `${topics.reduce((sum, topic) => sum + topic.fragments.length + topic.totems.length, 0)} run buffs, ` +
  `${topics.reduce((sum, topic) => sum + topic.concepts.length, 0)} concepts, ` +
  `${topics.reduce((sum, topic) => sum + topic.tech.length, 0)} tech nodes`);
