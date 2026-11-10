"use strict";

  const pages = window.BetterBattlePages || (window.BetterBattlePages = {});
  const battlePage = pages.battle || {};
  const renderBattle = battlePage.render || (() => {});
  const selectCard = battlePage.selectCard || (() => {});
  const selectSummon = battlePage.selectSummon || (() => {});
  const renderModuleTabs = battlePage.renderModuleTabs || (() => {});
  const enemyPage = pages.enemies || {};
  const renderEnemies = enemyPage.render || (() => {});
  const selectEnemy = enemyPage.select || (() => {});
  const damagePage = pages.damage || {};
  const renderDamagePage = damagePage.render || (() => {});
  const mergeDamagePayload = damagePage.mergePayload || ((next) => next);
  const diagnosticsPage = pages.diagnostics || {};
  const renderDiagnostics = diagnosticsPage.render || (() => {});
  const renderTrace = diagnosticsPage.renderTrace || (() => {});
  const renderTraceFields = diagnosticsPage.renderTraceFields || (() => {});
  const renderEvents = diagnosticsPage.renderEvents || (() => {});
  const configPage = pages.config || {};
  const configTopicData = configPage.topicData || (() => null);
  const configEntry = configPage.entry || (() => null);
  const applyConfigToSquad = configPage.applyToSquad || (() => false);
  const normalizeBattleConfig = configPage.normalizeBattleConfig || ((value) => value);
  const normalizeSkillLevels = configPage.normalizeSkillLevels || ((value) => value);
  const renderConfig = configPage.render || (() => {});
  const renderConfigCopper = configPage.renderCopper || (() => {});
  const renderConfigRelics = configPage.renderRelics || (() => {});
  const renderConfigRunBuffs = configPage.renderRunBuffs || (() => {});
  const rogueConfigArgs = configPage.rogueConfigArgs || (() => "");
  const setConfigConceptLayer = configPage.setConceptLayer || (() => {});
  const setConfigRelicLayer = configPage.setRelicLayer || (() => {});
  const setConfigEntry = configPage.setEntry || (() => {});
  const setConfigMasteryLevel = configPage.setMasteryLevel || (() => {});
  const setConfigMasteryZone = configPage.setMasteryZone || (() => {});
  const setConfigResource = configPage.setResource || (() => {});
  const setConfigSpecialAwake = configPage.setSpecialAwake || (() => {});
  const setConfigSpecialVariant = configPage.setSpecialVariant || (() => {});
  const setConfigTechAll = configPage.setTechAll || (() => {});
  const toggleConfigCharBuff = configPage.toggleCharBuff || (() => {});
  const toggleConfigCharBuffTarget = configPage.toggleCharBuffTarget || (() => {});
  const toggleConfigConcept = configPage.toggleConcept || (() => {});
  const toggleConfigCopper = configPage.toggleCopper || (() => {});
  const toggleConfigMastery = configPage.toggleMastery || (() => {});
  const toggleConfigRelic = configPage.toggleRelic || (() => {});
  const toggleConfigRunBuff = configPage.toggleRunBuff || (() => {});
  const toggleConfigSpecial = configPage.toggleSpecial || (() => {});
  const toggleConfigTech = configPage.toggleTech || (() => {});
  const truncate = configPage.truncate || ((value) => String(value ?? ""));
  const skillLevelsFor = configPage.skillLevelsFor || (() => []);
  const squadPage = pages.squad || {};
  const stageLabel = squadPage.stageLabel || (() => "");
  const activeRogueTopic = squadPage.activeRogueTopic || (() => "");
  const activeStageCatalog = squadPage.activeStageCatalog || (() => []);
  const stageById = squadPage.stageById || (() => null);
  const stageTopicName = squadPage.stageTopicName || (() => "");
  const resolveStageQuery = squadPage.resolveStageQuery || (() => null);
  const currentStageId = squadPage.currentStageId || (() => "");
  const stageInputShowsSelectedLabel =
    squadPage.stageInputShowsSelectedLabel || (() => false);
  const positionStageOptions = squadPage.positionStageOptions || (() => {});
  const renderStageOptions = squadPage.renderStageOptions || (() => {});
  const openStageDropdown = squadPage.openStageDropdown || (() => {});
  const closeStageDropdown = squadPage.closeStageDropdown || (() => {});
  const selectStage = squadPage.selectStage || (() => {});
  const moveStageActive = squadPage.moveStageActive || (() => {});
  const renderStageField = squadPage.renderStageField || (() => {});
  const firstEmptySquadIndex = squadPage.firstEmptySquadIndex || (() => -1);
  const eliteCapsFor = squadPage.eliteCapsFor || (() => null);
  const isSquadLocked = squadPage.isSquadLocked || (() => false);
  const battleInProgress = squadPage.battleInProgress || (() => false);
  const syncSquadFromSnapshot = squadPage.syncSquadFromSnapshot || (() => {});
  const serializeSquad = squadPage.serializeSquad || (() => "");
  const addOperator = squadPage.addOperator || (() => {});
  const removeOperator = squadPage.removeOperator || (() => {});
  const renderSquad = squadPage.render || (() => {});
  const renderSquadPicker = squadPage.renderSquadPicker || (() => {});
  const openSquadPicker = squadPage.openSquadPicker || (() => {});
  const closeSquadPicker = squadPage.closeSquadPicker || (() => {});
  const renderRosterPreview = squadPage.renderRosterPreview || (() => {});
  const renderQuickResults = squadPage.renderQuickResults || (() => {});

  const MAX_SLOTS = 14;
  const DEFAULT_SQUAD_LIMIT = 12;
  const protocol = window.BetterBattleProtocol;
  const session = window.BetterBattleSession;
  const UI_STATE_VERSION = 2;
  const STORAGE_KEY = "betterbattle.ui.v1";
  const TIME_UNIT_STORAGE_KEY = "betterbattle.timeUnit.v1";
  const TIME_FPS = 30;
  const ROSTER_CATALOG = Array.isArray(window.ROSTER_CATALOG)
    ? window.ROSTER_CATALOG
    : [];
  const ROSTER_BY_ID = new Map(
    ROSTER_CATALOG.map((operator) => [String(operator.id), operator]),
  );
  const STAGE_CATALOG = Array.isArray(window.BETTERBATTLE_STAGES)
    ? window.BETTERBATTLE_STAGES
    : [];
  const STAGE_BY_ID = new Map(
    STAGE_CATALOG.map((stage) => [String(stage.id), stage]),
  );
  function variantLabel(index) {
    let value = index + 1;
    let label = "";
    while (value > 0) {
      value -= 1;
      label = String.fromCharCode(65 + value % 26) + label;
      value = Math.floor(value / 26);
    }
    return label;
  }

  const ROGUE_TOPICS = (Array.isArray(window.BETTERBATTLE_ROGUELIKE)
    ? window.BETTERBATTLE_ROGUELIKE
    : []).map((topic) => {
    const seen = new Map();
    const stages = (topic.stages || []).map((stage) => {
      const baseName = String(stage.name || "").trim();
      const duplicateIndex = seen.get(baseName) || 0;
      seen.set(baseName, duplicateIndex + 1);
      const suffix = duplicateIndex > 0
        ? `（变体 ${variantLabel(duplicateIndex)}）`
        : "";
      return {
        ...stage,
        baseName,
        name: `${baseName || stage.id}${suffix}`,
        topicId: topic.id,
      };
    });
    return { ...topic, stages };
  });
  const ROGUE_BY_TOPIC = new Map(
    ROGUE_TOPICS.map((topic) => [String(topic.id), topic]),
  );
  const ROGUE_STAGE_BY_ID = new Map();
  for (const topic of ROGUE_TOPICS) {
    for (const stage of topic.stages || []) {
      ROGUE_STAGE_BY_ID.set(String(stage.id), { ...stage, topicId: topic.id });
    }
  }
  const STAGE_BY_LEVEL = new Map();
  const indexStageByLevel = (stage) => {
    const level = String(stage?.level || "").trim();
    if (!level) return;
    const existing = STAGE_BY_LEVEL.get(level);
    if (!existing ||
        (existing.difficulty !== "NORMAL" && stage.difficulty === "NORMAL")) {
      STAGE_BY_LEVEL.set(level, stage);
    }
  };
  STAGE_CATALOG.forEach(indexStageByLevel);
  for (const topic of ROGUE_TOPICS) {
    for (const stage of topic.stages || []) indexStageByLevel(stage);
  }
  const BATTLE_CONFIG_DATA = window.BETTERBATTLE_BATTLE_CONFIG || {
    version: 1,
    topics: [],
  };
  const CONFIG_TOPICS = Array.isArray(BATTLE_CONFIG_DATA.topics)
    ? BATTLE_CONFIG_DATA.topics
    : [];
  const CONFIG_BY_TOPIC = new Map(
    CONFIG_TOPICS.map((topic) => [String(topic.id), topic]),
  );
  const CONFIG_RELIC_BY_TOPIC = new Map();
  const CONFIG_TECH_BY_TOPIC = new Map();
  const CONFIG_CHARBUFF_BY_TOPIC = new Map();
  const CONFIG_FRAGMENT_BY_TOPIC = new Map();
  const CONFIG_TOTEM_BY_TOPIC = new Map();
  for (const topic of CONFIG_TOPICS) {
    CONFIG_RELIC_BY_TOPIC.set(
      String(topic.id),
      new Map((topic.relics || []).map((entry) => [String(entry.id), entry])),
    );
    CONFIG_TECH_BY_TOPIC.set(
      String(topic.id),
      new Map((topic.tech || []).map((entry) => [String(entry.id), entry])),
    );
    CONFIG_CHARBUFF_BY_TOPIC.set(
      String(topic.id),
      new Map((topic.charBuffs || []).map((entry) => [String(entry.id), entry])),
    );
    CONFIG_FRAGMENT_BY_TOPIC.set(
      String(topic.id),
      new Map((topic.fragments || []).map((entry) => [String(entry.id), entry])),
    );
    CONFIG_TOTEM_BY_TOPIC.set(
      String(topic.id),
      new Map((topic.totems || []).map((entry) => [String(entry.id), entry])),
    );
  }
  const TRACE_FIELDS = [
    ["state", "状态"],
    ["cost", "费用"],
    ["hp", "生命"],
    ["sp", "技力"],
    ["skill", "技能状态"],
    ["affect", "影响中"],
    ["remain", "剩余时间"],
    ["trigger", "触发次数"],
    ["anim", "开始动画"],
    ["stateid", "状态机 ID"],
    ["alive", "存活"],
  ];
  const DEFAULT_TRACE_FIELDS = new Set([
    "state", "cost", "hp", "sp", "skill", "affect", "alive",
  ]);
  const DAMAGE_TYPES = [
    { key: "physical", label: "物理", color: "#f2b35d" },
    { key: "magical", label: "法术", color: "#7aa7ff" },
    { key: "true", label: "真实", color: "#ff6b72" },
    { key: "elemental", label: "元素", color: "#b784ff" },
  ];
  const STATS_MODES = {
    damage: {
      label: "伤害",
      totalLabel: "总伤害",
      rateLabel: "DPS",
      total: (damage) => Number(damage?.total?.actual) || 0,
      actor: (actor) => Number(actor?.actual) || 0,
      curve: (actor) => actor?.curve,
      color: "#8fe0b0",
      rail: "输出干员",
      scope: "伤害",
      title: "全部干员输出曲线",
      hint: "横轴为逻辑帧（30 FPS），纵轴为累计实际伤害",
      chartUnit: "累计伤害",
      empty: "等待战斗伤害数据",
    },
    healing: {
      label: "治疗",
      totalLabel: "总治疗",
      rateLabel: "HPS",
      total: (damage) => Number(damage?.total?.healing) || 0,
      actor: (actor) => Number(actor?.healing) || 0,
      curve: (actor) => actor?.healing_curve,
      color: "#83d6a5",
      rail: "治疗干员",
      scope: "治疗",
      title: "全部干员治疗曲线",
      hint: "横轴为逻辑帧（30 FPS），纵轴为累计有效治疗",
      chartUnit: "累计治疗",
      empty: "等待战斗治疗数据",
    },
    taken: {
      label: "承伤",
      totalLabel: "总承伤",
      rateLabel: "承伤速率",
      total: (damage) => Number(damage?.total?.taken) || 0,
      actor: (actor) => Number(actor?.taken) || 0,
      curve: (actor) => actor?.taken_curve,
      color: "#ff7b72",
      rail: "承伤干员",
      scope: "承伤",
      title: "全部干员承伤曲线",
      hint: "横轴为逻辑帧（30 FPS），纵轴为累计生命损失",
      chartUnit: "累计承伤",
      empty: "等待战斗承伤数据",
    },
  };
  const DAMAGE_TOTAL_COLOR = "#8fe0b0";
  const DAMAGE_ACTOR_COLORS = [
    "#8fe0b0", "#7aa7ff", "#f2b35d", "#ff6b72", "#b784ff",
    "#61d4c7", "#ff9f6e", "#c6d36b", "#e78fd4", "#9fb4ff",
    "#f6d365", "#6ee7b7", "#f49ac2", "#a3e4d7", "#ffb4a2",
  ];

  const dom = {};
  const ids = [
    "pauseButton", "resumeButton", "stateBadge", "pauseBadge", "deckBadge", "costBadge", "resultBox",
    "unitRail", "artStage", "artCanvas", "artEmpty", "focusName", "focusMeta",
    "focusBadges", "artError", "skillName", "skillId", "skillDescription",
    "skillSpLabel", "skillSpBar", "skillRemainLabel", "skillRemainBar",
    "skillFacts", "focusActions", "moduleIconBox", "moduleIcon", "moduleType",
    "moduleName", "moduleLevel", "moduleDescription", "moduleStats", "talentPage",
    "talentGrid", "modulePage", "statsGrid", "statsHint",
    "statsAbnormalCount", "statsAbnormalChips", "focusBuffCount",
    "focusBuffs", "globalBuffCount", "globalBuffs",
    "stageInput", "stageResolvedName", "stageCombobox", "stageToggle",
    "stageOptions", "stageMode", "rogueTopic", "squadCount", "squadLimitHint",
    "squadBoard", "squadReplayLock", "squadStartButton", "squadClearButton",
    "squadAddButton",
    "squadRowTop", "squadRowBottom", "squadSelectionName", "squadSelectionDetail",
    "squadSelectionEditor",
    "rosterPreview", "squadPicker", "squadPickerTitle", "squadPickerTarget",
    "squadPickerSearch", "squadPickerCount", "squadPickerList",
    "traceBadge", "traceChanges",
    "traceDropped", "traceFrame", "traceFields",
    "battleRightTabs", "battleRightStats", "battleRightBuffs", "battleRightGlobal",
    "battleStatCharts",
    "damageUnitRail", "damageTotalBadge", "damageDpsBadge",
    "damageStatusBadge", "damageSelectedName", "damageSelectedMeta",
    "damageSelectedShare", "damageTypeGrid", "damageChart", "damageLegend",
    "damageStatsTabs", "damagePageSubtitle",
    "damageRailTitle", "damageChartTitle", "damageChartHint",
    "enemyCountBadge", "enemyWaveBadge", "enemyFocusBadge", "enemyList",
    "enemyDetailName", "enemyDetailMeta", "enemyDetailSerial",
    "enemyAvatar", "enemyProfileMeta", "enemyDescription", "enemyTags",
    "enemyStatGrid", "enemyAbnormalCount", "enemyAbnormalChips",
    "enemyAbilities", "enemySkills",
    "enemyBuffs", "enemyWaveMeta", "enemyWaves",
    "diagConnected", "diagQueue", "diagCatalog",
    "diagEpoch", "diagFrame", "diagAge", "diagRequestPath", "diagStage",
    "diagTraceFields", "eventLog", "rawJson", "toasts", "nav",
    "cacheArtButton", "artCacheProgress", "artCacheBar", "artCacheLabel",
  ];
  for (const id of ids) dom[id] = document.getElementById(id);

  const saved = loadSaved();
  const state = {
    get snapshot() {
      return session?.snapshot || null;
    },
    get catalog() {
      return session?.catalog || [];
    },
    get catalogById() {
      return session?.catalogById || new Map();
    },
    get profiles() {
      return session?.profiles || new Map();
    },
    hostConnected: false,
    requestPath: "BetterBattle.request",
    get lastSnapshotAt() {
      return session?.lastSnapshotAt || 0;
    },
    resultRevisionSeen: 0,
    activeTab: "battle",
    moduleTab: "talent",
    get selectedCardUid() {
      return session?.selection.cardUid ?? null;
    },
    set selectedCardUid(value) {
      if (session) session.selection.cardUid = value;
    },
    artKey: null,
    squad: saved.squad,
    savedSquad: cloneSquad(saved.squad),
    squadSynced: false,
    squadEdited: saved.squadEdited,
    squadLimit: saved.squadLimit,
    selectedSquadIndex: saved.squad.findIndex(Boolean),
    pickerOpen: false,
    pickerTarget: null,
    pickerQuery: "",
    get replayActive() {
      return session ? session.replayActive() : false;
    },
    stage: saved.stage,
    savedStage: saved.stage,
    stageSynced: false,
    stageEdited: saved.stageEdited,
    stageMode: saved.stageMode === "rogue" ? "rogue" : "normal",
    rogueTopic: saved.rogueTopic || ROGUE_TOPICS[0]?.id || "",
    battleConfig: saved.battleConfig,
    configTopic: saved.configTopic || saved.rogueTopic || CONFIG_TOPICS[0]?.id || "",
    configRelicQuery: "",
    configCopperQuery: "",
    configFragmentQuery: "",
    configTotemQuery: "",
    configRarity: "all",
    configSpecialKind: "",
    stageDropdownOpen: false,
    stageShowAll: false,
    stageOptions: [],
    stageActiveIndex: -1,
    events: [],
    unitRailKey: "",
    get selectedSummonUid() {
      return session?.selection.summonUid ?? null;
    },
    set selectedSummonUid(value) {
      if (session) session.selection.summonUid = value;
    },
    focusActionsKey: "",
    damage: null,
    selectedDamageId: null,
    get selectedEnemyUid() {
      return session?.selection.enemyUid ?? null;
    },
    set selectedEnemyUid(value) {
      if (session) session.selection.enemyUid = value;
    },
    pageStatsMode: "damage",
    battleStatsMode: "damage",
    battleRightTab: "stats",
    lastDamageChartKey: "",
    lastBattleDamageChartKey: "",
    timeUnit: loadTimeUnit(),
  };
  let snapshotRenderQueued = false;

  const esc = (value) => String(value ?? "").replace(/[&<>"']/g, (ch) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
  }[ch]));
  const setHtml = (node, html) => {
    if (!node || node.__bbHtml === html) return;
    node.__bbHtml = html;
    node.innerHTML = html;
  };

  const fmt = (value, digits = 1) => {
    const number = Number(value);
    return Number.isFinite(number) ? number.toFixed(digits) : "--";
  };
  const fmtCompact = (value, digits = 1) => {
    const number = Number(value);
    if (!Number.isFinite(number)) return "--";
    const abs = Math.abs(number);
    if (abs >= 1e9) return `${(number / 1e9).toFixed(digits)}B`;
    if (abs >= 1e6) return `${(number / 1e6).toFixed(digits)}M`;
    if (abs >= 1e4) return `${(number / 1e3).toFixed(digits)}K`;
    return number.toFixed(abs >= 100 ? 0 : digits);
  };
  const INFINITE_DURATION_THRESHOLD = 100000;
  const isInfiniteDuration = (value) => {
    const seconds = Number(value);
    return Number.isFinite(seconds) &&
      (seconds < 0 || seconds >= INFINITE_DURATION_THRESHOLD);
  };
  const formatDurationValue = (seconds) => {
    const value = Math.max(0, Number(seconds));
    if (!Number.isFinite(value)) return "--";
    if (state.timeUnit === "frame") {
      return `${Math.round(value * TIME_FPS)}F`;
    }
    if (value >= 1) return `${fmt(value, 2)}s`;
    return `${Math.round(value * 1000)}ms`;
  };
  const fmtDuration = (value) => {
    const seconds = Number(value);
    if (!Number.isFinite(seconds)) return "--";
    if (seconds >= INFINITE_DURATION_THRESHOLD) {
      return "无限";
    }
    if (seconds < 0) return "无限";
    return formatDurationValue(seconds);
  };
  const formatChartFrame = (frame) => state.timeUnit === "frame"
    ? `F${Math.round(Number(frame) || 0)}`
    : `${(Math.max(0, Number(frame) || 0) / TIME_FPS).toFixed(2)}s`;
  const formatChartTime = (frame) => formatChartFrame(frame);
  const statsModeHint = (mode) => state.timeUnit === "frame"
    ? mode.hint
    : mode.hint.replace("逻辑帧（30 FPS）", "秒（30 FPS 逻辑帧）");
  const SKILL_TRIGGER_LABELS = {
    0: "被动",
    1: "手动触发",
    2: "自动触发",
  };
  const SKILL_DURATION_TYPE = {
    NONE: 0,
    AMMO: 1,
  };
  const skillTriggerLabel = (value) => {
    const type = Number(value);
    return SKILL_TRIGGER_LABELS[type] || "触发方式未知";
  };
  const MODULE_DATA = window.BETTER_BATTLE_MODULES || {
    modules: {},
    characters: {},
  };
  const MODULE_BY_RESKEY = new Map();
  for (const module of Object.values(MODULE_DATA.modules || {})) {
    for (const level of module?.levels || []) {
      for (const resKey of level?.resKeys || []) {
        MODULE_BY_RESKEY.set(String(resKey), {
          module,
          level: clampInt(level.level, 1, 3, 3),
        });
      }
    }
  }
  const MODULE_ATTR_LABELS = {
    max_hp: "生命",
    atk: "攻击",
    def: "防御",
    magic_resistance: "法抗",
    attack_speed: "攻速",
    block_cnt: "阻挡",
    cost: "费用",
    sp_recovery_per_sec: "技力回复",
  };
  const modulesFor = (operatorId) =>
    (MODULE_DATA.characters?.[String(operatorId || "")] || [])
      .map((id) => MODULE_DATA.modules?.[id])
      .filter(Boolean);
  const moduleFor = (moduleId) => {
    const key = String(moduleId || "");
    return MODULE_DATA.modules?.[key] || MODULE_BY_RESKEY.get(key)?.module || null;
  };
  const moduleLevelData = (module, level) => {
    const levels = Array.isArray(module?.levels) ? module.levels : [];
    const wanted = clampInt(level, 1, 3, 3);
    return levels
      .filter((entry) => Number(entry.level) <= wanted)
      .sort((left, right) => Number(right.level) - Number(left.level))[0] || null;
  };
  const moduleEffects = (module, level, potential) => {
    const data = moduleLevelData(module, level);
    if (!data) return { traits: [], talents: [] };
    const traits = (data.traits || [])
      .filter((entry) => Number(entry.potential) <= potential)
      .map((entry) => entry.description)
      .filter(Boolean);
    const grouped = new Map();
    for (const entry of data.talents || []) {
      if (Number(entry.potential) > potential || !entry.description) continue;
      const name = entry.name || "模组天赋";
      const previous = grouped.get(name);
      if (!previous || Number(entry.potential) > Number(previous.potential)) {
        grouped.set(name, entry);
      }
    }
    return {
      traits,
      talents: Array.from(grouped.values()),
      attributes: data.attributes || {},
    };
  };
  const baseTalentGroupsFor = (operatorId) =>
    MODULE_DATA.talents?.[String(operatorId || "")] || [];
  const baseTalentDescription = (operatorId, name, profile) => {
    const group = baseTalentGroupsFor(operatorId)
      .find((entry) => entry.name === name);
    const evolve = Number(profile?.evolve) || 0;
    const level = Number(profile?.level) || 0;
    const potential = Number(profile?.potential) || 0;
    const candidate = (group?.candidates || [])
      .filter((entry) => Number(entry.phase) <= evolve &&
        Number(entry.level) <= level &&
        Number(entry.potential) <= potential)
      .sort((left, right) =>
        Number(right.phase) - Number(left.phase) ||
        Number(right.level) - Number(left.level) ||
        Number(right.potential) - Number(left.potential))[0];
    return candidate?.description || "";
  };

  const nowTime = () => new Date().toLocaleTimeString("zh-CN", { hour12: false });
  const stateName = (stateValue) => {
    if (stateValue === 2) return "战斗中";
    if (stateValue === 3) return "已结束";
    if (stateValue === 1) return "准备中";
    return "状态 " + stateValue;
  };
  const isPermanent = (buff) => Number(buff.life) < 0 || Number(buff.remaining) < -1000;
  const buffRatio = (buff) => {
    if (isPermanent(buff)) return 1;
    const ratio = Number(buff.ratio);
    if (Number.isFinite(ratio) && ratio >= 0) return Math.max(0, Math.min(1, ratio));
    const life = Number(buff.life);
    const remaining = Number(buff.remaining);
    return life > 0 ? Math.max(0, Math.min(1, remaining / life)) : 0;
  };
  const normalizeDescription = (value) => String(value ?? "")
    .replace(/\\r\\n/g, "\n")
    .replace(/\\n/g, "\n")
    .replace(/\\r/g, "\n")
    .replace(/\\t/g, "\t")
    .replace(/<@ba\.talpu>.*?<\/>/gs, "")
    .replace(/<\/?[@$]ba[^>]*>/g, "")
    .replace(/<\/>/g, "");
  const renderRichText = (value) => {
    const text = normalizeDescription(value);
    const tags = /<(\/?)(color|b|i|size)(?:=([^>]+))?>/gi;
    let output = "";
    let cursor = 0;
    for (const match of text.matchAll(tags)) {
      output += esc(text.slice(cursor, match.index)).replace(/\n/g, "<br>");
      const [tag, closing, name, rawValue] = match;
      const kind = name.toLowerCase();
      if (closing) {
        output += kind === "color" || kind === "size" ? "</span>" :
          kind === "b" ? "</b>" : "</i>";
      } else if (kind === "color") {
        const color = String(rawValue || "").trim();
        if (/^#[0-9a-f]{3,8}$/i.test(color)) {
          output += `<span style="color:${color}">`;
        }
      } else if (kind === "size") {
        const size = String(rawValue || "").trim();
        if (/^\d{1,3}(?:\.\d+)?$/.test(size)) {
          output += `<span style="font-size:${size}px">`;
        }
      } else if (kind === "b") {
        output += "<b>";
      } else if (kind === "i") {
        output += "<i>";
      } else {
        output += esc(tag);
      }
      cursor = match.index + tag.length;
    }
    output += esc(text.slice(cursor)).replace(/\n/g, "<br>");
    return output;
  };
  const mine = (unit) => !!unit?.mine;
  const unitActionUid = (unit) => Number(unit.uid);
  const unitBuffs = (unit) => Array.isArray(unit.buffs) ? unit.buffs : [];

  function loadSaved() {
    try {
      const raw = JSON.parse(localStorage.getItem(STORAGE_KEY) || "{}");
      const stageMode = raw.stageMode === "rogue" ? "rogue" : "normal";
      const rogueTopic = String(raw.rogueTopic || ROGUE_TOPICS[0]?.id || "");
      const defaultStage = stageMode === "rogue"
        ? String(ROGUE_BY_TOPIC.get(rogueTopic)?.stages?.[0]?.id || "")
        : String(STAGE_CATALOG[0]?.id || "");
      const squadEdited = raw.squadEdited === true;
      const rawSquad = squadEdited && Array.isArray(raw.squad)
        ? raw.squad.slice(0, MAX_SLOTS)
        : [];
      const savedSlots = Array.from({ length: MAX_SLOTS }, (_, index) => {
        const slot = rawSquad[index];
        if (!slot || !slot.id) return null;
        const caps = eliteCapsFor(slot.id);
        const evolve = clampInt(slot.evolve, 0, caps.length - 1, 0);
        const skill = clampInt(slot.skill, 1, 3, 1);
        const skillLevels = normalizeSkillLevels(
          slot.skill_levels, slot.skill_level,
        );
        return {
          id: String(slot.id),
          level: clampInt(slot.level, 1, caps[evolve], caps[evolve]),
          evolve,
          potential: clampInt(slot.potential, 0, 6, 0),
          trust: clampInt(slot.trust, 0, 200, 100),
          skill,
          skill_levels: skillLevels,
          skill_level: skillLevels[skill - 1],
          module: String(slot.module || ""),
          module_level: clampInt(slot.module_level, 1, 3, 3),
        };
      });
      const compact = savedSlots.filter(Boolean);
      const squad = compact.concat(Array(MAX_SLOTS - compact.length).fill(null));
      const stageEdited = raw.stageEdited === true;
      const stage = stageEdited ? String(raw.stage || "") : defaultStage;
      return {
        squad,
        squadEdited,
        squadLimit: clampInt(raw.squadLimit, 1, MAX_SLOTS, DEFAULT_SQUAD_LIMIT),
        stage: stage || defaultStage,
        stageEdited,
        stageMode,
        rogueTopic,
        battleConfig: normalizeBattleConfig(raw.battleConfig),
        configTopic: String(raw.configTopic || raw.rogueTopic || CONFIG_TOPICS[0]?.id || ""),
      };
    } catch {
      return {
        squad: Array(MAX_SLOTS).fill(null),
        squadEdited: false,
        squadLimit: DEFAULT_SQUAD_LIMIT,
        stage: String(STAGE_CATALOG[0]?.id || ""),
        stageEdited: false,
        stageMode: "normal",
        rogueTopic: ROGUE_TOPICS[0]?.id || "",
        battleConfig: {},
        configTopic: CONFIG_TOPICS[0]?.id || "",
      };
    }
  }

  function loadTimeUnit() {
    try {
      return localStorage.getItem(TIME_UNIT_STORAGE_KEY) === "frame"
        ? "frame"
        : "seconds";
    } catch {
      return "seconds";
    }
  }

  function cloneSquad(squad) {
    return Array.from({ length: MAX_SLOTS }, (_, index) => {
      const slot = Array.isArray(squad) ? squad[index] : null;
      return slot
        ? { ...slot, skill_levels: skillLevelsFor(slot) }
        : null;
    });
  }

  function commitSquadEdit() {
    state.squadEdited = true;
    state.savedSquad = cloneSquad(state.squad);
  }

  function saveUi() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify({
        version: UI_STATE_VERSION,
        squad: state.squadEdited ? state.savedSquad : Array(MAX_SLOTS).fill(null),
        squadEdited: state.squadEdited,
        squadLimit: state.squadLimit,
        stage: state.stageEdited ? state.savedStage : "",
        stageEdited: state.stageEdited,
        stageMode: state.stageMode,
        rogueTopic: state.rogueTopic,
        battleConfig: state.battleConfig,
        configTopic: state.configTopic,
      }));
    } catch {
    }
  }

  function clampInt(value, min, max, fallback) {
    const number = Number.parseInt(value, 10);
    return Number.isFinite(number) ? Math.max(min, Math.min(max, number)) : fallback;
  }

  function squadCount() {
    return state.squad.filter(Boolean).length;
  }

  const STAGE_OPTION_LIMIT = 120;

  function toast(text, kind = "") {
    const node = document.createElement("div");
    node.className = "toast " + kind;
    node.textContent = text;
    dom.toasts.append(node);
    setTimeout(() => node.remove(), 3600);
  }

  function addEvent(text, kind = "") {
    state.events.unshift({ time: nowTime(), text: String(text), kind });
    if (state.events.length > 90) state.events.length = 90;
    if (state.activeTab === "diagnostics") renderEvents();
  }

  function sendCommand(line) {
    const command = String(line || "").trim();
    if (!command) return false;
    if (/[\r\n]/.test(command)) {
      toast("命令不能包含换行", "error");
      return false;
    }
    if (!window.chrome?.webview) {
      toast("请通过 BetterBattleWeb.exe 打开界面", "error");
      return false;
    }
    protocol.post({ type: protocol.Message.Command, line: command });
    addEvent("→ " + command);
    return true;
  }

  function displayResult(text) {
    const value = String(text || "");
    const replay = value.match(/^录像回放已装载：.+?(\d+) 个操作$/);
    return replay ? `回放已装载 · ${replay[1]} 个操作` : value;
  }

  function setActiveBadge(node, active, activeText, idleText, tone = "green") {
    node.textContent = active ? activeText : idleText;
    node.className = "badge " + (active ? tone : "");
  }

  const CONFIG_BATTLE_DISPLAY_TYPES = new Set([
    "display_bat_attack",
    "display_bat_def",
    "display_bat_max_hp",
  ]);

  const MERGED_SPECIAL_KINDS = new Set(["weather", "wrath", "disaster"]);

  async function startBattle() {
    const replayActive = window.BetterBattleSession
      ? window.BetterBattleSession.replayActive()
      : !!state.replayActive;
    if (replayActive) {
      toast("回放中，编队已锁定", "error");
      return false;
    }
    const stage = currentStageId();
    state.stage = stage;
    state.savedStage = stage;
    state.stageEdited = true;
    saveUi();
    if (!stage || /\s/.test(stage)) {
      toast("关卡 ID 不能为空或包含空白", "error");
      return false;
    }
    if (!squadCount()) {
      toast("编队不能为空", "error");
      return false;
    }
    if (squadCount() > state.squadLimit) {
      toast(`当前关卡最多 ${state.squadLimit} 人，请移除超出上限的干员`, "error");
      return false;
    }
    const current = state.snapshot;
    if (session?.requiresRestartConfirmation(current)) {
      if (!await confirmAction({
        kicker: "BATTLE CONTROL",
        title: "重新进入战斗",
        message: "当前仍在战斗中，重新进入会覆盖当前状态。继续？",
        confirmLabel: "继续进入",
        danger: true,
      })) return false;
    }
    if (state.stageMode === "rogue") {
      const rogue = ROGUE_STAGE_BY_ID.get(stage);
      if (!rogue) {
        toast("请选择有效的肉鸽关卡", "error");
        return false;
      }
      const entry = configEntry(rogue.topicId);
      sendCommand(protocol.commands.rogueStart(
        rogue.topicId, stage, serializeSquad(), entry.mode, entry.grade, 0,
        rogueConfigArgs(rogue.topicId)));
    } else {
      sendCommand(protocol.commands.start(stage, serializeSquad()));
    }
    return true;
  }

  function renderResultEvent() {
    const snapshot = state.snapshot;
    const revision = session?.resultRevision || 0;
    if (snapshot?.result && revision !== state.resultRevisionSeen) {
      state.resultRevisionSeen = revision;
      addEvent((snapshot.ok ? "✓ " : "✗ ") + snapshot.result, snapshot.ok ? "" : "error");
    }
  }

  function renderAll() {
    renderBattleRightTabs();
    renderResultEvent();
    renderActivePage();
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

  function renderSnapshotPage() {
    renderBattleRightTabs();
    renderResultEvent();
    const liveEnemies = Array.isArray(state.snapshot?.enemies)
      ? state.snapshot.enemies
      : [];
    for (const enemy of liveEnemies) {
      if (enemy?.id) window.BetterBattleAssets?.request("enemy-icon", enemy.id);
    }
    switch (state.activeTab) {
    case "battle":
      renderBattle();
      break;
    case "gameview":
      window.BetterBattleGameView?.render();
      break;
    case "enemies":
      renderEnemies();
      break;
    case "squad":
      break;
    case "config":
      renderConfig();
      break;
    case "timeline":
      window.BetterBattleTimeline?.render();
      break;
    case "damage":
      renderDamagePage();
      break;
    case "diagnostics":
      renderDiagnostics();
      renderTrace();
      break;
    default:
      break;
    }
  }

  function scheduleSnapshotRender() {
    if (snapshotRenderQueued) return;
    snapshotRenderQueued = true;
    requestAnimationFrame(() => {
      snapshotRenderQueued = false;
      renderSnapshotPage();
    });
  }

  function renderActivePage() {
    switch (state.activeTab) {
    case "battle":
      renderBattle();
      break;
    case "gameview":
      window.BetterBattleGameView?.render();
      break;
    case "enemies":
      renderEnemies();
      break;
    case "squad":
      renderSquad();
      renderQuickResults();
      break;
    case "config":
      renderConfig();
      break;
    case "timeline":
      window.BetterBattleTimeline?.render();
      break;
    case "damage":
      renderDamagePage();
      break;
    case "diagnostics":
      renderDiagnostics();
      renderTrace();
      break;
    default:
      break;
    }
  }

  function switchTab(tab) {
    const previous = state.activeTab;
    if (previous === "gameview" && tab !== "gameview") {
      window.BetterBattleGameView?.leave();
    }
    state.activeTab = tab;
    document.querySelectorAll(".nav-item").forEach((node) => {
      node.classList.toggle("active", node.dataset.tab === tab);
    });
    document.querySelectorAll(".page").forEach((node) => {
      node.classList.toggle("active", node.id === "page-" + tab);
    });
    window.BetterBattleAssets?.observe(document.getElementById("page-" + tab));
    renderActivePage();
    syncTimeUnitButtons();
    if (tab === "timeline") {
      window.BetterBattleTimeline?.onTabActivated();
    }
    if (tab === "gameview" && previous !== "gameview") {
      window.BetterBattleGameView?.enter();
    }
  }

  function syncTimeUnitButtons() {
    const timelineActive = state.activeTab === "timeline";
    document.querySelectorAll("[data-time-unit]").forEach((button) => {
      const active = button.dataset.timeUnit === state.timeUnit;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-pressed", active ? "true" : "false");
      button.disabled = timelineActive;
      button.title = timelineActive
        ? "时间轴固定使用帧；切换仅影响战斗页与数据统计"
        : (button.dataset.timeUnit === "frame"
          ? "战斗页与统计页以帧显示"
          : "统计图以秒显示；战斗页长时长用秒");
    });
  }

  function setTimeUnit(unit) {
    const next = unit === "frame" ? "frame" : "seconds";
    if (state.timeUnit === next) {
      syncTimeUnitButtons();
      return;
    }
    state.timeUnit = next;
    try {
      localStorage.setItem(TIME_UNIT_STORAGE_KEY, next);
    } catch {
    }
    state.lastDamageChartKey = "";
    state.lastBattleDamageChartKey = "";
    syncTimeUnitButtons();
    renderActivePage();
  }

  function handleCommandMessage(message) {
    const line = String(message.line || "").slice(0, 240);
    const commandState = String(message.state || "");
    if (commandState === "queued") {
      addEvent("排队 " + line);
    } else if (commandState === "sent") {
      addEvent("发送 " + line);
    } else if (commandState === "waiting") {
      addEvent("等待插件消费 " + line, "error");
      toast(message.message || "游戏插件尚未消费请求", "error");
    } else if (commandState === "failed" || commandState === "rejected") {
      addEvent("命令失败 " + line, "error");
      toast(message.message || "命令未被接受", "error");
    }
  }

  function handleCatalogMessage() {
    state.hostConnected = true;
    addEvent(`目录 ${state.catalog.length} 条`);
    renderActivePage();
  }

  function handleProfilesMessage() {
    state.hostConnected = true;
    state.artKey = null;
    renderAll();
  }

  function handleSnapshotMessage(payload, previous) {
    state.hostConnected = true;
    if (payload.damage) {
      state.damage = mergeDamagePayload(payload.damage);
    }
    const replayWasActive = session ? session.replayActive(previous) : false;
    const replayActive = session
      ? session.replayActive(payload)
      : !!payload.replay?.active;
    let squadChanged = false;
    const reportedLimit = Number(
      payload.squad_limit ?? payload.squadLimit,
    );
    if (Number.isFinite(reportedLimit)) {
      const nextLimit = clampInt(reportedLimit, 1, MAX_SLOTS, state.squadLimit);
      if (nextLimit !== state.squadLimit) {
        state.squadLimit = nextLimit;
        if (!replayActive) saveUi();
        squadChanged = true;
      }
    }
    if (replayActive || battleInProgress(payload)) {
      closeSquadPicker();
      if (syncSquadFromSnapshot(payload.squad)) {
        state.squadSynced = true;
        squadChanged = true;
      }
    } else {
      if (state.squadSynced) {
        state.squad = cloneSquad(state.savedSquad);
        state.squadSynced = false;
        state.selectedSquadIndex = state.squad.findIndex(Boolean);
        squadChanged = true;
      }
      if (state.stageSynced) {
        state.stage = state.savedStage;
        state.stageSynced = false;
      }
    }
    if (squadChanged || replayWasActive !== replayActive) {
      if (state.activeTab === "squad") {
        renderSquad();
        renderQuickResults();
      }
    }
    scheduleSnapshotRender();
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
    if (message.type === protocol.Message.Host) {
      const changed = state.hostConnected !== !!message.connected;
      state.hostConnected = !!message.connected;
      if (changed) {
        addEvent(state.hostConnected ? "IPC 已连接" : "IPC 已断开", state.hostConnected ? "" : "error");
      }
      return;
    }
    if (message.type === protocol.Message.RequestPath) {
      state.requestPath = message.path || state.requestPath;
      addEvent("请求文件 " + state.requestPath);
      if (state.activeTab === "diagnostics") renderDiagnostics();
      return;
    }
    if (message.type === protocol.Message.Command) {
      handleCommandMessage(message);
      return;
    }
  }

  const pointerBattleActions = new Set([
    "select-card", "select-summon", "skill", "withdraw", "enemy-focus",
  ]);

  document.addEventListener("pointerdown", (event) => {
    if (event.button !== 0) return;
    const navButton = event.target.closest("#nav button[data-tab]");
    if (navButton) {
      event.preventDefault();
      switchTab(navButton.dataset.tab);
      return;
    }
    const damageRow = event.target.closest("button[data-damage-id]");
    if (damageRow) {
      event.preventDefault();
      state.selectedDamageId = String(damageRow.dataset.damageId);
      renderDamagePage();
      return;
    }
    const button = event.target.closest("button[data-action]");
    if (!button) return;
    const action = button.dataset.action;
    if (!pointerBattleActions.has(action)) return;
    event.preventDefault();
    if (action === "select-card") {
      selectCard(button.dataset.uid);
    } else if (action === "select-summon") {
      selectSummon(button.dataset.uid);
    } else if (action === "enemy-focus") {
      selectEnemy(button.dataset.uid, true);
    } else if (action === "skill") {
      sendCommand(protocol.commands.skill(button.dataset.uid));
    } else if (action === "withdraw") {
      sendCommand(protocol.commands.withdraw(button.dataset.uid));
    }
  }, true);

  let actionModalResolve = null;

  function closeActionModal(result) {
    const modal = document.getElementById("actionModal");
    if (!modal || !actionModalResolve) return;
    const resolve = actionModalResolve;
    actionModalResolve = null;
    modal.hidden = true;
    resolve(result);
  }

  function confirmAction({
    kicker = "BETTERBATTLE",
    title = "确认操作",
    message = "",
    confirmLabel = "确认",
    danger = false,
  } = {}) {
    const modal = document.getElementById("actionModal");
    if (!modal) return Promise.resolve(window.confirm(message));
    return new Promise((resolve) => {
      if (actionModalResolve) closeActionModal(false);
      actionModalResolve = resolve;
      document.getElementById("actionModalKicker").textContent = kicker;
      document.getElementById("actionModalTitle").textContent = title;
      document.getElementById("actionModalMessage").textContent = message;
      const ok = document.getElementById("actionModalOk");
      ok.textContent = confirmLabel;
      ok.classList.toggle("danger", danger);
      ok.classList.toggle("primary", !danger);
      modal.querySelector(".tas-modal-panel")?.classList.toggle("is-danger", danger);
      modal.hidden = false;
      setTimeout(() => ok.focus(), 0);
    });
  }

  document.getElementById("actionModal")?.querySelectorAll("[data-action-close]")
    .forEach((node) => node.addEventListener("click", () => closeActionModal(false)));
  document.getElementById("actionModalOk")
    ?.addEventListener("click", () => closeActionModal(true));
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && actionModalResolve) closeActionModal(false);
  });

  document.addEventListener("click", async (event) => {
    const timeUnitButton = event.target.closest("button[data-time-unit]");
    if (timeUnitButton) {
      setTimeUnit(timeUnitButton.dataset.timeUnit);
      return;
    }
    const moduleTab = event.target.closest("button[data-module-tab]");
    if (moduleTab) {
      state.moduleTab = moduleTab.dataset.moduleTab;
      renderModuleTabs();
      return;
    }
    const filledSlot = event.target.closest(".squad-slot.is-filled");
    if (filledSlot && !event.target.closest("button,input,select,label")) {
      if (isSquadLocked()) {
        toast("回放中，编队已锁定", "error");
        return;
      }
      const index = Number(filledSlot.dataset.squadIndex);
      if (Number.isInteger(index) && state.squad[index]) {
        state.selectedSquadIndex = index;
        renderSquad();
        openSquadPicker(index);
      }
      return;
    }
    const button = event.target.closest("button[data-action]");
    if (!button) return;
    const action = button.dataset.action;
    if (event.detail > 0 && pointerBattleActions.has(action)) return;

    if (action === "pause") sendCommand(protocol.commands.pause());
    else if (action === "resume") sendCommand(protocol.commands.resume());
    else if (action === "refresh") sendCommand(protocol.commands.refresh());
    else if (action === "cache-art") {
      window.BetterBattleAssets?.cacheAll();
    }
    else if (action === "leave") {
      if (await confirmAction({
        kicker: "BATTLE CONTROL",
        title: "退出战斗",
        message: "确认离开当前战斗并返回主界面？",
        confirmLabel: "退出战斗",
        danger: true,
      })) sendCommand(protocol.commands.leave());
    } else if (action === "select-card") {
      selectCard(button.dataset.uid);
    } else if (action === "select-summon") {
      selectSummon(button.dataset.uid);
    } else if (action === "skill") {
      sendCommand(protocol.commands.skill(button.dataset.uid));
    } else if (action === "withdraw") {
      sendCommand(protocol.commands.withdraw(button.dataset.uid));
    } else if (action === "enemy-focus") {
      selectEnemy(button.dataset.uid, true);
    } else if (action === "select-damage") {
      state.selectedDamageId = String(button.dataset.damageId);
      renderDamagePage();
    } else if (action === "battle-right-tab") {
      const tab = String(button.dataset.tab || "stats");
      if (!["stats", "buffs", "global"].includes(tab)) return;
      state.battleRightTab = tab;
      renderBattleRightTabs();
      renderBattle();
    } else if (action === "stats-mode") {
      const mode = String(button.dataset.mode || "damage");
      if (!STATS_MODES[mode]) return;
      if (button.dataset.scope === "battle") {
        state.battleStatsMode = mode;
      } else {
        state.pageStatsMode = mode;
      }
      state.lastBattleDamageChartKey = "";
      state.lastDamageChartKey = "";
      renderBattle();
      renderDamagePage();
    } else if (action === "open-squad-picker") {
      openSquadPicker(firstEmptySquadIndex() >= 0 ? firstEmptySquadIndex() : null);
    } else if (action === "squad-open-slot") {
      openSquadPicker(Number(button.dataset.index));
    } else if (action === "squad-picker-close") {
      closeSquadPicker();
    } else if (action === "squad-picker-select") {
      addOperator(button.dataset.id, state.pickerTarget);
    } else if (action === "squad-remove") {
      removeOperator(Number(button.dataset.index));
    } else if (action === "squad-skill") {
      if (isSquadLocked()) {
        toast("回放中，编队已锁定", "error");
        return;
      }
      const index = Number(button.dataset.index);
      const slot = state.squad[index];
      if (!slot) return;
      slot.skill = clampInt(button.dataset.skill, 1, 3, 1);
      const levels = skillLevelsFor(slot);
      slot.skill_levels = levels;
      slot.skill_level = levels[slot.skill - 1];
      commitSquadEdit();
      state.selectedSquadIndex = index;
      saveUi();
      renderSquad();
    } else if (action === "squad-skill-level") {
      if (isSquadLocked()) {
        toast("战斗中，编队已锁定", "error");
        return;
      }
      const index = Number(button.dataset.index);
      const slot = state.squad[index];
      if (!slot) return;
      const skillIndex = clampInt(slot.skill, 1, 3, 1) - 1;
      const levels = skillLevelsFor(slot);
      levels[skillIndex] = clampInt(button.dataset.level, 1, 10, 10);
      slot.skill_levels = levels;
      slot.skill_level = levels[skillIndex];
      commitSquadEdit();
      state.selectedSquadIndex = index;
      saveUi();
      renderSquad();
    } else if (action === "squad-module") {
      if (isSquadLocked()) {
        toast("回放中，编队已锁定", "error");
        return;
      }
      const index = Number(button.dataset.index);
      const slot = state.squad[index];
      if (!slot) return;
      slot.module = String(button.dataset.module || "");
      if (slot.module) slot.module_level = clampInt(slot.module_level, 1, 3, 3);
      commitSquadEdit();
      state.selectedSquadIndex = index;
      saveUi();
      renderSquad();
    } else if (action === "clear-squad") {
      if (isSquadLocked()) {
        toast("回放中，编队已锁定", "error");
        return;
      }
      if (!squadCount() || await confirmAction({
        kicker: "SQUAD",
        title: "清空编队",
        message: "确认清空当前编队？",
        confirmLabel: "清空编队",
        danger: true,
      })) {
        state.squad = Array(MAX_SLOTS).fill(null);
        commitSquadEdit();
        state.selectedSquadIndex = -1;
        saveUi();
        renderSquad();
        renderQuickResults();
      }
    } else if (action === "config-topic") {
      state.configTopic = String(button.dataset.topic || "");
      state.configSpecialKind = "";
      state.configRelicQuery = "";
      state.configCopperQuery = "";
      state.configFragmentQuery = "";
      state.configTotemQuery = "";
      const search = document.getElementById("configRelicSearch");
      if (search) search.value = "";
      const copperSearch = document.getElementById("configCopperSearch");
      if (copperSearch) copperSearch.value = "";
      const fragmentSearch = document.getElementById("configFragmentSearch");
      if (fragmentSearch) fragmentSearch.value = "";
      const totemSearch = document.getElementById("configTotemSearch");
      if (totemSearch) totemSearch.value = "";
      saveUi();
      renderConfig();
    } else if (action === "config-relic-toggle") {
      toggleConfigRelic(button.dataset.id);
    } else if (action === "config-relic-remove") {
      toggleConfigRelic(button.dataset.id);
    } else if (action === "config-relic-layer") {
      const topic = configTopicData(state.configTopic);
      const entry = topic ? configEntry(topic.id) : null;
      const item = entry?.relics.find(
        (candidate) => String(candidate.id) === String(button.dataset.id));
      if (item) {
        setConfigRelicLayer(
          button.dataset.id,
          Number(item.layer) + Number(button.dataset.delta || 0));
      }
    } else if (action === "config-special-tab") {
      state.configSpecialKind = String(button.dataset.kind || "");
      renderConfig();
    } else if (action === "config-special-toggle") {
      toggleConfigSpecial(
        String(button.dataset.kind || ""),
        String(button.dataset.id || ""),
        clampInt(button.dataset.layer, 0, 9, 1));
    } else if (action === "config-special-variant") {
      setConfigSpecialVariant(
        String(button.dataset.kind || ""),
        String(button.dataset.family || ""),
        String(button.dataset.id || ""));
    } else if (action === "config-tech-toggle") {
      toggleConfigTech(button.dataset.id);
    } else if (action === "config-tech-all") {
      setConfigTechAll(true);
    } else if (action === "config-tech-none") {
      setConfigTechAll(false);
    } else if (action === "config-concept-toggle") {
      toggleConfigConcept(button.dataset.id);
    } else if (action === "config-concept-remove") {
      toggleConfigConcept(button.dataset.id);
    } else if (action === "config-concept-layer") {
      const topic = configTopicData(state.configTopic);
      const entry = topic ? configEntry(topic.id) : null;
      const item = entry?.concepts.find(
        (candidate) => String(candidate.id) === String(button.dataset.id));
      if (item) {
        setConfigConceptLayer(
          button.dataset.id,
          Number(item.layer) + Number(button.dataset.delta || 0));
      }
    } else if (action === "config-copper-toggle") {
      toggleConfigCopper(button.dataset.id);
    } else if (action === "config-charbuff-toggle") {
      toggleConfigCharBuff(button.dataset.id);
    } else if (action === "config-charbuff-target") {
      toggleConfigCharBuffTarget(
        button.dataset.id, button.dataset.target);
    } else if (action === "config-fragment-toggle") {
      toggleConfigRunBuff("fragments", button.dataset.id);
    } else if (action === "config-totem-toggle") {
      toggleConfigRunBuff("totems", button.dataset.id);
    } else if (action === "config-rarity") {
      state.configRarity = String(button.dataset.rarity || "all");
      document.querySelectorAll("#configRarityTabs button").forEach((node) => {
        node.classList.toggle("is-active", node.dataset.rarity === state.configRarity);
      });
      renderConfig();
    } else if (action === "config-apply") {
      if (applyConfigToSquad() && await startBattle()) switchTab("gameview");
    } else if (action === "config-reset") {
      const topic = configTopicData(state.configTopic);
      if (!topic) return;
      if (!await confirmAction({
        kicker: "CONFIG",
        title: "重置战斗配置",
        message: `重置 ${topic.name} 的战斗配置？`,
        confirmLabel: "重置配置",
        danger: true,
      })) return;
      const next = { ...(state.battleConfig || {}) };
      delete next[topic.id];
      state.battleConfig = next;
      state.configSpecialKind = "";
      saveUi();
      renderConfig();
      toast(`${topic.name} 配置已重置`);
    } else if (action === "start") {
      if (isSquadLocked()) {
        toast("回放中，编队已锁定", "error");
        return;
      }
      if (state.stageMode === "rogue") {
        const topic = activeRogueTopic();
        const rogue = ROGUE_STAGE_BY_ID.get(currentStageId());
        if (!topic || !rogue || String(rogue.topicId) !== String(topic.id)) {
          state.stage = String(topic?.stages?.[0]?.id || state.stage);
        }
        if (!topic || !topic.stages?.length) {
          toast("请选择有效的肉鸽关卡", "error");
          return;
        }
        state.configTopic = String(topic.id);
        state.rogueTopic = String(topic.id);
        state.configSpecialKind = "";
        saveUi();
        switchTab("config");
        return;
      }
      if (await startBattle()) switchTab("gameview");
    } else if (action === "trace-start") {
      const fields = Array.from(dom.traceFields.querySelectorAll("input:checked"))
        .map((input) => input.value);
      if (!fields.length) {
        toast("至少选择一个采集字段", "error");
        return;
      }
      sendCommand(protocol.commands.traceStart(fields.join(",")));
    } else if (action === "trace-stop") {
      sendCommand(protocol.commands.traceStop());
    } else if (action === "clear-log") {
      state.events = [];
      renderEvents();
    }
  });

  document.addEventListener("input", (event) => {
    const target = event.target;
    if (target?.matches?.("[data-config-relic-layer]")) {
      setConfigRelicLayer(target.dataset.id, target.value, false);
      return;
    }
    if (target?.matches?.("[data-config-concept-layer]")) {
      setConfigConceptLayer(target.dataset.id, target.value, false);
      return;
    }
    if (target?.matches?.("[data-config-resource]")) {
      setConfigResource(target.dataset.configResource, target.value);
      return;
    }
    if (target === dom.stageInput) {
      if (isSquadLocked()) return;
      const resolved = resolveStageQuery(target.value);
      if (resolved) state.stage = String(resolved.id);
      dom.stageResolvedName.textContent = resolved
        ? [stageTopicName(resolved), stageLabel(resolved)].filter(Boolean).join(" · ")
        : "未匹配关卡，请检查 ID、代号或名称";
      state.stageShowAll = false;
      if (!state.stageDropdownOpen) {
        state.stageDropdownOpen = true;
        dom.stageCombobox.classList.add("open");
        dom.stageOptions.hidden = false;
        dom.stageInput.setAttribute("aria-expanded", "true");
      }
      state.stageActiveIndex = 0;
      renderStageOptions(target.value, false);
      saveUi();
      return;
    }
    if (target === dom.squadPickerSearch) {
      state.pickerQuery = target.value;
      renderSquadPicker();
      return;
    }
    if (target === dom.quickSearch) {
      renderQuickResults();
      return;
    }
    const field = target.dataset.squadField;
    if (!field) return;
    const row = target.closest("tr[data-squad-index]");
    const index = Number(row?.dataset.squadIndex);
    if (!Number.isInteger(index) || index < 0 || index >= state.squad.length) return;
    if (!state.squad[index]) return;
    if (field === "module") return;
    const ranges = {
      level: [1, 90, 90],
      evolve: [0, 2, 2],
      potential: [0, 6, 0],
      trust: [0, 200, 100],
      skill: [1, 3, 1],
      skill_level: [1, 10, 10],
      module_level: [1, 3, 3],
    };
    const range = ranges[field];
    if (!range) return;
    state.squad[index][field] = clampInt(target.value, range[0], range[1], range[2]);
    commitSquadEdit();
    saveUi();
    renderRosterPreview();
  });

  dom.stageInput.addEventListener("focus", () => {
    if (!state.stageDropdownOpen) openStageDropdown(stageInputShowsSelectedLabel());
  });

  dom.stageInput.addEventListener("click", () => {
    if (!state.stageDropdownOpen) openStageDropdown(stageInputShowsSelectedLabel());
  });

  dom.stageInput.addEventListener("keydown", (event) => {
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      event.preventDefault();
      if (!state.stageDropdownOpen) {
        openStageDropdown(stageInputShowsSelectedLabel());
        return;
      }
      moveStageActive(event.key === "ArrowDown" ? 1 : -1);
      return;
    }
    if (event.key === "Enter") {
      if (!state.stageDropdownOpen) return;
      event.preventDefault();
      const stage = state.stageOptions[state.stageActiveIndex] ||
        resolveStageQuery(dom.stageInput.value);
      if (stage) selectStage(stage);
      return;
    }
    if (event.key === "Escape") {
      if (!state.stageDropdownOpen) return;
      event.preventDefault();
      closeStageDropdown(true);
      return;
    }
    if (event.key === "Tab") {
      closeStageDropdown(false);
    }
  });

  dom.stageToggle.addEventListener("click", (event) => {
    event.preventDefault();
    if (dom.stageInput.disabled) return;
    if (!state.stageDropdownOpen) openStageDropdown(true);
    else if (!state.stageShowAll) openStageDropdown(true);
    else closeStageDropdown(true);
  });

  dom.stageMode.addEventListener("change", () => {
    if (dom.stageMode.disabled) return;
    state.stageMode = dom.stageMode.value === "rogue" ? "rogue" : "normal";
    if (state.stageMode === "rogue") {
      const topic = activeRogueTopic();
      if (topic) state.rogueTopic = String(topic.id);
      if (!topic?.stages?.some((stage) => String(stage.id) === String(state.stage))) {
        state.stage = String(activeStageCatalog()[0]?.id || "");
      }
    } else if (!STAGE_BY_ID.has(String(state.stage))) {
      state.stage = String(STAGE_CATALOG[0]?.id || "");
    }
    state.savedStage = state.stage;
    closeStageDropdown();
    renderStageField();
    saveUi();
  });

  dom.rogueTopic.addEventListener("change", () => {
    if (dom.rogueTopic.disabled) return;
    state.rogueTopic = dom.rogueTopic.value;
    const topic = activeRogueTopic();
    if (!topic?.stages?.some((stage) => String(stage.id) === String(state.stage))) {
      state.stage = String(topic?.stages?.[0]?.id || "");
    }
    state.savedStage = state.stage;
    closeStageDropdown();
    renderStageField();
    saveUi();
  });

  dom.stageOptions.addEventListener("mousedown", (event) => {
    const option = event.target.closest(".stage-option[data-stage-id]");
    if (!option) return;
    event.preventDefault();
    selectStage(stageById(option.dataset.stageId));
  });

  dom.stageOptions.addEventListener("pointermove", (event) => {
    const option = event.target.closest(".stage-option[data-stage-id]");
    if (!option) return;
    const index = Array.from(dom.stageOptions.children).indexOf(option);
    if (index >= 0 && index !== state.stageActiveIndex) {
      state.stageActiveIndex = index;
      dom.stageOptions.querySelectorAll(".stage-option").forEach((item, itemIndex) =>
        item.classList.toggle("active", itemIndex === index));
    }
  });

  document.addEventListener("pointerdown", (event) => {
    if (!state.stageDropdownOpen) return;
    if (dom.stageCombobox.contains(event.target) || dom.stageOptions.contains(event.target)) {
      return;
    }
    closeStageDropdown(false);
  });

  window.addEventListener("resize", positionStageOptions);
  window.addEventListener("scroll", positionStageOptions, true);

  document.addEventListener("change", (event) => {
    const target = event.target;
    if (target?.matches?.("[data-config-special-awake]")) {
      setConfigSpecialAwake(
        String(target.dataset.kind || ""),
        String(target.dataset.family || ""),
        !!target.checked);
      return;
    }
    if (target?.matches?.("[data-config-mastery-toggle]")) {
      toggleConfigMastery(
        String(target.dataset.char || ""),
        String(target.dataset.master || ""),
        !!target.checked);
      return;
    }
    if (target?.matches?.("[data-config-mastery-level]")) {
      setConfigMasteryLevel(
        String(target.dataset.char || ""),
        String(target.dataset.master || ""),
        target.value);
      return;
    }
    if (target?.matches?.("[data-config-mastery-zone]")) {
      setConfigMasteryZone(
        String(target.dataset.char || ""),
        String(target.dataset.master || ""),
        target.value);
      return;
    }
    if (target?.matches?.("[data-config-relic-layer]")) {
      setConfigRelicLayer(target.dataset.id, target.value);
      return;
    }
    if (target?.matches?.("[data-config-concept-layer]")) {
      setConfigConceptLayer(target.dataset.id, target.value);
      return;
    }
    if (target?.matches?.("[data-config-resource]")) {
      setConfigResource(target.dataset.configResource, target.value);
      renderConfig();
    }
  });

  document.addEventListener("change", (event) => {
    const field = event.target.dataset.squadField;
    if (!field) return;
    const row = event.target.closest("tr[data-squad-index]");
    const index = Number(row?.dataset.squadIndex);
    if (!Number.isInteger(index) || index < 0 || index >= state.squad.length) return;
    if (!state.squad[index]) return;
    if (field === "module") {
      state.squad[index].module = String(event.target.value || "");
      if (state.squad[index].module) {
        state.squad[index].module_level = clampInt(
          state.squad[index].module_level, 1, 3, 3);
      }
      commitSquadEdit();
      saveUi();
      renderSquad();
      return;
    }
    const ranges = {
      level: [1, 90, 90],
      evolve: [0, 2, 2],
      potential: [0, 6, 0],
      skill: [1, 3, 1],
      skill_level: [1, 10, 10],
      module_level: [1, 3, 3],
    };
    const range = ranges[field];
    if (!range) return;
    state.squad[index][field] = clampInt(event.target.value, range[0], range[1], range[2]);
    commitSquadEdit();
    saveUi();
    renderRosterPreview();
  });

  document.addEventListener("change", (event) => {
    const control = event.target.closest("[data-squad-stat]");
    if (!control) return;
    if (isSquadLocked()) {
      toast("回放中，编队已锁定", "error");
      renderSquad();
      return;
    }
    const card = control.closest(".squad-slot.is-filled");
    const index = Number(card?.dataset.squadIndex);
    const slot = state.squad[index];
    if (!slot) return;
    const caps = eliteCapsFor(slot.id);
    const stat = control.dataset.squadStat;
    if (stat === "evolve") {
      slot.evolve = clampInt(control.value, 0, caps.length - 1, 0);
      slot.level = clampInt(slot.level, 1, caps[slot.evolve], caps[slot.evolve]);
    } else if (stat === "level") {
      slot.evolve = clampInt(slot.evolve, 0, caps.length - 1, 0);
      slot.level = clampInt(control.value, 1, caps[slot.evolve], caps[slot.evolve]);
    } else if (stat === "potential") {
      slot.potential = clampInt(control.value, 0, 6, 0);
    } else {
      return;
    }
    commitSquadEdit();
    state.selectedSquadIndex = index;
    saveUi();
    renderSquad();
  });

  document.addEventListener("change", (event) => {
    const control = event.target.closest?.("[data-squad-tuner]");
    if (!control) return;
    if (isSquadLocked()) {
      toast("战斗中，编队已锁定", "error");
      renderSquad();
      return;
    }
    const index = Number(control.dataset.index);
    const slot = state.squad[index];
    if (!slot) return;
    const tuner = control.dataset.squadTuner;
    if (tuner === "skill-level") {
      const levels = skillLevelsFor(slot);
      const skillIndex = clampInt(slot.skill, 1, 3, 1) - 1;
      levels[skillIndex] = clampInt(control.value, 1, 10, 10);
      slot.skill_levels = levels;
      slot.skill_level = levels[skillIndex];
    } else if (tuner === "module-level") {
      if (!slot.module) return;
      slot.module_level = clampInt(control.value, 1, 3, 3);
    } else if (tuner === "trust") {
      slot.trust = clampInt(control.value, 0, 200, 100);
    } else {
      return;
    }
    commitSquadEdit();
    state.selectedSquadIndex = index;
    saveUi();
    renderSquad();
  });

  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape" && state.pickerOpen) {
      closeSquadPicker();
      return;
    }
    const card = event.target.closest?.(".squad-slot.is-filled");
    if (!card || (event.key !== "Enter" && event.key !== " ")) return;
    if (isSquadLocked()) {
      toast("回放中，编队已锁定", "error");
      return;
    }
    event.preventDefault();
    const index = Number(card.dataset.squadIndex);
    if (!Number.isInteger(index) || !state.squad[index]) return;
    state.selectedSquadIndex = index;
    renderSquad();
    openSquadPicker(index);
  });

  dom.nav.addEventListener("click", (event) => {
    const button = event.target.closest("button[data-tab]");
    if (button && event.detail === 0) switchTab(button.dataset.tab);
  });

  {
    const search = document.getElementById("configRelicSearch");
    if (search) {
      search.addEventListener("input", () => {
        state.configRelicQuery = search.value;
        const topic = configTopicData(state.configTopic);
        if (topic) renderConfigRelics(topic, configEntry(topic.id));
      });
    }
    const copperSearch = document.getElementById("configCopperSearch");
    if (copperSearch) {
      copperSearch.addEventListener("input", () => {
        state.configCopperQuery = copperSearch.value;
        const topic = configTopicData(state.configTopic);
        if (topic) renderConfigCopper(topic, configEntry(topic.id));
      });
    }
    const fragmentSearch = document.getElementById("configFragmentSearch");
    if (fragmentSearch) {
      fragmentSearch.addEventListener("input", () => {
        state.configFragmentQuery = fragmentSearch.value;
        const topic = configTopicData(state.configTopic);
        if (topic) renderConfigRunBuffs(topic, configEntry(topic.id));
      });
    }
    const totemSearch = document.getElementById("configTotemSearch");
    if (totemSearch) {
      totemSearch.addEventListener("input", () => {
        state.configTotemQuery = totemSearch.value;
        const topic = configTopicData(state.configTopic);
        if (topic) renderConfigRunBuffs(topic, configEntry(topic.id));
      });
    }
    const difficulty = document.getElementById("configDifficulty");
    if (difficulty) {
      difficulty.addEventListener("change", () => {
        const topic = configTopicData(state.configTopic);
        if (!topic) return;
        const [mode, grade] = String(difficulty.value || "").split(":").map(Number);
        if (!Number.isFinite(mode) || !Number.isFinite(grade)) return;
        setConfigEntry(topic.id, { mode, grade });
      });
    }
  }

  syncTimeUnitButtons();
  renderTraceFields();
  renderSquad();
  renderQuickResults();
  renderBattle();
  renderTrace();
  renderDiagnostics();
  {
    const requestedTopic = new URLSearchParams(window.location.search).get("topic");
    if (requestedTopic && CONFIG_BY_TOPIC.has(requestedTopic)) {
      state.configTopic = requestedTopic;
      state.configSpecialKind = "";
    }
  }
  renderConfig();
  window.BetterBattleGameView?.init();
  {
    const requestedTab = new URLSearchParams(window.location.search).get("tab");
    const tabs = new Set([
      "battle", "gameview", "squad", "config", "record", "timeline", "damage",
      "diagnostics",
    ]);
    switchTab(tabs.has(requestedTab) ? requestedTab : "battle");
  }

  session?.onCatalog(handleCatalogMessage);
  session?.onProfiles(handleProfilesMessage);
  session?.onSnapshot(handleSnapshotMessage);

  if (window.chrome?.webview) {
    window.chrome.webview.addEventListener("message", receive);
  } else {
    toast("请通过 BetterBattleWeb.exe 打开界面", "error");
  }

  const reportWebError = (message) => {
    try {
      protocol.post({ type: protocol.Message.Log, message: String(message) });
    } catch {
    }
  };
  window.addEventListener("error", (event) => {
    reportWebError(`${event.message} @ ${event.filename}:${event.lineno}:${event.colno}`);
  });
  window.addEventListener("unhandledrejection", (event) => {
    reportWebError("unhandled rejection: " + (event.reason?.stack || event.reason));
  });

  setInterval(() => {
    if (state.activeTab === "diagnostics") {
      dom.diagAge.textContent = state.lastSnapshotAt
        ? `${((Date.now() - state.lastSnapshotAt) / 1000).toFixed(1)}s`
        : "--";
    }
  }, 1000);
