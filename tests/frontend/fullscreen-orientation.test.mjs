import assert from "node:assert/strict";
import test from "node:test";
import { FullscreenOrientation } from "../../WebApps/ARServer/www/js/fullscreen-orientation.js";

function createHarness({ requestError, lockError } = {}) {
  const target = {
    requestCalls: 0,
    async requestFullscreen() {
      this.requestCalls += 1;
      if (requestError) throw requestError;
      documentObject.fullscreenElement = target;
    }
  };
  const documentObject = {
    fullscreenElement: null,
    exitCalls: 0,
    async exitFullscreen() {
      this.exitCalls += 1;
      this.fullscreenElement = null;
    }
  };
  const screenObject = {
    orientation: {
      lockCalls: [],
      unlockCalls: 0,
      async lock(value) {
        this.lockCalls.push(value);
        if (lockError) throw lockError;
      },
      unlock() { this.unlockCalls += 1; }
    }
  };
  let fallbackCalls = 0;
  const controller = new FullscreenOrientation({
    documentObject,
    screenObject,
    onLandscapeFallback: () => { fallbackCalls += 1; }
  });
  return { controller, documentObject, fallbackCalls: () => fallbackCalls, screenObject, target };
}

test("进入全屏成功后锁定横屏", async () => {
  const { controller, screenObject, target } = createHarness();

  assert.equal(await controller.toggle(target), true);

  assert.deepEqual(screenObject.orientation.lockCalls, ["landscape"]);
});

test("再次切换时退出全屏并解除方向锁定", async () => {
  const { controller, documentObject, screenObject, target } = createHarness();
  await controller.toggle(target);

  assert.equal(await controller.toggle(target), true);

  assert.equal(documentObject.exitCalls, 1);
  assert.equal(screenObject.orientation.unlockCalls, 1);
});

test("横屏锁定失败时保持全屏并且只提示一次", async () => {
  const { controller, documentObject, fallbackCalls, target } = createHarness({
    lockError: new Error("lock unsupported")
  });

  assert.equal(await controller.toggle(target), true);
  assert.equal(documentObject.fullscreenElement, target);
  assert.equal(fallbackCalls(), 1);

  controller.handleFullscreenChange(target);
  assert.equal(fallbackCalls(), 1);
});

test("请求全屏失败时返回 false 且不尝试锁定横屏", async () => {
  const { controller, screenObject, target } = createHarness({
    requestError: new Error("fullscreen denied")
  });

  assert.equal(await controller.toggle(target), false);
  assert.deepEqual(screenObject.orientation.lockCalls, []);
});
