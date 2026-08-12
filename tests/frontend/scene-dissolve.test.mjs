import assert from "node:assert/strict";
import test from "node:test";
import { SceneDissolve } from "../../WebApps/ARServer/www/js/scene-dissolve.js";

class FakeClassList {
  constructor() {
    this.values = new Set();
  }

  add(...names) {
    names.forEach((name) => this.values.add(name));
  }

  remove(...names) {
    names.forEach((name) => this.values.delete(name));
  }

  contains(name) {
    return this.values.has(name);
  }
}

function fakeOverlay() {
  const properties = new Map();
  return {
    src: "",
    classList: new FakeClassList(),
    style: {
      setProperty(name, value) { properties.set(name, value); },
      removeProperty(name) { properties.delete(name); },
      getPropertyValue(name) { return properties.get(name) || ""; }
    }
  };
}

function fakeClock() {
  let now = 0;
  let nextId = 1;
  const tasks = new Map();
  return {
    now: () => now,
    setTimeout(callback, delay) {
      const id = nextId++;
      tasks.set(id, { callback, at: now + Number(delay) });
      return id;
    },
    clearTimeout(id) { tasks.delete(id); },
    advance(milliseconds) {
      const target = now + milliseconds;
      while (true) {
        const due = [...tasks.entries()]
          .filter(([, task]) => task.at <= target)
          .sort((left, right) => left[1].at - right[1].at)[0];
        if (!due) break;
        const [id, task] = due;
        tasks.delete(id);
        now = task.at;
        task.callback();
      }
      now = target;
    },
    pendingCount: () => tasks.size
  };
}

test("慢网 900ms 后保留旧图，且仅当前 generation 可进入完成叠化", () => {
  const overlay = fakeOverlay();
  const clock = fakeClock();
  const dissolve = new SceneDissolve({
    viewer: { querySelector: () => ({ toDataURL: () => "data:image/jpeg;base64,old" }) },
    overlay,
    nowFn: clock.now,
    setTimeoutFn: clock.setTimeout,
    clearTimeoutFn: clock.clearTimeout
  });

  assert.equal(dissolve.begin({ generation: 4 }), true);
  assert.equal(overlay.src, "data:image/jpeg;base64,old");
  assert.equal(overlay.classList.contains("is-releasing"), true);

  clock.advance(900);
  assert.equal(overlay.classList.contains("is-waiting"), true);
  assert.equal(overlay.src, "data:image/jpeg;base64,old");
  assert.equal(dissolve.markPreviewVisible(3), false);
  assert.equal(overlay.classList.contains("is-waiting"), true);
  assert.equal(dissolve.markPreviewVisible(4), true);
  assert.equal(overlay.classList.contains("is-completing"), true);
  assert.equal(overlay.style.getPropertyValue("--scene-dissolve-complete-ms"), "600ms");

  clock.advance(599);
  assert.equal(overlay.src, "data:image/jpeg;base64,old");
  clock.advance(1);
  assert.equal(overlay.src, "");
});

test("首次进入没有 canvas 时使用场景预览作为启动遮罩", () => {
  const overlay = fakeOverlay();
  const dissolve = new SceneDissolve({
    viewer: { querySelector: () => null },
    overlay
  });

  assert.equal(dissolve.begin({ generation: 1, fallbackUrl: "/preview.jpg" }), true);
  assert.equal(overlay.src, "/preview.jpg");
  assert.equal(overlay.classList.contains("is-releasing"), true);
  dissolve.cancel(1);
});

test("画布导出失败时回退预览 URL，缺少两者才不启动", () => {
  const overlay = fakeOverlay();
  const dissolve = new SceneDissolve({
    viewer: { querySelector: () => ({ toDataURL: () => { throw new Error("tainted canvas"); } }) },
    overlay
  });

  assert.equal(dissolve.begin({ generation: 1, fallbackUrl: "/fallback.jpg" }), true);
  assert.equal(overlay.src, "/fallback.jpg");
  dissolve.cancel(1);
  assert.equal(dissolve.begin({ generation: 2 }), false);
  assert.equal(overlay.src, "");
});

test("取消当前 generation 清空全部状态、图片和计时器", () => {
  const overlay = fakeOverlay();
  const clock = fakeClock();
  const dissolve = new SceneDissolve({
    viewer: { querySelector: () => null },
    overlay,
    nowFn: clock.now,
    setTimeoutFn: clock.setTimeout,
    clearTimeoutFn: clock.clearTimeout
  });

  dissolve.begin({ generation: 7, fallbackUrl: "/preview.jpg" });
  assert.equal(clock.pendingCount(), 1);
  assert.equal(dissolve.cancel(6), false);
  assert.equal(clock.pendingCount(), 1);
  assert.equal(dissolve.cancel(7), true);
  assert.equal(clock.pendingCount(), 0);
  assert.equal(overlay.src, "");
  for (const name of ["is-releasing", "is-waiting", "is-completing"])
    assert.equal(overlay.classList.contains(name), false);
});

test("新 generation 无可用承载图时仍清理旧遮罩和旧计时器", () => {
  const overlay = fakeOverlay();
  const clock = fakeClock();
  const dissolve = new SceneDissolve({
    viewer: { querySelector: () => null },
    overlay,
    nowFn: clock.now,
    setTimeoutFn: clock.setTimeout,
    clearTimeoutFn: clock.clearTimeout
  });

  assert.equal(dissolve.begin({ generation: 1, fallbackUrl: "/old-preview.jpg" }), true);
  assert.equal(clock.pendingCount(), 1);
  assert.equal(dissolve.begin({ generation: 2 }), false);
  assert.equal(clock.pendingCount(), 0);
  assert.equal(overlay.src, "");
  assert.equal(overlay.classList.contains("is-releasing"), false);
});

test("完整加载事件可在预览事件缺失时启动至少 300ms 的恢复叠化", () => {
  const overlay = fakeOverlay();
  const clock = fakeClock();
  const dissolve = new SceneDissolve({
    viewer: { querySelector: () => null },
    overlay,
    nowFn: clock.now,
    setTimeoutFn: clock.setTimeout,
    clearTimeoutFn: clock.clearTimeout
  });

  dissolve.begin({ generation: 8, fallbackUrl: "/preview.jpg" });
  clock.advance(1800);
  assert.equal(dissolve.complete(7), false);
  assert.equal(dissolve.complete(8), true);
  assert.equal(overlay.classList.contains("is-completing"), true);
  assert.equal(overlay.style.getPropertyValue("--scene-dissolve-complete-ms"), "300ms");
  clock.advance(300);
  assert.equal(overlay.src, "");
});
