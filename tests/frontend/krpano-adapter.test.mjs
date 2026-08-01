import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

async function loadAdapterModule() {
  const target = await mkdtemp(join(tmpdir(), "jingjie-ar-krpano-adapter-"));
  const source = new URL("../../WebApps/ARServer/www/js/krpano-adapter.js", import.meta.url);
  const content = await readFile(source, "utf8");
  const modulePath = join(target, "krpano-adapter.mjs");
  await writeFile(modulePath, content);
  return { target, module: await import(pathToFileURL(modulePath).href) };
}

let loaded;
try {
  loaded = await loadAdapterModule();
} catch (error) {
  // 保留真实的模块缺失错误，让 RED 阶段明确失败在待实现文件上。
  throw error;
}
process.once("exit", () => rmSync(loaded.target, { recursive: true, force: true }));

const { KrpanoAdapter, VIEW_MODES, buildSceneXml, xmlEscape } = loaded.module;

const scene = {
  sceneId: "76196992",
  name: "展厅入口 <一>",
  previewUrl: "/assets/pano/15949056/preview.jpg?x=1&y=2",
  cubeUrl: "/assets/pano/15949056/15949056_%s.jpg",
  view: { hlookat: -5.5, vlookat: 2.25, fov: 90 },
  hotspots: [
    {
      hotspotId: "scene-hotspot",
      type: "scene",
      title: `前往 "第二展厅"`,
      ath: -4.2,
      atv: 13.9,
      iconUrl: "/assets/hotspot/new_spotd1_gif.png",
      targetSceneId: "76196993",
      renderable: true
    },
    {
      hotspotId: "inactive-hotspot",
      type: "inactive",
      title: "不可用",
      ath: 0,
      atv: 0,
      iconUrl: "/assets/hotspot/new_spotd1_gif.png",
      renderable: false
    }
  ]
};

test("XML 转义覆盖标签、引号、与号和单引号", () => {
  assert.equal(xmlEscape(`<tag a="1">&'`),
    "&lt;tag a=&quot;1&quot;&gt;&amp;&apos;");
});

test("场景 XML 包含低清预览、高清立方体和视角，且不渲染 inactive 热点", () => {
  const xml = buildSceneXml(scene);
  assert.match(xml, /15949056_%s\.jpg/);
  assert.match(xml, /preview\.jpg\?x=1&amp;y=2/);
  assert.match(xml, /hlookat="-5\.5"/);
  assert.match(xml, /前往 &quot;第二展厅&quot;/);
  assert.match(xml, /type="image"/);
  assert.match(xml, /crop="0\|0\|128\|128"/);
  assert.doesNotMatch(xml, /type="scene"/);
  assert.doesNotMatch(xml, /type="inactive"/);
  assert.doesNotMatch(xml, /inactive-hotspot/);
});

test("热点精灵素材按 128 像素方形帧裁剪", async () => {
  const sprite = await readFile(new URL(
    "../../WebApps/ARServer/www/assets/hotspot/new_spotd1_gif.png", import.meta.url
  ));
  assert.equal(sprite.readUInt32BE(16), 128);
  assert.equal(sprite.readUInt32BE(20), 3200);
  assert.match(buildSceneXml(scene), /crop="0\|0\|128\|128"/);
});

test("初始化尚未完成时也能立即作废旧 generation", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  let ready;
  const player = { get() { return "0"; }, call(command) { calls.push(command); } };
  globalThis.embedpano = (options) => { ready = options.onready; };

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama", onHotspot() {} });
    const staleLoad = adapter.loadScene(scene, 1);
    adapter.invalidate(2);
    ready(player);
    assert.equal(await staleLoad, false);
    assert.equal(calls.length, 0);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("播放器仅嵌入一次，旧 generation 不能覆盖新场景", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const values = {
    "view.hlookat": "12",
    "view.vlookat": "-3",
    "view.fov": "77"
  };
  const player = {
    get(key) { return values[key]; },
    call(command) { calls.push(command); }
  };
  let embeds = 0;
  globalThis.embedpano = (options) => {
    embeds += 1;
    options.onready(player);
  };

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama", onHotspot() {} });
    await Promise.all([adapter.initialize(), adapter.initialize()]);
    assert.equal(embeds, 1);

    assert.equal(await adapter.loadScene(scene, 2), true);
    assert.equal(await adapter.loadScene({ ...scene, sceneId: "old" }, 1), false);
    assert.equal(calls.length, 1);
    assert.match(calls[0], /15949056_%s\.jpg/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("第二次加载场景沿用切换前视角", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const values = {
    "view.hlookat": "33.5",
    "view.vlookat": "-7",
    "view.fov": "68"
  };
  const player = {
    get(key) { return values[key]; },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama", onHotspot() {} });
    await adapter.loadScene(scene, 1);
    await adapter.loadScene({ ...scene, sceneId: "76196993" }, 2);
    assert.equal(calls.length, 2);
    assert.match(calls[1], /hlookat="33\.5"/);
    assert.match(calls[1], /vlookat="-7"/);
    assert.match(calls[1], /fov="68"/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("loadxml 抛错时保留旧场景热点映射与已加载状态", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const selected = [];
  let shouldThrow = false;
  const player = {
    get() { return "0"; },
    call() {
      if (shouldThrow) throw new Error("loadxml failed");
    }
  };
  globalThis.embedpano = (options) => options.onready(player);
  const nextScene = {
    ...scene,
    sceneId: "76196993",
    hotspots: [{
      hotspotId: "new-scene-hotspot",
      type: "scene",
      title: "新场景热点",
      ath: 1,
      atv: 2,
      iconUrl: "/assets/hotspot/new_spotd1_gif.png",
      targetSceneId: "76196994",
      renderable: true
    }]
  };

  try {
    const adapter = new KrpanoAdapter({
      targetId: "panorama",
      onHotspot(hotspot) { selected.push(hotspot.hotspotId); }
    });
    assert.equal(await adapter.loadScene(scene, 1), true);
    assert.equal(adapter.loaded, true);
    assert.equal(adapter.currentHotspots[0].hotspotId, "scene-hotspot");

    shouldThrow = true;
    await assert.rejects(adapter.loadScene(nextScene, 2), /loadxml failed/);
    assert.equal(adapter.loaded, true);
    assert.equal(adapter.currentHotspots[0].hotspotId, "scene-hotspot");
    globalThis.JingjieARHotspotBridge(0);
    assert.deepEqual(selected, ["scene-hotspot"]);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("导出四种稳定的语义视角模式", () => {
  assert.deepEqual(VIEW_MODES, {
    NORMAL: "normal",
    PLANET: "planet",
    FISHEYE: "fisheye",
    CRYSTAL: "crystal"
  });
});

test("四种视角模式映射为独立的 krpano 投影动作", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const values = {
    "view.hlookat": "18",
    "view.vlookat": "-4",
    "view.fov": "72",
    "webvr.isavailable": true
  };
  const player = {
    get(key) { return values[key]; },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    await adapter.initialize();
    assert.equal(adapter.setViewMode("planet"), "planet");
    assert.match(calls.at(-1), /stereographic,true/);
    assert.match(calls.at(-1), /vlookat,90/);
    assert.equal(adapter.setViewMode("fisheye"), "fisheye");
    assert.match(calls.at(-1), /fisheye,1\.0/);
    assert.equal(adapter.setViewMode("crystal"), "crystal");
    assert.match(calls.at(-1), /stereographic,true/);
    assert.match(calls.at(-1), /vlookat,0/);
    assert.equal(adapter.setViewMode("normal"), "normal");
    assert.match(calls.at(-1), /stereographic,false/);
    assert.match(calls.at(-1), /lookto\(18,-4,72/);
    assert.throws(() => adapter.setViewMode("unknown"), /不支持的视角模式/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("切换场景后重新应用当前特殊视角", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const player = {
    get(key) {
      return { "view.hlookat": "0", "view.vlookat": "0", "view.fov": "90" }[key];
    },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    await adapter.loadScene(scene, 1);
    adapter.setViewMode("fisheye");
    await adapter.loadScene({ ...scene, sceneId: "76196993" }, 2);
    assert.match(calls.at(-1), /fisheye,1\.0/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("VR 仅在播放器和插件可用时进入", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  let available = false;
  const player = {
    get(key) { return key === "webvr.isavailable" ? available : "0"; },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    assert.throws(() => adapter.enterVr(), /尚未就绪/);
    await adapter.initialize();
    assert.equal(adapter.isVrAvailable(), false);
    assert.throws(() => adapter.enterVr(), /当前设备或浏览器不支持 VR/);
    available = true;
    assert.equal(adapter.isVrAvailable(), true);
    assert.equal(adapter.enterVr(), true);
    assert.equal(calls.at(-1), "webvr.enterVR();");
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});
