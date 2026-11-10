"use strict";

(() => {
  const components = window.BetterBattleComponents || {};

  function setHtmlIfChanged(node, html) {
    if (!node || node.__componentHtml === html) return false;
    node.__componentHtml = html;
    node.innerHTML = html;
    return true;
  }

  function createSelectionList(options) {
    const mount = options.mount;
    const onSelect = options.onSelect || (() => {});
    const itemSelector = options.itemSelector || "[data-ui-key]";
    let items = [];
    let selectedKey = "";
    let lastHtml = "";

    function render(nextItems, nextSelectedKey) {
      items = Array.isArray(nextItems) ? nextItems : [];
      selectedKey = String(nextSelectedKey ?? "");
      const html = items.map((item) => {
        const key = String(item?.key ?? "");
        return typeof item?.render === "function"
          ? item.render(key === selectedKey)
          : String(item?.html ?? "");
      }).join("");
      if (html !== lastHtml) {
        lastHtml = html;
        setHtmlIfChanged(mount, html);
      }
    }

    function handleClick(event) {
      const button = event.target.closest(itemSelector);
      if (!button || !mount.contains(button)) return;
      const key = String(button.dataset.uiKey || "");
      const item = items.find((candidate) => String(candidate?.key ?? "") === key);
      if (!item) return;
      selectedKey = key;
      render(items, selectedKey);
      onSelect(item, key);
    }

    mount?.addEventListener("click", handleClick);

    return {
      render,
      selectedKey: () => selectedKey,
      destroy() {
        mount?.removeEventListener("click", handleClick);
      },
    };
  }

  function createDetailView(options) {
    const kindNode = options.kindNode;
    const nameNode = options.nameNode;
    const metaNode = options.metaNode;
    const badgeNode = options.badgeNode;
    const bodyNode = options.bodyNode;
    let lastHtml = "";

    return {
      render(view) {
        if (kindNode) kindNode.textContent = String(view?.kind ?? "");
        if (nameNode) nameNode.textContent = String(view?.name ?? "");
        if (metaNode) metaNode.textContent = String(view?.meta ?? "");
        if (badgeNode) badgeNode.textContent = String(view?.badge ?? "");
        const html = String(view?.html ?? "");
        if (html !== lastHtml) {
          lastHtml = html;
          setHtmlIfChanged(bodyNode, html);
        }
      },
    };
  }

  function createPanelDock(options) {
    const host = options.host;
    const panel = options.panel;
    const className = options.className || "component-docked-panel";
    const home = panel?.parentNode || null;
    const homeNext = panel?.nextSibling || null;
    let docked = false;

    return {
      mount() {
        if (!host || !panel || docked) return;
        host.appendChild(panel);
        panel.classList.add(className);
        docked = true;
      },
      unmount() {
        if (!panel || !docked) return;
        panel.classList.remove(className);
        if (home) home.insertBefore(panel, homeNext);
        docked = false;
      },
      isDocked: () => docked,
    };
  }

  components.createSelectionList = createSelectionList;
  components.createDetailView = createDetailView;
  components.createPanelDock = createPanelDock;
  window.BetterBattleComponents = components;
})();
