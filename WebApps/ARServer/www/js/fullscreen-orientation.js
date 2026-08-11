export class FullscreenOrientation {
  constructor({ documentObject = globalThis.document, screenObject = globalThis.screen, onLandscapeFallback } = {}) {
    this.document = documentObject;
    this.orientation = screenObject?.orientation;
    this.onLandscapeFallback = onLandscapeFallback || (() => {});
    this.orientationLocked = false;
    this.fallbackShown = false;
  }

  async toggle(target) {
    if (this.document.fullscreenElement) {
      try {
        await this.document.exitFullscreen();
        this.unlock();
        this.fallbackShown = false;
        return true;
      } catch (_) {
        return false;
      }
    }

    try {
      await target.requestFullscreen();
    } catch (_) {
      return false;
    }

    await this.lockLandscape();
    return true;
  }

  handleFullscreenChange(target) {
    const active = this.document.fullscreenElement === target;
    if (!active) {
      this.unlock();
      this.fallbackShown = false;
    }
    return active;
  }

  async lockLandscape() {
    try {
      if (typeof this.orientation?.lock !== "function") throw new Error("Screen Orientation API unavailable");
      await this.orientation.lock("landscape");
      this.orientationLocked = true;
    } catch (_) {
      this.orientationLocked = false;
      if (!this.fallbackShown) {
        this.fallbackShown = true;
        this.onLandscapeFallback();
      }
    }
  }

  unlock() {
    if (!this.orientationLocked) return;
    this.orientationLocked = false;
    try {
      if (typeof this.orientation?.unlock === "function") this.orientation.unlock();
    } catch (_) {}
  }
}
