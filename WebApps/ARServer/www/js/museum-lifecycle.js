// 页面生命周期内复用访客初始化 Promise，并管理不影响浏览计数的页面展示轮询。
const COUNTER_INTERVAL_MS = 15 * 1000;

function browserEventTarget() {
  return typeof globalThis.addEventListener === "function" ? globalThis : null;
}

export class MuseumLifecycle {
  constructor({
    visitor,
    refreshCounters = null,
    eventTarget = browserEventTarget(),
    setIntervalImpl = globalThis.setInterval,
    clearIntervalImpl = globalThis.clearInterval
  } = {}) {
    if (!visitor) throw new Error("MuseumLifecycle requires visitor session");
    this.visitor = visitor;
    this.refreshCounters = refreshCounters;
    this.eventTarget = eventTarget;
    this.setIntervalImpl = setIntervalImpl;
    this.clearIntervalImpl = clearIntervalImpl;
    this.visitorBootstrapPromise = null;
    this.counterTimer = null;
    this.counterEventsBound = false;
    this.pageGeneration = 0;
    this.onPagehide = this.onPagehide.bind(this);
    this.onPageshow = this.onPageshow.bind(this);
  }

  bootstrapVisitorOnce() {
    if (!this.visitorBootstrapPromise) {
      this.visitorBootstrapPromise = Promise.resolve().then(() => this.visitor.bootstrap());
    }
    return this.visitorBootstrapPromise;
  }

  startCounterPolling() {
    if (this.counterTimer === null && typeof this.setIntervalImpl === "function") {
      this.counterTimer = this.setIntervalImpl(
        () => { if (this.refreshCounters) this.refreshCounters(); },
        COUNTER_INTERVAL_MS
      );
    }
    if (this.eventTarget && !this.counterEventsBound) {
      this.eventTarget.addEventListener("pagehide", this.onPagehide);
      this.eventTarget.addEventListener("pageshow", this.onPageshow);
      this.counterEventsBound = true;
    }
  }

  stopCounterPolling() {
    if (this.counterTimer === null) return;
    this.clearIntervalImpl(this.counterTimer);
    this.counterTimer = null;
  }

  onPagehide() {
    this.pageGeneration += 1;
    this.stopCounterPolling();
  }

  async onPageshow(event) {
    if (!event || event.persisted !== true) return;
    const generation = this.pageGeneration;
    await Promise.resolve();
    if (typeof this.visitor.waitForPageRestore === "function") {
      await this.visitor.waitForPageRestore();
    }
    if (generation !== this.pageGeneration) return;
    if (this.refreshCounters) await this.refreshCounters();
    if (generation !== this.pageGeneration) return;
    this.startCounterPolling();
  }
}
