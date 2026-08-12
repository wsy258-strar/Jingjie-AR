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
  const xml = buildSceneXml(scene, null, VIEW_MODES.NORMAL, false, 12);
  assert.match(xml, /fullscreen_mirroring="true"/);
  assert.match(xml, /mobilevr_fake_support="true"/);
  assert.match(xml, /<plugin name="webvr" devices="html5" keep="true" url="\/assets\/krp\/plugins\/webvr\.js" mobilevr_support="true"/);
  assert.match(xml, /15949056_%s\.jpg/);
  assert.match(xml, /preview\.jpg\?x=1&amp;y=2/);
  assert.match(xml, /hlookat="-5\.5"/);
  assert.match(xml, /fovtype="MFOV"/);
  assert.match(xml, /前往 &quot;第二展厅&quot;/);
  assert.match(xml, /type="image"/);
  assert.match(xml, /crop="0\|0\|128\|128"/);
  assert.doesNotMatch(xml, /type="scene"/);
  assert.doesNotMatch(xml, /type="inactive"/);
  assert.doesNotMatch(xml, /inactive-hotspot/);
  assert.match(xml, /onpreviewcomplete="js\(JingjieARSceneBridge\(0,12\)\);"/);
  assert.match(xml, /onloadcomplete="js\(JingjieARSceneBridge\(1,12\)\);"/);
});

test("场景与展品热点分别注册循环引导动效，并在点击前停止 caller 动画", () => {
  const xml = buildSceneXml({
    ...scene,
    hotspots: [
      scene.hotspots[0],
      {
        hotspotId: "artwork-hotspot",
        type: "artwork",
        title: "青花瓷",
        ath: 6,
        atv: -3,
        iconUrl: "/assets/hotspot/artwork.png",
        renderable: true
      }
    ]
  });

  assert.match(xml, /<action name="scene_hotspot_pulse">/);
  assert.match(xml, /<action name="artwork_hotspot_pulse">/);
  assert.match(xml, /hotspot\[%1\]/);
  assert.match(xml, /onloaded="scene_hotspot_pulse\(get\(name\)\);"/);
  assert.match(xml, /onloaded="artwork_hotspot_pulse\(get\(name\)\);"/);
  const actionsXml = xml.match(/<action name="scene_hotspot_pulse">[\s\S]*?<\/action><action name="artwork_hotspot_pulse">[\s\S]*?<\/action>/)?.[0] || "";
  assert.doesNotMatch(actionsXml, /tween\(caller\./);

  const sceneAction = xml.match(
    /<action name="scene_hotspot_pulse"><!\[CDATA\[([\s\S]*?)\]\]><\/action>/
  )?.[1] || "";
  assert.match(sceneAction, /tween\(hotspot\[%1\]\.oy,-18,0\.6/);
  assert.match(sceneAction, /tween\(hotspot\[%1\]\.oy,0,0\.6/);
  assert.doesNotMatch(sceneAction, /\.scale/);
  assert.doesNotMatch(sceneAction, /\.alpha/);

  const artworkAction = xml.match(
    /<action name="artwork_hotspot_pulse"><!\[CDATA\[([\s\S]*?)\]\]><\/action>/
  )?.[1] || "";
  assert.match(artworkAction, /tween\(hotspot\[%1\]\.scale,1\.26,0\.6/);
  assert.match(artworkAction, /tween\(hotspot\[%1\]\.scale,1,0\.6/);
  assert.match(artworkAction, /tween\(hotspot\[%1\]\.alpha,1,0\.6/);
  assert.match(artworkAction, /tween\(hotspot\[%1\]\.alpha,0\.82,0\.6/);
  assert.match(artworkAction, /tween\(hotspot\[%1\]\.oy,-7,0\.6/);
  assert.match(artworkAction, /tween\(hotspot\[%1\]\.oy,0,0\.6/);
  assert.match(xml, /onclick="stoptween\(caller\.scale\); stoptween\(caller\.alpha\); stoptween\(caller\.oy\); js\(JingjieARHotspotBridge\(0\)\);"/);
  assert.match(xml, /onclick="stoptween\(caller\.scale\); stoptween\(caller\.alpha\); stoptween\(caller\.oy\); js\(JingjieARHotspotBridge\(1\)\);"/);

  for (const actionName of ["scene_hotspot_pulse", "artwork_hotspot_pulse"]) {
    const actionBody = xml.match(
      new RegExp(`<action name="${actionName}"><!\\[CDATA\\[([\\s\\S]*?)\\]\\]></action>`)
    )?.[1] || "";
    const openingParentheses = (actionBody.match(/\(/g) || []).length;
    const closingParentheses = (actionBody.match(/\)/g) || []).length;
    assert.equal(
      openingParentheses,
      closingParentheses,
      `${actionName} 的 krpano action 括号必须闭合`
    );
  }
});

test("reduced motion 场景不输出热点循环，但保留可点击热点", () => {
  const xml = buildSceneXml({
    ...scene,
    hotspots: [
      scene.hotspots[0],
      {
        hotspotId: "artwork-hotspot",
        type: "artwork",
        title: "青花瓷",
        ath: 6,
        atv: -3,
        iconUrl: "/assets/hotspot/artwork.png",
        renderable: true
      }
    ]
  }, null, VIEW_MODES.NORMAL, true);

  assert.doesNotMatch(xml, /hotspot_pulse/);
  assert.match(xml, /onclick="stoptween\(caller\.scale\); stoptween\(caller\.alpha\); stoptween\(caller\.oy\); js\(JingjieARHotspotBridge\(0\)\);"/);
  assert.match(xml, /onclick="stoptween\(caller\.scale\); stoptween\(caller\.alpha\); stoptween\(caller\.oy\); js\(JingjieARHotspotBridge\(1\)\);"/);
});

test("场景 XML 注册 Gyro2 且默认由页面控制启用", () => {
  const xml = buildSceneXml(scene);
  assert.match(xml, /<plugin name="gyro" devices="html5" keep="true"/);
  assert.match(xml, /url="\/assets\/krp\/plugins\/gyro2\.js"/);
  assert.match(xml, /enabled="false"/);
});

test("Gyro2 适配器调用插件并读取可用性", () => {
  const calls = [];
  const adapter = new KrpanoAdapter({ targetId: "panorama" });
  adapter.player = {
    get(key) {
      assert.equal(key, "plugin[gyro].isavailable");
      return true;
    },
    call(command) { calls.push(command); }
  };

  adapter.enableGyro();
  adapter.disableGyro();
  assert.deepEqual(calls, ["gyro.enable();", "gyro.disable();"]);
  assert.equal(adapter.isGyroAvailable(), true);
});

test("场景 XML 将 WebVR 可用性和进出事件桥接到适配层", () => {
  const xml = buildSceneXml(scene);
  assert.match(xml, /onavailable="js\(JingjieARWebVrBridge\(1\)\);"/);
  assert.match(xml, /onunavailable="js\(JingjieARWebVrBridge\(0\)\);"/);
  assert.match(xml, /onentervr="js\(JingjieARWebVrBridge\(2\)\);"/);
  assert.match(xml, /onexitvr="js\(JingjieARWebVrBridge\(3\)\);"/);
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

test("场景事件桥只向应用层转发最新 generation", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const events = [];
  const calls = [];
  const player = {
    get() { return "0"; },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({
      targetId: "panorama",
      onSceneEvent(event) { events.push(event); }
    });
    await adapter.loadScene(scene, 4);
    assert.match(calls.at(-1), /JingjieARSceneBridge\(0,4\)/);
    globalThis.JingjieARSceneBridge(0, 3);
    globalThis.JingjieARSceneBridge(1, 3);
    assert.deepEqual(events, []);
    globalThis.JingjieARSceneBridge(0, 4);
    globalThis.JingjieARSceneBridge(1, 4);
    assert.deepEqual(events, [
      { type: "preview-visible", generation: 4 },
      { type: "complete", generation: 4 }
    ]);
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
    assert.match(calls.at(-1), /tween\(/);
    assert.equal(adapter.setViewMode("fisheye"), "fisheye");
    assert.match(calls.at(-1), /fisheye,1\.0/);
    assert.match(calls.at(-1), /tween\(/);
    assert.equal(adapter.setViewMode("crystal"), "crystal");
    assert.match(calls.at(-1), /stereographic,true/);
    assert.match(calls.at(-1), /vlookat,0/);
    assert.match(calls.at(-1), /tween\(/);
    assert.equal(adapter.setViewMode("normal"), "normal");
    assert.match(calls.at(-1), /stereographic,false/);
    assert.match(calls.at(-1), /lookto\(18,-4,72/);
    assert.match(calls.at(-1), /smooth\(/);
    assert.throws(() => adapter.setViewMode("unknown"), /不支持的视角模式/);
    assert.throws(() => adapter.setViewMode("constructor"), /不支持的视角模式/);
    assert.throws(() => adapter.setViewMode("toString"), /不支持的视角模式/);
    assert.throws(() => adapter.applyViewMode("constructor"), /不支持的视角模式/);
    assert.throws(() => adapter.applyViewMode("toString"), /不支持的视角模式/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("reduced motion 下四种视角只使用即时 set 和 lookat 动作", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const player = {
    get(key) {
      return { "view.hlookat": "18", "view.vlookat": "-4", "view.fov": "72" }[key];
    },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama", reducedMotion: true });
    await adapter.initialize();
    for (const mode of ["planet", "fisheye", "crystal", "normal"]) {
      adapter.setViewMode(mode);
      const action = calls.at(-1);
      assert.doesNotMatch(action, /tween\(/, `${mode} 不应 tween`);
      assert.doesNotMatch(action, /smooth\(/, `${mode} 不应 smooth`);
      assert.match(action, /set\(/, `${mode} 应使用即时 set`);
      if (mode === "normal") assert.match(action, /lookat\(18,-4,72\)/);
    }
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
    assert.match(calls.at(-1), /fisheye="1\.0"/);
    assert.match(calls.at(-1), /fov="120"/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("特殊视角切场景通过单次 XML 提交保持画面和热点一致", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const selected = [];
  let rejectProjectionAction = false;
  let renderedSceneId = null;
  const player = {
    get(key) {
      return { "view.hlookat": "0", "view.vlookat": "0", "view.fov": "90" }[key];
    },
    call(command) {
      calls.push(command);
      if (command.includes("loadxml(")) {
        renderedSceneId = command.includes("new-scene-hotspot") ? "new" : "old";
        return;
      }
      if (rejectProjectionAction) throw new Error("projection action failed");
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
    adapter.setViewMode("fisheye");
    const callsBeforeSwitch = calls.length;
    rejectProjectionAction = true;

    assert.equal(await adapter.loadScene(nextScene, 2), true);
    assert.equal(calls.length, callsBeforeSwitch + 1);
    assert.match(calls.at(-1), /fisheye="1\.0"/);
    assert.match(calls.at(-1), /fov="120"/);
    assert.equal(renderedSceneId, "new");
    assert.equal(adapter.loaded, true);
    assert.equal(adapter.currentHotspots[0].hotspotId, "new-scene-hotspot");
    globalThis.JingjieARHotspotBridge(0);
    assert.deepEqual(selected, ["new-scene-hotspot"]);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("返回 normal 使用特殊模式中的最新方向和进入前的普通 FOV", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const values = {
    "view.hlookat": "18",
    "view.vlookat": "-4",
    "view.fov": "72"
  };
  const player = {
    get(key) { return values[key]; },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    await adapter.loadScene(scene, 1);
    adapter.setViewMode("planet");
    values["view.hlookat"] = "133";
    values["view.vlookat"] = "21";
    values["view.fov"] = "150";
    await adapter.loadScene({ ...scene, sceneId: "76196993" }, 2);

    adapter.setViewMode("normal");
    assert.match(calls.at(-1), /lookto\(133,21,72,/);
    assert.doesNotMatch(calls.at(-1), /lookto\(18,-4,/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("已经处于 normal 时重复选择不会改写当前 FOV", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const player = {
    get(key) {
      return { "view.hlookat": "9", "view.vlookat": "-2", "view.fov": "68" }[key];
    },
    call(command) { calls.push(command); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    await adapter.initialize();
    adapter.setViewMode("normal");
    assert.match(calls.at(-1), /lookto\(9,-2,68,/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("WebVR 插件事件维护 unknown、available、unavailable 和 entered 状态", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const player = { get() { return "0"; }, call() {} };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    await adapter.initialize();
    assert.equal(adapter.vrState, "unknown");
    globalThis.JingjieARWebVrBridge(1);
    assert.equal(adapter.vrState, "available");
    assert.equal(adapter.isVrAvailable(), true);
    globalThis.JingjieARWebVrBridge(0);
    assert.equal(adapter.vrState, "unavailable");
    assert.equal(adapter.isVrAvailable(), false);
    globalThis.JingjieARWebVrBridge(1);
    globalThis.JingjieARWebVrBridge(2);
    assert.equal(adapter.vrState, "entered");
    globalThis.JingjieARWebVrBridge(3);
    assert.equal(adapter.vrState, "available");
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("WebVR 进入和退出通知应用层切换覆盖 UI", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const states = [];
  const player = { get() { return "0"; }, call() {} };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({
      targetId: "panorama",
      onVrStateChange: (state) => states.push(state)
    });
    await adapter.initialize();
    globalThis.JingjieARWebVrBridge(2);
    globalThis.JingjieARWebVrBridge(3);
    assert.deepEqual(states, ["entered", "exited"]);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("VR 在非安全上下文立即给出明确 HTTPS 错误", () => {
  const previousDescriptor = Object.getOwnPropertyDescriptor(globalThis, "isSecureContext");
  Object.defineProperty(globalThis, "isSecureContext", {
    configurable: true,
    value: false
  });
  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    assert.throws(() => adapter.enterVr(), /HTTPS/);
  } finally {
    if (previousDescriptor) Object.defineProperty(globalThis, "isSecureContext", previousDescriptor);
    else delete globalThis.isSecureContext;
  }
});

test("VR 插件 unknown 与 unavailable 使用不同中文错误", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const player = { get() { return "0"; }, call() {} };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({ targetId: "panorama" });
    await adapter.initialize();
    assert.throws(() => adapter.enterVr(), /插件尚未完成可用性检测/);
    globalThis.JingjieARWebVrBridge(0);
    assert.throws(() => adapter.enterVr(), /当前设备或浏览器不支持 VR/);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("VR 进入动作同步发出并仅在 onentervr 后完成", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const calls = [];
  const cleared = [];
  const player = { get() { return "0"; }, call(command) { calls.push(command); } };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({
      targetId: "panorama",
      vrEnterTimeoutMs: 25,
      setTimeoutFn() { return 17; },
      clearTimeoutFn(id) { cleared.push(id); }
    });
    await adapter.initialize();
    globalThis.JingjieARWebVrBridge(1);
    const entering = adapter.enterVr();
    assert.equal(calls.at(-1), "webvr.enterVR();");
    assert.equal(adapter.vrState, "entering");
    let settled = false;
    entering.finally(() => { settled = true; });
    await Promise.resolve();
    assert.equal(settled, false);
    globalThis.JingjieARWebVrBridge(2);
    assert.equal(await entering, true);
    assert.equal(adapter.vrState, "entered");
    assert.deepEqual(cleared, [17]);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("VR 调用抛错会拒绝并恢复 available 以便重试", async () => {
  const previousEmbedpano = globalThis.embedpano;
  let shouldThrow = true;
  const player = {
    get() { return "0"; },
    call() { if (shouldThrow) throw new Error("permission denied"); }
  };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({
      targetId: "panorama",
      setTimeoutFn() { return 1; },
      clearTimeoutFn() {}
    });
    await adapter.initialize();
    globalThis.JingjieARWebVrBridge(1);
    await assert.rejects(adapter.enterVr(), /进入 VR 失败.*permission denied/);
    assert.equal(adapter.vrState, "available");
    shouldThrow = false;
    const retry = adapter.enterVr();
    globalThis.JingjieARWebVrBridge(2);
    assert.equal(await retry, true);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("VR entering 期间收到 unavailable 会拒绝并可在重新 available 后重试", async () => {
  const previousEmbedpano = globalThis.embedpano;
  const player = { get() { return "0"; }, call() {} };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({
      targetId: "panorama",
      setTimeoutFn() { return 1; },
      clearTimeoutFn() {}
    });
    await adapter.initialize();
    globalThis.JingjieARWebVrBridge(1);
    const entering = adapter.enterVr();
    globalThis.JingjieARWebVrBridge(0);
    await assert.rejects(entering, /当前设备或浏览器不支持 VR/);
    assert.equal(adapter.vrState, "unavailable");
    globalThis.JingjieARWebVrBridge(1);
    const retry = adapter.enterVr();
    globalThis.JingjieARWebVrBridge(2);
    assert.equal(await retry, true);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});

test("VR 进入超时使用可注入计时器拒绝并恢复 available", async () => {
  const previousEmbedpano = globalThis.embedpano;
  let timeoutCallback;
  const scheduled = [];
  const player = { get() { return "0"; }, call() {} };
  globalThis.embedpano = (options) => options.onready(player);

  try {
    const adapter = new KrpanoAdapter({
      targetId: "panorama",
      vrEnterTimeoutMs: 25,
      setTimeoutFn(callback, delay) {
        timeoutCallback = callback;
        scheduled.push(delay);
        return 9;
      },
      clearTimeoutFn() {}
    });
    await adapter.initialize();
    globalThis.JingjieARWebVrBridge(1);
    const entering = adapter.enterVr();
    timeoutCallback();
    await assert.rejects(entering, /进入 VR 超时，请重试/);
    assert.deepEqual(scheduled, [25]);
    assert.equal(adapter.vrState, "available");
    const retry = adapter.enterVr();
    globalThis.JingjieARWebVrBridge(2);
    assert.equal(await retry, true);
  } finally {
    if (previousEmbedpano === undefined) delete globalThis.embedpano;
    else globalThis.embedpano = previousEmbedpano;
  }
});
