(() => {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const state = {
    coverage: null,
    operators: [],
    operator: null,
    selectedId: null,
    skillIndex: 0,
    pendingSkill: null,
    kind: "all",
    search: "",
    showCosmetic: true,
    buffKey: "",
    componentIndex: new Map(),
    componentDetail: null,
  };

  const esc = (value) =>
    String(value ?? "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;");

  const pct = (value) => `${Math.round(Number(value || 0) * 1000) / 10}%`;

  function el(tag, className, text) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }

  function badge(text, cls = "") {
    return el("span", `badge ${cls}`.trim(), text);
  }

  function chip(text, cls = "") {
    return el("span", `chip ${cls}`.trim(), text);
  }

  async function fetchJSON(url) {
    const response = await fetch(url, { cache: "no-store" });
    if (!response.ok) {
      const body = await response.text();
      throw new Error(`${url}: ${response.status} ${body}`);
    }
    return response.json();
  }

  function setEmpty(container, text) {
    container.replaceChildren();
    container.append(el("div", "empty", text));
  }

  function renderTopStats() {
    const counts = state.coverage.counts;
    $("statOperators").textContent = `${counts.operators} 干员 / ${counts.tokens} Token`;
    $("statSkills").textContent = `${counts.skills} 技能`;
    $("statNodes").textContent = `${counts.prefabsReached} prefab · ${counts.nodesTotal} 节点`;
    $("operatorCount").textContent = `${state.operators.length}`;
  }

  function renderOperatorList() {
    const list = $("operatorList");
    const query = state.search.trim().toLowerCase();
    const filtered = state.operators.filter((operator) => {
      if (state.kind !== "all" && operator.kind !== state.kind) return false;
      if (!query) return true;
      return [operator.name, operator.id, operator.appellation]
        .filter(Boolean)
        .some((value) => String(value).toLowerCase().includes(query));
    });
    list.replaceChildren();
    if (!filtered.length) {
      setEmpty(list, "没有匹配项");
      return;
    }
    filtered.sort(compareOperators);
    for (const operator of filtered) {
      const button = el("button", "operator-item");
      if (operator.id === state.selectedId) button.classList.add("is-active");
      const top = el("div", "operator-item-top");
      top.append(el("strong", "", operator.name || operator.id));
      top.append(
        badge(
          operator.coverage?.core_fully_covered ? "完整" : "有缺口",
          operator.coverage?.core_fully_covered ? "ok" : "warn",
        ),
      );
      const meta = el("div", "operator-item-meta");
      meta.append(el("code", "", operator.id));
      meta.append(
        el(
          "span",
          "",
          `${operator.skills?.length || 0} 技能 · ${
            operator.character_prefab?.component_kinds
              ? Object.values(operator.character_prefab.component_kinds).reduce(
                  (sum, value) => sum + value,
                  0,
                )
              : 0
          } 组件`,
        ),
      );
      button.append(top, meta);
      button.onclick = () => selectOperator(operator.id);
      list.append(button);
    }
  }

  function compareOperators(a, b) {
    const kindOrder = (a.kind === "operator" ? 0 : 1) - (b.kind === "operator" ? 0 : 1);
    if (kindOrder) return kindOrder;
    const skillOrder = (b.skills?.length || 0) - (a.skills?.length || 0);
    if (skillOrder) return skillOrder;
    return String(a.name || a.id).localeCompare(String(b.name || b.id), "zh-CN");
  }

  async function selectOperator(operatorId) {
    state.selectedId = operatorId;
    state.skillIndex = 0;
    renderOperatorList();
    setEmpty($("center"), "加载中…");
    try {
      state.operator = await fetchJSON(
        `/api/operator?id=${encodeURIComponent(operatorId)}`,
      );
      state.componentIndex = indexOperatorComponents(state.operator);
      state.componentDetail = null;
      $("componentTitle").textContent = "组件详情";
      $("componentSubtitle").textContent = "从浏览页点击组件卡片打开。";
      $("componentBreadcrumb").replaceChildren();
      const detail = $("componentDetail");
      detail.replaceChildren();
      const empty = el("article", "panel empty-panel");
      empty.append(el("h3", "", "未选择组件"));
      detail.append(empty);
      if ($("page-component").classList.contains("is-active")) switchTab("browse");
      if (state.pendingSkill) {
        const index = (state.operator.skills || []).findIndex(
          (skill) => skill.skill_id === state.pendingSkill,
        );
        if (index >= 0) state.skillIndex = index;
        state.pendingSkill = null;
      }
      renderOperator();
      if ($("page-talent").classList.contains("is-active")) {
        renderTalentPage();
      }
    } catch (error) {
      setEmpty($("center"), `加载失败：${error.message}`);
    }
  }

  function renderOperator() {
    const operator = state.operator;
    const center = $("center");
    center.replaceChildren();

    const header = el("article", "panel operator-head");
    const title = el("div", "operator-title");
    title.append(el("div", "eyebrow", operator.kind === "token" ? "TOKEN" : "OPERATOR"));
    title.append(el("h2", "", operator.name || operator.id));
    const meta = el("div", "meta-line");
    meta.append(el("code", "", operator.id));
    if (operator.appellation) meta.append(chip(operator.appellation));
    if (operator.profession) meta.append(chip(operator.profession));
    title.append(meta);
    header.append(title);
    header.append(
      badge(
        operator.coverage?.core_fully_covered ? "prefab 完整" : "prefab 有缺口",
        operator.coverage?.core_fully_covered ? "ok" : "warn",
      ),
    );
    center.append(header);

    const prefabMeta = el("div", "operator-prefab-meta");
    prefabMeta.append(el("span", "label", "Character prefab"));
    prefabMeta.append(el("code", "", operator.character_prefab?.id || "—"));
    if (operator.character_prefab?.missing?.length) {
      prefabMeta.append(badge(`missing ${operator.character_prefab.missing.length}`, "danger"));
    }
    center.append(prefabMeta);

    const modeContext = operatorModeContext(operator);
    const modes = modeContext.modes;
    if (modes.length) {
      const strip = el("div", "mode-strip");
      strip.append(el("span", "label", "Modes"));
      for (const mode of modes) {
        const chip = el("button", "mode-chip");
        chip.type = "button";
        chip.append(el("b", "", `#${mode.index}`));
        chip.append(document.createTextNode(mode.label));
        if (mode.component) {
          chip.classList.add("clickable");
          chip.onclick = () => openModeDialog(mode, modeContext);
        }
        strip.append(chip);
      }
      strip.append(
        el(
          "span",
          "mode-note",
          "SwitchMode 按索引切换这组子树；攻击、范围、能力都在子树内部。",
        ),
      );
      center.append(strip);
    }

    const skills = operator.skills || [];
    if (!skills.length) {
      const empty = el("article", "panel empty-panel");
      empty.append(el("h3", "", "没有技能条目"));
      center.append(empty);
      return;
    }
    const skillTabs = el("div", "skill-tabs");
    skills.forEach((skill, index) => {
      const button = el("button", "skill-tab");
      if (index === state.skillIndex) button.classList.add("is-active");
      button.append(el("strong", "", skill.name || skill.skill_id));
      button.append(el("code", "", skill.skill_id));
      button.onclick = () => {
        state.skillIndex = index;
        renderOperator();
      };
      skillTabs.append(button);
    });
    center.append(skillTabs);
    renderSkill(skills[state.skillIndex], center, modeContext);
  }

  function operatorModeContext(operator) {
    const components = operator.character_prefab?.components || [];
    const componentsById = new Map(
      components.filter((component) => component.id).map((component) => [component.id, component]),
    );
    const root = components.find((component) => component.key_fields?._modes);
    const refs = root?.key_fields?._modes;
    const linkById = new Map((root?.links || []).map((link) => [link.id, link]));
    const modes = (Array.isArray(refs) ? refs : []).map((ref, index) => {
      const id = ref?.$component;
      const link = linkById.get(id);
      return {
        index,
        id,
        label: link?.go || link?.path || id || `#${index}`,
        path: link?.path || "",
        component: componentsById.get(id) || null,
      };
    });
    return {
      characterName: operator.name || operator.id,
      componentsById,
      modes,
    };
  }

  function indexOperatorComponents(operator) {
    const index = new Map();
    const addPrefab = (prefab, meta) => {
      if (!prefab) return;
      for (const component of prefab.components || []) {
        if (!component.id) continue;
        index.set(component.id, {
          component,
          prefabId: prefab.id || "",
          ...meta,
        });
      }
      for (const projectile of prefab.projectiles || []) {
        addPrefab(projectile, meta);
      }
    };
    addPrefab(operator.character_prefab, { scope: "character", skillName: "" });
    for (const skill of operator.skills || []) {
      for (const prefab of skill.prefabs || []) {
        addPrefab(prefab, {
          scope: "skill",
          skillName: skill.name || skill.skill_id,
        });
      }
    }
    for (const module of operator.modules?.items || []) {
      for (const item of module.prefabs || []) {
        if (item.prefab) {
          addPrefab(item.prefab, {
            scope: "module",
            skillName: module.name || module.uni_equip_id || "",
          });
        }
      }
    }
    return index;
  }

  function renderSkill(skill, container, modeContext) {
    const card = el("article", "panel skill-detail");
    const head = el("div", "panel-head");
    const title = el("div", "");
    title.append(el("div", "eyebrow", "SKILL"));
    title.append(el("h3", "", skill.name || skill.skill_id));
    title.append(el("code", "", skill.skill_id));
    head.append(title);
    if (skill.missing?.length) {
      head.append(badge(`missing ${skill.missing.length}`, "danger"));
    } else {
      head.append(badge("prefab resolved", "ok"));
    }
    card.append(head);

    const facts = el("div", "fact-grid");
    addFact(facts, "类型", skill.skill_type || "—");
    addFact(facts, "持续", skill.duration_type || "—");
    addFact(facts, "prefab", String(skill.prefab_ids?.length || 0));
    for (const prefab of skill.prefabs || []) {
      if (prefab.missing?.length) continue;
      addFact(facts, "组件", String(sumKinds(prefab.component_kinds)));
      addFact(facts, "Buff 图", String(prefab.buff_graphs || 0));
      addFact(facts, "内联图", String(prefab.inline_graphs || 0));
    }
    card.append(facts);

    if (!skill.prefabs?.length) {
      card.append(el("div", "empty", "该技能没有 prefab。"));
    }
    for (const prefab of skill.prefabs || []) {
      renderPrefab(prefab, card, 0, modeContext);
    }
    container.append(card);
  }

  function sumKinds(kinds) {
    return Object.values(kinds || {}).reduce((sum, value) => sum + Number(value || 0), 0);
  }

  function addFact(grid, label, value) {
    const item = el("div", "fact");
    item.append(el("span", "", label));
    item.append(el("strong", "", value));
    grid.append(item);
  }

  function renderPrefab(prefab, container, depth, modeContext) {
    const box = el("section", `prefab-box depth-${depth}`);
    const head = el("div", "prefab-head");
    const title = el("div", "");
    title.append(
      el("div", "eyebrow", depth === 0 ? "SKILL PREFAB" : "PROJECTILE PREFAB"),
    );
    title.append(el("code", "prefab-id", prefab.id || "—"));
    if (prefab.bundle) title.append(chip(prefab.bundle));
    head.append(title);
    if (prefab.exists === false) {
      head.append(badge("missing", "danger"));
    } else {
      head.append(badge(`${prefab.components?.length || 0} components`));
    }
    box.append(head);

    if (prefab.missing?.length) {
      const missing = el("div", "missing-line");
      missing.append(badge("missing refs", "danger"));
      missing.append(el("code", "", prefab.missing.join(", ")));
      box.append(missing);
    }

    const components = prefab.components || [];
    if (!components.length) {
      box.append(el("div", "empty", "没有组件。"));
    }
    const grid = el("div", "component-grid");
    for (const component of components) {
      grid.append(renderComponentCard(component, modeContext, prefab));
    }
    box.append(grid);

    for (const projectile of prefab.projectiles || []) {
      renderPrefab(projectile, box, depth + 1, modeContext);
    }
    container.append(box);
  }

  function renderComponentCard(component, modeContext, prefab) {
    const card = el("article", "component-card component-card-compact");
    card.tabIndex = 0;
    card.setAttribute("role", "button");
    const open = () =>
      openComponentDetail(component, modeContext, { prefabId: prefab?.id });
    card.onclick = (event) => {
      if (event.target.closest(".component-open")) return;
      open();
    };
    card.onkeydown = (event) => {
      if (event.key !== "Enter" && event.key !== " ") return;
      event.preventDefault();
      open();
    };

    const top = el("div", "component-top");
    top.append(badge(component.kind || "Unknown", "kind"));
    if (component.is_ability) top.append(badge("Ability", "ability"));
    if (component.missing?.length) {
      top.append(badge(`missing ${component.missing.length}`, "danger"));
    }
    card.append(top);
    card.append(el("code", "path", component.path || component.id || "—"));

    const fieldNames = Object.keys(component.key_fields || {});
    if (fieldNames.length) {
      const list = el("div", "field-chips");
      for (const name of fieldNames.slice(0, 6)) {
        list.append(el("span", "field-chip", name));
      }
      card.append(list);
    }

    const graphs = component.buff_graphs?.length || 0;
    const inline = component.inline_graph?.details?.length || 0;
    const links =
      (component.links?.length || 0) + (component.ability_links?.length || 0);
    const foot = el("div", "component-card-foot");
    foot.append(
      el(
        "span",
        "muted",
        `Buff ${graphs} · 内联 ${inline ? 1 : 0} · 引用 ${links}`,
      ),
    );
    const button = el("button", "btn tiny component-open", "打开详情");
    button.type = "button";
    button.onclick = (event) => {
      event.stopPropagation();
      open();
    };
    foot.append(button);
    card.append(foot);
    return card;
  }

  function renderComponent(component, modeContext = null, options = {}) {
    const card = el("article", "component-card");
    const top = el("div", "component-top");
    top.append(badge(component.kind || "Unknown", "kind"));
    if (component.is_ability) top.append(badge("Ability", "ability"));
    if (component.missing?.length) top.append(badge(`missing ${component.missing.length}`, "danger"));
    card.append(top);

    const path = el("code", "path", component.path || component.id || "—");
    card.append(path);

    const keyFields = component.key_fields || {};
    const fieldNames = Object.keys(keyFields);
    if (fieldNames.length && !options.hideKeyFields) {
      const list = el("div", "field-chips");
      for (const name of fieldNames.slice(0, 8)) {
        list.append(el("span", "field-chip", name));
      }
      card.append(list);
    }

    for (const [index, graph] of (component.buff_graphs || []).entries()) {
      const graphBox = renderBuffGraph(graph, true, modeContext);
      graphBox.open = index === 0 && (component.buff_graphs || []).length <= 2;
      card.append(graphBox);
    }
    if (component.inline_graph?.details?.length) {
      card.append(renderInlineGraph(component.inline_graph, modeContext));
    }
    const links = [
      ...(component.links || []),
      ...(component.ability_links || []),
    ];
    if (links.length) {
      const box = el("div", "ref-list");
      box.append(el("span", "ref-title", "引用组件"));
      for (const link of links) {
        const entry = options.onLink
          ? state.componentIndex.get(link.id)
          : null;
        const item = el(entry ? "button" : "span", `ref-chip ${entry ? "clickable" : ""}`.trim());
        if (entry) {
          item.type = "button";
          item.onclick = () => options.onLink(link, entry);
        }
        item.append(badge(link.kind || "?", "kind"));
        item.append(el("code", "", link.go || link.path || link.id));
        box.append(item);
      }
      card.append(box);
    }
    for (const [label, info] of [
      ["Selector", component.selector],
      ["Target validator", component.target_validator],
    ]) {
      if (!info || info.kind === "default") continue;
      const box = el("div", "ref-list");
      box.append(el("span", "ref-title", label));
      box.append(badge(info.kind, info.kind === "resolved" ? "ok" : "danger"));
      if (info.target && typeof info.target === "object") {
        const targetId = info.target.id;
        const entry = options.onLink && targetId
          ? state.componentIndex.get(targetId)
          : null;
        const item = el(entry ? "button" : "span", `ref-chip ${entry ? "clickable" : ""}`.trim());
        if (entry) {
          item.type = "button";
          item.onclick = () => options.onLink(info.target, entry);
        }
        item.append(el("code", "", info.target.go || info.target.path || info.target.id));
        box.append(item);
      } else if (info.target) {
        box.append(el("code", "", String(info.target)));
      }
      card.append(box);
    }
    return card;
  }

  function openComponentDetail(component, modeContext = null, meta = null) {
    const entry = state.componentIndex.get(component.id) || {};
    state.componentDetail = {
      component,
      modeContext,
      meta: { ...entry, ...(meta || {}) },
    };
    renderComponentDetailPage();
    switchTab("component");
    window.scrollTo(0, 0);
  }

  function previewValue(value) {
    if (value === null || value === undefined) return "—";
    if (Array.isArray(value)) return `Array(${value.length})`;
    if (typeof value === "object") {
      if (value.$component) return `→ ${value.$component}`;
      const keys = Object.keys(value);
      if (!keys.length) return "{}";
      const shown = keys.slice(0, 6).join(", ");
      return `{ ${shown}${keys.length > 6 ? ", …" : ""} }`;
    }
    return String(value);
  }

  function renderComponentDetailPage() {
    const body = $("componentDetail");
    body.replaceChildren();
    const detail = state.componentDetail;
    if (!detail) {
      const empty = el("article", "panel empty-panel");
      empty.append(el("h3", "", "未选择组件"));
      body.append(empty);
      return;
    }

    const { component, modeContext, meta } = detail;
    const pathText = component.path || component.id || "—";
    const shortName =
      pathText.split("/").filter(Boolean).pop() || component.kind || "Component";
    $("componentTitle").textContent = `${component.kind || "Component"} · ${shortName}`;
    $("componentSubtitle").textContent = [
      state.operator?.name || state.operator?.id,
      meta.skillName,
      meta.prefabId,
    ]
      .filter(Boolean)
      .join(" · ");

    const crumb = $("componentBreadcrumb");
    crumb.replaceChildren();
    const addCrumb = (text) => {
      if (!text) return;
      crumb.append(el("code", "", String(text)));
    };
    addCrumb(state.operator?.id);
    addCrumb(meta.scope === "character" ? "Character prefab" : meta.skillName);
    addCrumb(meta.prefabId);
    addCrumb(pathText);

    const facts = el("div", "fact-grid");
    addFact(facts, "类型", component.kind || "—");
    addFact(facts, "Ability", component.is_ability ? "是" : "否");
    addFact(facts, "字段", String(Object.keys(component.fields || {}).length));
    addFact(facts, "Buff 图", String(component.buff_graphs?.length || 0));
    addFact(
      facts,
      "内联图",
      component.inline_graph?.details?.length ? "有" : "无",
    );
    addFact(
      facts,
      "引用",
      String(
        (component.links?.length || 0) + (component.ability_links?.length || 0),
      ),
    );
    body.append(facts);

    const keyFields = component.key_fields || {};
    const keyNames = Object.keys(keyFields);
    if (keyNames.length) {
      const panel = el("article", "panel component-key-panel");
      const head = el("div", "panel-head");
      head.append(el("h3", "", "关键字段"));
      head.append(badge(`${keyNames.length}`));
      panel.append(head);
      const table = el("div", "field-table");
      for (const name of keyNames) {
        const row = el("div", "field-row");
        row.append(el("code", "field-name", name));
        row.append(el("span", "field-value", previewValue(keyFields[name])));
        table.append(row);
      }
      panel.append(table);
      body.append(panel);
    }

    const card = renderComponent(component, modeContext, {
      onLink: (link, entry) => {
        if (entry?.component) {
          openComponentDetail(entry.component, modeContext, entry);
        }
      },
      hideKeyFields: true,
    });
    card.classList.add("component-card-detail");
    body.append(card);

    if (
      !keyNames.length
      && !component.buff_graphs?.length
      && !component.inline_graph?.details?.length
      && !Object.keys(component.fields || {}).length
    ) {
      body.insertBefore(
        el(
          "div",
          "empty-panel panel",
          "该组件没有字段或图；它主要保存引用目标。",
        ),
        card,
      );
    }

    const fieldNames = Object.keys(component.fields || {});
    if (fieldNames.length) {
      const panel = el("article", "panel component-fields-panel");
      const head = el("div", "panel-head");
      head.append(el("h3", "", "字段名"));
      head.append(badge(`${fieldNames.length}`));
      panel.append(head);
      const chips = el("div", "field-chips component-field-list");
      for (const name of fieldNames) {
        chips.append(el("span", "field-chip", name));
      }
      panel.append(chips);
      body.append(panel);
    }
  }

  function cleanRichText(text) {
    return String(text || "")
      .replace(/<[^>]+>/g, "")
      .replace(/\s+/g, " ")
      .trim();
  }

  function unlockLabel(condition) {
    const phase =
      {
        PHASE_0: "精英0",
        PHASE_1: "精英1",
        PHASE_2: "精英2",
      }[condition?.phase] || condition?.phase || "?";
    return `${phase} Lv${condition?.level ?? "?"}`;
  }

  function blackboardText(item) {
    const key = item?.key || "?";
    if (item?.valueStr) return `${key} = ${item.valueStr}`;
    const value = item?.value;
    if (typeof value !== "number") return `${key} = ${value ?? "—"}`;
    const raw = Number.isInteger(value) ? String(value) : String(value);
    const percent =
      value >= 0 && value <= 1
        ? ` (${Math.round(value * 1000) / 10}%)`
        : "";
    return `${key} = ${raw}${percent}`;
  }

  function renderTalentEntry(entry, modeContext) {
    const panel = el("article", "panel talent-card");
    const head = el("div", "panel-head");
    const title = el("div");
    const hasDescription = (entry.candidates || []).some((candidate) =>
      cleanRichText(candidate.description),
    );
    title.append(el("div", "eyebrow", `TALENT #${entry.index ?? "?"}`));
    title.append(
      el("h3", "", entry.name || (hasDescription ? "未命名天赋" : "内部天赋")),
    );
    head.append(title);
    head.append(badge(`${entry.candidates?.length || 0} 等级/潜能`));
    if (!hasDescription) head.append(badge("无描述", "warn"));
    panel.append(head);

    const candidates = el("div", "talent-candidates");
    for (const candidate of entry.candidates || []) {
      const row = el("div", "talent-candidate");
      const top = el("div", "talent-candidate-top");
      top.append(chip(unlockLabel(candidate.unlock_condition)));
      top.append(chip(`潜能${candidate.required_potential_rank ?? 0}`));
      if (candidate.prefab_key) {
        top.append(el("code", "", `prefab ${candidate.prefab_key}`));
      }
      row.append(top);

      const description = cleanRichText(candidate.description);
      if (description) row.append(el("p", "talent-desc", description));

      const blackboard = candidate.blackboard || [];
      if (blackboard.length) {
        const values = el("div", "talent-bb");
        for (const item of blackboard) {
          values.append(el("span", "talent-bb-item", blackboardText(item)));
        }
        row.append(values);
      }
      candidates.append(row);
    }
    panel.append(candidates);

    const components = new Map();
    for (const candidate of entry.candidates || []) {
      for (const item of candidate.components || []) {
        if (item?.component && item.id) components.set(item.id, item);
      }
    }
    if (components.size) {
      const box = el("div", "talent-components");
      const titleBox = el("div", "talent-components-head");
      titleBox.append(el("h4", "", "关联组件"));
      titleBox.append(badge(`${components.size}`));
      box.append(titleBox);
      const grid = el("div", "component-grid");
      for (const item of components.values()) {
        grid.append(
          renderComponentCard(item.component, modeContext, {
            id: item.parent_prefab || "",
          }),
        );
      }
      box.append(grid);
      panel.append(box);
    }
    return panel;
  }

  function renderTalentPage() {
    const body = $("talentDetail");
    body.replaceChildren();
    const operator = state.operator;
    if (!operator) {
      const empty = el("article", "panel empty-panel");
      empty.append(el("h3", "", "未选择干员"));
      body.append(empty);
      return;
    }
    const allEntries = operator.talents?.entries || [];
    const entries = allEntries.filter((entry) => !entry.hidden);
    const internalEntries = allEntries.filter((entry) => entry.hidden);
    const moduleCount = operator.modules?.items?.length || 0;
    const visibleComponentIds = new Set();
    for (const entry of entries) {
      for (const candidate of entry.candidates || []) {
        for (const item of candidate.components || []) {
          if (item.id) visibleComponentIds.add(item.id);
        }
      }
    }
    $("talentTitle").textContent = `天赋 · ${operator.name || operator.id}`;
    $("talentSubtitle").textContent =
      `${entries.length} 个天赋 · ${visibleComponentIds.size} 个组件`
      + (moduleCount ? ` · ${moduleCount} 个模组` : "");
    const modeContext = operatorModeContext(operator);

    if (!entries.length) {
      const empty = el("article", "panel empty-panel");
      empty.append(el("h3", "", "该干员没有天赋数据"));
      body.append(empty);
    } else {
      for (const entry of entries) {
        body.append(renderTalentEntry(entry, modeContext));
      }
    }

    const trait = operator.trait;
    const traitDescription = cleanRichText(trait?.description);
    if (traitDescription || trait?.items?.length) {
      const panel = el("article", "panel talent-card");
      const head = el("div", "panel-head");
      const title = el("div");
      title.append(el("div", "eyebrow", "TRAIT"));
      title.append(el("h3", "", "特性"));
      head.append(title);
      head.append(
        badge(
          trait.sub_profession_id
            ? `${trait.sub_profession_id} · ${trait.items?.length || 0} 组件`
            : `${trait.items?.length || 0} 组件`,
        ),
      );
      panel.append(head);
      if (traitDescription) {
        panel.append(el("p", "trait-description", traitDescription));
      }
      const grid = el("div", "component-grid");
      for (const item of trait.items || []) {
        if (!item.component) continue;
        grid.append(
          renderComponentCard(item.component, modeContext, {
            id: item.parent_prefab || "",
          }),
        );
      }
      if (grid.childElementCount) panel.append(grid);
      body.append(panel);
    }

    for (const module of operator.modules?.items || []) {
      const panel = el("article", "panel talent-card module-card");
      const head = el("div", "panel-head");
      const title = el("div");
      const typeLabel =
        { INITIAL: "基础模组", ADVANCED: "进阶模组" }[module.type]
        || module.type
        || "MODULE";
      title.append(el("div", "eyebrow", `MODULE · ${typeLabel}`));
      title.append(el("h3", "", module.name || module.uni_equip_id || "未命名模组"));
      head.append(title);
      if (module.uni_equip_id) {
        head.append(el("code", "", module.uni_equip_id));
      }
      panel.append(head);
      for (const item of module.prefabs || []) {
        const prefab = item.prefab || {};
        const section = el("div", "module-prefab");
        const bar = el("div", "module-prefab-head");
        bar.append(el("code", "", item.id || "—"));
        bar.append(
          badge(
            item.exists === false
              ? "missing"
              : `${prefab.components?.length || 0} components`,
            item.exists === false ? "danger" : "",
          ),
        );
        section.append(bar);
        const components = prefab.components || [];
        if (!components.length) {
          section.append(el("div", "empty", "没有组件。"));
        } else {
          const grid = el("div", "component-grid");
          for (const component of components) {
            grid.append(
              renderComponentCard(component, modeContext, { id: item.id }),
            );
          }
          section.append(grid);
        }
        panel.append(section);
      }
      body.append(panel);
    }

    if (internalEntries.length) {
      const box = el("details", "panel talent-internal");
      const summary = el("summary", "");
      summary.append(el("strong", "", "内部逻辑（不计入天赋）"));
      summary.append(badge(`${internalEntries.length}`));
      box.append(summary);
      const inner = el("div", "talent-internal-body");
      inner.append(
        el(
          "p",
          "talent-internal-note",
          "这些条目没有玩家可读的名称或描述（prefabKey = -1），通常是技能内部状态使用的组件，不计入天赋数量。",
        ),
      );
      for (const entry of internalEntries) {
        inner.append(renderTalentEntry(entry, modeContext));
      }
      box.append(inner);
      body.append(box);
    }
  }

  const NODE_LABELS = {
    CreateBuff: "创建 Buff",
    CreateBuffById: "创建 Buff",
    FinishBuffsById: "结束 Buff",
    FinishDerivedBuffById: "结束派生 Buff",
    CheckContainsBuff: "检查 Buff",
    CheckContainsDerviedBuff: "检查派生 Buff",
    CheckAbnormalFlag: "检查异常状态",
    IfElse: "条件分支",
    IfConditions: "组合条件",
    TriggerAbility: "触发能力",
    TriggerEnemySkill: "触发敌方技能",
    SwitchMode: "切换模式",
    CreateProjectile: "创建投射物",
    AdvancedApplyDamage: "造成伤害",
    FixedValueDamage: "固定伤害",
    DamageViaCurHpRatio: "按生命比例伤害",
    AssignValueToBB: "写入黑板",
    BlackboardAdd: "黑板加法",
    CalculateBlackboardValueViaParams: "计算黑板值",
    FilterByBlackboardValue: "按黑板过滤",
    FilterByBuffStackCount: "按层数过滤",
    CreateBuffUseAbilitySelector: "按选择器创建 Buff",
    TriggerBuffsByKeys: "按 Key 触发 Buff",
    ModifySp: "修改 SP",
    LogExtraBattleInfo: "调试日志",
    DoNothing: "无操作",
  };

  const CATEGORY_LABELS = {
    buff: "Buff",
    check: "判定",
    damage: "伤害",
    ability: "能力",
    value: "数值",
    cosmetic: "表现",
    other: "其它",
  };

  const EVENT_LABELS = {
    ON_BORN: "出生",
    ON_DEAD: "死亡",
    ON_ATTACK: "攻击时",
    ON_OUTPUT_DAMAGE: "造成伤害后",
    ON_BEING_ATTACKED: "受击时",
    ON_TAKE_DAMAGE: "受到伤害后",
    ON_TICK: "周期触发",
    ON_BUFF_ADDED: "获得 Buff",
    ON_BUFF_REMOVED: "移除 Buff",
    ON_BUFF_START: "Buff 开始",
    ON_BUFF_FINISH: "Buff 结束",
    ON_BUFF_ENABLE: "Buff 启用",
    ON_BUFF_LATE_ENABLE: "Buff 延迟启用",
    ON_BUFF_TRIGGER: "Buff 触发",
    ON_ABNORMAL_FLAG_DIRTY: "异常状态变化",
    ON_OWNER_LOCATE: "宿主定位",
    ON_OWNER_FINISH: "宿主结束",
    ON_TARGET_KILLED: "击杀目标",
    ON_SKILL_START: "技能开始",
    ON_SKILL_END: "技能结束",
    ON_KILL: "击杀",
  };

  function nodeLabel(node) {
    return NODE_LABELS[node?.name] || node?.name || node?.id || "?";
  }

  function cosmeticToggle() {
    const toolbar = el("div", "graph-toolbar");
    const toggle = el(
      "button",
      `btn tiny ${state.showCosmetic ? "is-active" : ""}`,
      state.showCosmetic ? "隐藏表现节点" : "显示表现节点",
    );
    toggle.onclick = () => {
      state.showCosmetic = !state.showCosmetic;
      document.dispatchEvent(new Event("ark-cosmetic-change"));
    };
    toolbar.append(toggle);
    if (!state.showCosmetic) {
      toolbar.append(el("span", "muted", "表现节点已隐藏"));
    }
    return toolbar;
  }

  function attachCosmeticSync(box, renderBody) {
    const onChange = () => {
      if (!box.isConnected) {
        document.removeEventListener("ark-cosmetic-change", onChange);
        return;
      }
      renderBody();
    };
    document.addEventListener("ark-cosmetic-change", onChange);
  }

  function renderBuffGraph(graph, open = true, modeContext = null) {
    const box = el("details", "graph-box");
    box.open = open;
    const summary = el("summary", "");
    summary.append(el("strong", "", "Buff graph"));
    summary.append(el("code", "", graph.template_key || "—"));
    summary.append(badge(`${graph.events || 0} events`));
    if (graph.missing?.length) summary.append(badge("missing", "danger"));
    for (const [category, count] of Object.entries(graph.action_counts || {})) {
      if (!count) continue;
      summary.append(
        badge(`${CATEGORY_LABELS[category] || category} ${count}`, category),
      );
    }
    box.append(summary);

    const events = graph.event_graphs || {};
    const names = Object.keys(events);
    if (!names.length) {
      box.append(el("div", "empty", "该 Buff 模板没有事件图。"));
      return box;
    }
    const labels = graph.event_labels || {};
    const explanations = graph.explanations || {};
    const body = el("div", "graph-body");
    box.append(body);

    const renderBody = () => {
      body.replaceChildren();
      body.append(cosmeticToggle());
      for (const eventName of names) {
        const eventBox = el("div", "event-box");
        const eventHead = el("div", "event-head");
        eventHead.append(
          el(
            "strong",
            "event-name",
            labels[eventName] || EVENT_LABELS[eventName] || eventName,
          ),
        );
        eventHead.append(el("code", "event-code", eventName));
        eventBox.append(eventHead);
        if (explanations[eventName]) {
          eventBox.append(el("p", "event-explain", explanations[eventName]));
        }
        eventBox.append(renderNodeList(events[eventName] || [], modeContext));
        body.append(eventBox);
      }
    };
    renderBody();
    attachCosmeticSync(box, renderBody);
    return box;
  }

  function renderInlineGraph(graph, modeContext = null) {
    const box = el("details", "graph-box");
    box.open = true;
    const summary = el("summary", "");
    summary.append(el("strong", "", "Inline ActionGraph"));
    summary.append(badge(`${graph.roots?.length || 0} roots`));
    if (graph.missing?.length) summary.append(badge("missing", "danger"));
    box.append(summary);

    const details = graph.details || [];
    if (!details.length) {
      box.append(el("div", "empty", "没有节点。"));
      return box;
    }
    const body = el("div", "graph-body");
    box.append(body);
    const renderBody = () => {
      body.replaceChildren();
      body.append(cosmeticToggle());
      body.append(renderNodeList(details, modeContext));
    };
    renderBody();
    attachCosmeticSync(box, renderBody);
    return box;
  }

  function renderNodeList(nodes, modeContext = null) {
    const list = el("div", "action-flow");
    const cards = [];
    for (const node of nodes) {
      const card = renderNode(node, 0, modeContext);
      if (card) cards.push(card);
    }
    if (!cards.length) {
      list.append(el("div", "empty", "没有可显示的节点（表现节点已隐藏）。"));
      return list;
    }
    for (const [index, card] of cards.entries()) {
      if (index) list.append(el("div", "flow-arrow", "↓"));
      list.append(card);
    }
    return list;
  }

  function renderNode(node, depth = 0, modeContext = null) {
    if (!node || node.missing) {
      return el("div", "node missing-node", `missing node ${node?.id || "—"}`);
    }
    if (node.category === "cosmetic" && !state.showCosmetic) return null;

    const card = el("div", `action-card cat-${node.category || "other"}`);
    const head = el("div", "action-head");
    head.append(el("span", "action-name", nodeLabel(node)));
    head.append(
      badge(
        CATEGORY_LABELS[node.category] || node.category || "其它",
        node.category || "other",
      ),
    );
    if (node.category === "cosmetic") head.append(badge("表现", "cosmetic"));
    if (node.cycle) head.append(badge("cycle", "warn"));
    head.append(el("code", "node-id", node.id));
    card.append(head);
    if (node.summary) card.append(el("div", "action-summary", node.summary));

    const buffJump = renderBuffJump(node);
    if (buffJump) card.append(buffJump);

    if (node.name === "SwitchMode") {
      const jump = renderModeJump(node, modeContext);
      if (jump) card.append(jump);
    }

    const groups = [];
    for (const child of node.children || []) {
      const role = child.role || "执行";
      let group = groups.find((item) => item.role === role);
      if (!group) {
        group = { role, nodes: [] };
        groups.push(group);
      }
      group.nodes.push(child.node);
    }
    if (node.name === "IfElse") {
      const order = { 条件: 0, 满足: 1, 否则: 2, 执行: 3 };
      groups.sort((a, b) => (order[a.role] ?? 9) - (order[b.role] ?? 9));
    }
    for (const group of groups) {
      const branch = el("div", "action-branch");
      if (group.role !== "执行") {
        branch.append(el("span", `branch-label role-${group.role}`, group.role));
      }
      let rendered = 0;
      for (const childNode of group.nodes) {
        const childCard = renderNode(childNode, depth + 1, modeContext);
        if (!childCard) continue;
        branch.append(childCard);
        rendered += 1;
      }
      if (rendered) card.append(branch);
    }
    return card;
  }

  function buffDescriptors(node) {
    const fields = node.fields || {};
    const descriptors = [];
    if (fields._buff && typeof fields._buff === "object" && !Array.isArray(fields._buff)) {
      descriptors.push(fields._buff);
    }
    if (Array.isArray(fields._buffs)) {
      descriptors.push(...fields._buffs);
    }
    return descriptors.filter(
      (descriptor) => descriptor && typeof descriptor === "object",
    );
  }

  function renderBuffJump(node) {
    const descriptors = buffDescriptors(node);
    if (!descriptors.length) return null;
    const box = el("div", "mode-jump");
    box.append(el("span", "mode-jump-info", "Buff 模板"));
    const seen = new Set();
    for (const descriptor of descriptors) {
      const templateKey = String(descriptor.templateKey || "").trim();
      const buffKey = String(descriptor.buffKey || "").trim();
      if (!templateKey || seen.has(templateKey)) continue;
      seen.add(templateKey);
      if (templateKey === "empty") {
        box.append(
          el(
            "span",
            "mode-jump-note",
            `empty 模板${descriptor.overrideEffectKey ? ` · ${descriptor.overrideEffectKey}` : ""}`,
          ),
        );
      } else {
        const button = el("button", "mode-jump-btn", `查看 ${templateKey}`);
        button.type = "button";
        button.onclick = () => openBuffGraph(templateKey);
        box.append(button);
      }
      if (buffKey && buffKey !== templateKey) {
        box.append(el("code", "", `实例 ${buffKey}`));
      }
    }
    return box;
  }

  function openBuffGraph(key) {
    state.buffKey = key;
    $("buffInput").value = key;
    switchTab("buff");
    loadBuff(key);
  }

  function renderModeJump(node, modeContext) {
    if (!modeContext?.modes?.length) return null;
    const fields = node.fields || {};
    const box = el("div", "mode-jump");
    box.append(el("span", "mode-jump-info", "目标模式"));
    const open = (mode) => openModeDialog(mode, modeContext);

    if (fields._loadModeFromBlackboard) {
      box.append(el("span", "mode-jump-note", "由运行时黑板决定"));
      for (const mode of modeContext.modes) {
        const button = el(
          "button",
          "mode-jump-btn",
          `Modes[${mode.index}] ${mode.label}`,
        );
        button.type = "button";
        button.onclick = () => open(mode);
        box.append(button);
      }
    } else {
      let index = Number(fields._modeIndex);
      if (fields._restoreDefault) index = 0;
      if (fields._restoreDefault) {
        box.append(el("span", "mode-jump-note", "restoreDefault"));
      }
      const target = Number.isInteger(index) ? modeContext.modes[index] : null;
      if (target) {
        const button = el(
          "button",
          "mode-jump-btn",
          `跳到 Modes[${target.index}] · ${target.label}`,
        );
        button.type = "button";
        button.onclick = () => open(target);
        box.append(button);
      } else {
        box.append(
          el("span", "mode-jump-note", `Modes[${fields._modeIndex}] 不存在`),
        );
      }
    }
    if (fields._restartFSM) box.append(badge("restartFSM", "warn"));
    if (fields._targetType) {
      box.append(el("code", "", `target=${fields._targetType}`));
    }
    return box;
  }

  function openModeDialog(mode, context) {
    const dialog = $("modeDialog");
    if (!dialog) return;
    $("modeTitle").textContent = `Modes[${mode.index}] · ${mode.label}`;
    $("modeSubtitle").textContent = [context.characterName, mode.path]
      .filter(Boolean)
      .join(" · ");
    const body = $("modeBody");
    body.replaceChildren();
    body.append(
      el(
        "p",
        "mode-dialog-note",
        "SwitchMode 只负责切到这个模式子树；攻击、范围、Buff 都在下面的组件里。",
      ),
    );
    if (!mode.component) {
      body.append(el("div", "empty", "没有解析到该模式组件。"));
      if (!dialog.open) dialog.showModal();
      return;
    }

    const seen = new Set();
    const walk = (component, depth) => {
      if (!component || seen.has(component.id) || depth > 2) return;
      seen.add(component.id);
      body.append(renderComponent(component, context));
      if (depth === 2) return;
      for (const link of component.links || []) {
        const child = context.componentsById?.get(link.id);
        if (!child || seen.has(child.id)) continue;
        const section = el("section", "mode-linked");
        section.append(
          el("h4", "", `引用组件 · ${link.go || link.path || link.id}`),
        );
        body.append(section);
        walk(child, depth + 1);
      }
    };
    walk(mode.component, 0);
    if (!dialog.open) dialog.showModal();
  }

  function renderCoverage() {
    const coverage = state.coverage;
    const c = coverage.coverage;
    $("coverageStamp").textContent = coverage.generatedAt || "";
    const metrics = $("coverageMetrics");
    metrics.replaceChildren();
    addMetric(metrics, "角色 prefab", `${c.characterPrefabs.resolved}/${c.characterPrefabs.total}`, pct(c.characterPrefabs.ratio));
    addMetric(metrics, "技能 prefab 出现", `${c.skillPrefabs.resolvedOccurrences}/${c.skillPrefabs.occurrences}`, pct(c.skillPrefabs.ratio));
    const unresolvedSkillPrefabs = new Set(
      (coverage.unresolvedItems || [])
        .filter((item) => String(item.ref || "").startsWith("skill:"))
        .map((item) => item.ref),
    ).size;
    addMetric(
      metrics,
      "唯一技能 prefab",
      `${c.skillPrefabs.unique}`,
      `${c.skillPrefabs.unique - unresolvedSkillPrefabs} 已解析`,
    );
    addMetric(metrics, "天赋组件", `${c.talents.resolved}/${c.talents.components}`, pct(c.talents.ratio));
    addMetric(metrics, "特性组件", `${c.traits.resolved}/${c.traits.components}`, pct(c.traits.ratio));
    addMetric(metrics, "模组 prefab", `${c.modules.resolved}/${c.modules.prefabs}`, pct(c.modules.ratio));
    addMetric(
      metrics,
      "核心覆盖",
      `${c.operators.coreFullyCovered}/${coverage.counts.operatorEntries}`,
      pct(c.operators.coreRatio),
    );
    addMetric(metrics, "Buff 图", `${c.graphs.buffTemplatesResolved}/${c.graphs.buffTemplatesWithEvents}`, "有事件模板");
    addMetric(metrics, "内联图", `${c.graphs.inlineGraphsResolved}/${c.graphs.inlineGraphs}`, "ActionAbility/projectile");
    addMetric(metrics, "节点", `${c.graphs.nodesReached}`, `${c.graphs.nodesMissing} missing`);

    const unresolved = coverage.unresolvedItems || [];
    $("unresolvedCount").textContent = `${unresolved.length}`;
    const unresolvedList = $("unresolvedList");
    unresolvedList.replaceChildren();
    if (!unresolved.length) {
      unresolvedList.append(el("div", "empty", "没有静态缺口。"));
    } else {
      for (const item of unresolved) {
        const row = el("div", "list-row");
        row.append(badge(item.kind || "ref", "danger"));
        const text = el("div", "");
        text.append(el("code", "", item.ref));
        text.append(el("span", "muted", ` · ${item.owner || "—"}`));
        row.append(text);
        unresolvedList.append(row);
      }
    }

    const failed = (coverage.operators || []).filter(
      (operator) => !operator.coverage?.core_fully_covered,
    );
    const failedList = $("failedOperators");
    failedList.replaceChildren();
    if (!failed.length) {
      failedList.append(el("div", "empty", "全部核心 prefab 已覆盖。"));
    } else {
      for (const operator of failed) {
        const row = el("button", "list-row clickable");
        row.append(el("strong", "", operator.name || operator.id));
        row.append(el("code", "", operator.id));
        row.append(
          badge(
            `${operator.coverage?.missing_skill_refs || 0} skill refs`,
            "warn",
          ),
        );
        row.onclick = () => {
          switchTab("browse");
          selectOperator(operator.id);
        };
        failedList.append(row);
      }
    }

    const kinds = c.componentKinds || {};
    const max = Math.max(1, ...Object.values(kinds));
    const kindBox = $("componentKinds");
    kindBox.replaceChildren();
    for (const [name, count] of Object.entries(kinds).sort((a, b) => b[1] - a[1])) {
      const row = el("div", "bar-row");
      row.append(el("code", "bar-label", name));
      const track = el("div", "bar-track");
      const fill = el("i", "");
      fill.style.width = `${(count / max) * 100}%`;
      track.append(fill);
      row.append(track);
      row.append(el("strong", "bar-value", String(count)));
      kindBox.append(row);
    }
  }

  function addMetric(container, label, value, sub) {
    const card = el("div", "metric");
    card.append(el("span", "metric-label", label));
    card.append(el("strong", "", value));
    card.append(el("span", "metric-sub", sub));
    container.append(card);
  }

  function renderRuntime() {
    const runtime = state.coverage.needsRuntimeVerification || {};
    const cards = $("runtimeCards");
    cards.replaceChildren();
    const gaps = runtime.staticExtractionGaps || {};
    const stateData = runtime.runtimeState || {};
    const modules = stateData.modules || {};
    const buffs = stateData.buffs || {};
    const dynamicNodes = stateData.dynamicNodes || {};
    const graph = runtime.staticGraph || {};
    const refs = runtime.staticRefs || {};

    addRuntimeCard(cards, "静态展开", "prefab 内部引用", [
      ["未解析 prefab", gaps.unresolvedPrefabs],
      ["Selector 已解析", refs.selectors?.resolved],
      ["Selector 默认", refs.selectors?.default],
      ["Selector 未解析", refs.selectors?.unresolved],
      ["Validator 已解析", refs.targetValidators?.resolved],
      ["Validator 未解析", refs.targetValidators?.unresolved],
      ["组件引用已解析", refs.components?.resolved],
      ["组件引用未解析", refs.components?.unresolved],
      ["能力引用已解析", refs.abilities?.resolved],
      ["能力引用未解析", refs.abilities?.unresolved],
      ["无字段辅助组件", refs.opaqueAuxComponents],
    ]);
    addRuntimeCard(cards, "运行时状态", "结构已知，实机确认", [
      ["mode 组件", modules.modeComponents],
      ["mode 条目", modules.modeEntries],
      ["额外 Ability 组件", modules.extraAbilityComponents],
      ["额外 Ability 条目", modules.extraAbilityEntries],
      ["原生 Ability 字段出现", modules.nativeAbilityFields],
      ["loadFromDB Buff", buffs.loadFromDb],
      ["动态节点类型", dynamicNodes.nodeTypes],
    ]);
    addRuntimeCard(cards, "已解析静态图", "不需要 runtime 才能读结构", [
      ["已解析 Buff 模板", graph.resolvedBuffTemplates],
      ["缺失 Buff 模板", graph.missingBuffTemplates],
      ["有事件 Buff 图", graph.buffGraphsWithEvents],
      ["内联图", graph.inlineGraphs],
      ["到达节点", graph.nodesReached],
    ]);

    const dynamicBox = el("article", "panel runtime-note");
    dynamicBox.append(
      el("h3", "", "动态节点样本"),
      el(
        "p",
        "muted",
        "这些节点的图是静态的，但创建出来的实体、Buff、黑板和目标需要实机确认。",
      ),
    );
    const chips = el("div", "field-chips");
    for (const name of dynamicNodes.nodeTypesSample || []) {
      chips.append(chip(name));
    }
    dynamicBox.append(chips);
    cards.append(dynamicBox);
  }

  async function loadBuff(key) {
    const value = String(key || "").trim();
    if (!value) return;
    state.buffKey = value;
    const result = $("buffResult");
    setEmpty(result, "加载中…");
    try {
      const graph = await fetchJSON(`/api/buff?key=${encodeURIComponent(value)}`);
      result.replaceChildren();
      if (!graph.exists) {
        const missing = el("article", "panel empty-panel");
        missing.append(el("h3", "", "没有这个 Buff 模板"));
        missing.append(el("code", "", value));
        result.append(missing);
        return;
      }
      const panel = el("article", "panel");
      const head = el("div", "panel-head");
      const title = el("div");
      title.append(el("div", "eyebrow", "BUFF TEMPLATE"));
      title.append(el("h3", "", graph.template_key || value));
      if (graph.alias_of) {
        title.append(el("code", "", `实例 ${graph.requested_key}`));
      }
      head.append(title);
      head.append(badge(`${graph.events || 0} events`));
      panel.append(head);
      if (graph.alias_of) {
        const note = el("div", "buff-alias-note");
        note.append(badge("实例 key", "warn"));
        note.append(el("code", "", graph.requested_key));
        note.append(el("span", "", "是运行时的 Buff 实例名；它的模板是"));
        const templateButton = el(
          "button",
          "mode-jump-btn",
          graph.alias_of,
        );
        templateButton.type = "button";
        templateButton.onclick = () => loadBuff(graph.alias_of);
        note.append(templateButton);
        note.append(el("span", "", "，下面是模板的 ActionNode 图。"));
        panel.append(note);
      }
      panel.append(renderBuffGraph(graph));
      result.append(panel);
    } catch (error) {
      setEmpty(result, `加载失败：${error.message}`);
    }
  }

  function addRuntimeCard(container, title, subtitle, rows) {
    const card = el("article", "panel runtime-card");
    const head = el("div", "panel-head");
    const titleBox = el("div");
    titleBox.append(el("h3", "", title));
    titleBox.append(el("span", "muted", subtitle));
    head.append(titleBox);
    card.append(head);
    for (const [label, value] of rows) {
      const row = el("div", "runtime-row");
      row.append(el("span", "", label));
      row.append(el("strong", "", String(value ?? 0)));
      card.append(row);
    }
    container.append(card);
  }

  function switchTab(tab, updateHash = true) {
    for (const button of document.querySelectorAll(".tab")) {
      button.classList.toggle("is-active", button.dataset.tab === tab);
    }
    for (const page of document.querySelectorAll(".page")) {
      page.classList.toggle("is-active", page.id === `page-${tab}`);
    }
    if (updateHash) {
      history.replaceState(null, "", `#${tab}`);
    }
    if (tab === "talent") renderTalentPage();
  }

  async function init() {
    try {
      const [coverage, operators] = await Promise.all([
        fetchJSON("/api/coverage"),
        fetchJSON("/api/operators"),
      ]);
      state.coverage = coverage;
      state.operators = operators.operators || coverage.operators || [];
      renderTopStats();
      renderCoverage();
      renderRuntime();
      renderOperatorList();
      const initialTab = window.location.hash.replace("#", "");
      if (
        initialTab === "coverage"
        || initialTab === "runtime"
        || initialTab === "buff"
        || initialTab === "component"
        || initialTab === "talent"
      ) {
        switchTab(initialTab, false);
      }
      const params = new URLSearchParams(window.location.search);
      const requestedBuff = params.get("buff");
      if (requestedBuff) {
        switchTab("buff", false);
        $("buffInput").value = requestedBuff;
        loadBuff(requestedBuff);
      } else if (initialTab === "buff") {
        loadBuff("c2e_cold");
      }
      if (state.operators.length) {
        const requestedOperator = params.get("operator") || params.get("op");
        state.pendingSkill = params.get("skill");
        const requested = requestedOperator
          ? state.operators.find((operator) => operator.id === requestedOperator)
          : null;
        const preferred =
          requested ||
          [...state.operators].sort(compareOperators).find(
            (operator) => operator.kind === "operator" && operator.skills?.length,
          ) || state.operators[0];
        selectOperator(preferred.id);
      }
    } catch (error) {
      setEmpty($("operatorList"), `加载失败：${error.message}`);
      setEmpty($("center"), `加载失败：${error.message}`);
    }
  }

  $("tabs").addEventListener("click", (event) => {
    const button = event.target.closest("[data-tab]");
    if (button) {
      switchTab(button.dataset.tab);
      if (button.dataset.tab === "buff" && !state.buffKey) {
        loadBuff($("buffInput").value || "c2e_cold");
      }
    }
  });
  $("searchInput").addEventListener("input", (event) => {
    state.search = event.target.value;
    renderOperatorList();
  });
  $("kindFilter").addEventListener("click", (event) => {
    const button = event.target.closest("[data-kind]");
    if (!button) return;
    state.kind = button.dataset.kind;
    for (const item of $("kindFilter").querySelectorAll("button")) {
      item.classList.toggle("is-active", item === button);
    }
    renderOperatorList();
  });
  $("buffLoad").addEventListener("click", () => loadBuff($("buffInput").value));
  $("buffInput").addEventListener("keydown", (event) => {
    if (event.key === "Enter") loadBuff(event.target.value);
  });
  $("buffExamples").addEventListener("click", (event) => {
    const button = event.target.closest("[data-buff]");
    if (!button) return;
    $("buffInput").value = button.dataset.buff;
    loadBuff(button.dataset.buff);
  });
  $("componentBack").addEventListener("click", () => switchTab("browse"));
  $("talentBack").addEventListener("click", () => switchTab("browse"));

  const modeDialog = $("modeDialog");
  $("modeClose").addEventListener("click", () => modeDialog.close());
  modeDialog.addEventListener("click", (event) => {
    if (event.target === modeDialog) modeDialog.close();
  });

  init();
})();
