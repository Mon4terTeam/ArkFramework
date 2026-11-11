"use strict";

/*
 * Static roguelike mechanic recipes.
 *
 * The client tables only contain presentation data (id/name/level/description)
 * for chaos / disaster / wrath / weather.  The numbers live in the server-side
 * unKeepBuff payload.  To keep BetterBattle usable without first capturing a
 * real run we translate the description into the same global-buff instructions
 * the client already understands.
 *
 * Recipes are intentionally marked "derived": they are exact for the common
 * numeric modifiers and best-effort for scripted mechanics (extra enemies,
 * tile/zone conditions, shop/map effects).
 */

const ALL_PROFESSIONS =
  "warrior|sniper|tank|medic|support|caster|special|pioneer";

const PROFESSION_BY_SUFFIX = {
  _1: "warrior",
  _2: "sniper",
  _3: "support",
  _4: "caster",
  _5: "special",
  _6: "pioneer",
};

function pair(key, value) {
  return { key, value };
}

function makeBuff(key, keyField, pairs) {
  return {
    key,
    keyField: keyField || "",
    blackboard: (pairs || []).map(([name, value]) => pair(name, value)),
  };
}

function direct(key, pairs) {
  return makeBuff(key, "", pairs);
}

function prefab(key, pairs) {
  return makeBuff("global_buff_normal", key, pairs);
}

function hiddenGroup(group) {
  return makeBuff("level_hidden_group_enable", group, []);
}

function hiddenGroupChoice(groups) {
  return {
    key: "level_hidden_group_enable",
    keyField: "",
    blackboard: [],
    randomKeyField: groups,
  };
}

function enemyMul(pairs) {
  return direct("enemy_attribute_mul", pairs);
}

function allyMul(pairs) {
  return direct("char_attribute_mul", pairs);
}

function allyAdd(pairs) {
  return direct("char_attribute_add", pairs);
}

function costAdd(value) {
  return direct("char_cost_add", [
    ["cost", value],
    ["selector.profession", ALL_PROFESSIONS],
  ]);
}

function initCostAdd(value) {
  return direct("level_init_cost_add", [["value", value]]);
}

function limitAdd(value) {
  return direct("level_char_limit_add", [["value", value]]);
}

function lifeAdd(value) {
  return direct("level_life_point_add", [["value", value]]);
}

function costRecover(scale) {
  return direct("level_cost_increase_time_mul", [["scale", scale]]);
}

function skillCost(scale) {
  return direct("char_skill_cost_mul", [
    ["scale", scale],
    ["selector.profession", ALL_PROFESSIONS],
  ]);
}

function enemyAttrFinal(values) {
  const pairs = [];
  if (values.max_hp !== undefined) pairs.push(["max_hp", values.max_hp]);
  if (values.move_speed !== undefined) pairs.push(["move_speed", values.move_speed]);
  if (values.attack_speed !== undefined) pairs.push(["attack_speed", values.attack_speed]);
  if (values.atk !== undefined) pairs.push(["atk", values.atk]);
  if (values.def !== undefined) pairs.push(["def", values.def]);
  return prefab("enemy_attr_final[tag]", pairs);
}

function probDoubleHp(prob, multiplier) {
  return prefab("rogue_3_probDoubleEnemyHP", [
    ["prob", prob],
    ["max_hp", multiplier],
  ]);
}

function deployDamage(ratio, once) {
  return prefab(
    once === false ? "deploy_damage_except_trap" : "deploy_damage_except_trap[once]",
    [["value", ratio]],
  );
}

function deployMaxHpDamage(ratio, once) {
  return prefab(once === false ? "deploy_damage" : "deploy_damage[once]", [
    ["value", ratio],
  ]);
}

function camouflage() {
  return prefab("rogue_2_camouflage", []);
}

function hideEnemyHud() {
  return prefab("rogue_3_hideEnemyHUD", []);
}

function nonBattle(note) {
  return { buffs: [], battle: false, note: note || "该机制不产生战斗 Buff" };
}

function recipe(buffs, note, confidence) {
  return {
    buffs: (buffs || []).filter(Boolean),
    battle: true,
    note: note || "",
    confidence: confidence || "derived",
  };
}

function levelIndex(id, suffixA, suffixB, suffixC) {
  if (id.endsWith("_c")) return 2;
  if (id.endsWith("_b")) return 1;
  if (id.endsWith("_a")) return 0;
  return -1;
}

function pick(values, index) {
  return values[Math.max(0, Math.min(values.length - 1, index))];
}

function professionForWrath(id) {
  const match = id.match(/_(?:a|b|c)_([1-6])$/);
  if (match) return PROFESSION_BY_SUFFIX[`_${match[1]}`];
  if (id.endsWith("_b_1")) return "warrior|support";
  if (id.endsWith("_b_2")) return "special|sniper";
  if (id.endsWith("_b_3")) return "caster|pioneer";
  if (id.endsWith("_c_1")) return "warrior|support|special";
  if (id.endsWith("_c_2")) return "pioneer|sniper|caster";
  return "";
}

function chaosRecipe(id) {
  const level = id.endsWith("_a") ? 1 : 0;
  const base = id.replace(/_a$/, "");

  switch (base) {
    case "rogue_3_chaos_1":
      return nonBattle("非战斗节点选项：只影响希望/源石锭与坍缩值");
    case "rogue_3_chaos_2":
      return recipe([
        prefab("character_on_creep_tile[atk_speed_down]", [
          ["attack_speed", level ? -75 : -50],
        ]),
      ], "国度按通用地形减速实现；地形生成位置由关卡脚本决定");
    case "rogue_3_chaos_3":
      return recipe([
        enemyAttrFinal(
          level
            ? { max_hp: 1.2, move_speed: 1.2, attack_speed: 20 }
            : { max_hp: 1.15, move_speed: 1.15, attack_speed: 0 },
        ),
      ], "0 级数值已与关卡说明核对");
    case "rogue_3_chaos_4":
      return recipe([probDoubleHp(level ? 0.1 : 0.05, 2.0)]);
    case "rogue_3_chaos_5":
      return nonBattle("只影响诡意行商售价");
    case "rogue_3_chaos_6":
      return recipe([costRecover(level ? 1.6666667 : 1.25)],
        "按费用回复速度降低 40%/20% 换算为时间倍率");
    case "rogue_3_chaos_7":
      return recipe([deployMaxHpDamage(level ? 0.6 : 0.3, true)], "部署时按最大生命值扣血，退场前一次");
    case "rogue_3_chaos_8":
      return recipe([lifeAdd(level ? -2 : -1)], "抗干扰指数降低时目标生命上限减少");
    case "rogue_3_chaos_9":
      return recipe([
        hideEnemyHud(),
        ...(level ? [enemyMul([["max_hp", 0.1]])] : []),
      ]);
    case "rogue_3_chaos_10":
      return recipe([
        camouflage(),
        ...(level ? [costAdd(1)] : []),
      ]);
    default:
      return null;
  }
}

function disasterRecipe(id) {
  const level = levelIndex(id, "_a", "_b", "_c");
  if (level < 0) return null;
  const base = id.replace(/_[abc]$/, "");

  switch (base) {
    case "rogue_4_disaster_1":
      return recipe([
        enemyMul([["max_hp", [1.0, 1.5, 2.0][level]]]),
      ], "年代之刺最大生命值；额外年代之刺由关卡脚本生成，Buff 无法生成敌人");
    case "rogue_4_disaster_2":
      return recipe([
        enemyMul([
          ["atk", [0.2, 0.35, 0.5][level]],
          ["selector.tag", "sarkaz"],
        ]),
        prefab("rogue_4_disaster_buff_8", []),
      ], "萨卡兹敌人攻击力 + 年代印痕内不可主动撤退");
    case "rogue_4_disaster_3":
      return recipe([deployDamage([0.25, 0.5, 0.7][level], true)]);
    case "rogue_4_disaster_4":
      return recipe([costAdd([2, 3, 5][level])]);
    case "rogue_4_disaster_5":
      return recipe([enemyMul([["max_hp", [0.2, 0.3, 0.5][level]]])]);
    case "rogue_4_disaster_6":
      return recipe([limitAdd([-1, -2, -3][level])]);
    case "rogue_4_disaster_7":
      return nonBattle("只影响去伪存真解读概率");
    case "rogue_4_disaster_8":
      return recipe([allyMul([["max_hp", [0.2, 0.3, 0.5][level]]])]);
    case "rogue_4_disaster_9":
      return recipe([camouflage()]);
    default:
      return null;
  }
}

function wrathRecipe(id) {
  if (id.endsWith("_d")) {
    switch (id) {
      case "rogue_5_wrath_1_d":
        return recipe([prefab("periodic_damage[stun&frozen&unmovable]", [
          ["damage", 1],
          ["interval", 1],
          ["selector.tag", "huawu"],
        ])], "化物敌人每秒 1 点伤害；tag 为按描述推导");
      case "rogue_5_wrath_2_d":
        return recipe([
          direct("deck_card_buff", [
            ["value", -6],
            ["cost_scale", 1],
            ["selector.profession", ALL_PROFESSIONS],
          ]),
          prefab("rogue_5_first_character_create_card_buff_to_deck", [
            ["value", 6],
            ["cost_scale", 1],
            ["selector.profession", ALL_PROFESSIONS],
          ]),
        ], "实测 rogue_5_wrath_2_d：deck_card_buff(value=-6) + "
          + "rogue_5_first_character_create_card_buff_to_deck(value=6)；"
          + "两条共同实现仅首位干员部署费用 -6");
      case "rogue_5_wrath_3_d":
        return nonBattle("只影响诡意行商/易与商品价格");
      case "rogue_5_wrath_4_d":
        return recipe([lifeAdd(1)]);
      case "rogue_5_wrath_5_d":
        return recipe([skillCost(0.95)],
          "实测 rogue_5_wrath_5_d：char_skill_cost_mul，" +
          "scale=0.95、全职业；真实战斗捕获");
      case "rogue_5_wrath_6_d":
        return recipe([allyMul([["max_hp", 0.1]])]);
      case "rogue_5_wrath_7_d":
        return recipe([allyMul([["atk", 0.1]])]);
      case "rogue_5_wrath_8_d":
        return recipe([enemyMul([
          ["atk", -0.05],
          ["selector.tag", "animated"],
        ])], "按申铸普通档实测 selector.tag=animated 推导；醒觉仅化物攻击力 -5%");
      case "rogue_5_wrath_9_d":
        return recipe([prefab("rogue_5_first_attack_with_stun", [
          ["stun_time", 1],
        ])]);
      case "rogue_5_wrath_10_d":
        return recipe([
          prefab("rogue_5_enemy_take_damage_random_sleep", [
            ["prob", 0.01],
            ["sleep_time", 5],
          ]),
          enemyMul([["damage_scale", 0.05]]),
        ]);
      case "rogue_5_wrath_11_d":
        return recipe([camouflage(), initCostAdd(3)]);
      case "rogue_5_wrath_12_d":
        return recipe([prefab("rogue_5_sp_recovery_with_tag", [
          ["tag", "sui"],
          ["interval", 3],
          ["sp", 1],
        ])], "岁阵营技力回复；tag 为按描述推导");
      default:
        return null;
    }
  }

  const match = id.match(/^rogue_5_wrath_(\d+)_([abc])(?:_([1-6]))?$/);
  if (!match) return null;
  const index = match[2] === "c" ? 2 : match[2] === "b" ? 1 : 0;
  const number = Number(match[1]);
  const profession = professionForWrath(id);

  switch (number) {
    case 1: {
      const pairs = [
        ["prob", 1],
        ["max_hp", 0.9],
        ["atk", 0.9],
      ];
      if (profession) pairs.push(["selector.profession", profession]);
      return recipe([prefab("rogue_5_character_deploy_attribute_down", pairs)],
        "便符没有独立客户端模板，按首次部署属性下降近似；具体便符效果需实测校准");
    }
    case 2:
      return recipe(
        [costAdd([2, 3, 4][index])],
        index === 0
          ? "实测 rogue_5_wrath_2_a：CCostAdd，cost=2，全职业；真实战斗捕获"
          : "同 CCostAdd 链路，按等级取 cost=3/4");
    case 3:
      return nonBattle("只影响诡意行商售价");
    case 4:
      return nonBattle("只影响非战斗节点的事件概率/目标生命");
    case 5:
      return recipe([prefab("rogue_5_character_deploy_lose_sp", [
        ["prob", 1],
        ["sp", [0.1, 0.2, 0.3][index]],
        ["selector.profession", ALL_PROFESSIONS],
      ])], index === 1
        ? "实测 rogue_5_wrath_5_b：rogue_5_character_deploy_lose_sp，" +
          "sp=0.2、prob=1、全职业；真实战斗捕获"
        : "同 rogue_5_character_deploy_lose_sp 链路，按等级取 sp=0.1/0.3");
    case 6:
      return recipe([prefab("rogue_5_character_deploy_attribute_down", [
        ["prob", [0.1, 0.1, 0.15][index]],
        ["max_hp", [0.8, 0.7, 0.6][index]],
      ])]);
    case 7:
      return recipe([prefab("rogue_5_enemy_speg_weather", [
        ["damage_scale", [1.1, 1.15, 1.25][index]],
        ["prob", 1],
        ["selector.profession", ALL_PROFESSIONS],
      ])], index === 0
        ? "实测 rogue_5_wrath_7_a：rogue_5_enemy_speg_weather，"
          + "damage_scale=1.1、prob=1、全职业；真实战斗捕获"
        : "同 rogue_5_enemy_speg_weather 链路，按等级取 1.15/1.25");
    case 8:
      return recipe([enemyMul([
        ["max_hp", [0.1, 0.2, 0.3][index]],
        ["atk", [0.1, 0.2, 0.3][index]],
        ["selector.tag", "animated"],
      ])], "实测 rogue_5_wrath_8_a：selector.tag=animated，max_hp/atk +10%");
    case 9:
      return recipe([prefab("rogue_5_character_random_add_stun", [
        ["prob", [0.005, 0.01, 0.01][index]],
        ["stun_time", [0.5, 0.5, 1][index]],
      ])], "模板读取 prob；眩晕时长通过 stun 的 Blackboard 传递");
    case 10:
      return recipe([prefab("rogue_5_enemy_take_damage_random_sleep", [
        ["prob", 0.01],
        ["sleep_time", 5],
      ])]);
    case 11:
      return recipe([camouflage()]);
    case 12:
      return recipe([prefab("rogue_5_sp_recovery_with_tag", [
        ["tag", "sui"],
        ["interval", [4, 3, 2][index]],
        ["sp", 1],
      ])], "岁阵营技力回复；tag 为按描述推导");
    default:
      return null;
  }
}

function weatherRecipe(id) {
  if (id.startsWith("rogue_6_subweather_")) {
    switch (id) {
      case "rogue_6_subweather_1":
        return nonBattle("只影响节点源石锭消耗");
      case "rogue_6_subweather_2":
        return recipe([lifeAdd(-1)], "护盾 -1 用目标生命上限近似");
      case "rogue_6_subweather_3":
        return recipe([lifeAdd(-1)]);
      case "rogue_6_subweather_4":
        return nonBattle("只影响自然物估价");
      default:
        return null;
    }
  }

  const level = levelIndex(id, "_a", "_b", "_c");
  if (level < 0) return null;
  const base = id.replace(/_[abc]$/, "");

  switch (base) {
    case "rogue_6_weather_1":
      return recipe([
        prefab("enemy_max_hp_down", [
          ["max_hp", [1.1, 1.2, 1.3][level]],
        ]),
        prefab("enemy_atk_down", [
          ["atk", [1.1, 1.2, 1.3][level]],
        ]),
        level === 0
          ? hiddenGroup("shsdgo")
          : level === 2
            ? hiddenGroup("shsdgo4")
            : hiddenGroupChoice(["shsdgo2", "shsdgo3"]),
      ], level === 1
        ? "黑流地脉 _b 的真实隐藏组未确认；为补全在 shsdgo2/shsdgo3 中随机选择一个"
        : level === 2
          ? "黑流地脉 _c 实测打开 shsdgo4（8 条猎犬动作）"
          : "黑流地脉 _a 实测打开 shsdgo（4 条猎犬动作）");
    case "rogue_6_weather_2":
      return recipe([
        makeBuff("misc_add_env_system", "", [
          ["key", "env_047_rogue6_storm"],
          ["trig_type", "TAKE_OUT"],
          ["atk", -[0.2, 0.35, 0.5][level]],
          ["level", level],
        ]),
        enemyMul([["move_speed", -0.15]]),
      ], level === 1
        ? "实测 _b：环境系统 env_047_rogue6_storm，atk=-0.35、level=1；敌方移速 -15%"
        : "环境系统 env_047_rogue6_storm；_b 已实测，0/2 级数值按描述推导");
    case "rogue_6_weather_3":
      return recipe([prefab("rogue_6_weather_3", [
        ["freeze_duration", [5, 10, 15][level]],
        ["cold_duration", 15],
        ["interval", [60, 45, 30][level]],
      ])], "部署冻结 + 周期寒冷");
    case "rogue_6_weather_4":
      return recipe([prefab("damage_scale[in][phy]", [
        ["damage_scale", [1.25, 1.4, 1.7][level]],
      ])], "我方受到物理伤害提高");
    case "rogue_6_weather_5":
      return nonBattle("只影响零件售价与商店栏位");
    case "rogue_6_weather_6":
      return recipe([deployMaxHpDamage([0.3, 0.5, 0.7][level], true)]);
    case "rogue_6_weather_7":
      return recipe([costRecover([1.25, 1.3333334, 1.6666667][level])]);
    case "rogue_6_weather_8":
      return recipe([allyMul([
        ["atk", [0.1, 0.15, 0.2][level]],
        ["max_hp", [0.1, 0.2, 0.3][level]],
      ])]);
    case "rogue_6_weather_9":
      return nonBattle("只影响节点信息揭示");
    case "rogue_6_weather_10":
      return recipe([camouflage()]);
    default:
      return null;
  }
}

function variationRecipe(id) {
  switch (id) {
    case "variation_1":
      return recipe([
        allyMul([["atk", 0.5]]),
        makeBuff("char_ability_new", "rogue_6_wooden_weather_1", [
          ["range_id", "x-4"],
          ["damage_scale", 0.2],
        ]),
      ], "巨人摇篮：我方攻击力 +50%；x-4 范围外目标受到物理/法术伤害 ×0.2");
    case "variation_2":
      return recipe([
        direct("char_skill_cost_mul", [["scale", 0.6]]),
        prefab("rogue_6_wooden_weather_2", [["cooldown", 3]]),
      ], "迪斯科狂热：技力需求 ×0.6；技能结束后撤退并按原格/周围八格随机方向再部署，3 秒冷却");
    case "variation_3":
      return recipe([
        prefab("rogue_6_wooden_weather_3", [
          ["ally_damage", 0.03],
          ["enemy_damage", 0.005],
          ["cnt", 1],
        ]),
      ], "已知浩劫：每秒按最大生命比例造成真实伤害；敌方角色/召唤物/装置除外，不触发受击回复");
    case "variation_4":
      return recipe([
        makeBuff("char_ability_new", "rogue_6_wooden_weather_4", []),
      ], "孤立石林：动态 ability；攻击范围内有友方时攻速 +30，否则 -50");
    case "variation_7":
      return recipe([
        prefab("rogue_6_wooden_weather_7", [
          ["value", 1],
          ["atk", 0.004],
          ["max_hp", 0.01],
          ["atk_max", 0.4],
          ["max_hp_max", 1.0],
        ]),
      ], "源石之城：每 1 源石锭使敌方攻击力 +0.4%、最大生命 +1%，攻击力最多 +40%、生命最多 +100%");
    case "variation_5":
      return nonBattle("只影响地图节点刷新");
    case "variation_6":
      return nonBattle("只影响战斗外“居民”恶意生成");
    case "variation_8":
      return nonBattle("只影响狭路相逢出战限制");
    case "variation_9":
      return nonBattle("只影响失与得节点源石锭");
    default:
      return null;
  }
}

function staticMechanic(topicId, kind, entry) {
  const id = String(entry?.chaosId || entry?.id || "");
  if (!id) return null;
  let result = null;
  if (topicId === "rogue_3" && kind === "chaos") {
    result = chaosRecipe(id);
  } else if (topicId === "rogue_4" && kind === "disaster") {
    result = disasterRecipe(id);
  } else if (topicId === "rogue_5" && kind === "wrath") {
    result = wrathRecipe(id);
  } else if (topicId === "rogue_6" && (kind === "weather" || kind === "weather_sub")) {
    result = weatherRecipe(id);
  } else if (topicId === "rogue_6" && kind === "variation") {
    result = variationRecipe(id);
  }
  if (!result) return null;
  return {
    ...result,
    source: "static",
    derived: true,
  };
}

module.exports = { staticMechanic };
