"use strict";

(() => {
  const pages = window.BetterBattlePages || (window.BetterBattlePages = {});
  const { collectAbnormalChips, renderAbnormalChips } = pages.battle || {};

  let lastEnemyFocusAt = 0;
  let lastEnemyFocusUid = 0;

  function selectEnemy(uid, focus = false) {
    const value = Number(uid);
    if (!Number.isFinite(value) || value <= 0) return;
    window.BetterBattleSession?.selectEnemy(value);
    renderEnemies();
    if (focus) {
      const now = Date.now();
      if (value === lastEnemyFocusUid && now - lastEnemyFocusAt < 300) return;
      lastEnemyFocusUid = value;
      lastEnemyFocusAt = now;
      sendCommand(window.BetterBattleProtocol.commands.enemyFocus(value));
    }
  }

  function enemyHpRatio(enemy) {
    const max = Number(enemy?.max_hp);
    const hp = Number(enemy?.hp);
    return Number.isFinite(max) && max > 0 && Number.isFinite(hp)
      ? Math.max(0, Math.min(1, hp / max))
      : 0;
  }

  function enemyLevelLabel(value) {
    return ["普通", "精英", "BOSS"][Number(value) || 0] || "未知";
  }

  function enemyDamageTypeLabel(value) {
    return ["物理", "法术", "治疗", "无伤害"][Number(value) || 0] || String(value);
  }

  function fmtExact(value) {
    const number = Number(value);
    return Number.isFinite(number)
      ? Math.round(number).toLocaleString("zh-CN")
      : "--";
  }

  function enemyProfileFor(profiles, id) {
    return profiles.get(String(id || "")) || null;
  }

  function enemyDisplayLabel(id, profiles, enemies = [], serialOverride = 0) {
    const enemyId = String(id || "");
    const profile = enemyProfileFor(profiles, enemyId);
    const name = (
      profile?.name && profile.name !== enemyId ? profile.name : ""
    ) || "未知敌人";
    const serials = [...new Set(
      (Array.isArray(enemies) ? enemies : [])
        .filter((enemy) => String(enemy?.id || "") === enemyId)
        .map((enemy) => Number(enemy?.serial) || 0)
        .filter((serial) => serial > 0),
    )].sort((left, right) => left - right);
    const serial = Number(serialOverride) || (serials.length === 1 ? serials[0] : 0);
    return serial ? `#${serial} ${name}` : name;
  }

  function enemyDisplayLabelByUid(uid, identities, profiles, enemies = []) {
    const value = Number(uid) || 0;
    if (!value) return "";
    let identity = (Array.isArray(identities) ? identities : [])
      .find((entry) => Number(entry?.uid) === value);
    if (!identity) {
      const current = (Array.isArray(enemies) ? enemies : [])
        .find((enemy) => Number(enemy?.uid) === value);
      if (current) {
        identity = { id: current.id, serial: current.serial };
      }
    }
    if (!identity) return "";
    const id = String(identity.id || "");
    const profile = enemyProfileFor(profiles, id);
    const name = (
      profile?.name && profile.name !== id ? profile.name : ""
    ) || "未知敌人";
    const serial = Number(identity.serial) || 0;
    return serial ? `#${serial} ${name}` : name;
  }

  window.BetterBattleEnemyDisplay = Object.freeze({
    profileFor: enemyProfileFor,
    label: enemyDisplayLabel,
    labelByUid: enemyDisplayLabelByUid,
  });

  function enemyStatRows(enemy) {
    const hp = Number(enemy?.hp) || 0;
    const maxHp = Number(enemy?.max_hp) || 0;
    return [
      ["生命", `${fmtExact(hp)} / ${fmtExact(maxHp)}`],
      ["攻击", fmt(enemy?.atk, 0)],
      ["防御", fmt(enemy?.def, 0)],
      ["法抗", `${fmt(enemy?.magic_resistance, 0)}%`],
      ["攻速", fmt(enemy?.attack_speed, 0)],
      ["移速", fmt(enemy?.move_speed, 3)],
      ["阻挡", String(enemy?.block_count ?? 0)],
      ["重量", String(enemy?.mass_level ?? 0)],
    ];
  }

  function gameTextHtml(value) {
    const text = String(value ?? "").replace(/^\s*text\s*=\s*/i, "");
    let html = "";
    let depth = 0;
    let cursor = 0;
    while (cursor < text.length) {
      const open = text.indexOf("<", cursor);
      if (open < 0) {
        html += esc(text.slice(cursor));
        break;
      }
      html += esc(text.slice(cursor, open));
      const close = text.indexOf(">", open + 1);
      if (close < 0) {
        html += esc(text.slice(open));
        break;
      }
      const tag = text.slice(open + 1, close);
      if (/^(?:(?:\/|\$|@)[^<>]*|[a-zA-Z][\w-]*(?:=[^<>]*)?)$/.test(tag)) {
        if (tag.startsWith("/")) {
          if (depth > 0) {
            html += "</u></strong>";
            depth -= 1;
          }
        } else {
          html += "<strong><u>";
          depth += 1;
        }
      } else {
        html += esc(text.slice(open, close + 1));
      }
      cursor = close + 1;
    }
    while (depth-- > 0) {
      html += "</u></strong>";
    }
    return html;
  }

  function renderEnemies() {
    const snapshot = state.snapshot;
    const enemies = window.BetterBattleSession?.aliveEnemies(snapshot) || [];
    const profiles = window.BetterBattleSession
      ? window.BetterBattleSession.enemyProfiles(snapshot)
      : new Map(
        (Array.isArray(snapshot?.enemy_profiles) ? snapshot.enemy_profiles : [])
          .map((profile) => [String(profile?.id || ""), profile]),
      );
    const waves = snapshot?.enemy_waves || {};
    const waveList = Array.isArray(waves.waves) ? waves.waves : [];
    const waveTotal = Number(waves.total) || 0;
    const waveCurrent = Math.max(
      0,
      Math.min(Number(waves.current) || 0, waveTotal),
    );
    const focusUid = Number(snapshot?.enemy_focus?.uid) || 0;
    if (state.selectedEnemyUid !== null &&
        !enemies.some((enemy) => Number(enemy.uid) === Number(state.selectedEnemyUid))) {
      state.selectedEnemyUid = null;
    }
    if (state.selectedEnemyUid === null && enemies.length) {
      state.selectedEnemyUid = Number(enemies[0].uid);
    }
    const selected = enemies.find(
      (enemy) => Number(enemy.uid) === Number(state.selectedEnemyUid),
    ) || null;

    dom.enemyCountBadge.textContent = `在场 ${enemies.length}`;
    dom.enemyWaveBadge.textContent = waveTotal > 0
      ? `波次 ${waveCurrent}/${waveTotal}`
      : "波次 --";
    dom.enemyFocusBadge.textContent = focusUid
      ? `高亮 #${enemies.find((enemy) => Number(enemy.uid) === focusUid)?.serial ?? "?"}`
      : "未选中";
    dom.enemyFocusBadge.className = "badge" + (focusUid ? " green" : "");

    if (!enemies.length) {
      setHtml(dom.enemyList, `<div class="empty-small">等待敌人出场</div>`);
    } else {
      setHtml(dom.enemyList, enemies.map((enemy) => {
        const uid = Number(enemy.uid);
        const hpRatio = enemyHpRatio(enemy) * 100;
        const profile = enemyProfileFor(profiles, enemy.id);
        const displayName =
          (profile?.name && profile.name !== enemy.id ? profile.name : "") ||
          "未知敌人";
        const displayLabel = enemyDisplayLabel(
          enemy.id,
          profiles,
          enemies,
          enemy.serial,
        );
        const row = Number(enemy.row);
        const col = Number(enemy.col);
        const coordinate = Number.isInteger(row) && row >= 0 &&
          Number.isInteger(col) && col >= 0
          ? `<span class="enemy-row-coord">R${row} · C${col}</span>`
          : "";
        return `<button type="button" class="enemy-row${uid === Number(state.selectedEnemyUid) ? " is-selected" : ""}${uid === focusUid ? " is-focused" : ""}" data-action="enemy-focus" data-uid="${uid}">
          <span class="enemy-row-avatar"><img data-asset-kind="enemy-icon" data-asset-id="${esc(enemy.id)}" alt="" loading="lazy"></span>
          <span class="enemy-row-main">
            <span class="enemy-row-name"><span class="enemy-row-name-text">${esc(displayLabel)}</span></span>
            ${coordinate}
            <span class="enemy-hp"><i style="width:${hpRatio.toFixed(1)}%"></i></span>
          </span>
        </button>`;
      }).join(""));
      window.BetterBattleAssets?.observe(dom.enemyList);
    }

    if (!selected) {
      dom.enemyDetailName.textContent = "未选择敌人";
      dom.enemyDetailMeta.textContent = "";
      dom.enemyDetailSerial.textContent = "--";
      dom.enemyAvatar.removeAttribute("src");
      dom.enemyAvatar.dataset.assetKind = "";
      dom.enemyAvatar.dataset.assetId = "";
      dom.enemyProfileMeta.textContent = "--";
      dom.enemyDescription.textContent = "等待敌人资料";
      setHtml(dom.enemyTags, "");
      setHtml(dom.enemyStatGrid, "");
      renderAbnormalChips(dom.enemyAbnormalChips, dom.enemyAbnormalCount, [],
        "未选择敌人");
      setHtml(dom.enemyAbilities, `<div class="empty-small">未选择敌人</div>`);
      setHtml(dom.enemySkills, `<div class="empty-small">未选择敌人</div>`);
      setHtml(dom.enemyBuffs, `<div class="empty-small">未选择敌人</div>`);
    } else {
      const profile = enemyProfileFor(profiles, selected.id) || {};
      const hp = Number(selected.hp) || 0;
      const maxHp = Number(selected.max_hp) || 0;
      const displayName =
        (profile.name && profile.name !== selected.id ? profile.name : "") ||
        "未知敌人";
      dom.enemyDetailName.textContent = displayName;
      dom.enemyDetailMeta.textContent =
        `出现于 ${formatDurationValue(Number(selected.created_time) || 0)}`;
      dom.enemyDetailSerial.textContent = `#${selected.serial}`;
      dom.enemyAvatar.dataset.assetKind = "enemy-icon";
      dom.enemyAvatar.dataset.assetId = String(selected.id || "");
      if (!dom.enemyAvatar.getAttribute("src")) {
        dom.enemyAvatar.removeAttribute("src");
      }
      window.BetterBattleAssets?.ensure(dom.enemyAvatar);
      const profileMeta = [
        profile.index || "--",
        enemyLevelLabel(profile.level_type),
        profile.race || "",
        profile.attack_type || "",
      ].filter(Boolean).join(" · ");
      dom.enemyProfileMeta.textContent = profileMeta || "--";
      setHtml(dom.enemyDescription,
        gameTextHtml(profile.description || "游戏内没有返回该敌人的图鉴描述"));
      const tags = [
        ...(Array.isArray(profile.tags) ? profile.tags : []),
        ...(Array.isArray(profile.damage_types)
          ? profile.damage_types.map(enemyDamageTypeLabel)
          : []),
      ].filter(Boolean);
      setHtml(dom.enemyTags, tags.length
        ? tags.map((tag) => `<span>${esc(tag)}</span>`).join("")
        : "");
      const stats = enemyStatRows(selected);
      setHtml(dom.enemyStatGrid, stats.map(([label, value]) =>
        `<div class="enemy-stat"><span>${label}</span><strong>${esc(value)}</strong></div>`,
      ).join(""));
      renderAbnormalChips(
        dom.enemyAbnormalChips,
        dom.enemyAbnormalCount,
        collectAbnormalChips(selected.buffs),
        "无异常");
      const abilities = Array.isArray(profile.abilities)
        ? profile.abilities.filter(Boolean)
        : [];
      if (!abilities.length && profile.ability) {
        abilities.push(String(profile.ability));
      }
      setHtml(dom.enemyAbilities, abilities.length
        ? `<div class="enemy-ability">${abilities.map((ability) =>
          `<div class="enemy-ability-line">${gameTextHtml(ability)}</div>`,
        ).join("")}</div>`
        : `<div class="empty-small">该敌人没有可读取的图鉴能力</div>`);
      const skills = Array.isArray(selected.skills) ? selected.skills : [];
      let skillCards = skills.map((skill) => {
          const flags = [
            skill.enabled === false ? "未启用" : "启用",
            skill.used_up ? "已耗尽" : "",
            skill.casting ? "施放中" : "",
            skill.affecting ? "生效中" : "",
          ].filter(Boolean).join(" · ");
          const cooldown = Number(skill.cooldown_total) > 0 || Number(skill.cooldown) > 0
            ? `CD ${fmt(skill.cooldown, 1)}/${fmt(skill.cooldown_total, 1)}s`
            : "CD --";
          return `<div class="enemy-skill"><div class="enemy-skill-head"><strong>${esc(skill.key || "未知技能")}</strong><span class="enemy-skill-meta">${esc(flags)}</span></div>
            <div class="enemy-skill-meta">${esc(cooldown)} · 触发 ${Number(skill.trigger_count) || 0}/${Number(skill.max_trigger_count) || "∞"}${skill.signal ? ` · ${esc(skill.signal)}` : ""}</div></div>`;
        });
      const configuredSkills = Array.isArray(profile.skills)
        ? profile.skills.filter(Boolean)
        : [];
      if (!skillCards.length && configuredSkills.length) {
        skillCards = configuredSkills.map((skill) => {
          const cooldown = Number(skill.cooldown) > 0
            ? `冷却 ${fmt(skill.cooldown, 1)}s`
            : "冷却 --";
          const initial = Number(skill.initial_cooldown) > 0
            ? `初始 ${fmt(skill.initial_cooldown, 1)}s`
            : "";
          const sp = Number(skill.sp_cost) > 0
            ? `SP ${fmt(skill.sp_cost, 0)}`
            : "";
          return `<div class="enemy-skill"><div class="enemy-skill-head"><strong>${esc(skill.key || "未知技能")}</strong><span class="enemy-skill-meta">技能配置</span></div>
            <div class="enemy-skill-meta">${esc([cooldown, initial, sp].filter(Boolean).join(" · "))} · 优先级 ${Number(skill.priority) || 0}</div></div>`;
        });
      }
      setHtml(dom.enemySkills, skillCards.length
        ? skillCards.join("")
        : `<div class="empty-small">该敌人没有可读取的技能配置</div>`);
      const buffs = Array.isArray(selected.buffs) ? selected.buffs : [];
      const catalog = window.BuffCatalog;
      setHtml(dom.enemyBuffs, buffs.length
        ? buffs.map((buff) => {
          const info = catalog ? catalog.resolve(buff) : null;
          const name = info?.name || buff.key || buff.template || "Buff";
          const board = catalog ? catalog.blackboardEntries(buff) : [];
          const description = catalog && info?.description &&
            !catalog.hidesDescription(info.entry)
            ? catalog.substituteDescription(info.description, board)
            : "";
          const rendered = description
            ? (typeof renderRichText === "function"
                ? renderRichText(description)
                : esc(description))
            : "";
          const abnormal = [
            ["异常", buff.abnormal],
            ["免疫", buff.abnormal_immune],
            ["反制", buff.abnormal_anti],
            ["组合", buff.abnormal_combo],
            ["组合免疫", buff.abnormal_combo_immune],
          ].filter(([, value]) => value).map(([label, value]) => [
            label,
            catalog ? catalog.abnormalLabels(value).join("、") : value,
          ]);
          const modifiers = catalog && Array.isArray(buff.modifier_entries) &&
            buff.modifier_entries.length
            ? catalog.modifierGroups(buff.modifier_entries)
            : null;
          const rawFields = [
            ["模板", buff.template],
            ["覆盖", buff.override],
            ["效果", buff.effect],
            ["来源", buff.source],
          ].filter(([, value]) => value);
          const life = isPermanent(buff)
            ? "永久"
            : `${fmtDuration(Number(buff.remaining) || 0)} · ${(buffRatio(buff) * 100).toFixed(0)}%`;
          return `<div class="enemy-buff"><div class="enemy-buff-head"><strong>${esc(name)}</strong><span class="enemy-buff-meta">${esc(life)}</span></div>
            ${name !== buff.key && buff.key ? `<div class="enemy-buff-mod"><code>${esc(buff.key)}</code>${info ? "" : " · 目录未收录"}</div>` : ""}
            <div class="enemy-buff-meta">层数 ${Number(buff.stack) || 0}/${Number(buff.max_stack) || "?"} · 触发 ${Number(buff.trigger_count) || 0}${buff.status_resistable ? " · 可抵抗" : ""}</div>
            ${rendered ? `<div class="buff-desc compact">${rendered}</div>` : ""}
            ${modifiers && modifiers.groups.length
              ? `<div class="buff-effects"><span class="chip ${modifiers.category === "减益" ? "bad" : modifiers.category === "加成" ? "good" : ""}">${esc(modifiers.category)}</span>` +
                modifiers.groups.map((group) =>
                  `<div class="buff-effect-row"><span class="buff-effect-kind">${esc(group.label)}</span>` +
                  group.items.map((item) =>
                    `<code class="${item.value > 0 ? "up" : "down"}">${esc(catalog.modifierText(item))}</code>`,
                  ).join("") + `</div>`).join("") + `</div>`
              : (buff.modifiers ? `<div class="enemy-buff-mod">${esc(buff.modifiers)}</div>` : "")}
            ${abnormal.length ? `<div class="buff-abnormal">${abnormal.map(([label, value]) =>
              `${esc(label)} ${esc(value)}`).join("<br>")}</div>` : ""}
            ${board.length ? `<div class="buff-blackboard"><div class="buff-blackboard-head">参数</div>` +
              board.map((entry) => `<div class="buff-blackboard-row"><span>${esc(entry.key)}</span>` +
                `<code>${esc(catalog ? catalog.boardValue(entry) : entry.value_str)}</code></div>`).join("") + `</div>` : ""}
            ${rawFields.length ? `<div class="enemy-buff-mod">${rawFields.map(([label, value]) => `${esc(label)} ${esc(value)}`).join("<br>")}</div>` : ""}</div>`;
        }).join("")
        : `<div class="empty-small">当前没有 Buff</div>`);
    }

    dom.enemyWaveMeta.textContent = waveTotal > 0
      ? `计划 ${waveList.reduce((sum, wave) => sum + (Number(wave.total) || 0), 0)} 个敌人 · ${waveCurrent}/${waveTotal} 波已出场；动态召唤不在其中`
      : "等待关卡波次数据；动态召唤不在计划列表中";
    setHtml(dom.enemyWaves, waveList.length
      ? waveList.map((wave) => {
        const actions = Array.isArray(wave.actions) ? wave.actions : [];
        const spawned = Number(wave.index) <= waveCurrent;
        return `<div class="enemy-wave"><div class="enemy-wave-head"><strong>第 ${Number(wave.index) || "?"} 波</strong><span class="enemy-subtle">${spawned ? "已出场" : "预计"} · ${Number(wave.total) || 0} 个</span></div>
          <div class="enemy-wave-actions">${actions.map((action) => {
            const label = enemyDisplayLabel(action.key, profiles, enemies);
            return `<div class="enemy-wave-action"><strong>${esc(label)}</strong><span>×${Number(action.count) || 0}</span><code>${formatDurationValue(Number(action.first_time) || 0)} · R${Number(action.route) >= 0 ? Number(action.route) : "?"}</code></div>`;
          }).join("")}</div></div>`;
      }).join("")
      : `<div class="empty-small">该关卡没有可读取的静态波次计划</div>`);
  }

  pages.enemies = {
    render: renderEnemies,
    select: selectEnemy,
    profileFor: enemyProfileFor,
    statRows: enemyStatRows,
    displayLabel: enemyDisplayLabel,
    displayLabelByUid: enemyDisplayLabelByUid,
    gameTextHtml,
  };
})();
