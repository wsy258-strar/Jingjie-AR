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
  return { src: "", classList: new FakeClassList() };
}

test("仅当前代次可让已捕获的旧全景快照叠化并清理", () => {
  const overlay = fakeOverlay();
  const timers = [];
  const dissolve = new SceneDissolve({
    viewer: { querySelector: () => ({ toDataURL: () => "data:image/jpeg;base64,old" }) },
    overlay,
    requestAnimationFrameFn: (callback) => callback(),
    setTimeoutFn: (callback) => {
      timers.push(callback);
      return timers.length;
    },
    clearTimeoutFn: () => {}
  });

  assert.equal(dissolve.begin(2), true);
  assert.equal(overlay.src, "data:image/jpeg;base64,old");
  assert.equal(overlay.classList.contains("is-held"), true);
  assert.equal(dissolve.finish(1), false);
  assert.equal(overlay.classList.contains("is-held"), true);
  assert.equal(dissolve.finish(2), true);
  assert.equal(overlay.classList.contains("is-dissolving"), true);
  timers.at(-1)();
  assert.equal(overlay.src, "");
  assert.equal(overlay.classList.contains("is-held"), false);
  assert.equal(overlay.classList.contains("is-dissolving"), false);
});

test("无法导出画布快照时不阻断场景切换", () => {
  const overlay = fakeOverlay();
  const dissolve = new SceneDissolve({
    viewer: { querySelector: () => ({ toDataURL: () => { throw new Error("tainted canvas"); } }) },
    overlay
  });

  assert.equal(dissolve.begin(1), false);
  assert.equal(overlay.src, "");
  assert.equal(overlay.classList.contains("is-held"), false);
});
