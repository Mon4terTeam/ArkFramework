"use strict";

(() => {
  const pages = window.BetterBattlePages || (window.BetterBattlePages = {});
  const { operatorName } = pages.squad || {};

  function mergeDamagePayload(next) {
    if (!next || typeof next !== "object") return null;
    const previous = state.damage;
    const sameBattle = previous &&
      Number(previous.battle) === Number(next.battle) &&
      (Number(next.battle) !== 0 || !next.started);
    const previousById = new Map(
      (sameBattle ? previous.operators || [] : []).map((actor) => [String(actor.id), actor]),
    );
    const operators = (Array.isArray(next.operators) ? next.operators : []).map((actor) => {
      const prior = previousById.get(String(actor.id));
      const curve = Array.isArray(actor.curve) && actor.curve.length
        ? actor.curve
        : (prior?.curve || []);
      const healingCurve = Array.isArray(actor.healing_curve) && actor.healing_curve.length
        ? actor.healing_curve
        : (prior?.healing_curve || []);
      const takenCurve = Array.isArray(actor.taken_curve) && actor.taken_curve.length
        ? actor.taken_curve
        : (prior?.taken_curve || []);
      return {
        ...actor,
        curve,
        healing_curve: healingCurve,
        taken_curve: takenCurve,
      };
    });
    const series = next.series && Array.isArray(next.series.total)
      ? next.series
      : (sameBattle ? previous?.series || null : null);
    return { ...next, operators, series };
  }

  function damageOperators() {
    return Array.isArray(state.damage?.operators) ? state.damage.operators : [];
  }

  function damageOperatorFor(id) {
    if (id === null || id === undefined) return null;
    return damageOperators().find((actor) => String(actor.id) === String(id)) || null;
  }

  function damageTypeValue(actor, key) {
    return Number(actor?.types?.[key]?.actual) || 0;
  }

  function statsModeFor(scope) {
    const key = scope === "battle" ? state.battleStatsMode : state.pageStatsMode;
    return STATS_MODES[key] || STATS_MODES.damage;
  }

  function syncStatsTabs() {
    dom.damageStatsTabs?.querySelectorAll("button[data-mode]").forEach((button) => {
      const active = button.dataset.mode === state.pageStatsMode;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-selected", active ? "true" : "false");
    });
  }

  function statsOperators(mode) {
    return damageOperators()
      .filter((actor) => mode.actor(actor) > 0.009)
      .sort((left, right) => mode.actor(right) - mode.actor(left));
  }

  function statsCurve(actor, mode) {
    return Array.isArray(mode.curve(actor)) ? mode.curve(actor) : [];
  }

  function damageActorColor(id) {
    let hash = 0;
    for (const ch of String(id || "")) hash = (hash * 31 + ch.charCodeAt(0)) | 0;
    return DAMAGE_ACTOR_COLORS[Math.abs(hash) % DAMAGE_ACTOR_COLORS.length];
  }

  function damageAvatar(id) {
    const portraitId = window.BetterBattleAssets?.portraitIdFor(id) || "";
    return portraitId
      ? { kind: "portrait", id: portraitId }
      : { kind: "avatar", id: String(id || "") };
  }

  function damageShare(value, total) {
    const amount = Number(value) || 0;
    const overall = Number(total) || 0;
    return overall > 0 ? amount / overall * 100 : 0;
  }

  function renderDamageLegend(container, series, selectedId = null) {
    const items = series.map((entry) => {
      const item = document.createElement("span");
      item.className = "damage-legend-item";
      item.style.setProperty("--series-color", entry.color);
      item.classList.toggle("is-selected", entry.id === selectedId);
      const swatch = document.createElement("i");
      swatch.className = "damage-legend-swatch";
      const label = document.createElement("span");
      label.textContent = entry.label;
      const value = document.createElement("b");
      value.className = "damage-legend-value";
      value.textContent = fmtCompact(entry.total);
      item.append(swatch, label, value);
      return item;
    });
    container.replaceChildren(...items);
  }

  function damageSvgNode(tag, attributes = {}) {
    const node = document.createElementNS("http://www.w3.org/2000/svg", tag);
    for (const [key, value] of Object.entries(attributes)) {
      node.setAttribute(key, String(value));
    }
    return node;
  }

  function damageFrameTrack() {
    const series = state.damage?.series;
    const frames = Array.isArray(series?.frames)
      ? series.frames.map((frame) => Number(frame)).filter(Number.isFinite)
      : [];
    const lastFrame = frames.length ? frames.at(-1) : 0;
    const endFrame = Math.max(Number(state.damage?.end_frame) || 0, lastFrame);
    return {
      frames,
      endFrame,
      renderKey: Math.floor(endFrame / 8),
    };
  }

  function damageLinePath(frames, values, xScale, yScale, maxPoints, endFrame) {
    const count = Math.min(frames.length, values.length);
    let path = "";
    if (!count) return path;
    const stride = Math.max(1, Math.ceil(count / Math.max(2, maxPoints)));
    let lastIndex = -1;
    for (let index = 0; index < count; index += stride) {
      const x = xScale(Number(frames[index]) || 0);
      const y = yScale(Number(values[index]) || 0);
      path += `${lastIndex >= 0 ? " L" : "M"}${x.toFixed(2)},${y.toFixed(2)}`;
      lastIndex = index;
    }
    if (lastIndex !== count - 1) {
      const x = xScale(Number(frames[count - 1]) || 0);
      const y = yScale(Number(values[count - 1]) || 0);
      path += ` L${x.toFixed(2)},${y.toFixed(2)}`;
    }
    const lastTrackFrame = Number(frames[count - 1]) || 0;
    if (Number(endFrame) > lastTrackFrame) {
      const x = xScale(Number(endFrame));
      const y = yScale(Number(values[count - 1]) || 0);
      path += ` L${x.toFixed(2)},${y.toFixed(2)}`;
    }
    return path;
  }

  function nearestFrameIndex(frames, frame) {
    let low = 0;
    let high = frames.length - 1;
    while (low < high) {
      const middle = Math.floor((low + high) / 2);
      if (frames[middle] < frame) {
        low = middle + 1;
      } else {
        high = middle;
      }
    }
    if (low > 0 && Math.abs(frames[low - 1] - frame) <= Math.abs(frames[low] - frame)) {
      return low - 1;
    }
    return low;
  }

  function buildDamageChart(series, {
    frames = [],
    endFrame = 0,
    selectedId = null,
    compact = false,
    narrow = false,
    unitLabel = "累计 HP",
  } = {}) {
    if (!series.length) {
      const empty = document.createElement("div");
      empty.className = "empty-small";
      empty.textContent = "等待战斗伤害数据";
      return empty;
    }
    const width = narrow ? 460 : 920;
    const height = narrow ? 260 : compact ? 250 : 340;
    const margin = narrow
      ? { left: 42, right: 42, top: 20, bottom: 30 }
      : { left: 62, right: 22, top: 26, bottom: 38 };
    const plotWidth = width - margin.left - margin.right;
    const plotHeight = height - margin.top - margin.bottom;
    const plotBottom = height - margin.bottom;
    const trackFrames = frames.map((frame) => Number(frame)).filter(Number.isFinite);
    const minFrame = trackFrames.length ? trackFrames[0] : 0;
    const lastFrame = trackFrames.length ? trackFrames.at(-1) : minFrame;
    const maxFrame = Math.max(minFrame + 1, Number(endFrame) || 0, lastFrame);
    const frameSpan = maxFrame - minFrame;
    let maxY = 1;
    for (const entry of series) {
      for (const value of entry.values) {
        maxY = Math.max(maxY, Number(value) || 0);
      }
    }
    const fallbackLength =
      Math.max(1, ...series.map((entry) => entry.values.length));
    const xScale = (frame) =>
      margin.left + (frame - minFrame) / frameSpan * plotWidth;
    const yScale = (value) => margin.top + (1 - value / maxY) * plotHeight;
    const svg = damageSvgNode("svg", {
      viewBox: `0 0 ${width} ${height}`,
      class: "damage-chart-svg",
      role: "img",
      "aria-label": "伤害累计曲线",
    });

    for (let index = 0; index <= 4; index += 1) {
      const y = margin.top + plotHeight * index / 4;
      svg.append(damageSvgNode("line", {
        x1: margin.left, y1: y, x2: width - margin.right, y2: y,
        class: "damage-chart-grid",
      }));
      const label = damageSvgNode("text", {
        x: margin.left - 8, y: y + 4, class: "damage-chart-axis",
        "text-anchor": "end",
      });
      label.textContent = fmtCompact(maxY * (1 - index / 4));
      svg.append(label);
    }
    for (let index = 0; index <= 5; index += 1) {
      const x = margin.left + plotWidth * index / 5;
      svg.append(damageSvgNode("line", {
        x1: x, y1: margin.top, x2: x, y2: plotBottom, class: "damage-chart-grid",
      }));
      const label = damageSvgNode("text", {
        x, y: plotBottom + 17, class: "damage-chart-axis", "text-anchor": "middle",
      });
      const frame = Math.round(minFrame + frameSpan * index / 5);
      label.textContent = formatChartFrame(frame);
      svg.append(label);
    }

    const ordered = [...series].sort((left, right) =>
      Number(left.id === selectedId) - Number(right.id === selectedId));
    for (const entry of ordered) {
      const path = damageSvgNode("path", {
        d: damageLinePath(
          trackFrames, entry.values, xScale, yScale, plotWidth, maxFrame),
        stroke: entry.color,
        class: `damage-series-line${entry.id === selectedId ? " is-selected" : ""}`,
      });
      const title = damageSvgNode("title");
      title.textContent = `${entry.label} · ${fmtCompact(entry.total)}`;
      path.append(title);
      svg.append(path);
    }

    const hoverLine = damageSvgNode("line", {
      x1: margin.left, y1: margin.top, x2: margin.left, y2: plotBottom,
      class: "damage-chart-hover", visibility: "hidden",
    });
    const hoverDots = damageSvgNode("g", { visibility: "hidden" });
    svg.append(hoverLine, hoverDots);
    const overlay = damageSvgNode("rect", {
      x: margin.left, y: margin.top, width: plotWidth, height: plotHeight,
      class: "damage-chart-hit",
    });
    svg.append(overlay);
    svg.append(Object.assign(damageSvgNode("text", {
      x: margin.left, y: 15, class: "damage-chart-axis-unit",
    }), { textContent: unitLabel }));

    const wrapper = document.createElement("div");
    wrapper.className = "damage-chart-wrap";
    const footer = document.createElement("div");
    footer.className = "damage-chart-footer";
    const frameUnit = document.createElement("span");
    frameUnit.textContent = state.timeUnit === "frame"
      ? "逻辑帧 · 30 FPS"
      : "秒 · 30 FPS 逻辑帧";
    footer.append(frameUnit);
    const tooltip = document.createElement("div");
    tooltip.className = "damage-tooltip";
    tooltip.hidden = true;

    overlay.addEventListener("pointermove", (event) => {
      const rect = svg.getBoundingClientRect();
      if (!rect.width) return;
      const chartX = (event.clientX - rect.left) / rect.width * width;
      const ratio = Math.max(0, Math.min(1, (chartX - margin.left) / plotWidth));
      const frame = Math.round(minFrame + ratio * frameSpan);
      const index = trackFrames.length
        ? nearestFrameIndex(trackFrames, frame)
        : Math.min(fallbackLength - 1,
            Math.round(ratio * Math.max(0, fallbackLength - 1)));
      const hoverFrame = trackFrames.length
        ? Number(trackFrames[index]) || 0
        : Math.round(minFrame + ratio * frameSpan);
      const x = xScale(hoverFrame);
      hoverLine.setAttribute("x1", String(x));
      hoverLine.setAttribute("x2", String(x));
      hoverLine.setAttribute("visibility", "visible");
      hoverDots.replaceChildren();
      for (const entry of series) {
        const value = Number(entry.values[index]) || 0;
        if (value <= 0) continue;
        const dot = damageSvgNode("circle", {
          cx: x, cy: yScale(value), r: 3.2, fill: entry.color,
        });
        hoverDots.append(dot);
      }
      hoverDots.setAttribute("visibility", "visible");
      tooltip.replaceChildren();
      const timeLabel = document.createElement("strong");
      timeLabel.textContent = formatChartTime(hoverFrame);
      tooltip.append(timeLabel);
      const rows = [...series]
        .map((entry) => ({ entry, value: Number(entry.values[index]) || 0 }))
        .filter(({ value }) => value > 0)
        .sort((left, right) => right.value - left.value);
      for (const { entry, value } of rows) {
        const row = document.createElement("div");
        row.className = "damage-tooltip-row";
        const label = document.createElement("span");
        label.textContent = entry.label;
        const amount = document.createElement("b");
        amount.textContent = fmtCompact(value);
        row.append(label, amount);
        tooltip.append(row);
      }
      tooltip.hidden = false;
      tooltip.style.left = `${Math.min(82, Math.max(2, x / width * 100))}%`;
      tooltip.style.top = "10px";
    });
    overlay.addEventListener("pointerleave", () => {
      hoverLine.setAttribute("visibility", "hidden");
      hoverDots.setAttribute("visibility", "hidden");
      tooltip.hidden = true;
    });

    wrapper.append(svg, tooltip, footer);
    return wrapper;
  }

  function damageActorStatus() {
    const damage = state.damage;
    if (!damage?.available) return "服务不可用";
    if (damage.overrun) return "事件溢出";
    if (damage.ended) return "已结束";
    if (damage.started) return damage.partial ? "采集中（中途加入）" : "采集中";
    return "等待战斗";
  }

  const BATTLE_STAT_MODES = ["damage", "healing", "taken"];
  let battleStatNodes = null;

  function ensureBattleStatBlocks() {
    if (battleStatNodes) return battleStatNodes;
    if (!dom.battleStatCharts) return null;
    battleStatNodes = {};
    dom.battleStatCharts.replaceChildren();
    for (const key of BATTLE_STAT_MODES) {
      const mode = STATS_MODES[key];
      const block = document.createElement("section");
      block.className = "battle-stat-block";
      block.dataset.mode = key;
      const head = document.createElement("div");
      head.className = "battle-stat-head";
      const title = document.createElement("div");
      const heading = document.createElement("h3");
      heading.textContent = mode.label;
      const scope = document.createElement("span");
      title.append(heading, scope);
      const legend = document.createElement("div");
      legend.className = "damage-legend";
      head.append(title, legend);
      const meta = document.createElement("div");
      meta.className = "damage-chart-meta";
      const total = document.createElement("span");
      total.className = "badge";
      const rate = document.createElement("span");
      rate.className = "badge";
      const status = document.createElement("span");
      status.className = "damage-subtle";
      meta.append(total, rate, status);
      const chart = document.createElement("div");
      chart.className = "damage-chart damage-chart-compact battle-damage-chart";
      block.append(head, meta, chart);
      dom.battleStatCharts.append(block);
      battleStatNodes[key] = { block, scope, legend, total, rate, status, chart };
    }
    window.BetterBattleProtocol?.post({
      type: window.BetterBattleProtocol?.Message?.Log,
      message: `战斗统计面板：${BATTLE_STAT_MODES.length} 个纵向图表`,
    });
    return battleStatNodes;
  }

  function renderBattleDamagePanel(operatorId = "") {
    const damage = state.damage;
    const actor = damageOperatorFor(operatorId);
    const actorId = String(actor?.id || operatorId || "");
    const name = actor?.name || (operatorId ? operatorName(operatorId) : "") ||
      "未选择干员";
    const duration = Number(damage?.duration) || 0;
    const track = damageFrameTrack();
    const nodes = ensureBattleStatBlocks();
    if (!nodes) return;
    for (const key of BATTLE_STAT_MODES) {
      const mode = STATS_MODES[key];
      const node = nodes[key];
      const total = mode.actor(actor);
      node.scope.textContent = operatorId ? `当前选中：${name}` : "当前选中干员";
      node.total.textContent = `${mode.totalLabel} ${fmtCompact(total)}`;
      node.rate.textContent =
        `${mode.rateLabel} ${fmtCompact(duration > 0 ? total / duration : 0)}`;
      node.status.textContent = damageActorStatus();
      const curve = statsCurve(actor, mode);
      const chartKey =
        `${damage?.battle || 0}:${damage?.series_revision || 0}:` +
        `${track.renderKey}:${key}:${actorId}:` +
        `${curve.length}:${Math.round(total)}`;
      if (node.chart.dataset.renderKey === chartKey &&
          node.chart.childElementCount) {
        continue;
      }
      node.chart.dataset.renderKey = chartKey;
      const series = curve.length >= 2
        ? [{
            id: actorId || "selected",
            label: name,
            color: key === "damage"
              ? damageActorColor(actorId)
              : mode.color,
            values: curve,
            total,
          }]
        : [];
      renderDamageLegend(node.legend, series);
      node.chart.replaceChildren(series.length
        ? buildDamageChart(series, {
            ...track,
            compact: true,
            narrow: true,
            unitLabel: mode.chartUnit,
          })
        : Object.assign(document.createElement("div"), {
            className: "empty-small",
            textContent: operatorId
              ? `该干员暂无${mode.label}数据`
              : "等待选择干员",
          }));
    }
  }

  function renderDamagePage() {
    const damage = state.damage;
    const mode = statsModeFor("page");
    const hint = statsModeHint(mode);
    const operators = statsOperators(mode);
    const total = mode.total(damage);
    const duration = Number(damage?.duration) || 0;
    syncStatsTabs();
    dom.damagePageSubtitle.textContent = hint;
    dom.damageRailTitle.textContent = mode.rail;
    dom.damageChartTitle.textContent = mode.title;
    dom.damageChartHint.textContent = hint;
    dom.damageTotalBadge.textContent = `${mode.totalLabel} ${fmtCompact(total)}`;
    dom.damageDpsBadge.textContent =
      `${mode.rateLabel} ${fmtCompact(duration > 0 ? total / duration : 0)}`;
    dom.damageStatusBadge.textContent = damageActorStatus();

    if (!operators.length) {
      dom.damageUnitRail.dataset.renderKey = "";
      dom.damageUnitRail.innerHTML =
        `<div class="empty-small">等待${mode.label}事件</div>`;
      dom.damageSelectedName.textContent = "未选择干员";
      dom.damageSelectedMeta.textContent = `暂无${mode.label}记录`;
      dom.damageSelectedShare.textContent = "0%";
      dom.damageTypeGrid.innerHTML = "";
      dom.damageLegend.replaceChildren();
      dom.damageChart.replaceChildren(
        Object.assign(document.createElement("div"), {
          className: "empty-small",
          textContent: mode.empty,
        }),
      );
      state.lastDamageChartKey = "";
      return;
    }

    if (!operators.some((actor) => String(actor.id) === String(state.selectedDamageId))) {
      state.selectedDamageId = String(operators[0].id);
    }
    const rosterKey = operators.map((actor) => [
      actor.id,
      Math.round(mode.actor(actor)),
      state.pageStatsMode,
      state.pageStatsMode === "damage"
        ? DAMAGE_TYPES.map((type) => Math.round(damageTypeValue(actor, type.key))).join(",")
        : "",
      String(actor.id) === String(state.selectedDamageId) ? state.selectedDamageId : "",
    ].join(":")).join("|");
    if (dom.damageUnitRail.dataset.renderKey !== rosterKey) {
      dom.damageUnitRail.dataset.renderKey = rosterKey;
      dom.damageUnitRail.innerHTML = operators.map((actor) => {
        const avatar = damageAvatar(actor.id);
        const name = actor.name || operatorName(actor.id) || actor.id;
        const active = String(actor.id) === String(state.selectedDamageId);
        const avatarHtml = avatar?.id
          ? `<span class="damage-row-avatar"><img data-asset-kind="${avatar.kind}" ` +
            `data-asset-id="${esc(avatar.id)}" alt=""></span>`
          : `<span class="damage-row-avatar">${esc(Array.from(name || "?")[0] || "?")}</span>`;
        const types = state.pageStatsMode === "damage"
          ? DAMAGE_TYPES.map((type) => {
              const value = damageTypeValue(actor, type.key);
              return `<span class="damage-row-type" style="--type-color:${type.color}">` +
                `<i></i><span>${type.label}</span><b>${esc(fmtCompact(value))}</b></span>`;
            }).join("")
          : `<span class="damage-row-type" style="--type-color:${mode.color}">` +
            `<i></i><span>${mode.label}</span><b>${esc(fmtCompact(mode.actor(actor)))}</b></span>`;
        const actorTotal = mode.actor(actor);
        return `<button class="damage-row ${active ? "is-active" : ""}" ` +
          `data-action="select-damage" data-damage-id="${esc(actor.id)}">` +
          avatarHtml +
          `<span class="damage-row-copy"><span class="damage-row-name">${esc(name)}</span>` +
          `<span class="damage-row-types">${types}</span></span>` +
          `<span class="damage-row-total"><strong>${esc(fmtCompact(actorTotal))}</strong>` +
          `<span>${fmt(damageShare(actorTotal, total))}%</span></span></button>`;
      }).join("");
      window.BetterBattleAssets?.observe(dom.damageUnitRail);
    }

    const selected = damageOperatorFor(state.selectedDamageId) || operators[0];
    const selectedTotal = mode.actor(selected);
    const selectedDps = duration > 0 ? selectedTotal / duration : 0;
    dom.damageSelectedName.textContent = selected.name || operatorName(selected.id) || selected.id;
    dom.damageSelectedMeta.textContent = state.pageStatsMode === "damage"
      ? `实际 ${fmtCompact(selectedTotal)} · 计算 ${fmtCompact(selected.calculated)} · ` +
        `DPS ${fmtCompact(selectedDps)} · 干员 ${operators.length}`
      : `累计 ${fmtCompact(selectedTotal)} · ${mode.rateLabel} ` +
        `${fmtCompact(selectedDps)} · 干员 ${operators.length}`;
    dom.damageSelectedShare.textContent =
      `${fmt(damageShare(selectedTotal, total))}%`;
    const typeKey = state.pageStatsMode === "damage"
      ? `${state.pageStatsMode}:` + DAMAGE_TYPES.map((type) => {
          const value = damageTypeValue(selected, type.key);
          return `${type.key}:${Math.round(value)}`;
        }).join(":") + `:${Math.round(selectedTotal)}`
      : `${state.pageStatsMode}:${Math.round(selectedTotal)}:` +
        `${Math.round(selectedDps)}:${Math.round(total)}`;
    if (dom.damageTypeGrid.dataset.renderKey !== typeKey) {
      dom.damageTypeGrid.dataset.renderKey = typeKey;
      dom.damageTypeGrid.innerHTML = state.pageStatsMode === "damage"
        ? DAMAGE_TYPES.map((type) => {
            const value = damageTypeValue(selected, type.key);
            return `<div class="damage-type-card" style="--type-color:${type.color}">` +
              `<span>${type.label}伤害</span><strong>${esc(fmtCompact(value))}</strong>` +
              `<em>${fmt(damageShare(value, selectedTotal))}% · 全队 ` +
              `${fmt(damageShare(value, total))}%</em></div>`;
          }).join("")
        : [
            [mode.totalLabel, selectedTotal, mode.color],
            [mode.rateLabel, selectedDps, mode.color],
            ["全队占比", damageShare(selectedTotal, total), "#8f9ba1"],
          ].map(([label, value, color]) =>
            `<div class="damage-type-card" style="--type-color:${color}">` +
            `<span>${label}</span><strong>${esc(
              label === "全队占比" ? `${fmt(value)}%` : fmtCompact(value),
            )}</strong><em>${esc(mode.label)}</em></div>`,
          ).join("");
    }

    const chartSeries = operators
      .filter((actor) => statsCurve(actor, mode).length >= 2)
      .map((actor) => ({
        id: String(actor.id),
        label: actor.name || operatorName(actor.id) || actor.id,
        color: state.pageStatsMode === "damage"
          ? damageActorColor(actor.id)
          : mode.color,
        values: statsCurve(actor, mode),
        total: mode.actor(actor),
      }));
    const track = damageFrameTrack();
    const chartKey =
      `${damage?.battle || 0}:${damage?.series_revision || 0}:` +
      `${track.renderKey}:${state.pageStatsMode}:${state.selectedDamageId || ""}:` +
      `${chartSeries.length}`;
    if (state.lastDamageChartKey !== chartKey) {
      state.lastDamageChartKey = chartKey;
      renderDamageLegend(dom.damageLegend, chartSeries, String(state.selectedDamageId));
      dom.damageChart.replaceChildren(chartSeries.length
        ? buildDamageChart(chartSeries, {
            ...track,
            selectedId: String(state.selectedDamageId),
            unitLabel: mode.chartUnit,
          })
        : Object.assign(document.createElement("div"), {
            className: "empty-small",
            textContent: `等待战斗${mode.label}曲线`,
          }));
    }
  }

  pages.damage = {
    render: renderDamagePage,
    renderBattlePanel: renderBattleDamagePanel,
    mergePayload: mergeDamagePayload,
  };
})();
