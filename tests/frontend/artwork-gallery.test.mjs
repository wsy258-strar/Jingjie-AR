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
  const listeners = new Map();
  const attributes = {};
  const captures = new Set();
  return {
    attributes,
    addEventListener(type, listener) {
      const handlers = listeners.get(type) || [];
      handlers.push(listener);
      listeners.set(type, handlers);
    },
    dispatchEvent(event) {
      event.target ||= this;
      for (const listener of listeners.get(event.type) || []) listener(event);
      return !event.defaultPrevented;
    },
    disabled: false,
    hidden: false,
    height: 200,
    width: 300,
    clientHeight: 200,
    clientWidth: 300,
    scrollTop: 0,
    style: {},
    textContent: "",
    setAttribute(name, value) { attributes[name] = String(value); },
    getAttribute(name) { return attributes[name] ?? null; },
    removeAttribute(name) { delete attributes[name]; },
    setPointerCapture(pointerId) {
      captures.add(pointerId);
      this.capturedPointerIds = [...captures];
      this.capturedPointerId = pointerId;
    },
    releasePointerCapture(pointerId) {
      this.releasedPointerIds = [...(this.releasedPointerIds || []), pointerId];
      captures.delete(pointerId);
      this.capturedPointerIds = [...captures];
      if (this.capturedPointerId === pointerId) this.capturedPointerId = null;
    },
    hasPointerCapture(pointerId) {
      if (this.capturedPointerId === null) return false;
      return captures.has(pointerId);
    }
  };
}

function timerHarness() {
  let now = 0;
  let nextId = 1;
  const tasks = new Map();
  return {
    now: () => now,
    setTimeout(callback, delay) {
      const id = nextId++;
      tasks.set(id, { callback, due: now + delay });
      return id;
    },
    clearTimeout(id) { tasks.delete(id); },
    advance(milliseconds) {
      now += milliseconds;
      const ready = [...tasks.entries()]
        .filter(([, task]) => task.due <= now)
        .sort((left, right) => left[1].due - right[1].due);
      for (const [id, task] of ready) {
        if (!tasks.delete(id)) continue;
        task.callback();
      }
    },
    get pending() { return tasks.size; }
  };
}

function mediaQueryHarness(initialMatches = true) {
  const listeners = new Set();
  return {
    matches: initialMatches,
    addEventListener(type, listener) {
      if (type === "change") listeners.add(listener);
    },
    setMatches(matches) {
      this.matches = matches;
      for (const listener of listeners) listener({ type: "change", matches });
    }
  };
}

function createGallery(overrides = {}) {
  const clock = overrides.clock || timerHarness();
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
    immersiveRoot: element(),
    immersiveStage: element(),
    immersiveImage: element(),
    immersiveCloseButton: element(),
    scrollContainer: element(),
    isMobile: () => true,
    now: clock.now,
    setTimeout: clock.setTimeout,
    clearTimeout: clock.clearTimeout,
    imageFactory: () => ({ set src(value) {} }),
    ...overrides
  });
}

function pointer(clientX, clientY, pointerId = 1) {
  return {
    clientX,
    clientY,
    pointerId,
    preventDefault() { this.defaultPrevented = true; }
  };
}

function immersivePointer(gallery, type, clientX, clientY, pointerId = 1, target = gallery.immersiveImage) {
  gallery.immersiveStage.dispatchEvent({
    ...pointer(clientX, clientY, pointerId),
    type,
    target
  });
}

function immersiveTap(gallery, clientX, clientY, pointerId = 1) {
  immersivePointer(gallery, "pointerdown", clientX, clientY, pointerId);
  immersivePointer(gallery, "pointerup", clientX, clientY, pointerId);
}

function immersiveBackgroundTap(gallery, target, clientX, clientY, pointerId = 1) {
  const eventTarget = target === "root" ? gallery.immersiveRoot : gallery.immersiveStage;
  const eventHost = target === "root" ? gallery.immersiveRoot : gallery.immersiveStage;
  eventHost.dispatchEvent({
    ...pointer(clientX, clientY, pointerId), type: "pointerdown", target: eventTarget
  });
  eventHost.dispatchEvent({
    ...pointer(clientX, clientY, pointerId), type: "pointerup", target: eventTarget
  });
  gallery.immersiveRoot.dispatchEvent({
    type: "click", target: eventTarget, clientX, clientY, detail: 1
  });
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
  assert.equal(gallery.status.hidden, true);
  gallery.image.onerror();
  assert.equal(gallery.status.textContent, "图片暂时无法加载");
  assert.equal(gallery.status.hidden, false);
  assert.equal(gallery.next(), true);
  assert.equal(gallery.status.textContent, "");
  assert.equal(gallery.status.hidden, true);
});

test("舞台只为图片拖动捕获指针，并在取消时释放对应捕获", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg"], "作品");
  gallery.zoomIn();
  const buttonEvent = {
    ...pointer(100, 80),
    type: "pointerdown",
    target: { closest: (selector) => selector === "button, input, textarea, select, a" ? {} : null }
  };
  gallery.stage.dispatchEvent(buttonEvent);
  assert.equal(gallery.pointer, null);
  assert.equal(gallery.stage.capturedPointerId, undefined);

  const down = { ...pointer(100, 80), type: "pointerdown", target: gallery.image };
  gallery.stage.dispatchEvent(down);
  assert.equal(gallery.stage.capturedPointerId, 1);
  gallery.stage.dispatchEvent({ ...pointer(60, 80), type: "pointermove", target: gallery.image });
  assert.notEqual(gallery.offsetX, 0);

  const dragStart = { type: "dragstart", preventDefault() { this.defaultPrevented = true; } };
  gallery.image.dispatchEvent(dragStart);
  assert.equal(dragStart.defaultPrevented, true);

  gallery.stage.dispatchEvent({ ...pointer(60, 80), type: "pointercancel", target: gallery.image });
  assert.equal(gallery.pointer, null);
  assert.equal(gallery.stage.capturedPointerId, null);

  gallery.stage.dispatchEvent(down);
  gallery.stage.capturedPointerId = null;
  gallery.stage.dispatchEvent({ ...pointer(60, 80), type: "lostpointercapture", target: gallery.image });
  assert.equal(gallery.pointer, null);
  assert.deepEqual(gallery.stage.releasedPointerIds, [1]);
});

test("缩小时按新的图片边界重新夹紧拖动位移", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg"], "作品");
  for (let index = 0; index < 20; index += 1) gallery.zoomIn();
  gallery.offsetX = 300;
  gallery.offsetY = -200;

  gallery.zoomOut();

  assert.equal(gallery.scale, 2.5);
  assert.deepEqual([gallery.offsetX, gallery.offsetY], [225, -150]);
});

test("舞台尺寸变化后重新夹紧当前图片位移", () => {
  const originalResizeObserver = globalThis.ResizeObserver;
  let notifyResize = () => {};
  globalThis.ResizeObserver = class {
    constructor(callback) { notifyResize = callback; }
    observe() {}
  };

  try {
    const gallery = createGallery();
    gallery.setImages(["/a.jpg"], "作品");
    for (let index = 0; index < 20; index += 1) gallery.zoomIn();
    gallery.offsetX = 300;
    gallery.stage.clientWidth = 400;

    notifyResize();

    assert.equal(gallery.offsetX, 250);
  } finally {
    globalThis.ResizeObserver = originalResizeObserver;
  }
});

test("跨越移动端断点或清空图片会刷新普通图片入口语义", () => {
  const originalResizeObserver = globalThis.ResizeObserver;
  let notifyResize = () => {};
  let mobile = true;
  globalThis.ResizeObserver = class {
    constructor(callback) { notifyResize = callback; }
    observe() {}
  };
  try {
    const gallery = createGallery({ isMobile: () => mobile });
    gallery.setImages(["/a.jpg"], "作品");
    assert.equal(gallery.image.getAttribute("role"), "button");

    mobile = false;
    notifyResize();
    assert.equal(gallery.image.getAttribute("role"), null);
    assert.equal(gallery.image.getAttribute("tabindex"), null);

    mobile = true;
    gallery.clear();
    assert.equal(gallery.image.getAttribute("role"), null);
  } finally {
    globalThis.ResizeObserver = originalResizeObserver;
  }
});

test("顶层打开后跨出移动端断点会立即退出并清理交互状态", () => {
  const clock = timerHarness();
  const mobileMediaQuery = mediaQueryHarness(true);
  let hideCount = 0;
  let gallery;
  gallery = createGallery({
    clock,
    mobileMediaQuery,
    isMobile: () => mobileMediaQuery.matches,
    hideImmersiveLayer: () => {
      assert.equal(gallery.image.getAttribute("tabindex"), "0");
      hideCount += 1;
    }
  });
  gallery.setImages(["/a.jpg"], "作品");
  gallery.scrollContainer.scrollTop = 126;
  gallery.openImmersive();
  gallery.scrollContainer.scrollTop = 0;
  gallery.immersiveScale = 3;
  immersiveTap(gallery, 100, 100);
  immersivePointer(gallery, "pointerdown", 40, 50, 7);

  mobileMediaQuery.setMatches(false);

  assert.equal(gallery.isImmersive(), false);
  assert.equal(gallery.immersiveRoot.getAttribute("aria-hidden"), "true");
  assert.deepEqual(
    [gallery.immersiveScale, gallery.immersiveOffsetX, gallery.immersiveOffsetY],
    [1, 0, 0]
  );
  assert.equal(gallery.pointers.size, 0);
  assert.equal(clock.pending, 0);
  assert.equal(gallery.scrollContainer.scrollTop, 126);
  assert.equal(gallery.image.getAttribute("role"), null);
  assert.equal(hideCount, 1);
});

test("移动端点击普通图片进入最高层并显示当前图片", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg", "/b.jpg"], "作品");
  gallery.next();
  gallery.scrollContainer.scrollTop = 168;

  gallery.stage.dispatchEvent({
    ...pointer(100, 80), type: "pointerdown", target: gallery.image
  });
  gallery.stage.dispatchEvent({
    ...pointer(100, 80), type: "pointerup", target: gallery.stage
  });

  assert.equal(gallery.isImmersive(), true);
  assert.equal(gallery.immersiveRoot.getAttribute("aria-hidden"), "false");
  assert.equal(gallery.immersiveImage.src, "/b.jpg");
  assert.equal(gallery.savedScrollTop, 168);
  assert.equal(gallery.image.getAttribute("role"), "button");
  assert.equal(gallery.image.getAttribute("tabindex"), "0");
});

test("移动端普通画廊横滑后的合成点击不会误开顶层查看器", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg", "/b.jpg"], "作品");

  gallery.stage.dispatchEvent({
    ...pointer(140, 80), type: "pointerdown", target: gallery.image
  });
  gallery.stage.dispatchEvent({
    ...pointer(60, 80), type: "pointerup", target: gallery.image
  });
  gallery.image.dispatchEvent({ type: "click", detail: 1, target: gallery.image });

  assert.equal(gallery.currentIndex, 1);
  assert.equal(gallery.isImmersive(), false);
});

test("顶层图片 280ms 内近距离双击在 1 倍与 2 倍间切换且不退出", () => {
  const clock = timerHarness();
  const gallery = createGallery({ clock });
  gallery.setImages(["/a.jpg"], "作品");
  gallery.openImmersive();

  immersiveTap(gallery, 100, 100);
  clock.advance(200);
  immersiveTap(gallery, 120, 112);

  assert.equal(gallery.immersiveScale, 2);
  assert.equal(gallery.isImmersive(), true);
  assert.equal(clock.pending, 0);

  clock.advance(100);
  immersiveTap(gallery, 106, 104);
  clock.advance(120);
  immersiveTap(gallery, 100, 100);
  assert.equal(gallery.immersiveScale, 1);
  assert.equal(gallery.isImmersive(), true);
});

test("顶层原始比例单击图片延迟到期后退出并恢复详情滚动位置", () => {
  const clock = timerHarness();
  const gallery = createGallery({ clock });
  gallery.setImages(["/a.jpg"], "作品");
  gallery.scrollContainer.scrollTop = 142;
  gallery.openImmersive();
  gallery.scrollContainer.scrollTop = 0;

  immersiveTap(gallery, 80, 60);
  clock.advance(279);
  assert.equal(gallery.isImmersive(), true);
  clock.advance(1);

  assert.equal(gallery.isImmersive(), false);
  assert.equal(gallery.scrollContainer.scrollTop, 142);
});

test("顶层双指缩放被限制在 1 至 4 倍", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg"], "作品");
  gallery.openImmersive();

  immersivePointer(gallery, "pointerdown", 100, 100, 1);
  immersivePointer(gallery, "pointerdown", 200, 100, 2);
  immersivePointer(gallery, "pointermove", 600, 100, 2);

  assert.equal(gallery.immersiveScale, 4);
  assert.equal(gallery.isImmersive(), true);
});

test("顶层放大后单指拖动只改变偏移且单击图片不退出", () => {
  const clock = timerHarness();
  const gallery = createGallery({ clock });
  gallery.setImages(["/a.jpg", "/b.jpg"], "作品");
  gallery.openImmersive();
  gallery.immersiveScale = 2;

  immersivePointer(gallery, "pointerdown", 180, 100, 1);
  immersivePointer(gallery, "pointermove", 90, 70, 1);
  immersivePointer(gallery, "pointerup", 90, 70, 1);

  assert.equal(gallery.currentIndex, 0);
  assert.notDeepEqual([gallery.immersiveOffsetX, gallery.immersiveOffsetY], [0, 0]);
  immersiveTap(gallery, 100, 100);
  clock.advance(280);
  assert.equal(gallery.isImmersive(), true);
});

test("顶层原始比例横滑超过 50px 切换相邻图片并保持查看层", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg", "/b.jpg"], "作品");
  gallery.openImmersive();

  immersivePointer(gallery, "pointerdown", 150, 80, 1);
  immersivePointer(gallery, "pointerup", 80, 82, 1);

  assert.equal(gallery.currentIndex, 1);
  assert.equal(gallery.immersiveImage.src, "/b.jpg");
  assert.equal(gallery.isImmersive(), true);
});

test("顶层单击后紧接横滑会取消待执行退出", () => {
  const clock = timerHarness();
  const gallery = createGallery({ clock });
  gallery.setImages(["/a.jpg", "/b.jpg"], "作品");
  gallery.openImmersive();
  immersiveTap(gallery, 100, 100);
  clock.advance(100);

  immersivePointer(gallery, "pointerdown", 160, 90, 1);
  immersivePointer(gallery, "pointerup", 80, 90, 1);
  clock.advance(180);

  assert.equal(gallery.currentIndex, 1);
  assert.equal(gallery.isImmersive(), true);
  assert.equal(clock.pending, 0);
});

test("顶层 pinch 结束后由剩余手指平滑接管拖动", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg"], "作品");
  gallery.openImmersive();
  immersivePointer(gallery, "pointerdown", 100, 100, 1);
  immersivePointer(gallery, "pointerdown", 200, 100, 2);
  immersivePointer(gallery, "pointermove", 80, 100, 1);
  immersivePointer(gallery, "pointerup", 200, 100, 2);
  const offsetBeforePan = gallery.immersiveOffsetX;

  immersivePointer(gallery, "pointermove", 81, 100, 1);

  assert.ok(Math.abs(gallery.immersiveOffsetX - offsetBeforePan) <= 1);
});

test("顶层 pinch 指针取消后由剩余手指平滑接管拖动", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg"], "作品");
  gallery.openImmersive();
  immersivePointer(gallery, "pointerdown", 100, 100, 1);
  immersivePointer(gallery, "pointerdown", 200, 100, 2);
  immersivePointer(gallery, "pointermove", 80, 100, 1);
  immersivePointer(gallery, "pointercancel", 200, 100, 2);
  const offsetBeforePan = gallery.immersiveOffsetX;

  immersivePointer(gallery, "pointermove", 81, 100, 1);

  assert.ok(Math.abs(gallery.immersiveOffsetX - offsetBeforePan) <= 1);
});

test("顶层舞台尺寸变化后重新夹紧放大图片位移", () => {
  const originalResizeObserver = globalThis.ResizeObserver;
  let notifyResize = () => {};
  globalThis.ResizeObserver = class {
    constructor(callback) { notifyResize = callback; }
    observe() {}
  };
  try {
    const gallery = createGallery();
    gallery.setImages(["/a.jpg"], "作品");
    gallery.openImmersive();
    gallery.immersiveScale = 4;
    gallery.immersiveOffsetX = 500;
    gallery.immersiveStage.clientWidth = 600;

    notifyResize();

    assert.equal(gallery.immersiveOffsetX, 300);
  } finally {
    globalThis.ResizeObserver = originalResizeObserver;
  }
});

test("顶层双指缩小时比例不会低于 1", () => {
  const gallery = createGallery();
  gallery.setImages(["/a.jpg"], "作品");
  gallery.openImmersive();
  immersivePointer(gallery, "pointerdown", 100, 100, 1);
  immersivePointer(gallery, "pointerdown", 200, 100, 2);

  immersivePointer(gallery, "pointermove", 110, 100, 2);

  assert.equal(gallery.immersiveScale, 1);
});

test("关闭顶层查看器清理缩放、偏移、指针与待执行单击", () => {
  const clock = timerHarness();
  const gallery = createGallery({ clock });
  gallery.setImages(["/a.jpg"], "作品");
  gallery.openImmersive();
  gallery.immersiveScale = 3;
  gallery.immersiveOffsetX = 50;
  gallery.immersiveOffsetY = -20;
  immersiveTap(gallery, 100, 100);
  immersivePointer(gallery, "pointerdown", 40, 50, 7);
  assert.equal(clock.pending, 1);
  assert.equal(gallery.pointers.size, 1);

  gallery.closeImmersive();

  assert.equal(gallery.isImmersive(), false);
  assert.deepEqual(
    [gallery.immersiveScale, gallery.immersiveOffsetX, gallery.immersiveOffsetY],
    [1, 0, 0]
  );
  assert.equal(gallery.pointers.size, 0);
  assert.equal(gallery.singleTapTimer, null);
  assert.equal(clock.pending, 0);
});

test("顶层舞台背景单击让出双击窗口且第二击放大", () => {
  const clock = timerHarness();
  const gallery = createGallery({ clock });
  gallery.setImages(["/a.jpg"], "作品");
  gallery.openImmersive();

  immersiveBackgroundTap(gallery, "stage", 100, 100);
  clock.advance(279);
  assert.equal(gallery.isImmersive(), true);

  immersiveBackgroundTap(gallery, "stage", 112, 108);
  assert.equal(gallery.immersiveScale, 2);
  assert.equal(gallery.isImmersive(), true);
  assert.equal(clock.pending, 0);
});

test("顶层根背景单击让出双击窗口且关闭按钮仍立即退出", () => {
  const clock = timerHarness();
  const gallery = createGallery({ clock });
  gallery.setImages(["/a.jpg"], "作品");
  gallery.openImmersive();

  immersiveBackgroundTap(gallery, "root", 80, 60);
  clock.advance(279);
  assert.equal(gallery.isImmersive(), true);

  immersiveBackgroundTap(gallery, "root", 86, 66);
  assert.equal(gallery.immersiveScale, 2);
  assert.equal(gallery.isImmersive(), true);

  gallery.immersiveCloseButton.dispatchEvent({ type: "click", target: gallery.immersiveCloseButton });
  assert.equal(gallery.isImmersive(), false);
  assert.equal(clock.pending, 0);
});
