// 展馆页面协调层：目录、访客统计、场景竞态、登录和作品交互在此汇合。
import { ApiClient, ApiError } from "./api-client.js";
import { AuthSession } from "./auth-session.js";
import { VisitorSession } from "./visitor-session.js";
import { KrpanoAdapter } from "./krpano-adapter.js";
import { ArtworkModal } from "./artwork-modal.js";
import { MuseumLifecycle } from "./museum-lifecycle.js";
import { ModalFocusManager } from "./modal-focus.js";

const api = new ApiClient();
let loginWaiter = null;
let noticeTimer = null;

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

function openLogin() {
  if (loginWaiter) return loginWaiter.promise;
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
}

const auth = new AuthSession({ client: api, onAuthenticationRequired: openLogin });
const visitor = new VisitorSession({ client: api });
const lifecycle = new MuseumLifecycle({ visitor });
const modalManager = new ModalFocusManager();
const artworkModal = new ArtworkModal({ api, auth, modalManager, notify });

class MuseumApp {
  constructor() {
    this.catalog = null;
    this.currentScene = null;
    this.sceneGeneration = 0;
    this.sceneController = null;
    this.counterTimer = null;
    this.adapter = new KrpanoAdapter({
      targetId: "panorama",
      onHotspot: (hotspot) => this.handleHotspot(hotspot)
    });
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
      await this.switchScene(catalog.defaultSceneId);
      visitor.startHeartbeat();
      this.startCounterPolling();
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
    element("scene-count").textContent = `${scenes.length} 个场景`;
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
      button.addEventListener("click", () => this.switchScene(scene.sceneId));
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
      if (generation !== this.sceneGeneration) return;
      const loaded = await this.adapter.loadScene(scene, generation);
      if (!loaded || generation !== this.sceneGeneration) return;
      this.currentScene = scene;
      element("scene-title").textContent = scene.name;
      this.markCurrentScene(scene.sceneId);
      this.configureMusic(scene.music);
      element("scene-loading").hidden = true;
    } catch (error) {
      if (error && error.name === "AbortError") return;
      if (generation !== this.sceneGeneration) return;
      element("scene-loading").hidden = true;
      notify(error.message || "场景加载失败，已保留当前画面");
    }
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
    if (hotspot.type === "scene" && hotspot.targetSceneId) {
      this.switchScene(hotspot.targetSceneId);
    } else if (hotspot.type === "artwork" && hotspot.artworkId) {
      artworkModal.open(hotspot.artworkId);
    } else if (hotspot.type === "text") {
      artworkModal.openText(hotspot);
    } else {
      notify("该展项暂不支持打开");
    }
  }

  configureMusic(music = {}) {
    const audio = element("scene-audio");
    const button = element("music-toggle");
    audio.pause();
    audio.removeAttribute("src");
    button.textContent = "暂无音乐";
    button.disabled = true;
    if (!music.url) return;
    audio.src = music.url;
    audio.volume = Math.max(0, Math.min(1, Number(music.volume) || 1));
    audio.loop = Boolean(music.loop);
    button.disabled = false;
    button.textContent = "播放讲解";
    if (music.autoplay) {
      audio.play().then(() => { button.textContent = "暂停讲解"; }).catch(() => {});
    }
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

  startCounterPolling() {
    if (this.counterTimer) return;
    this.counterTimer = window.setInterval(() => this.loadCounters(), 15000);
  }
}

const app = new MuseumApp();

element("description-open").addEventListener("click", () => {
  const modal = element("description-modal");
  modalManager.open(modal, {
    initialFocus: modal.querySelector(".modal-card"),
    onEscape: () => modalManager.close(modal)
  });
});

document.querySelectorAll('[data-close="description"]').forEach((button) => {
  button.addEventListener("click", () => {
    modalManager.close(element("description-modal"));
  });
});

element("login-open").addEventListener("click", () => {
  if (auth.token()) {
    auth.clear();
    element("login-open").textContent = "注册 / 登录";
    notify("已退出登录");
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
  const button = element("music-toggle");
  if (!audio.src) return;
  if (audio.paused) {
    try {
      await audio.play();
      button.textContent = "暂停讲解";
    } catch (_) {
      notify("浏览器未允许播放音频，请再次尝试");
    }
  } else {
    audio.pause();
    button.textContent = "播放讲解";
  }
});

element("scene-audio").addEventListener("ended", () => {
  element("music-toggle").textContent = "播放讲解";
});

element("fullscreen-toggle").addEventListener("click", async () => {
  const target = element("panorama");
  try {
    if (!document.fullscreenElement) await target.requestFullscreen();
    else await document.exitFullscreen();
  } catch (_) {
    notify("当前浏览器无法进入全屏模式");
  }
});

element("retry-bootstrap").addEventListener("click", () => app.bootstrap());

window.addEventListener("pagehide", () => {
  if (app.sceneController) app.sceneController.abort();
  if (app.counterTimer) window.clearInterval(app.counterTimer);
});

if (auth.token()) element("login-open").textContent = "已登录 · 退出";
app.bootstrap();
