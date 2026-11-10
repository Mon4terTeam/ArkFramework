"use strict";

const fs = require("fs");
const path = require("path");

const resourceRoot = process.argv[2] ||
  "E:/Games/ArkTest/_assets/ArknightsGameResource";
const webviewRoot = path.resolve(__dirname, "..");
const uiRoot = path.join(webviewRoot, "ui");
const gameAssets = path.join(uiRoot, "assets", "game");

const characters = JSON.parse(fs.readFileSync(
  path.join(resourceRoot, "gamedata", "excel", "character_table.json"),
  "utf8",
));
const skills = JSON.parse(fs.readFileSync(
  path.join(resourceRoot, "gamedata", "excel", "skill_table.json"),
  "utf8",
));

const moduleData = JSON.parse(
  fs.readFileSync(path.join(gameAssets, "module-index.js"), "utf8")
    .replace(/^window\.BETTER_BATTLE_MODULES\s*=\s*/, "")
    .replace(/;\s*$/, ""),
);
const avatarMap = JSON.parse(
  fs.readFileSync(path.join(gameAssets, "avatar-index.js"), "utf8")
    .replace(/^window\.BETTER_BATTLE_AVATARS\s*=\s*/, "")
    .replace(/;\s*$/, ""),
);

const portraitFiles = new Set(fs.readdirSync(path.join(gameAssets, "portrait")));
const skillFiles = new Set(fs.readdirSync(path.join(gameAssets, "skill")));

const professionMap = {
  PIONEER: "先锋",
  WARRIOR: "近卫",
  TANK: "重装",
  SNIPER: "狙击",
  CASTER: "术师",
  MEDIC: "医疗",
  SUPPORT: "辅助",
  SPECIAL: "特种",
};

function encodePath(file) {
  return file.split("/").map(encodeURIComponent).join("/");
}

function skillIconFor(character, skillId) {
  const expected = `skill_icon_${skillId}.png`;
  if (skillFiles.has(expected)) return expected;
  for (const entry of character.skills || []) {
    const candidate = `skill_icon_${entry.skillId}.png`;
    if (skillFiles.has(candidate)) return candidate;
  }
  return "generic.svg";
}

const operators = [];

for (const id of Object.keys(characters)) {
  const character = characters[id];
  if (!id.startsWith("char_") || character.isNotObtainable) continue;

  const portraitFile = portraitFiles.has(`${id}_2.png`)
    ? `${id}_2.png`
    : (portraitFiles.has(`${id}_1.png`) ? `${id}_1.png` : "");
  if (!portraitFile) continue;

  const avatarFile = avatarMap[id] ? path.basename(avatarMap[id]) : "";
  const operatorSkills = (character.skills || []).map((entry) => {
    const skill = skills[entry.skillId];
    const name = skill?.levels?.at(-1)?.name ||
      skill?.levels?.[0]?.name ||
      entry.skillId;
    return {
      id: entry.skillId,
      name,
      icon: `assets/game/skill/${encodePath(skillIconFor(character, entry.skillId))}`,
    };
  });

  const operatorModules = (moduleData.characters?.[id] || [])
    .map((moduleId) => moduleData.modules?.[moduleId])
    .filter(Boolean)
    .sort((left, right) => Number(left.order || 0) - Number(right.order || 0))
    .map((module) => ({
      id: module.id,
      name: module.name || module.id,
      type: module.typeLabel || module.type || "MOD",
    }));

  operators.push({
    id,
    name: character.name,
    rarity: Number(character.rarity) + 1,
    profession: professionMap[character.profession] || character.profession || "未知",
    avatar: avatarFile
      ? `assets/game/avatar/${encodePath(avatarFile)}`
      : `assets/game/portrait/${encodePath(portraitFile)}`,
    portrait: `assets/game/portrait/${encodePath(portraitFile)}`,
    skills: operatorSkills,
    modules: operatorModules,
    eliteCaps: (character.phases || [])
      .map((phase) => Number(phase.maxLevel))
      .filter((level) => level > 0),
    defaultSkill: Math.max(0, operatorSkills.length - 1),
    defaultModule: "",
    sortIndex: Number(character.sortIndex),
  });
}

operators.sort((left, right) =>
  left.sortIndex - right.sortIndex ||
  left.name.localeCompare(right.name, "zh-CN"));

const output = `window.ROSTER_CATALOG = ${JSON.stringify(operators)};\n`;
fs.writeFileSync(path.join(gameAssets, "roster-data.js"), output, "utf8");

console.log(JSON.stringify({
  operators: operators.length,
  withModules: operators.filter((operator) => operator.modules.length).length,
  skills: operators.reduce((sum, operator) => sum + operator.skills.length, 0),
  bytes: Buffer.byteLength(output),
}, null, 2));
