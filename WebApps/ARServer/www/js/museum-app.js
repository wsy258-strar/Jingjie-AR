// 展馆页面协调层：目录、访客统计、场景竞态、登录和作品交互在此汇合。
import { ApiClient, ApiError } from "./api-client.js";
import { AuthSession } from "./auth-session.js";
import { VisitorSession } from "./visitor-session.js";
import { KrpanoAdapter } from "./krpano-adapter.js";
import { ArtworkModal } from "./artwork-modal.js";
import { GyroController } from "./gyro-controller.js";
import { MuseumLifecycle } from "./museum-lifecycle.js";
import { ModalFocusManager } from "./modal-focus.js";
import { MuseumUiState } from "./museum-ui-state.js";
import { SceneDissolve } from "./scene-dissolve.js";
import { FullscreenOrientation } from "./fullscreen-orientation.js";
import { AudioControlState } from "./audio-control-state.js";

const api = new ApiClient();
let loginWaiter = null;
let noticeTimer = null;
let landscapeHintTimer = null;
let gyroController = null;
let transientUiSuspended = false;

function element(id) {
  return document.getElementById(id);
}

function notify(message) {
  const notice = element("notice");
  notice.textContent = message;
  notice.hidden = false;
  window.clearTimeout(noticeTimer);
  noticeTimer = window.setTimeout(() => { notice.hidden = true; }, 4500);
}

function showLandscapeHint() {
  const hint = element("landscape-hint");
  hint.hidden = false;
  window.clearTimeout(landscapeHintTimer);
  landscapeHintTimer = window.setTimeout(() => { hint.hidden = true; }, 2500);
}

function suspendGyroForModal() {
  gyroController?.suspend("modal");
}

function resumeGyroAfterModal() {
  if (!modalManager.stack.length) gyroController?.resume("modal");
}

function openLogin() {
  if (loginWaiter) return loginWaiter.promise;
  suspendGyroForModal();
  uiState.closeTransientLayers();
  const modal = element("login-modal");
  element("login-message").textContent = "";
  let resolveWaiter;
  let rejectWaiter;
  const promise = new Promise((resolve, reject) => {
    resolveWaiter = resolve;
    rejectWaiter = reject;
  });
  loginWaiter = { promise, resolve: resolveWaiter, reject: rejectWaiter };
  modalManager.open(modal, {
    initialFocus: element("login-username"),
    onEscape: () => closeLogin(true)
  });
  return promise;
}

function closeLogin(cancelled = true) {
  if (cancelled && loginWaiter) {
    const error = new ApiError(0, "LOGIN_CANCELLED", "已取消登录", "");
    loginWaiter.reject(error);
  }
  loginWaiter = null;
  modalManager.close(element("login-modal"));
  resumeGyroAfterModal();
}

const auth = new AuthSession({ client: api, onAuthenticationRequired: openLogin });
const visitor = new VisitorSession({ client: api });
const lifecycle = new MuseumLifecycle({ visitor, refreshCounters: () => app.loadCounters() });
const modalManager = new ModalFocusManager();
const gyroModalManager = {
  open(...args) {
    suspendGyroForModal();
    return modalManager.open(...args);
  },
  close(...args) {
    const closed = modalManager.close(...args);
    resumeGyroAfterModal();
    return closed;
  }
};
const artworkModal = new ArtworkModal({ api, auth, modalManager: gyroModalManager, notify });

function renderUiState(state) {
  const drawer = element("scene-drawer");
  const drawerToggle = element("scene-drawer-toggle");
  drawer.hidden = !state.sceneDrawerOpen;
  drawer.classList.toggle("is-open", state.sceneDrawerOpen);
  drawerToggle.setAttribute("aria-expanded", String(state.sceneDrawerOpen));

  const viewPanel = element("view-panel");
  const viewToggle = element("view-toggle");
  viewPanel.hidden = !state.viewPanelOpen;
  viewToggle.classList.toggle("is-active", state.viewPanelOpen);
  viewToggle.setAttribute("aria-expanded", String(state.viewPanelOpen));

  document.querySelectorAll("[data-view-mode]").forEach((button) => {
    button.setAttribute("aria-pressed", String(button.dataset.viewMode === state.viewMode));
  });

  const hasTransientLayer = uiState.hasTransientLayer();
  if (hasTransientLayer && !transientUiSuspended) {
    gyroController?.suspend("transient-ui");
    transientUiSuspended = true;
  } else if (!hasTransientLayer && transientUiSuspended) {
    gyroController?.resume("transient-ui");
    transientUiSuspended = false;
  }
}

export function artworkIdFromLocation(locationObject) {
  try {
    const href = locationObject?.href;
    if (typeof href !== "string") return null;
    const value = new URL(href).searchParams.get("artwork");
    return value && value.trim() ? value : null;
  } catch (_) {
    return null;
  }
}

const uiState = new MuseumUiState({ onChange: renderUiState });

export class MuseumApp {
  constructor({ artworkModal: injectedArtworkModal = artworkModal, locationObject = window.location } = {}) {
    this.catalog = null;
    this.currentScene = null;
    this.sceneGeneration = 0;
    this.sceneController = null;
    this.artworkModal = injectedArtworkModal;
    this.locationObject = locationObject;
    this.document = document;
    this.musicUrl = "";
    this.musicAutoplayRetry = null;
    this.musicControl = new AudioControlState({
      audio: element("scene-audio"),
      button: element("music-toggle")
    });
    this.gyroAutoEnableRequested = false;
    this.gyroGestureRequested = false;
    this.sceneDissolve = new SceneDissolve({
      viewer: element("panorama"),
      overlay: element("scene-dissolve")
    });
    const reducedMotion = typeof window.matchMedia === "function" &&
      window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    this.adapter = new KrpanoAdapter({
      targetId: "panorama",
      onHotspot: (hotspot) => this.handleHotspot(hotspot),
      onVrStateChange: (state) => {
        element("museum-fullscreen-root").classList.toggle("is-vr-mode", state === "entered");
      },
      onSceneEvent: ({ type, generation }) => {
        if (type === "preview-visible") this.sceneDissolve.markPreviewVisible(generation);
        else if (type === "complete") this.sceneDissolve.complete(generation);
      },
      reducedMotion
    });
    this.gyro = new GyroController({
      adapter: this.adapter,
      onDenied: () => notify("未能启用陀螺仪，仍可拖动浏览")
    });
    gyroController = this.gyro;
  }

  autoEnableGyroAfterSceneLoad() {
    if (this.gyroAutoEnableRequested) return;
    this.gyroAutoEnableRequested = true;
    Promise.resolve(this.gyro.autoEnable()).catch(() => {});
  }

  async requestGyroFromGesture() {
    if (this.gyroGestureRequested) return false;
    try {
      const enabled = await this.gyro.requestFromGesture();
      if (enabled) this.gyroGestureRequested = true;
      return enabled;
    } catch (_) {
      return false;
    }
  }

  async bootstrap() {
    element("fatal-error").hidden = true;
    try {
      const [visitorResult] = await Promise.allSettled([
        lifecycle.bootstrapVisitorOnce(), this.loadCounters()
      ]);
      if (visitorResult.status === "fulfilled" && visitorResult.value &&
          visitorResult.value.totalViews !== null) {
        element("total-views").textContent = String(visitorResult.value.totalViews);
      }
      const catalog = await api.request("/api/scenes");
      this.catalog = catalog;
      this.renderExhibition(catalog);
      const defaultSceneLoaded = await this.switchScene(catalog.defaultSceneId);
      if (defaultSceneLoaded) this.openSharedArtwork();
      visitor.startHeartbeat();
      lifecycle.startCounterPolling();
    } catch (error) {
      if (error && error.name === "AbortError") return;
      element("scene-loading").hidden = true;
      element("fatal-error").hidden = false;
      notify(error.message || "展馆目录暂时无法加载");
    }
  }

  renderExhibition(catalog) {
    element("museum-title").textContent = catalog.title || "数字展馆";
    element("museum-description").textContent = catalog.remark || "暂无展馆简介。";
    document.title = catalog.title || "数字展馆";
    const scenes = Array.isArray(catalog.scenes) ? catalog.scenes : [];
    const container = element("scene-catalog");
    container.textContent = "";
    for (const scene of scenes) {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "scene-card";
      button.dataset.sceneId = scene.sceneId;
      const image = document.createElement("img");
      image.src = scene.thumbnailUrl;
      image.alt = "";
      image.loading = "lazy";
      const label = document.createElement("span");
      label.textContent = scene.name;
      button.append(image, label);
      button.addEventListener("click", () => {
        uiState.closeTransientLayers();
        this.switchScene(scene.sceneId);
      });
      container.appendChild(button);
    }
  }

  async switchScene(sceneId) {
    const generation = ++this.sceneGeneration;
    this.adapter.invalidate(generation);
    if (this.sceneController) this.sceneController.abort();
    const controller = new AbortController();
    this.sceneController = controller;
    element("scene-loading").hidden = false;

    try {
      const scene = await api.request(`/api/scenes/${encodeURIComponent(sceneId)}`, {
        signal: controller.signal
      });
      if (generation !== this.sceneGeneration) {
        this.sceneDissolve.cancel(generation);
        return false;
      }
      this.sceneDissolve.begin({ generation, fallbackUrl: scene.previewUrl });
      const loaded = await this.adapter.loadScene(scene, generation);
      if (!loaded || generation !== this.sceneGeneration) {
        this.sceneDissolve.cancel(generation);
        return false;
      }
      this.autoEnableGyroAfterSceneLoad();
      this.currentScene = scene;
      this.markCurrentScene(scene.sceneId);
      this.configureMusic(scene.music);
      element("scene-loading").hidden = true;
      return true;
    } catch (error) {
      if (error && error.name === "AbortError") {
        this.sceneDissolve.cancel(generation);
        return false;
      }
      if (generation !== this.sceneGeneration) {
        this.sceneDissolve.cancel(generation);
        return false;
      }
      this.sceneDissolve.cancel(generation);
      element("scene-loading").hidden = true;
      notify(error.message || "场景加载失败，已保留当前画面");
      return false;
    }
  }

  openSharedArtwork() {
    const sharedArtworkId = artworkIdFromLocation(this.locationObject);
    if (!sharedArtworkId) return;
    try {
      Promise.resolve(this.artworkModal.open(sharedArtworkId)).catch(() => {});
    } catch (_) {}
  }

  markCurrentScene(sceneId) {
    document.querySelectorAll("#scene-catalog .scene-card").forEach((button) => {
      const current = button.dataset.sceneId === sceneId;
      button.classList.toggle("is-current", current);
      if (current) button.setAttribute("aria-current", "true");
      else button.removeAttribute("aria-current");
    });
  }

  handleHotspot(hotspot) {
    if (hotspot.type === "artwork" || hotspot.type === "text") suspendGyroForModal();
    uiState.closeTransientLayers();
    if (hotspot.type === "scene" && hotspot.targetSceneId) {
      this.switchScene(hotspot.targetSceneId);
    } else if (hotspot.type === "artwork" && hotspot.artworkId) {
      this.artworkModal.open(hotspot.artworkId);
    } else if (hotspot.type === "text") {
      this.artworkModal.openText(hotspot);
    } else {
      notify("该展项暂不支持打开");
    }
  }

  configureMusic(music = {}) {
    const audio = element("scene-audio");
    const musicUrl = typeof music.url === "string" ? music.url : "";
    if (musicUrl && musicUrl === this.musicUrl && audio.src) {
      audio.volume = Math.max(0, Math.min(1, Number(music.volume) || 1));
      audio.loop = Boolean(music.loop);
      this.musicControl.sync();
      return;
    }
    this.clearMusicAutoplayRetry();
    audio.pause();
    audio.removeAttribute("src");
    this.musicControl.setUnavailable();
    this.musicUrl = musicUrl;
    if (!musicUrl) return;
    audio.src = musicUrl;
    audio.volume = Math.max(0, Math.min(1, Number(music.volume) || 1));
    audio.loop = Boolean(music.loop);
    this.musicControl.sync();
    if (music.autoplay) this.playMusic(audio, true);
  }

  clearMusicAutoplayRetry() {
    if (!this.musicAutoplayRetry) return;
    this.document.removeEventListener?.("pointerdown", this.musicAutoplayRetry, true);
    this.document.removeEventListener?.("keydown", this.musicAutoplayRetry, true);
    this.musicAutoplayRetry = null;
  }

  playMusic(audio, retryAfterGesture) {
    return Promise.resolve().then(() => audio.play()).then(() => {
      this.clearMusicAutoplayRetry();
      this.musicControl.sync();
    }).catch(() => {
      this.musicControl.sync();
      if (retryAfterGesture) this.armMusicAutoplayRetry(audio);
    });
  }

  armMusicAutoplayRetry(audio) {
    if (this.musicAutoplayRetry) return;
    const retry = () => {
      if (this.musicAutoplayRetry !== retry) return;
      this.clearMusicAutoplayRetry();
      if (audio.src) return this.playMusic(audio, false);
    };
    this.musicAutoplayRetry = retry;
    this.document.addEventListener("pointerdown", retry, { capture: true });
    this.document.addEventListener("keydown", retry, { capture: true });
  }

  async loadCounters() {
    const [views, presence] = await Promise.allSettled([
      api.request("/api/statistics/views"),
      api.request("/api/presence")
    ]);
    if (views.status === "fulfilled" && views.value.statisticsAvailable &&
        views.value.totalViews !== null) {
      element("total-views").textContent = String(views.value.totalViews);
    }
    if (presence.status === "fulfilled") {
      element("online-count").textContent = String(presence.value.onlineCount);
    }
  }

}

const app = new MuseumApp();

element("description-open").addEventListener("click", () => {
  suspendGyroForModal();
  uiState.closeTransientLayers();
  const modal = element("description-modal");
  modalManager.open(modal, {
    initialFocus: modal.querySelector(".modal-card"),
    onEscape: () => {
      modalManager.close(modal);
      resumeGyroAfterModal();
    }
  });
});

element("scene-drawer-toggle").addEventListener("click", (event) => {
  event.stopPropagation();
  uiState.toggleSceneDrawer();
});

element("view-toggle").addEventListener("click", (event) => {
  event.stopPropagation();
  uiState.toggleViewPanel();
});

element("scene-drawer").addEventListener("click", (event) => event.stopPropagation());
element("view-panel").addEventListener("click", (event) => event.stopPropagation());

document.querySelectorAll("[data-view-mode]").forEach((button) => {
  button.addEventListener("click", () => {
    const mode = button.dataset.viewMode;
    try {
      app.adapter.setViewMode(mode);
      uiState.selectViewMode(mode);
    } catch (error) {
      notify(error.message || "视角切换失败");
    }
  });
});

for (const type of ["pointerdown", "wheel"]) {
  element("panorama").addEventListener(type, () => {
    uiState.closeTransientLayers();
    if (type === "pointerdown") return app.requestGyroFromGesture();
  }, {
    capture: true,
    passive: true
  });
}

document.addEventListener("click", () => uiState.closeTransientLayers());
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape") uiState.closeTransientLayers();
});

document.querySelectorAll('[data-close="description"]').forEach((button) => {
  button.addEventListener("click", () => {
    modalManager.close(element("description-modal"));
    resumeGyroAfterModal();
  });
});

element("login-open").addEventListener("click", async () => {
  if (auth.token()) {
    try {
      await auth.logout();
      element("login-open").textContent = "注册 / 登录";
      notify("已退出登录");
    } catch (error) {
      notify(error.message || "服务端会话撤销失败，请检查网络后重试");
    }
  } else {
    openLogin().catch(() => {});
  }
});

document.querySelectorAll("[data-login-close]").forEach((button) => {
  button.addEventListener("click", () => closeLogin(true));
});

element("login-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const username = element("login-username").value.trim();
  const password = element("login-password").value;
  const submit = element("login-submit");
  const message = element("login-message");
  message.textContent = "";
  submit.disabled = true;
  try {
    const result = await auth.authenticate(username, password);
    const waiter = loginWaiter;
    element("login-open").textContent = `${result.username} · 退出`;
    closeLogin(false);
    if (waiter) waiter.resolve(result.token);
    notify(result.isNew ? "注册并登录成功" : "登录成功");
  } catch (error) {
    message.textContent = error.message || "登录失败，请稍后重试";
  } finally {
    submit.disabled = false;
  }
});

element("music-toggle").addEventListener("click", async () => {
  const audio = element("scene-audio");
  if (!audio.src) return;
  if (audio.paused) {
    try {
      await audio.play();
      app.clearMusicAutoplayRetry();
      app.musicControl.sync();
    } catch (error) {
      if (error && error.name === "AbortError") return;
      notify("浏览器未允许播放音频，请再次尝试");
    }
  } else {
    audio.pause();
    app.musicControl.sync();
  }
});

const fullscreenOrientation = new FullscreenOrientation({
  documentObject: document,
  screenObject: globalThis.screen,
  onLandscapeFallback: showLandscapeHint
});

element("fullscreen-toggle").addEventListener("click", async () => {
  const target = element("museum-fullscreen-root");
  if (!await fullscreenOrientation.toggle(target)) notify("当前浏览器无法进入全屏模式");
});

document.addEventListener("fullscreenchange", () => {
  const active = fullscreenOrientation.handleFullscreenChange(element("museum-fullscreen-root"));
  const button = element("fullscreen-toggle");
  button.classList.toggle("is-fullscreen", active);
  button.setAttribute("aria-label", active ? "退出全屏" : "全屏浏览");
  button.title = button.getAttribute("aria-label");
});

element("vr-toggle").addEventListener("click", async () => {
  try {
    await app.adapter.enterVr();
  } catch (error) {
    notify(error.message || "当前设备或浏览器无法进入 VR");
  }
});

element("retry-bootstrap").addEventListener("click", () => app.bootstrap());

window.addEventListener("pagehide", () => {
  if (app.sceneController) app.sceneController.abort();
  app.gyro.destroy();
  app.musicControl.destroy();
});

if (auth.token()) element("login-open").textContent = "已登录 · 退出";
app.bootstrap();
