import assert from "node:assert/strict";
import { mkdtemp, readFile, rm, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

class FakeClassList {
  constructor() {
    this.values = new Set();
  }

  toggle(name, force) {
    const active = force === undefined ? !this.values.has(name) : Boolean(force);
    if (active) this.values.add(name);
    else this.values.delete(name);
    return active;
  }

  contains(name) {
    return this.values.has(name);
  }
}

class FakeEventTarget {
  constructor() {
    this.listeners = new Map();
    this.parentNode = null;
  }

  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }

  async dispatch(type, init = {}) {
    const event = {
      ...init,
      type,
      target: this,
      currentTarget: null,
      defaultPrevented: false,
      propagationStopped: false,
      preventDefault() { this.defaultPrevented = true; },
      stopPropagation() { this.propagationStopped = true; }
    };
    if (type === "click" && this.disabled) return event;

    for (let current = this; current; current = current.parentNode) {
      event.currentTarget = current;
      for (const listener of current.listeners.get(type) || []) {
        await listener(event);
      }
      if (event.propagationStopped) break;
    }
    return event;
  }
}

class FakeElement extends FakeEventTarget {
  constructor(id = "", ownerDocument = null) {
    super();
    this.id = id;
    this.ownerDocument = ownerDocument;
    this.attributes = new Map();
    this.children = [];
    this.classList = new FakeClassList();
    this.className = "";
    this.dataset = {};
    this.disabled = false;
    this.hidden = false;
    this.paused = true;
    this.src = "";
    this.title = "";
    this.type = "";
    this.value = "";
    this._textContent = "";
  }

  get textContent() {
    return this._textContent;
  }

  set textContent(value) {
    this._textContent = String(value);
    this.children.forEach((child) => {
      if (child && typeof child === "object") child.parentNode = null;
    });
    this.children = [];
  }

  append(...children) {
    children.forEach((child) => {
      if (child && typeof child === "object") child.parentNode = this;
    });
    this.children.push(...children);
  }

  appendChild(child) {
    if (child && typeof child === "object") child.parentNode = this;
    this.children.push(child);
    return child;
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
  }

  getAttribute(name) {
    return this.attributes.has(name) ? this.attributes.get(name) : null;
  }

  removeAttribute(name) {
    this.attributes.delete(name);
    if (name === "src") this.src = "";
  }

  querySelector() {
    if (!this.modalCard) {
      this.modalCard = new FakeElement(`${this.id}-card`, this.ownerDocument);
      this.modalCard.parentNode = this;
    }
    return this.modalCard;
  }

  pause() {
    this.paused = true;
  }

  async play() {
    if (this.playError) throw this.playError;
    this.paused = false;
  }

  async requestFullscreen() {
    this.ownerDocument.requestedFullscreen = this;
  }
}

const ELEMENT_IDS = [
  "description-modal", "description-open", "fatal-error", "fullscreen-toggle",
  "login-form", "login-message", "login-modal", "login-open", "login-password",
  "login-submit", "login-username", "museum-description", "museum-shell",
  "museum-title", "music-toggle", "notice", "online-count", "panorama",
  "retry-bootstrap", "scene-audio", "scene-catalog", "scene-drawer",
  "scene-drawer-toggle", "scene-loading", "total-views", "view-panel",
  "view-toggle", "vr-toggle"
];

class FakeDocument extends FakeEventTarget {
  constructor() {
    super();
    this.elements = new Map(ELEMENT_IDS.map((id) => [id, new FakeElement(id, this)]));
    this.elements.forEach((element) => { element.parentNode = this; });
    this.fullscreenElement = null;
    this.requestedFullscreen = null;
    this.title = "";
    this.viewButtons = ["normal", "planet", "fisheye", "crystal"].map((mode) => {
      const button = new FakeElement(`view-${mode}`, this);
      button.dataset.viewMode = mode;
      button.setAttribute("aria-pressed", String(mode === "normal"));
      button.parentNode = this.getElementById("view-panel");
      return button;
    });

    this.getElementById("scene-catalog").parentNode = this.getElementById("scene-drawer");
    this.getElementById("scene-drawer").hidden = true;
    this.getElementById("view-panel").hidden = true;
    this.getElementById("notice").hidden = true;
    this.getElementById("scene-drawer-toggle").setAttribute("aria-expanded", "false");
    this.getElementById("view-toggle").setAttribute("aria-expanded", "false");
    const music = this.getElementById("music-toggle");
    music.disabled = true;
    music.title = "播放讲解";
    music.setAttribute("aria-label", "播放讲解");
    music.append({ nodeName: "svg" });
  }

  getElementById(id) {
    return this.elements.get(id) || null;
  }

  querySelectorAll(selector) {
    if (selector === "[data-view-mode]") return this.viewButtons;
    if (selector === "#scene-catalog .scene-card") {
      return this.getElementById("scene-catalog").children;
    }
    return [];
  }

  createElement(tagName) {
    return new FakeElement(tagName, this);
  }

  async exitFullscreen() {
    this.fullscreenElement = null;
  }
}

class MuseumUiStateStub {
  constructor({ onChange } = {}) {
    this.sceneDrawerOpen = false;
    this.viewPanelOpen = false;
    this.viewMode = "normal";
    this.onChange = onChange || (() => {});
  }

  snapshot() {
    return {
      sceneDrawerOpen: this.sceneDrawerOpen,
      viewPanelOpen: this.viewPanelOpen,
      viewMode: this.viewMode
    };
  }

  update(next) {
    if (!Object.entries(next).some(([key, value]) => this[key] !== value)) return;
    Object.assign(this, next);
    this.onChange(this.snapshot());
  }

  toggleSceneDrawer() {
    this.update({ sceneDrawerOpen: !this.sceneDrawerOpen, viewPanelOpen: false });
  }

  toggleViewPanel() {
    this.update({ sceneDrawerOpen: false, viewPanelOpen: !this.viewPanelOpen });
  }

  closeTransientLayers() {
    this.update({ sceneDrawerOpen: false, viewPanelOpen: false });
  }

  selectViewMode(mode) {
    this.update({ viewMode: mode, viewPanelOpen: false });
  }
}

async function createHarness() {
  const directory = await mkdtemp(join(tmpdir(), "jingjie-ar-museum-wiring-"));
  const sourcePath = new URL("../../WebApps/ARServer/www/js/museum-app.js", import.meta.url);
  let source = await readFile(sourcePath, "utf8");
  if (process.env.MUSEUM_APP_WIRING_MUTATION === "remove-scene-toggle-stop") {
    const original = `element("scene-drawer-toggle").addEventListener("click", (event) => {
  event.stopPropagation();
  uiState.toggleSceneDrawer();
});`;
    const mutated = original.replace("  event.stopPropagation();\n", "");
    const mutatedSource = source.replace(original, mutated);
    assert.notEqual(mutatedSource, source, "scene toggle stopPropagation mutation target missing");
    source = mutatedSource;
  }
  source = source.replace(/^import .*;\n/gm, "");
  source = `const {
    ApiClient, ApiError, AuthSession, VisitorSession, KrpanoAdapter, ArtworkModal,
    MuseumLifecycle, ModalFocusManager, MuseumUiState
  } = globalThis.__museumAppTestDeps;\n${source}`;
  const modulePath = join(directory, "museum-app.mjs");
  await writeFile(modulePath, source);

  const document = new FakeDocument();
  const window = new FakeEventTarget();
  window.setTimeout = () => 1;
  window.clearTimeout = () => {};

  let rejectCatalog;
  const catalogPromise = new Promise((_, reject) => { rejectCatalog = reject; });
  class ApiError extends Error {}
  class ApiClient {
    async request(path) {
      if (path === "/api/scenes") return catalogPromise;
      if (path === "/api/statistics/views") {
        return { statisticsAvailable: false, totalViews: null };
      }
      if (path === "/api/presence") return { onlineCount: 0 };
      throw new Error(`unexpected API request: ${path}`);
    }
  }
  class AuthSession {
    token() { return null; }
    async logout() {}
    async authenticate() { return { username: "tester", token: "token", isNew: false }; }
  }
  class VisitorSession {
    startHeartbeat() {}
  }
  class MuseumLifecycle {
    async bootstrapVisitorOnce() { return null; }
    startCounterPolling() {}
  }
  class KrpanoAdapter {
    constructor() {
      this.viewMode = "normal";
      globalThis.__museumAppAdapter = this;
    }
    invalidate() {}
    async loadScene() { return true; }
    setViewMode(mode) {
      if (this.viewModeError) throw this.viewModeError;
      this.viewMode = mode;
    }
    enterVr() {
      if (this.vrError) throw this.vrError;
      return true;
    }
  }
  class ArtworkModal {
    open() {}
    openText() {}
  }
  class ModalFocusManager {
    open(modal) {
      modal.openedWithTransientLayersClosed =
        document.getElementById("scene-drawer").hidden &&
        document.getElementById("view-panel").hidden;
    }
    close() {}
  }

  const previous = {
    document: globalThis.document,
    window: globalThis.window,
    deps: globalThis.__museumAppTestDeps,
    adapter: globalThis.__museumAppAdapter
  };
  globalThis.document = document;
  globalThis.window = window;
  globalThis.__museumAppTestDeps = {
    ApiClient, ApiError, AuthSession, VisitorSession, KrpanoAdapter, ArtworkModal,
    MuseumLifecycle, ModalFocusManager, MuseumUiState: MuseumUiStateStub
  };

  await import(`${pathToFileURL(modulePath).href}?case=${Date.now()}-${Math.random()}`);
  const adapter = globalThis.__museumAppAdapter;

  return {
    adapter,
    document,
    failCatalog(error = new Error("展馆目录加载失败")) { rejectCatalog(error); },
    async cleanup() {
      globalThis.document = previous.document;
      globalThis.window = previous.window;
      globalThis.__museumAppTestDeps = previous.deps;
      globalThis.__museumAppAdapter = previous.adapter;
      await rm(directory, { recursive: true, force: true });
    }
  };
}

test("视角适配器失败时保留选择，成功后才推进 UI", async () => {
  const harness = await createHarness();
  try {
    const normal = harness.document.viewButtons[0];
    const fisheye = harness.document.viewButtons[2];
    harness.adapter.viewModeError = new Error("播放器尚未就绪");

    await fisheye.dispatch("click");
    assert.equal(normal.getAttribute("aria-pressed"), "true");
    assert.equal(fisheye.getAttribute("aria-pressed"), "false");
    assert.match(harness.document.getElementById("notice").textContent, /播放器尚未就绪/);

    harness.adapter.viewModeError = null;
    await fisheye.dispatch("click");
    assert.equal(normal.getAttribute("aria-pressed"), "false");
    assert.equal(fisheye.getAttribute("aria-pressed"), "true");
  } finally {
    await harness.cleanup();
  }
});

test("toggle click 冒泡时浮层保持打开，内部点击保留且真正外部点击关闭", async () => {
  const harness = await createHarness();
  try {
    const drawer = harness.document.getElementById("scene-drawer");
    const panel = harness.document.getElementById("view-panel");
    const panorama = harness.document.getElementById("panorama");

    await harness.document.getElementById("scene-drawer-toggle").dispatch("click");
    assert.equal(drawer.hidden, false);
    assert.equal(panel.hidden, true);
    await drawer.dispatch("click");
    assert.equal(drawer.hidden, false);

    await harness.document.getElementById("view-toggle").dispatch("click");
    assert.equal(drawer.hidden, true);
    assert.equal(panel.hidden, false);
    await panel.dispatch("click");
    assert.equal(panel.hidden, false);

    await panorama.dispatch("click");
    assert.equal(panel.hidden, true);

    await harness.document.getElementById("scene-drawer-toggle").dispatch("click");
    await harness.document.dispatch("keydown", { key: "Escape" });
    assert.equal(drawer.hidden, true);
  } finally {
    await harness.cleanup();
  }
});

test("全景 pointerdown 和 wheel 关闭当前临时浮层", async () => {
  const harness = await createHarness();
  try {
    const drawer = harness.document.getElementById("scene-drawer");
    const panel = harness.document.getElementById("view-panel");
    const panorama = harness.document.getElementById("panorama");

    await harness.document.getElementById("scene-drawer-toggle").dispatch("click");
    assert.equal(drawer.hidden, false);
    await panorama.dispatch("pointerdown");
    assert.equal(drawer.hidden, true);

    await harness.document.getElementById("view-toggle").dispatch("click");
    assert.equal(panel.hidden, false);
    await panorama.dispatch("wheel");
    assert.equal(panel.hidden, true);
  } finally {
    await harness.cleanup();
  }
});

test("展馆简介在打开模态框前关闭临时浮层", async () => {
  const harness = await createHarness();
  try {
    const drawer = harness.document.getElementById("scene-drawer");
    const modal = harness.document.getElementById("description-modal");

    await harness.document.getElementById("scene-drawer-toggle").dispatch("click");
    assert.equal(drawer.hidden, false);
    await harness.document.getElementById("description-open").dispatch("click");
    assert.equal(drawer.hidden, true);
    assert.equal(modal.openedWithTransientLayersClosed, true);
  } finally {
    await harness.cleanup();
  }
});

test("音乐状态更新保留按钮中的 SVG 子节点", async () => {
  const harness = await createHarness();
  try {
    const button = harness.document.getElementById("music-toggle");
    const svg = button.children[0];
    const audio = harness.document.getElementById("scene-audio");
    button.disabled = false;
    audio.src = "/audio/guide.mp3";

    await button.dispatch("click");
    assert.equal(button.children.length, 1);
    assert.equal(button.children[0], svg);
    assert.equal(button.classList.contains("is-playing"), true);

    await button.dispatch("click");
    assert.equal(button.children[0], svg);
    assert.equal(button.classList.contains("is-playing"), false);
  } finally {
    await harness.cleanup();
  }
});

test("全屏请求使用 museum-shell 且 fullscreenchange 同步按钮", async () => {
  const harness = await createHarness();
  try {
    const shell = harness.document.getElementById("museum-shell");
    const button = harness.document.getElementById("fullscreen-toggle");

    await button.dispatch("click");
    assert.equal(harness.document.requestedFullscreen, shell);

    harness.document.fullscreenElement = shell;
    await harness.document.dispatch("fullscreenchange");
    assert.equal(button.classList.contains("is-fullscreen"), true);
    assert.equal(button.getAttribute("aria-label"), "退出全屏");

    harness.document.fullscreenElement = null;
    await harness.document.dispatch("fullscreenchange");
    assert.equal(button.classList.contains("is-fullscreen"), false);
    assert.equal(button.getAttribute("aria-label"), "全屏浏览");
  } finally {
    await harness.cleanup();
  }
});

test("VR 异常显示中文通知且不破坏全景状态", async () => {
  const harness = await createHarness();
  try {
    const panorama = harness.document.getElementById("panorama");
    panorama.dataset.renderState = "loaded";
    harness.adapter.vrError = new Error("当前设备或浏览器无法进入 VR");

    await harness.document.getElementById("vr-toggle").dispatch("click");
    assert.match(harness.document.getElementById("notice").textContent, /无法进入 VR/);
    assert.equal(panorama.dataset.renderState, "loaded");
  } finally {
    await harness.cleanup();
  }
});

test("目录加载完成前禁用的音乐按钮立即呈现无音乐状态", async () => {
  const harness = await createHarness();
  try {
    const button = harness.document.getElementById("music-toggle");
    const audio = harness.document.getElementById("scene-audio");
    assert.equal(button.disabled, true);
    assert.equal(button.title, "当前场景暂无音乐");
    assert.equal(button.getAttribute("aria-label"), "播放讲解");
    audio.src = "/audio/guide.mp3";
    await button.dispatch("click");
    assert.equal(audio.paused, true);
    assert.equal(button.classList.contains("is-playing"), false);
    harness.failCatalog();
    await new Promise((resolve) => setImmediate(resolve));
  } finally {
    await harness.cleanup();
  }
});

test("手动播放被 AbortError 中断时不显示误导通知", async () => {
  const harness = await createHarness();
  try {
    const audio = harness.document.getElementById("scene-audio");
    harness.document.getElementById("music-toggle").disabled = false;
    audio.src = "/audio/guide.mp3";
    audio.playError = Object.assign(new Error("The play request was interrupted"), {
      name: "AbortError"
    });

    await harness.document.getElementById("music-toggle").dispatch("click");
    assert.equal(harness.document.getElementById("notice").hidden, true);
    assert.equal(harness.document.getElementById("notice").textContent, "");
  } finally {
    await harness.cleanup();
  }
});

test("手动播放的其他拒绝仍显示中文通知", async () => {
  const harness = await createHarness();
  try {
    const audio = harness.document.getElementById("scene-audio");
    harness.document.getElementById("music-toggle").disabled = false;
    audio.src = "/audio/guide.mp3";
    audio.playError = new Error("NotAllowedError");

    await harness.document.getElementById("music-toggle").dispatch("click");
    assert.match(harness.document.getElementById("notice").textContent, /浏览器未允许播放音频/);
  } finally {
    await harness.cleanup();
  }
});
