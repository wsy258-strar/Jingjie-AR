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

  addEventListener(type, listener, options = {}) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    const normalized = typeof options === "boolean" ? { capture: options } : options;
    this.listeners.get(type).push({
      listener,
      capture: Boolean(normalized.capture),
      passive: Boolean(normalized.passive)
    });
  }

  async dispatch(type, init = {}) {
    const event = {
      ...init,
      type,
      target: this,
      currentTarget: null,
      eventPhase: 0,
      defaultPrevented: false,
      propagationStopped: false,
      preventDefault() { this.defaultPrevented = true; },
      stopPropagation() { this.propagationStopped = true; }
    };
    if (type === "click" && this.disabled) return event;

    const path = [];
    for (let current = this; current; current = current.parentNode) path.push(current);

    const invoke = async (current, capture, phase) => {
      event.currentTarget = current;
      event.eventPhase = phase;
      for (const entry of current.listeners.get(type) || []) {
        if (entry.capture === capture) await entry.listener(event);
      }
    };

    for (let index = path.length - 1; index > 0; index -= 1) {
      await invoke(path[index], true, 1);
      if (event.propagationStopped) return event;
    }
    await invoke(this, true, 2);
    await invoke(this, false, 2);
    if (event.propagationStopped) return event;
    for (let index = 1; index < path.length; index += 1) {
      await invoke(path[index], false, 3);
      if (event.propagationStopped) return event;
    }
    event.eventPhase = 0;
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

  contains(candidate) {
    for (let current = candidate; current; current = current.parentNode) {
      if (current === this) return true;
    }
    return false;
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

  querySelectorAll() {
    return [];
  }

  focus() {
    this.ownerDocument.activeElement = this;
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
  "artwork-modal", "description-modal", "description-open", "fatal-error", "fullscreen-toggle",
  "login-form", "login-message", "login-modal", "login-open", "login-password",
  "login-submit", "login-username", "museum-description", "museum-fullscreen-root", "museum-shell",
  "museum-title", "music-toggle", "notice", "online-count", "panorama",
  "retry-bootstrap", "scene-audio", "scene-catalog", "scene-drawer",
  "scene-dissolve", "scene-drawer-toggle", "scene-loading", "total-views", "view-panel",
  "view-toggle", "vr-toggle"
];

class FakeDocument extends FakeEventTarget {
  constructor() {
    super();
    this.elements = new Map(ELEMENT_IDS.map((id) => [id, new FakeElement(id, this)]));
    const fullscreenRoot = this.getElementById("museum-fullscreen-root");
    const shell = this.getElementById("museum-shell");
    fullscreenRoot.parentNode = this;
    shell.parentNode = fullscreenRoot;
    this.elements.forEach((element) => {
      if (element !== fullscreenRoot && element !== shell) element.parentNode = shell;
    });
    for (const id of ["description-modal", "artwork-modal", "login-modal", "notice", "fatal-error"])
      this.getElementById(id).parentNode = fullscreenRoot;
    this.fullscreenElement = null;
    this.requestedFullscreen = null;
    this.title = "";
    this.activeElement = this.getElementById("description-open");
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
    for (const id of ["description-modal", "artwork-modal", "login-modal"])
      this.getElementById(id).setAttribute("aria-hidden", "true");
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

  querySelector(selector) {
    if (selector === ".museum-shell") return this.getElementById("museum-shell");
    return null;
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

async function createHarness({ reducedMotion = false, locationHref = "https://example.test/" } = {}) {
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
    MuseumLifecycle, ModalFocusManager, MuseumUiState, SceneDissolve
  } = globalThis.__museumAppTestDeps;\n${source}`;
  source += `
globalThis.__museumAppTestInstance = app;
globalThis.__museumAppTestClass = MuseumApp;
globalThis.__museumArtworkIdFromLocation =
  typeof artworkIdFromLocation === "function" ? artworkIdFromLocation : undefined;
globalThis.__museumVisitorSession = visitor;
`;
  const modulePath = join(directory, "museum-app.mjs");
  await writeFile(modulePath, source);
  const modalFocusPath = join(directory, "modal-focus.mjs");
  await writeFile(modalFocusPath, await readFile(new URL(
    "../../WebApps/ARServer/www/js/modal-focus.js", import.meta.url
  ), "utf8"));
  const { ModalFocusManager } = await import(`${pathToFileURL(modalFocusPath).href}?case=${Math.random()}`);

  const document = new FakeDocument();
  const window = new FakeEventTarget();
  window.setTimeout = () => 1;
  window.clearTimeout = () => {};
  window.matchMediaQueries = [];
  window.matchMedia = (query) => {
    window.matchMediaQueries.push(query);
    return { matches: reducedMotion };
  };
  window.location = { href: locationHref };

  let resolveCatalog;
  let rejectCatalog;
  const catalogPromise = new Promise((resolve, reject) => {
    resolveCatalog = resolve;
    rejectCatalog = reject;
  });
  const defaultCatalog = {
    title: "测试展馆",
    defaultSceneId: "scene-a",
    scenes: []
  };
  class ApiError extends Error {}
  class ApiClient {
    async request(path) {
      if (path === "/api/scenes") return catalogPromise;
      if (path === "/api/scenes/scene-a") return { sceneId: "scene-a", music: {} };
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
    constructor() {
      this.heartbeatCalls = 0;
    }
    startHeartbeat() { this.heartbeatCalls += 1; }
  }
  class MuseumLifecycle {
    async bootstrapVisitorOnce() { return null; }
    startCounterPolling() {}
  }
  class SceneDissolve {
    constructor() {
      this.beginCalls = [];
      this.finishCalls = [];
      this.cancelCalls = [];
      globalThis.__museumSceneDissolve = this;
    }
    begin(generation) { this.beginCalls.push(generation); return true; }
    finish(generation) { this.finishCalls.push(generation); return true; }
    cancel(generation) { this.cancelCalls.push(generation); return true; }
  }
  class KrpanoAdapter {
    constructor(options) {
      this.options = options;
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
      if (this.vrAsyncError) {
        const rejected = Promise.reject(this.vrAsyncError);
        rejected.catch(() => {});
        return rejected;
      }
      if (this.vrError) throw this.vrError;
      return Promise.resolve(true);
    }
  }
  class ArtworkModal {
    open() {}
    openText() {}
  }
  const previous = {
    document: globalThis.document,
    window: globalThis.window,
    deps: globalThis.__museumAppTestDeps,
    adapter: globalThis.__museumAppAdapter,
    dissolve: globalThis.__museumSceneDissolve,
    app: globalThis.__museumAppTestInstance,
    appClass: globalThis.__museumAppTestClass,
    artworkIdFromLocation: globalThis.__museumArtworkIdFromLocation,
    visitor: globalThis.__museumVisitorSession
  };
  globalThis.document = document;
  globalThis.window = window;
  globalThis.__museumAppTestDeps = {
    ApiClient, ApiError, AuthSession, VisitorSession, KrpanoAdapter, ArtworkModal,
    MuseumLifecycle, ModalFocusManager, MuseumUiState: MuseumUiStateStub, SceneDissolve
  };

  await import(`${pathToFileURL(modulePath).href}?case=${Date.now()}-${Math.random()}`);
  const adapter = globalThis.__museumAppAdapter;
  const dissolve = globalThis.__museumSceneDissolve;
  const app = globalThis.__museumAppTestInstance;
  const MuseumApp = globalThis.__museumAppTestClass;
  const artworkIdFromLocation = globalThis.__museumArtworkIdFromLocation;
  const visitor = globalThis.__museumVisitorSession;

  return {
    app,
    MuseumApp,
    artworkIdFromLocation,
    visitor,
    adapter,
    dissolve,
    document,
    window,
    resolveCatalog(catalog = defaultCatalog) { resolveCatalog(catalog); },
    failCatalog(error = new Error("展馆目录加载失败")) { rejectCatalog(error); },
    async cleanup() {
      globalThis.document = previous.document;
      globalThis.window = previous.window;
      globalThis.__museumAppTestDeps = previous.deps;
      globalThis.__museumAppAdapter = previous.adapter;
      globalThis.__museumSceneDissolve = previous.dissolve;
      globalThis.__museumAppTestInstance = previous.app;
      globalThis.__museumAppTestClass = previous.appClass;
      globalThis.__museumArtworkIdFromLocation = previous.artworkIdFromLocation;
      globalThis.__museumVisitorSession = previous.visitor;
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

test("krpano 子节点阻止冒泡时 panorama capture 仍关闭浮层且保持 passive", async () => {
  const harness = await createHarness();
  try {
    const drawer = harness.document.getElementById("scene-drawer");
    const panel = harness.document.getElementById("view-panel");
    const panorama = harness.document.getElementById("panorama");
    const krpanoChild = new FakeElement("krpano-child", harness.document);
    panorama.appendChild(krpanoChild);
    krpanoChild.addEventListener("pointerdown", (event) => event.stopPropagation());
    krpanoChild.addEventListener("wheel", (event) => event.stopPropagation());

    await harness.document.getElementById("scene-drawer-toggle").dispatch("click");
    assert.equal(drawer.hidden, false);
    await krpanoChild.dispatch("pointerdown");
    assert.equal(drawer.hidden, true);

    await harness.document.getElementById("view-toggle").dispatch("click");
    assert.equal(panel.hidden, false);
    await krpanoChild.dispatch("wheel");
    assert.equal(panel.hidden, true);
    for (const type of ["pointerdown", "wheel"]) {
      const listener = panorama.listeners.get(type).find((entry) => entry.capture);
      assert.equal(listener?.passive, true, `${type} capture listener 应保持 passive`);
    }
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
    assert.equal(modal.classList.contains("is-open"), true);
    assert.equal(modal.getAttribute("aria-hidden"), "false");
  } finally {
    await harness.cleanup();
  }
});

test("所有 hotspot 分支在业务动作前统一关闭临时浮层", async () => {
  const harness = await createHarness();
  try {
    const drawer = harness.document.getElementById("scene-drawer");
    const panorama = harness.document.getElementById("panorama");
    const closeSnapshots = [];
    const switchSnapshots = [];
    harness.app.switchScene = (sceneId) => {
      switchSnapshots.push({ sceneId, drawerHidden: drawer.hidden });
    };
    const hotspots = [
      { type: "artwork", artworkId: "artwork-1" },
      { type: "text", title: "文字展项" },
      { type: "unsupported" },
      { type: "scene", targetSceneId: "scene-2" }
    ];

    for (const hotspot of hotspots) {
      if (!drawer.hidden) await panorama.dispatch("pointerdown");
      await harness.document.getElementById("scene-drawer-toggle").dispatch("click");
      assert.equal(drawer.hidden, false);
      harness.app.handleHotspot(hotspot);
      closeSnapshots.push(drawer.hidden);
    }

    assert.deepEqual(closeSnapshots, [true, true, true, true]);
    assert.deepEqual(switchSnapshots, [{ sceneId: "scene-2", drawerHidden: true }]);
  } finally {
    await harness.cleanup();
  }
});

test("首次加载不叠化，后续场景切换在成功后叠化旧全景快照", async () => {
  const harness = await createHarness();
  try {
    assert.ok(harness.dissolve);
    await harness.app.switchScene("scene-a");
    assert.deepEqual(harness.dissolve.beginCalls, []);
    assert.deepEqual(harness.dissolve.finishCalls, []);

    await harness.app.switchScene("scene-a");
    assert.deepEqual(harness.dissolve.beginCalls, [2]);
    assert.deepEqual(harness.dissolve.finishCalls, [2]);
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

test("共同全屏根包含业务层，打开 modal 只 inert shell 且 modal 保持可见", async () => {
  const harness = await createHarness();
  try {
    const root = harness.document.getElementById("museum-fullscreen-root");
    const shell = harness.document.getElementById("museum-shell");
    const button = harness.document.getElementById("fullscreen-toggle");
    const modal = harness.document.getElementById("description-modal");
    for (const id of ["description-modal", "artwork-modal", "login-modal", "notice", "fatal-error"])
      assert.equal(root.contains(harness.document.getElementById(id)), true, `${id} 应在全屏根内`);

    await button.dispatch("click");
    assert.equal(harness.document.requestedFullscreen, root);

    harness.document.fullscreenElement = root;
    await harness.document.dispatch("fullscreenchange");
    assert.equal(button.classList.contains("is-fullscreen"), true);
    assert.equal(button.getAttribute("aria-label"), "退出全屏");

    await harness.document.getElementById("description-open").dispatch("click");
    assert.equal(shell.inert, true);
    assert.equal(modal.inert, false);
    assert.equal(root.contains(modal), true);
    assert.equal(modal.classList.contains("is-open"), true);
    assert.equal(modal.getAttribute("aria-hidden"), "false");

    harness.document.fullscreenElement = null;
    await harness.document.dispatch("fullscreenchange");
    assert.equal(button.classList.contains("is-fullscreen"), false);
    assert.equal(button.getAttribute("aria-label"), "全屏浏览");
  } finally {
    await harness.cleanup();
  }
});

test("VR 异步拒绝由 click handler await/catch 并保留全景状态", async () => {
  const harness = await createHarness();
  try {
    const panorama = harness.document.getElementById("panorama");
    panorama.dataset.renderState = "loaded";
    harness.adapter.vrAsyncError = new Error("VR 进入超时，请重试");

    await harness.document.getElementById("vr-toggle").dispatch("click");
    assert.match(harness.document.getElementById("notice").textContent, /VR 进入超时，请重试/);
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
    assert.equal(button.getAttribute("aria-label"), "当前场景暂无音乐");
    harness.app.configureMusic({ url: "/audio/guide.mp3" });
    assert.equal(button.disabled, false);
    assert.equal(button.title, "播放讲解");
    assert.equal(button.getAttribute("aria-label"), "播放讲解");
    button.disabled = true;
    await button.dispatch("click");
    assert.equal(audio.paused, true);
    assert.equal(button.classList.contains("is-playing"), false);
    harness.failCatalog();
    await new Promise((resolve) => setImmediate(resolve));
  } finally {
    await harness.cleanup();
  }
});

test("切换到使用同一背景音乐的场景时保留播放进度", async () => {
  const harness = await createHarness();
  try {
    const audio = harness.document.getElementById("scene-audio");
    const button = harness.document.getElementById("music-toggle");
    harness.app.configureMusic({
      url: "/assets/music/exhibition.mp3", volume: 0.05, loop: true, autoplay: false
    });
    audio.currentTime = 37;
    await audio.play();
    harness.app.configureMusic({
      url: "/assets/music/exhibition.mp3", volume: 0.05, loop: true, autoplay: true
    });

    assert.equal(audio.src, "/assets/music/exhibition.mp3");
    assert.equal(audio.currentTime, 37);
    assert.equal(audio.paused, false);
    assert.equal(button.disabled, false);
    assert.equal(button.title, "暂停讲解");
  } finally {
    await harness.cleanup();
  }
});

test("自动播放被拦截后在首次页面交互时续播背景音乐", async () => {
  const harness = await createHarness();
  try {
    const audio = harness.document.getElementById("scene-audio");
    const button = harness.document.getElementById("music-toggle");
    audio.playError = new Error("NotAllowedError");
    harness.app.configureMusic({
      url: "/assets/music/exhibition.mp3", volume: 0.05, loop: true, autoplay: true
    });
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(audio.paused, true);

    audio.playError = null;
    await harness.document.dispatch("pointerdown");

    assert.equal(audio.paused, false);
    assert.equal(button.title, "暂停讲解");
  } finally {
    await harness.cleanup();
  }
});

test("museum-app 按媒体查询结果显式注入 reducedMotion", async () => {
  const harness = await createHarness({ reducedMotion: true });
  try {
    assert.deepEqual(harness.window.matchMediaQueries, ["(prefers-reduced-motion: reduce)"]);
    assert.equal(harness.adapter.options.reducedMotion, true);
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

test("artworkIdFromLocation 对缺失、空白及畸形地址安全返回 null", async () => {
  const harness = await createHarness();
  try {
    assert.equal(typeof harness.artworkIdFromLocation, "function");
    assert.equal(harness.artworkIdFromLocation(), null);
    assert.equal(harness.artworkIdFromLocation({ href: "https://example.test/" }), null);
    assert.equal(harness.artworkIdFromLocation({ href: "https://example.test/?artwork=   " }), null);
    assert.equal(harness.artworkIdFromLocation({ href: "not a valid URL" }), null);
    assert.equal(
      harness.artworkIdFromLocation({ href: "https://example.test/?artwork=work-a" }),
      "work-a"
    );
  } finally {
    await harness.cleanup();
  }
});

test("启动完成后只对非空 artwork 参数打开一次作品", async () => {
  const harness = await createHarness();
  try {
    const opened = [];
    const artworkModal = { open(artworkId) { opened.push(artworkId); } };
    const app = new harness.MuseumApp({
      artworkModal,
      locationObject: { href: "https://example.test/?artwork=work-a" }
    });
    harness.resolveCatalog();

    await app.bootstrap();

    assert.deepEqual(opened, ["work-a"]);
  } finally {
    await harness.cleanup();
  }
});

test("作品弹窗拒绝不阻断默认场景、访客会话和心跳", async () => {
  const harness = await createHarness();
  try {
    const opened = [];
    const artworkModal = {
      open(artworkId) {
        opened.push(artworkId);
        return Promise.reject(new Error("作品详情暂不可用"));
      }
    };
    const app = new harness.MuseumApp({
      artworkModal,
      locationObject: { href: "https://example.test/?artwork=work-a" }
    });
    harness.resolveCatalog();
    await new Promise((resolve) => setImmediate(resolve));
    const heartbeatBefore = harness.visitor.heartbeatCalls;

    await app.bootstrap();
    await new Promise((resolve) => setImmediate(resolve));

    assert.deepEqual(opened, ["work-a"]);
    assert.equal(app.currentScene.sceneId, "scene-a");
    assert.equal(harness.document.getElementById("scene-loading").hidden, true);
    assert.equal(harness.visitor.heartbeatCalls, heartbeatBefore + 1);
  } finally {
    await harness.cleanup();
  }
});

test("作品弹窗同步抛错不阻断默认场景、访客会话和心跳", async () => {
  const harness = await createHarness();
  try {
    const opened = [];
    const artworkModal = {
      open(artworkId) {
        opened.push(artworkId);
        throw new Error("作品详情暂不可用");
      }
    };
    const app = new harness.MuseumApp({
      artworkModal,
      locationObject: { href: "https://example.test/?artwork=work-a" }
    });
    harness.resolveCatalog();
    await new Promise((resolve) => setImmediate(resolve));
    const heartbeatBefore = harness.visitor.heartbeatCalls;

    await app.bootstrap();

    assert.deepEqual(opened, ["work-a"]);
    assert.equal(app.currentScene.sceneId, "scene-a");
    assert.equal(harness.document.getElementById("scene-loading").hidden, true);
    assert.equal(harness.document.getElementById("fatal-error").hidden, true);
    assert.equal(harness.visitor.heartbeatCalls, heartbeatBefore + 1);
  } finally {
    await harness.cleanup();
  }
});
