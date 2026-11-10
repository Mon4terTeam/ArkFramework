"use strict";

(() => {
  const pages = window.BetterBattlePages || (window.BetterBattlePages = {});
  const {
    entry: configEntry,
    stageForTopic: configStageForTopic,
    topicData: configTopicData,
    render: renderConfig,
    renderCopper: renderConfigCopper,
    resolveSpecialItem,
    specialFamilies,
    specialFamilyId,
    specialLevelIndex,
    specialSelectionFamily,
    specialTierForGrade,
    specialVariantSlot,
  } = pages.config || {};
  const stageLabel = (stage) =>
    window.BetterBattlePages?.squad?.stageLabel?.(stage) || "";

  function setConfigEntry(topicId, patch) {
    const topic = configTopicData(topicId);
    if (!topic) return;
    const current = configEntry(topic.id);
    state.battleConfig = {
      ...(state.battleConfig || {}),
      [topic.id]: {
        ...current,
        ...patch,
      },
    };
    state.configTopic = topic.id;
    saveUi();
    renderConfig();
  }

  function toggleConfigCopper(id) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const index = entry.copper.findIndex((item) => String(item) === String(id));
    if (index >= 0) {
      entry.copper.splice(index, 1);
    } else {
      if (entry.copper.length >= 12) {
        toast("最多选择 12 个钱币", "error");
        return;
      }
      entry.copper.push(String(id));
    }
    setConfigEntry(topic.id, { copper: entry.copper });
    renderConfigCopper(topic, configEntry(topic.id));
  }

  function toggleConfigCharBuff(id) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const index = entry.charBuffs.findIndex(
      (item) => String(item.id) === String(id));
    if (index >= 0) {
      entry.charBuffs.splice(index, 1);
    } else {
      if (entry.charBuffs.length >= 8) {
        toast("最多选择 8 个干员 Buff", "error");
        return;
      }
      entry.charBuffs.push({ id: String(id), layer: 1, targets: [] });
    }
    setConfigEntry(topic.id, { charBuffs: entry.charBuffs });
  }

  function toggleConfigCharBuffTarget(id, target) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const item = entry.charBuffs.find(
      (candidate) => String(candidate.id) === String(id));
    if (!item) return;
    const index = item.targets.findIndex(
      (candidate) => String(candidate) === String(target));
    if (index >= 0) {
      item.targets.splice(index, 1);
    } else {
      item.targets.push(String(target));
    }
    setConfigEntry(topic.id, { charBuffs: entry.charBuffs });
  }

  function toggleConfigRunBuff(entryKey, id) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const index = entry[entryKey].findIndex(
      (item) => String(item) === String(id));
    if (index >= 0) {
      entry[entryKey].splice(index, 1);
    } else {
      if (entry[entryKey].length >= 12) {
        toast("最多选择 12 项", "error");
        return;
      }
      entry[entryKey].push(String(id));
    }
    setConfigEntry(topic.id, { [entryKey]: entry[entryKey] });
  }

  function applyConfigToSquad() {
    const topic = configTopicData(state.configTopic);
    if (!topic) return false;
    const stage = configStageForTopic(topic.id);
    if (!stage) {
      toast("该主题没有可用的肉鸽关卡", "error");
      return false;
    }
    state.stageMode = "rogue";
    state.rogueTopic = topic.id;
    state.configTopic = topic.id;
    state.stage = String(stage.id);
    state.savedStage = state.stage;
    state.stageEdited = true;
    saveUi();
    dom.stageMode.value = "rogue";
    dom.rogueTopic.hidden = false;
    dom.rogueTopic.value = String(topic.id);
    dom.stageInput.value = stageLabel(stage);
    dom.stageResolvedName.textContent =
      [topic.name, stageLabel(stage)].filter(Boolean).join(" · ");
    return true;
  }

  function toggleConfigRelic(id) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const index = entry.relics.findIndex((item) => String(item.id) === String(id));
    if (index >= 0) {
      entry.relics.splice(index, 1);
    } else {
      const relic = (CONFIG_RELIC_BY_TOPIC.get(topic.id) || new Map())
        .get(String(id));
      entry.relics.push({
        id: String(id),
        layer: clampInt(relic?.stack?.init, 1, relic?.stack?.max || 999, 1),
      });
    }
    setConfigEntry(topic.id, { relics: entry.relics });
  }

  function setConfigRelicLayer(id, value, rerender = true) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const item = entry.relics.find(
      (candidate) => String(candidate.id) === String(id));
    if (!item) return;
    const relic = (CONFIG_RELIC_BY_TOPIC.get(topic.id) || new Map())
      .get(String(id));
    const max = Number(relic?.stack?.max) || 999;
    item.layer = clampInt(value, 1, max, 1);
    if (rerender) {
      setConfigEntry(topic.id, { relics: entry.relics });
      return;
    }
    state.battleConfig = {
      ...(state.battleConfig || {}),
      [topic.id]: { ...entry, relics: entry.relics },
    };
    saveUi();
  }

  function setConfigResource(field, rawValue) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    if (!["gold", "scrap", "scrapLimit"].includes(field)) return;
    const entry = configEntry(topic.id);
    const value = rawValue === "" || rawValue === null || rawValue === undefined
      ? (field === "scrap" || field === "scrapLimit" ? -1 : 0)
      : clampInt(rawValue, 0, 9999, field === "scrapLimit" ? 10 : 0);
    entry[field] = value;
    state.battleConfig = {
      ...(state.battleConfig || {}),
      [topic.id]: entry,
    };
    saveUi();
  }

  function toggleConfigMastery(charId, masterId, enabled) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const index = entry.masters.findIndex((item) =>
      item.charId === charId && item.masterId === masterId);
    if (enabled) {
      if (index >= 0) {
        entry.masters[index].level = clampInt(
          entry.masters[index].level, 1, 3, 1);
      } else {
        entry.masters.push({ charId, masterId, level: 1, zone: 1 });
      }
    } else if (index >= 0) {
      entry.masters.splice(index, 1);
    }
    setConfigEntry(topic.id, { masters: entry.masters });
  }

  function setConfigMasteryLevel(charId, masterId, rawLevel) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const item = entry.masters.find((candidate) =>
      candidate.charId === charId && candidate.masterId === masterId);
    if (!item) return;
    item.level = clampInt(rawLevel, 1, 3, 1);
    const operator = (topic.specialOperators || []).find(
      (candidate) => candidate.charId === charId);
    const mastery = operator?.masteries.find(
      (candidate) => candidate.id === masterId);
    const zoneCount = mastery?.chains?.[item.level]?.length || 4;
    item.zone = clampInt(item.zone, 0, zoneCount, 0);
    setConfigEntry(topic.id, { masters: entry.masters });
  }

  function setConfigMasteryZone(charId, masterId, rawZone) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const item = entry.masters.find((candidate) =>
      candidate.charId === charId && candidate.masterId === masterId);
    if (!item) return;
    item.zone = clampInt(rawZone, 0, 4, 0);
    setConfigEntry(topic.id, { masters: entry.masters });
  }

  function toggleConfigSpecial(kind, id, layer) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const group = (topic.special || []).find(
      (candidate) => candidate.kind === kind);
    const family = group
      ? specialSelectionFamily(group, id)
      : specialFamilyId(kind, id);
    const multi = kind === "wrath";
    const familyOf = (item) => group
      ? specialSelectionFamily(group, item.id)
      : specialFamilyId(kind, item.id);
    const index = entry.special.findIndex(
      (item) => item.kind === kind && (!multi || familyOf(item) === family));
    if (index >= 0) {
      if (multi) entry.special.splice(index, 1);
      else entry.special[index] = { kind, id: family, layer };
    } else {
      entry.special.push({ kind, id: family, layer });
    }
    setConfigEntry(topic.id, { special: entry.special });
  }

  function setConfigSpecialVariant(kind, familyId, id) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const group = (topic.special || []).find(
      (candidate) => candidate.kind === kind);
    const index = entry.special.findIndex((item) => {
      if (item.kind !== kind) return false;
      if (String(item.id) === String(familyId)) return true;
      return group
        ? specialSelectionFamily(group, item.id) === familyId
        : false;
    });
    const currentItem = index >= 0 && group
      ? resolveSpecialItem(group, entry.special[index].id, entry.grade)
      : null;
    if (!id) {
      if (index >= 0) entry.special.splice(index, 1);
    } else if (currentItem && String(currentItem.id) === String(id)) {
      entry.special.splice(index, 1);
    } else if (index >= 0) {
      const next = { ...entry.special[index], kind, id };
      delete next.normalId;
      entry.special[index] = next;
    } else {
      entry.special.push({ kind, id, layer: 1 });
    }
    setConfigEntry(topic.id, { special: entry.special });
  }

  function setConfigSpecialAwake(kind, familyId, awake) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const group = (topic.special || []).find((item) => item.kind === kind);
    if (!group) return;
    const family = specialFamilies(group).find(
      (item) => item.id === familyId);
    const awakeItem = family?.items.find((item) => item.awakened);
    if (!family || !awakeItem) return;
    const index = entry.special.findIndex((item) =>
      item.kind === kind &&
      specialSelectionFamily(group, item.id) === familyId);
    const tier = specialTierForGrade(entry.grade);
    const tierItems = family.items.filter(
      (item) => specialLevelIndex(item.id) === tier);
    const concrete = tierItems.filter(
      (item) => specialVariantSlot(item.id) !== "");
    const pool = concrete.length ? concrete : tierItems;
    if (awake) {
      const current = index >= 0 ? entry.special[index] : null;
      const currentItem = current
        ? family.items.find((item) => String(item.id) === String(current.id))
        : null;
      const normalItem = currentItem && !currentItem.awakened
        ? currentItem
        : resolveSpecialItem(group, current?.id || family.id, entry.grade);
      const normalId = normalItem?.id || pool[0]?.id || "";
      const next = current
        ? { ...current, kind, id: awakeItem.id, normalId }
        : { kind, id: awakeItem.id, layer: 1, normalId };
      if (index >= 0) entry.special[index] = next;
      else entry.special.push(next);
    } else {
      if (index < 0) return;
      const current = entry.special[index];
      const normalId = pool.some((item) =>
        String(item.id) === String(current.normalId))
        ? current.normalId
        : (pool[0]?.id || family.id);
      const next = { ...current, kind, id: normalId };
      delete next.normalId;
      entry.special[index] = next;
    }
    setConfigEntry(topic.id, { special: entry.special });
  }

  function toggleConfigTech(id) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const index = entry.tech.findIndex((item) => String(item) === String(id));
    if (index >= 0) {
      entry.tech.splice(index, 1);
    } else {
      entry.tech.push(String(id));
    }
    setConfigEntry(topic.id, { tech: entry.tech });
  }

  function setConfigTechAll(selected) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    entry.tech = selected
      ? (topic.tech || []).map((node) => String(node.id))
      : [];
    setConfigEntry(topic.id, { tech: entry.tech });
  }

  function toggleConfigConcept(id) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const index = entry.concepts.findIndex(
      (item) => String(item.id) === String(id));
    if (index >= 0) {
      entry.concepts.splice(index, 1);
    } else {
      entry.concepts.push({ id: String(id), layer: 1 });
    }
    setConfigEntry(topic.id, { concepts: entry.concepts });
  }

  function setConfigConceptLayer(id, value, rerender = true) {
    const topic = configTopicData(state.configTopic);
    if (!topic) return;
    const entry = configEntry(topic.id);
    const item = entry.concepts.find(
      (candidate) => String(candidate.id) === String(id));
    if (!item) return;
    item.layer = clampInt(value, 1, 99, 1);
    if (rerender) {
      setConfigEntry(topic.id, { concepts: entry.concepts });
      return;
    }
    state.battleConfig = {
      ...(state.battleConfig || {}),
      [topic.id]: { ...entry, concepts: entry.concepts },
    };
    saveUi();
  }

  pages.config = Object.assign(pages.config || {}, {
    setEntry: setConfigEntry,
    toggleCopper: toggleConfigCopper,
    toggleCharBuff: toggleConfigCharBuff,
    toggleCharBuffTarget: toggleConfigCharBuffTarget,
    toggleRunBuff: toggleConfigRunBuff,
    applyToSquad: applyConfigToSquad,
    toggleRelic: toggleConfigRelic,
    setRelicLayer: setConfigRelicLayer,
    setResource: setConfigResource,
    toggleMastery: toggleConfigMastery,
    setMasteryLevel: setConfigMasteryLevel,
    setMasteryZone: setConfigMasteryZone,
    toggleSpecial: toggleConfigSpecial,
    setSpecialVariant: setConfigSpecialVariant,
    setSpecialAwake: setConfigSpecialAwake,
    toggleTech: toggleConfigTech,
    setTechAll: setConfigTechAll,
    toggleConcept: toggleConfigConcept,
    setConceptLayer: setConfigConceptLayer,
  });
})();
