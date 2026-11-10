(() => {
  "use strict";

  const protocol = window.BetterBattleProtocol;
  const ROSTER = Array.isArray(window.ROSTER_CATALOG)
    ? window.ROSTER_CATALOG
    : [];
  const ROSTER_BY_ID = new Map(
    ROSTER.map((entry) => [String(entry?.id || ""), entry]),
  );
  const PLACEHOLDER =
    "data:image/svg+xml;charset=UTF-8," +
    encodeURIComponent(
      '<svg xmlns="http://www.w3.org/2000/svg" width="180" height="360">' +
      '<rect width="180" height="360" fill="#202733"/>' +
      '<circle cx="90" cy="118" r="42" fill="#313b4d"/>' +
      '<path d="M28 360c0-82 28-142 62-142s62 60 62 142z" fill="#313b4d"/>' +
      "</svg>",
    );

  const ready = new Map();
  const pending = new Map();
  const failed = new Map();
  const waiters = new Map();
  let observer = null;
  let batch = null;

  const keyFor = (kind, id) => `${kind}:${String(id || "")}`;

  function portraitIdFromPath(path) {
    const name = String(path || "").split("/").pop() || "";
    return name.replace(/\.[^.]+$/, "");
  }

  function portraitIdFor(operatorId) {
    const entry = ROSTER_BY_ID.get(String(operatorId || ""));
    return portraitIdFromPath(entry?.portrait || entry?.file || "");
  }

  function avatarIdFor(operatorId) {
    return String(operatorId || "");
  }

  function imageUrl(kind, id) {
    return ready.get(keyFor(kind, id)) || "";
  }

  function resolveWaiters(key, value, error) {
    const list = waiters.get(key);
    if (!list) return;
    waiters.delete(key);
    for (const waiter of list) {
      if (error) waiter.reject(error);
      else waiter.resolve(value);
    }
  }

  function request(kind, id) {
    const value = String(id || "");
    if (!value) return false;
    const key = keyFor(kind, value);
    if (ready.has(key) || pending.has(key) || failed.has(key)) return false;
    pending.set(key, Date.now());
    protocol.post({ type: "asset-request", kind, id: value });
    window.setTimeout(() => {
      if (!pending.has(key)) return;
      pending.delete(key);
      failed.set(key, "请求超时");
      resolveWaiters(key, null, new Error("图片加载超时"));
      applyToDom();
    }, 15000);
    return true;
  }

  function loadImage(kind, id) {
    const value = String(id || "");
    if (!value) return Promise.reject(new Error("缺少图片 ID"));
    const key = keyFor(kind, value);
    const url = ready.get(key);
    if (url) return Promise.resolve(url);
    if (failed.has(key)) return Promise.reject(new Error(failed.get(key)));
    return new Promise((resolve, reject) => {
      const list = waiters.get(key) || [];
      list.push({ resolve, reject });
      waiters.set(key, list);
      request(kind, value);
    });
  }

  function applyImage(img) {
    const kind = img?.dataset?.assetKind;
    const id = img?.dataset?.assetId;
    if (!kind || !id) return;
    const key = keyFor(kind, id);
    const url = ready.get(key);
    if (url) {
      if (img.getAttribute("src") !== url) img.src = url;
      img.classList.remove("asset-loading", "asset-failed");
      return;
    }
    if (failed.has(key)) {
      img.classList.remove("asset-loading");
      img.classList.add("asset-failed");
      return;
    }
    const src = img.getAttribute("src");
    if (!src || src === PLACEHOLDER) img.src = PLACEHOLDER;
    img.classList.add("asset-loading");
    img.classList.remove("asset-failed");
  }

  function applyToDom(root = document) {
    root.querySelectorAll?.("img[data-asset-kind][data-asset-id]")
      .forEach(applyImage);
  }

  function ensure(img) {
    const kind = img?.dataset?.assetKind;
    const id = img?.dataset?.assetId;
    if (!kind || !id) return;
    applyImage(img);
    const key = keyFor(kind, id);
    if (!ready.has(key) && !pending.has(key) && !failed.has(key)) {
      request(kind, id);
    }
  }

  function observe(root = document) {
    const scope = root?.querySelectorAll
      ? root
      : document.getElementById(root);
    if (!scope) return;
    const images = Array.from(
      scope.querySelectorAll("img[data-asset-kind][data-asset-id]"),
    );
    if (!("IntersectionObserver" in window)) {
      images.forEach(ensure);
      return;
    }
    if (!observer) {
      observer = new IntersectionObserver((entries) => {
        for (const entry of entries) {
          if (!entry.isIntersecting) continue;
          observer.unobserve(entry.target);
          ensure(entry.target);
        }
      }, { rootMargin: "280px 0px" });
    }
    for (const img of images) {
      if (img.dataset.assetObserved === "1") {
        applyImage(img);
        continue;
      }
      img.dataset.assetObserved = "1";
      applyImage(img);
      observer.observe(img);
    }
  }

  function setProgress(done, total, label) {
    const box = document.getElementById("artCacheProgress");
    const bar = document.getElementById("artCacheBar");
    const text = document.getElementById("artCacheLabel");
    if (!box || !bar || !text) return;
    box.hidden = false;
    const percent = total > 0 ? Math.min(100, done / total * 100) : 0;
    bar.style.width = `${percent.toFixed(1)}%`;
    text.textContent = label || `${done} / ${total}`;
  }

  function finishBatch() {
    const button = document.getElementById("cacheArtButton");
    if (button) {
      button.disabled = false;
      button.textContent = "缓存图片资源";
    }
    if (batch) {
      const { done, total, failed: errors } = batch;
      setProgress(done, total,
        errors > 0 ? `完成 ${done} / ${total}，失败 ${errors}` : `完成 ${done} / ${total}`);
    }
    window.setTimeout(() => {
      const box = document.getElementById("artCacheProgress");
      if (box) box.hidden = true;
    }, 1800);
    batch = null;
  }

  function noteBatchResult(key, ok) {
    if (!batch || !batch.keys.has(key) || batch.counted.has(key)) return;
    batch.counted.add(key);
    batch.done += 1;
    if (!ok) batch.failed += 1;
    setProgress(batch.done, batch.total,
      `缓存中 ${batch.done} / ${batch.total}` +
      (batch.failed ? `，失败 ${batch.failed}` : ""));
    if (batch.done >= batch.total) finishBatch();
  }

  function cacheAll() {
    if (batch) return;
    if (!window.chrome?.webview) return;
    const items = [];
    const seen = new Set();
    const add = (kind, id) => {
      const value = String(id || "");
      if (!value) return;
      const key = keyFor(kind, value);
      if (seen.has(key) || ready.has(key) || failed.has(key)) return;
      seen.add(key);
      items.push({ kind, id: value });
    };
    for (const entry of ROSTER) {
      const id = String(entry?.id || "");
      if (!id) continue;
      add("avatar", avatarIdFor(id));
      add("portrait", portraitIdFor(id));
    }
    if (!items.length) {
      setProgress(1, 1, "已全部缓存");
      window.setTimeout(() => {
        const box = document.getElementById("artCacheProgress");
        if (box) box.hidden = true;
      }, 1500);
      return;
    }
    batch = {
      total: items.length,
      done: 0,
      failed: 0,
      keys: new Set(items.map((item) => keyFor(item.kind, item.id))),
      counted: new Set(),
    };
    const button = document.getElementById("cacheArtButton");
    if (button) {
      button.disabled = true;
      button.textContent = "缓存中…";
    }
    for (const item of items) pending.set(keyFor(item.kind, item.id), Date.now());
    setProgress(0, items.length, `缓存中 0 / ${items.length}`);
    protocol.post({ type: "asset-cache-all", items });
  }

  function onMessage(message) {
    if (!message || typeof message !== "object") return false;
    if (message.type === "asset-cache" && Array.isArray(message.items)) {
      for (const item of message.items) {
        if (!item?.kind || !item?.id || !item?.url) continue;
        const key = keyFor(item.kind, item.id);
        ready.set(key, item.url);
        pending.delete(key);
        failed.delete(key);
      }
      applyToDom();
      return true;
    }
    if (message.type === "asset-ready") {
      const key = message.key || keyFor(message.kind, message.id);
      pending.delete(key);
      if (message.ok && message.url) {
        ready.set(key, message.url);
        failed.delete(key);
        resolveWaiters(key, message.url);
      } else {
        const error = message.error || "图片导出失败";
        failed.set(key, error);
        resolveWaiters(key, null, new Error(error));
      }
      applyToDom();
      noteBatchResult(key, !!message.ok);
      return true;
    }
    return false;
  }

  if (window.chrome?.webview) {
    window.chrome.webview.addEventListener("message", (event) => {
      let message = event.data;
      if (typeof message === "string") {
        try {
          message = JSON.parse(message);
        } catch {
          return;
        }
      }
      onMessage(message);
    });
  }

  window.BetterBattleAssets = Object.freeze({
    observe,
    ensure,
    request,
    loadImage,
    cacheAll,
    portraitIdFor,
    avatarIdFor,
    imageUrl,
    keyFor,
  });

  window.addEventListener("load", () => {
    protocol.post({ type: "asset-cache-scan" });
    observe(document);
  });
})();
