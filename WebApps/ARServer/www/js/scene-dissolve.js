// 场景叠化控制器：旧画面先释放到保底透明度，预览可见后完成交叉淡入。

export const DEFAULT_DURATION_MS = 1500;
export const RELEASE_MS = 900;
export const COMPLETE_MIN_MS = 300;

function positiveDuration(value, fallback) {
  const duration = Number(value);
  return Number.isFinite(duration) && duration > 0 ? duration : fallback;
}

export class SceneDissolve {
  constructor({
    viewer,
    overlay,
    durationMs = DEFAULT_DURATION_MS,
    releaseMs = RELEASE_MS,
    completeMinMs = COMPLETE_MIN_MS,
    nowFn = () => Date.now(),
    setTimeoutFn = (callback, delay) => globalThis.setTimeout(callback, delay),
    clearTimeoutFn = (timerId) => globalThis.clearTimeout(timerId)
  } = {}) {
    this.viewer = viewer;
    this.overlay = overlay;
    this.durationMs = positiveDuration(durationMs, DEFAULT_DURATION_MS);
    this.releaseMs = positiveDuration(releaseMs, RELEASE_MS);
    this.completeMinMs = positiveDuration(completeMinMs, COMPLETE_MIN_MS);
    this.nowFn = nowFn;
    this.setTimeoutFn = setTimeoutFn;
    this.clearTimeoutFn = clearTimeoutFn;
    this.activeGeneration = null;
    this.startedAt = null;
    this.releaseTimer = null;
    this.clearTimer = null;
  }

  begin({ generation, fallbackUrl = "" } = {}) {
    const requestedGeneration = Number(generation);
    if (!this.overlay || !Number.isFinite(requestedGeneration)) return false;
    this.clear();

    let snapshot = "";
    const canvas = this.viewer?.querySelector?.("canvas");
    if (canvas && typeof canvas.toDataURL === "function") {
      try {
        snapshot = canvas.toDataURL("image/jpeg", 0.82);
      } catch (_) {}
    }
    if (typeof snapshot !== "string" || !snapshot)
      snapshot = typeof fallbackUrl === "string" ? fallbackUrl : "";
    if (!snapshot) return false;

    this.activeGeneration = requestedGeneration;
    this.startedAt = this.nowFn();
    this.overlay.src = snapshot;
    this.overlay.classList.add("is-releasing");
    this.releaseTimer = this.setTimeoutFn(() => {
      this.releaseTimer = null;
      if (this.activeGeneration !== requestedGeneration) return;
      this.overlay.classList.remove("is-releasing");
      this.overlay.classList.add("is-waiting");
    }, this.releaseMs);
    return true;
  }

  markPreviewVisible(generation) {
    if (Number(generation) !== this.activeGeneration || !this.overlay) return false;
    const releasing = this.overlay.classList.contains("is-releasing");
    const waiting = this.overlay.classList.contains("is-waiting");
    if (!releasing && !waiting) return false;

    if (this.releaseTimer !== null) this.clearTimeoutFn(this.releaseTimer);
    this.releaseTimer = null;
    const elapsed = Math.max(0, Number(this.nowFn()) - Number(this.startedAt));
    const remaining = Math.max(this.completeMinMs, this.durationMs - elapsed);
    this.overlay.style?.setProperty?.("--scene-dissolve-complete-ms", `${remaining}ms`);
    this.overlay.classList.remove("is-releasing", "is-waiting");
    this.overlay.classList.add("is-completing");
    const activeGeneration = this.activeGeneration;
    this.clearTimer = this.setTimeoutFn(() => {
      this.clearTimer = null;
      if (this.activeGeneration === activeGeneration) this.clear();
    }, remaining);
    return true;
  }

  complete(generation) {
    if (Number(generation) !== this.activeGeneration) return false;
    if (this.overlay?.classList.contains("is-completing")) return true;
    return this.markPreviewVisible(generation);
  }

  cancel(generation = null) {
    if (generation !== null && Number(generation) !== this.activeGeneration) return false;
    this.clear();
    return true;
  }

  clear() {
    if (this.releaseTimer !== null) this.clearTimeoutFn(this.releaseTimer);
    if (this.clearTimer !== null) this.clearTimeoutFn(this.clearTimer);
    this.releaseTimer = null;
    this.clearTimer = null;
    this.activeGeneration = null;
    this.startedAt = null;
    if (!this.overlay) return;
    this.overlay.classList.remove(
      "is-releasing", "is-waiting", "is-completing", "is-held", "is-dissolving"
    );
    this.overlay.style?.removeProperty?.("--scene-dissolve-complete-ms");
    this.overlay.src = "";
  }
}
