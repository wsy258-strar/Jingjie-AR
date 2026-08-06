// 场景叠化控制器：冻结旧全景画面，在新场景就绪后渐隐旧快照。

export class SceneDissolve {
  constructor({
    viewer,
    overlay,
    durationMs = 380,
    requestAnimationFrameFn = (callback) => {
      if (typeof globalThis.requestAnimationFrame === "function")
        return globalThis.requestAnimationFrame(callback);
      return callback();
    },
    setTimeoutFn = (callback, delay) => globalThis.setTimeout(callback, delay),
    clearTimeoutFn = (timerId) => globalThis.clearTimeout(timerId)
  } = {}) {
    this.viewer = viewer;
    this.overlay = overlay;
    this.durationMs = Number.isFinite(Number(durationMs)) ? Number(durationMs) : 380;
    this.requestAnimationFrameFn = requestAnimationFrameFn;
    this.setTimeoutFn = setTimeoutFn;
    this.clearTimeoutFn = clearTimeoutFn;
    this.activeGeneration = null;
    this.clearTimer = null;
  }

  begin(generation) {
    if (!this.overlay || !Number.isFinite(Number(generation))) return false;
    const canvas = this.viewer?.querySelector?.("canvas");
    if (!canvas || typeof canvas.toDataURL !== "function") return false;

    let snapshot;
    try {
      snapshot = canvas.toDataURL("image/jpeg", 0.82);
    } catch (_) {
      return false;
    }
    if (typeof snapshot !== "string" || !snapshot) return false;

    this.clear();
    this.activeGeneration = Number(generation);
    this.overlay.src = snapshot;
    this.overlay.classList.remove("is-dissolving");
    this.overlay.classList.add("is-held");
    return true;
  }

  finish(generation) {
    if (Number(generation) !== this.activeGeneration) return false;
    const activeGeneration = this.activeGeneration;
    this.requestAnimationFrameFn(() => {
      if (this.activeGeneration !== activeGeneration) return;
      this.overlay.classList.remove("is-held");
      this.overlay.classList.add("is-dissolving");
      this.clearTimer = this.setTimeoutFn(() => {
        if (this.activeGeneration === activeGeneration) this.clear();
      }, this.durationMs);
    });
    return true;
  }

  cancel(generation = null) {
    if (generation !== null && Number(generation) !== this.activeGeneration) return false;
    this.clear();
    return true;
  }

  clear() {
    if (this.clearTimer !== null) this.clearTimeoutFn(this.clearTimer);
    this.clearTimer = null;
    this.activeGeneration = null;
    if (!this.overlay) return;
    this.overlay.classList.remove("is-held", "is-dissolving");
    this.overlay.src = "";
  }
}
