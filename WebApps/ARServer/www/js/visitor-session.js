// 匿名访客会话只负责展馆级在线状态；故障时通过公开状态降级而不阻断场景浏览。
import { ApiError, VISITOR_TOKEN_KEY } from "./api-client.js";

const HEARTBEAT_INTERVAL_MS = 30 * 1000;

function browserEventTarget() {
  return typeof globalThis.addEventListener === "function" ? globalThis : null;
}

function browserRandomUUID() {
  if (!globalThis.crypto || typeof globalThis.crypto.randomUUID !== "function") {
    throw new Error("crypto.randomUUID is unavailable");
  }
  return globalThis.crypto.randomUUID();
}

function defaultRetryDelay() {
  return Promise.resolve();
}

export class VisitorSession {
  constructor({
    client,
    storage = typeof globalThis.sessionStorage === "undefined" ? null : globalThis.sessionStorage,
    randomUUID = browserRandomUUID,
    eventTarget = browserEventTarget(),
    setIntervalImpl = globalThis.setInterval,
    clearIntervalImpl = globalThis.clearInterval,
    retryDelay = defaultRetryDelay
  } = {}) {
    if (!client) throw new Error("VisitorSession requires an ApiClient");
    this.client = client;
    this.storage = storage;
    this.randomUUID = randomUUID;
    this.eventTarget = eventTarget;
    this.setIntervalImpl = setIntervalImpl;
    this.clearIntervalImpl = clearIntervalImpl;
    this.retryDelay = retryDelay;
    this.available = false;
    this.unavailable = false;
    this.bootstrapRequestId = "";
    this.bootstrapResult = null;
    this.heartbeatTimer = null;
    this.recoveryPromise = null;
    this.exitPromise = Promise.resolve();
    this.pageRestorePromise = Promise.resolve(false);
    this.pageGeneration = 0;
    this.pageVisible = true;
    this.statisticsPending = false;
    this.lastError = null;
    this.pagehideBound = false;
    this.onPagehide = this.onPagehide.bind(this);
    this.onPageshow = this.onPageshow.bind(this);
  }

  async bootstrap() {
    let requestId = "";
    try {
      requestId = this.randomUUID();
      this.bootstrapRequestId = requestId;
      const result = await this.requestBootstrap(requestId);
      return this.acceptBootstrap(result);
    } catch (error) {
      if (requestId && this.shouldRetry(error)) {
        try {
          await this.retryDelay();
          return this.acceptBootstrap(await this.requestBootstrap(requestId));
        } catch (retryError) {
          return this.markUnavailable(retryError);
        }
      }
      return this.markUnavailable(error);
    }
  }

  requestBootstrap(bootstrapRequestId) {
    return this.client.request("/api/visitors/session", {
      method: "POST",
      body: { bootstrapRequestId },
      visitor: true
    });
  }

  acceptBootstrap(result) {
    if (!result || typeof result.visitorToken !== "string" || !result.visitorToken) {
      return this.markUnavailable(new Error("visitor token is missing"));
    }
    if (this.storage) this.storage.setItem(VISITOR_TOKEN_KEY, result.visitorToken);
    this.available = true;
    this.unavailable = false;
    this.bootstrapResult = result;
    this.statisticsPending = result.statisticsAvailable === false;
    this.lastError = null;
    return result;
  }

  markUnavailable(error) {
    this.available = false;
    this.unavailable = true;
    this.bootstrapResult = null;
    this.lastError = error;
    return null;
  }

  shouldRetry(error) {
    return !(error instanceof ApiError) && !(error && error.name === "AbortError");
  }

  recover({ markFailureUnavailable = true } = {}) {
    if (this.recoveryPromise) return this.recoveryPromise;
    if (!this.bootstrapRequestId) return Promise.resolve(false);
    const requestId = this.bootstrapRequestId;
    this.recoveryPromise = Promise.resolve()
      .then(() => this.requestBootstrap(requestId))
      .then((result) => Boolean(this.acceptBootstrap(result)))
      .catch((error) => {
        if (markFailureUnavailable) this.markUnavailable(error);
        else this.lastError = error;
        return false;
      })
      .finally(() => { this.recoveryPromise = null; });
    return this.recoveryPromise;
  }

  restoreAfterExit() {
    if (!this.bootstrapRequestId) return Promise.resolve(false);
    return Promise.resolve()
      .then(() => this.requestBootstrap(this.bootstrapRequestId))
      .then((result) => Boolean(this.acceptBootstrap(result)))
      .catch((error) => {
        this.lastError = error;
        return false;
      });
  }

  async heartbeat() {
    try {
      await this.client.request("/api/presence/heartbeat", { method: "POST", visitor: true });
      this.available = true;
      this.unavailable = false;
      this.lastError = null;
      if (this.statisticsPending)
        return this.recover({ markFailureUnavailable: false });
      return true;
    } catch (error) {
      this.available = false;
      this.unavailable = true;
      this.lastError = error;
      if (error instanceof ApiError && error.status === 401)
        return this.recover();
      return false;
    }
  }

  startHeartbeat() {
    if (this.heartbeatTimer !== null) return;
    if (typeof this.setIntervalImpl !== "function") return;
    this.heartbeatTimer = this.setIntervalImpl(() => { this.heartbeat(); }, HEARTBEAT_INTERVAL_MS);
    if (this.eventTarget && !this.pagehideBound) {
      this.eventTarget.addEventListener("pagehide", this.onPagehide);
      this.eventTarget.addEventListener("pageshow", this.onPageshow);
      this.pagehideBound = true;
    }
  }

  stopHeartbeat({ sendExit = true } = {}) {
    if (this.heartbeatTimer !== null) {
      this.clearIntervalImpl(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
    if (sendExit && this.storage && this.storage.getItem(VISITOR_TOKEN_KEY)) {
      this.exitPromise = this.client.request("/api/presence/exit", {
        method: "POST", visitor: true, keepalive: true
      }).catch(() => {});
    }
  }

  onPagehide() {
    this.pageGeneration += 1;
    this.pageVisible = false;
    this.stopHeartbeat({ sendExit: true });
  }

  onPageshow(event) {
    if (!event || event.persisted !== true) return Promise.resolve(false);
    const generation = this.pageGeneration;
    this.pageVisible = true;
    this.pageRestorePromise = Promise.resolve(this.exitPromise)
      .then(() => this.restoreAfterExit())
      .then(() => {
        if (generation !== this.pageGeneration) {
          if (!this.pageVisible) this.stopHeartbeat({ sendExit: true });
          return false;
        }
        this.startHeartbeat();
        return true;
      });
    return this.pageRestorePromise;
  }

  waitForPageRestore() {
    return this.pageRestorePromise;
  }
}
