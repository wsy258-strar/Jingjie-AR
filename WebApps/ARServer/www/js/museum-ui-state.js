const VIEW_MODES = new Set(["normal", "planet", "fisheye", "crystal"]);

export class MuseumUiState {
  constructor({ onChange } = {}) {
    this.sceneDrawerOpen = false;
    this.viewPanelOpen = false;
    this.viewMode = "normal";
    this.onChange = typeof onChange === "function" ? onChange : () => {};
  }

  snapshot() {
    return {
      sceneDrawerOpen: this.sceneDrawerOpen,
      viewPanelOpen: this.viewPanelOpen,
      viewMode: this.viewMode
    };
  }

  update(next) {
    const changed = Object.entries(next).some(([key, value]) => this[key] !== value);
    if (!changed) return this.snapshot();
    Object.assign(this, next);
    const snapshot = this.snapshot();
    this.onChange(snapshot);
    return snapshot;
  }

  toggleSceneDrawer() {
    return this.update({
      sceneDrawerOpen: !this.sceneDrawerOpen,
      viewPanelOpen: false
    });
  }

  toggleViewPanel() {
    return this.update({
      sceneDrawerOpen: false,
      viewPanelOpen: !this.viewPanelOpen
    });
  }

  closeTransientLayers() {
    return this.update({ sceneDrawerOpen: false, viewPanelOpen: false });
  }

  selectViewMode(mode) {
    if (!VIEW_MODES.has(mode)) throw new Error(`不支持的视角模式：${mode}`);
    return this.update({ viewMode: mode, viewPanelOpen: false });
  }
}
