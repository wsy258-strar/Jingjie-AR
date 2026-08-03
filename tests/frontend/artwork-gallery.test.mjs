import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

const target = await mkdtemp(join(tmpdir(), "jingjie-ar-artwork-gallery-"));
const source = new URL("../../WebApps/ARServer/www/js/artwork-gallery.js", import.meta.url);
await writeFile(join(target, "artwork-gallery.mjs"), await readFile(source, "utf8"));
const { ArtworkGallery } = await import(pathToFileURL(join(target, "artwork-gallery.mjs")).href);
process.once("exit", () => rmSync(target, { recursive: true, force: true }));

function element() {
  return {
    addEventListener() {},
    disabled: false,
    hidden: false,
    height: 200,
    width: 300,
    style: {},
    textContent: ""
  };
}

function createGallery(overrides = {}) {
  return new ArtworkGallery({
    root: element(),
    stage: element(),
    image: element(),
    previousButton: element(),
    nextButton: element(),
    counter: element(),
    zoomInButton: element(),
    zoomOutButton: element(),
    resetButton: element(),
    status: element(),
    imageFactory: () => ({ set src(value) {} }),
    ...overrides
  });
}

function pointer(clientX, clientY) {
  return { clientX, clientY, pointerId: 1, preventDefault() {} };
}

test("缩放限制在 1 至 3 且重置恢复初始状态", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg"], "作品");
  for (let index = 0; index < 20; index += 1) gallery.zoomIn();
  assert.equal(gallery.scale, 3);
  gallery.offsetX = 80;
  gallery.offsetY = -40;
  for (let index = 0; index < 20; index += 1) gallery.zoomOut();
  gallery.resetView();
  assert.deepEqual([gallery.scale, gallery.offsetX, gallery.offsetY], [1, 0, 0]);
});

test("多图切换不循环并重置缩放和偏移", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg", "/b.jpg"], "作品");
  gallery.zoomIn();
  gallery.offsetX = 30;
  assert.equal(gallery.next(), true);
  assert.deepEqual([gallery.currentIndex, gallery.scale, gallery.offsetX], [1, 1, 0]);
  assert.equal(gallery.next(), false);
  assert.equal(gallery.previous(), true);
  assert.equal(gallery.previous(), false);
});

test("单图隐藏导航，多图显示计数并预加载相邻图", () => {
  const created = [];
  const gallery = createGallery({
    imageFactory: () => ({ set src(value) { created.push(value); } })
  });
  gallery.setImages(["/a.jpg", "/b.jpg", "/c.jpg"], "作品");
  assert.equal(gallery.counter.textContent, "1 / 3");
  assert.equal(gallery.previousButton.disabled, true);
  assert.deepEqual(created, ["/b.jpg"]);
  gallery.setImages(["/only.jpg"], "作品");
  assert.equal(gallery.previousButton.hidden, true);
  assert.equal(gallery.counter.hidden, true);
});

test("放大时拖动图片，原始比例时横向滑动换图", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg", "/b.jpg"], "作品");
  gallery.handlePointerDown(pointer(100, 80));
  gallery.handlePointerUp(pointer(20, 80));
  assert.equal(gallery.currentIndex, 1);
  gallery.zoomIn();
  gallery.handlePointerDown(pointer(100, 80));
  gallery.handlePointerMove(pointer(40, 60));
  gallery.handlePointerUp(pointer(40, 60));
  assert.equal(gallery.currentIndex, 1);
  assert.notEqual(gallery.offsetX, 0);
});

test("当前图片失败时保留导航并显示中文错误", () => {
  const gallery = createGallery();
  gallery.setImages(["/broken.jpg", "/ok.jpg"], "作品");
  gallery.image.onerror();
  assert.equal(gallery.status.textContent, "图片暂时无法加载");
  assert.equal(gallery.next(), true);
  assert.equal(gallery.status.textContent, "");
});
