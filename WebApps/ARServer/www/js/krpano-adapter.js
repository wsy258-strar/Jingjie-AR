// krpano 最小适配层：播放器只嵌入一次，场景数据只来自 ARServer。

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

function viewFor(scene, override) {
  const source = override || scene.view || {};
  return {
    hlookat: finiteNumber(source.hlookat, 0),
    vlookat: finiteNumber(source.vlookat, 0),
    fov: finiteNumber(source.fov, 90)
  };
}

function renderableHotspots(scene) {
  return Array.isArray(scene.hotspots)
    ? scene.hotspots.filter((hotspot) => hotspot && hotspot.type !== "inactive" && hotspot.renderable !== false)
    : [];
}

export function buildSceneXml(scene, viewOverride = null) {
  const view = viewFor(scene, viewOverride);
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
    '<preview url="', xmlEscape(scene.previewUrl), '" />',
    '<image><cube url="', xmlEscape(scene.cubeUrl), '" /></image>',
    '<view hlookat="', view.hlookat, '" vlookat="', view.vlookat,
    '" fov="', view.fov, '" />',
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
    this.currentHotspots = renderableHotspots(scene);
    const xml = buildSceneXml(scene, preservedView);
    this.player.call(`loadxml('${krpanoActionString(xml)}', null, RESET);`);
    this.loaded = true;
    return true;
  }
}
