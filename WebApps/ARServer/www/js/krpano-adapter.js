// krpano 最小适配层：播放器只嵌入一次，场景数据只来自 ARServer。

export const VIEW_MODES = Object.freeze({
  NORMAL: "normal",
  PLANET: "planet",
  FISHEYE: "fisheye",
  CRYSTAL: "crystal"
});

const VIEW_ACTIONS = Object.freeze({
  normal: ({ hlookat, vlookat, fov }) =>
    `set(view.stereographic,false); tween(view.fisheye,0.0,0.35); lookto(${hlookat},${vlookat},${fov},smooth(45,45,60));`,
  planet: () =>
    "set(view.stereographic,true); tween(view.fisheye,1.0,0.45); tween(view.vlookat,90,0.45); tween(view.fov,150,0.45);",
  fisheye: () =>
    "set(view.stereographic,false); tween(view.fisheye,1.0,0.35); tween(view.fov,120,0.35);",
  crystal: () =>
    "set(view.stereographic,true); tween(view.fisheye,1.0,0.45); tween(view.vlookat,0,0.45); tween(view.fov,150,0.45);"
});

function isSupportedViewMode(mode) {
  return Object.hasOwn(VIEW_ACTIONS, mode);
}

let hotspotBridge = null;

globalThis.JingjieARHotspotBridge = function (index) {
  if (hotspotBridge) hotspotBridge(Number(index));
};

export function xmlEscape(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&apos;");
}

function finiteNumber(value, fallback) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function viewFor(scene, override, mode = VIEW_MODES.NORMAL) {
  const source = override || scene.view || {};
  const view = {
    hlookat: finiteNumber(source.hlookat, 0),
    vlookat: finiteNumber(source.vlookat, 0),
    fov: finiteNumber(source.fov, 90)
  };
  switch (mode) {
    case VIEW_MODES.PLANET:
      return { ...view, vlookat: 90, fov: 150, stereographic: true, fisheye: "1.0" };
    case VIEW_MODES.FISHEYE:
      return { ...view, fov: 120, stereographic: false, fisheye: "1.0" };
    case VIEW_MODES.CRYSTAL:
      return { ...view, vlookat: 0, fov: 150, stereographic: true, fisheye: "1.0" };
    default:
      return { ...view, stereographic: false, fisheye: "0.0" };
  }
}

function renderableHotspots(scene) {
  return Array.isArray(scene.hotspots)
    ? scene.hotspots.filter((hotspot) => hotspot && hotspot.type !== "inactive" && hotspot.renderable !== false)
    : [];
}

export function buildSceneXml(scene, viewOverride = null, viewMode = VIEW_MODES.NORMAL) {
  const view = viewFor(scene, viewOverride, viewMode);
  const hotspots = renderableHotspots(scene);
  const hotspotXml = hotspots.map((hotspot, index) => [
    '<hotspot name="', xmlEscape(hotspot.hotspotId || `hotspot-${index}`),
    '" type="image" crop="0|0|128|128',
    '" title="', xmlEscape(hotspot.title),
    '" ath="', finiteNumber(hotspot.ath, 0),
    '" atv="', finiteNumber(hotspot.atv, 0),
    '" url="', xmlEscape(hotspot.iconUrl),
    '" onclick="js(JingjieARHotspotBridge(', index, '));" />'
  ].join("")).join("");

  return [
    '<krpano version="1.19">',
    '<plugin name="webvr" devices="html5" keep="true"',
    ' url="/assets/krp/plugins/webvr.js" mobilevr_support="true" />',
    '<preview url="', xmlEscape(scene.previewUrl), '" />',
    '<image><cube url="', xmlEscape(scene.cubeUrl), '" /></image>',
    '<view hlookat="', view.hlookat, '" vlookat="', view.vlookat,
    '" fov="', view.fov, '" stereographic="', view.stereographic,
    '" fisheye="', view.fisheye, '" />',
    hotspotXml,
    '</krpano>'
  ].join("");
}

function krpanoActionString(xml) {
  // XML 内的业务单引号已转义为 &apos;，这里只需保护反斜杠和换行。
  return String(xml).replaceAll("\\", "\\\\").replaceAll("\r", "").replaceAll("\n", " ");
}

export class KrpanoAdapter {
  constructor({ targetId, onHotspot } = {}) {
    if (!targetId) throw new Error("KrpanoAdapter requires targetId");
    this.targetId = targetId;
    this.onHotspot = typeof onHotspot === "function" ? onHotspot : () => {};
    this.player = null;
    this.initializePromise = null;
    this.latestGeneration = -1;
    this.loaded = false;
    this.currentHotspots = [];
    this.viewMode = VIEW_MODES.NORMAL;
    this.normalView = null;
  }

  initialize() {
    if (this.initializePromise) return this.initializePromise;
    this.initializePromise = new Promise((resolve, reject) => {
      if (typeof globalThis.embedpano !== "function") {
        reject(new Error("krpano runtime is unavailable"));
        return;
      }
      let settled = false;
      const fail = (error) => {
        if (settled) return;
        settled = true;
        reject(error instanceof Error ? error : new Error(String(error || "krpano initialization failed")));
      };
      try {
        globalThis.embedpano({
          target: this.targetId,
          html5: "only",
          xml: null,
          bgcolor: "#111111",
          mobilescale: 1.0,
          passQueryParameters: false,
          onready: (player) => {
            if (settled) return;
            settled = true;
            this.player = player;
            hotspotBridge = (index) => {
              const hotspot = this.currentHotspots[index];
              if (hotspot) this.onHotspot(hotspot);
            };
            resolve(player);
          },
          onerror: fail
        });
      } catch (error) {
        fail(error);
      }
    });
    return this.initializePromise;
  }

  getView() {
    if (!this.player || typeof this.player.get !== "function") return null;
    const view = {
      hlookat: Number(this.player.get("view.hlookat")),
      vlookat: Number(this.player.get("view.vlookat")),
      fov: Number(this.player.get("view.fov"))
    };
    return Object.values(view).every(Number.isFinite) ? view : null;
  }

  applyViewMode(mode) {
    if (!isSupportedViewMode(mode)) throw new Error(`不支持的视角模式：${mode}`);
    const actionFactory = VIEW_ACTIONS[mode];
    const fallback = this.normalView || this.getView() || { hlookat: 0, vlookat: 0, fov: 90 };
    this.player.call(actionFactory(fallback));
  }

  setViewMode(mode) {
    if (!this.player) throw new Error("全景播放器尚未就绪");
    if (!isSupportedViewMode(mode)) throw new Error(`不支持的视角模式：${mode}`);
    if (this.viewMode === VIEW_MODES.NORMAL && mode !== VIEW_MODES.NORMAL)
      this.normalView = this.getView();
    this.applyViewMode(mode);
    this.viewMode = mode;
    if (mode === VIEW_MODES.NORMAL) this.normalView = null;
    return mode;
  }

  isVrAvailable() {
    if (!this.player) return false;
    const available = this.player.get("webvr.isavailable");
    return available === true || available === "true";
  }

  enterVr() {
    if (!this.player) throw new Error("全景播放器尚未就绪");
    if (!this.isVrAvailable()) throw new Error("当前设备或浏览器不支持 VR");
    this.player.call("webvr.enterVR();");
    return true;
  }

  invalidate(generation) {
    const requestedGeneration = Number(generation);
    if (Number.isFinite(requestedGeneration) && requestedGeneration > this.latestGeneration)
      this.latestGeneration = requestedGeneration;
  }

  async loadScene(scene, generation) {
    const requestedGeneration = Number(generation);
    if (!Number.isFinite(requestedGeneration) || requestedGeneration < this.latestGeneration) return false;
    this.latestGeneration = requestedGeneration;
    await this.initialize();
    if (requestedGeneration !== this.latestGeneration) return false;

    const preservedView = this.loaded ? this.getView() : null;
    const nextHotspots = renderableHotspots(scene);
    const xml = buildSceneXml(scene, preservedView, this.viewMode);
    this.player.call(`loadxml('${krpanoActionString(xml)}', null, RESET);`);
    this.currentHotspots = nextHotspots;
    this.loaded = true;
    return true;
  }
}
