import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

async function loadModule() {
  const target = await mkdtemp(join(tmpdir(), "jingjie-ar-ui-state-"));
  const source = new URL("../../WebApps/ARServer/www/js/museum-ui-state.js", import.meta.url);
  const content = await readFile(source, "utf8");
  const modulePath = join(target, "museum-ui-state.mjs");
  await writeFile(modulePath, content);
  return { target, module: await import(pathToFileURL(modulePath).href) };
}

const loaded = await loadModule();
process.once("exit", () => rmSync(loaded.target, { recursive: true, force: true }));
const { MuseumUiState } = loaded.module;

test("场景抽屉和视角浮层始终互斥", () => {
  const state = new MuseumUiState();
  state.toggleSceneDrawer();
  assert.deepEqual(state.snapshot(), {
    sceneDrawerOpen: true, viewPanelOpen: false, viewMode: "normal"
  });
  state.toggleViewPanel();
  assert.deepEqual(state.snapshot(), {
    sceneDrawerOpen: false, viewPanelOpen: true, viewMode: "normal"
  });
});

test("关闭临时浮层不改变当前视角", () => {
  const state = new MuseumUiState();
  state.toggleViewPanel();
  state.selectViewMode("fisheye");
  state.toggleSceneDrawer();
  state.closeTransientLayers();
  assert.deepEqual(state.snapshot(), {
    sceneDrawerOpen: false, viewPanelOpen: false, viewMode: "fisheye"
  });
});

test("非法视角不会改变状态", () => {
  const state = new MuseumUiState();
  assert.throws(() => state.selectViewMode("flat"), /不支持的视角模式/);
  assert.equal(state.snapshot().viewMode, "normal");
});

test("仅在状态实际变化时通知快照", () => {
  const changes = [];
  const state = new MuseumUiState({ onChange: (snapshot) => changes.push(snapshot) });
  state.closeTransientLayers();
  assert.equal(changes.length, 0);
  state.toggleSceneDrawer();
  state.closeTransientLayers();
  assert.equal(changes.length, 2);
  assert.equal(changes[1].sceneDrawerOpen, false);
});
