import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

async function loadGyroControllerModule() {
  const target = await mkdtemp(join(tmpdir(), "jingjie-ar-gyro-controller-"));
  const source = new URL("../../WebApps/ARServer/www/js/gyro-controller.js", import.meta.url);
  const content = await readFile(source, "utf8");
  const modulePath = join(target, "gyro-controller.mjs");
  await writeFile(modulePath, content);
  return { target, module: await import(pathToFileURL(modulePath).href) };
}

let loaded;
try {
  loaded = await loadGyroControllerModule();
} catch (error) {
  // 保留真实的模块缺失错误，让 RED 阶段明确失败在待实现文件上。
  throw error;
}
process.once("exit", () => rmSync(loaded.target, { recursive: true, force: true }));

const { GyroController, isMobileDevice } = loaded.module;

test("移动设备判定优先采用 Client Hints 并回退到受限 UA", () => {
  assert.equal(isMobileDevice({
    userAgentData: { mobile: false },
    userAgent: "Mozilla/5.0 (Linux; Android 14)"
  }), false);
  assert.equal(isMobileDevice({
    userAgentData: { mobile: true },
    userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
  }), true);
  assert.equal(isMobileDevice({
    userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
  }), false);
  for (const userAgent of ["Android", "iPhone", "iPad", "iPod"])
    assert.equal(isMobileDevice({ userAgent }), true, `${userAgent} 应识别为移动设备`);
  assert.equal(isMobileDevice(undefined), false);
});

test("PC 插件 unavailable 静默降级", () => {
  let denied = 0;
  const controller = new GyroController({
    adapter: { enableGyro() {}, disableGyro() {} },
    deviceOrientation: undefined,
    deviceMotion: undefined,
    notifyUnavailable: false,
    onDenied: () => { denied += 1; }
  });

  assert.equal(controller.handlePluginState("unavailable"), false);
  assert.equal(denied, 0);
});

test("PC 策略不抑制显式权限拒绝提示", async () => {
  let denied = 0;
  const controller = new GyroController({
    adapter: { enableGyro() {}, disableGyro() {} },
    deviceOrientation: { requestPermission: async () => "denied" },
    deviceMotion: undefined,
    notifyUnavailable: false,
    onDenied: () => { denied += 1; }
  });

  assert.equal(await controller.requestFromGesture(), false);
  assert.equal(denied, 1);
});

test("iOS 同一页面只申请一次", async () => {
  let requests = 0;
  let enabled = 0;
  const controller = new GyroController({
    adapter: {
      isGyroAvailable: () => true,
      enableGyro: () => { enabled += 1; },
      disableGyro() {}
    },
    deviceOrientation: {
      requestPermission: async () => {
        requests += 1;
        return "granted";
      }
    }
  });
  controller.handlePluginState("available");
  assert.equal(await controller.requestFromGesture(), true);
  assert.equal(await controller.requestFromGesture(), true);
  assert.equal(requests, 1);
  assert.equal(enabled, 1);
});

test("所有暂停原因解除后才重新启用陀螺仪", async () => {
  let enabled = 0;
  let disabled = 0;
  const controller = new GyroController({
    adapter: {
      isGyroAvailable: () => true,
      enableGyro: () => { enabled += 1; },
      disableGyro: () => { disabled += 1; }
    }
  });

  controller.handlePluginState("available");
  assert.equal(await controller.autoEnable(), true);
  controller.suspend("modal");
  controller.suspend("drawer");
  controller.resume("modal");
  assert.equal(enabled, 1);
  controller.resume("drawer");
  assert.equal(enabled, 2);
  assert.equal(disabled, 1);
});

test("Android 类环境等待 available 后自动启用", async () => {
  let enabled = 0;
  const controller = new GyroController({
    adapter: {
      enableGyro: () => { enabled += 1; },
      disableGyro() {}
    },
    deviceOrientation: undefined,
    deviceMotion: undefined
  });

  assert.equal(await controller.autoEnable(), false);
  assert.equal(enabled, 0);
  assert.equal(controller.handlePluginState("available"), true);
  assert.equal(enabled, 1);
});

test("权限可在插件 available 前申请并于稍后启用", async () => {
  let requests = 0;
  let enabled = 0;
  const controller = new GyroController({
    adapter: {
      enableGyro: () => { enabled += 1; },
      disableGyro() {}
    },
    deviceOrientation: {
      requestPermission: async () => {
        requests += 1;
        return "granted";
      }
    },
    deviceMotion: undefined
  });

  assert.equal(await controller.requestFromGesture(), false);
  assert.equal(requests, 1);
  assert.equal(enabled, 0);
  assert.equal(controller.handlePluginState("available"), true);
  assert.equal(enabled, 1);
});

test("方向与运动权限在同一手势内均通过后才启用", async () => {
  const requests = [];
  let enabled = 0;
  const controller = new GyroController({
    adapter: {
      enableGyro: () => { enabled += 1; },
      disableGyro() {}
    },
    deviceOrientation: {
      requestPermission: async () => {
        requests.push("orientation");
        return "granted";
      }
    },
    deviceMotion: {
      requestPermission: async () => {
        requests.push("motion");
        return "granted";
      }
    }
  });

  controller.handlePluginState("available");
  assert.equal(await controller.requestFromGesture(), true);
  assert.deepEqual(requests.sort(), ["motion", "orientation"]);
  assert.equal(enabled, 1);
});

test("任一传感器权限拒绝时只提示一次且不启用", async () => {
  let denied = 0;
  let enabled = 0;
  const controller = new GyroController({
    adapter: {
      enableGyro: () => { enabled += 1; },
      disableGyro() {}
    },
    deviceOrientation: { requestPermission: async () => "granted" },
    deviceMotion: { requestPermission: async () => "denied" },
    onDenied: () => { denied += 1; }
  });

  controller.handlePluginState("available");
  assert.equal(await controller.requestFromGesture(), false);
  assert.equal(await controller.requestFromGesture(), false);
  controller.handlePluginState("unavailable");
  assert.equal(enabled, 0);
  assert.equal(denied, 1);
});

test("权限请求异常被吸收并只提示一次", async () => {
  let denied = 0;
  const controller = new GyroController({
    adapter: { enableGyro() {}, disableGyro() {} },
    deviceOrientation: {
      requestPermission: async () => { throw new Error("blocked"); }
    },
    deviceMotion: undefined,
    onDenied: () => { denied += 1; }
  });

  controller.handlePluginState("available");
  assert.equal(await controller.requestFromGesture(), false);
  assert.equal(await controller.requestFromGesture(), false);
  assert.equal(denied, 1);
});

test("unavailable 关闭插件且 resume 不会越过可用性条件", async () => {
  let enabled = 0;
  let disabled = 0;
  let denied = 0;
  const controller = new GyroController({
    adapter: {
      enableGyro: () => { enabled += 1; },
      disableGyro: () => { disabled += 1; }
    },
    deviceOrientation: undefined,
    deviceMotion: undefined,
    onDenied: () => { denied += 1; }
  });

  controller.handlePluginState("available");
  assert.equal(enabled, 1);
  controller.suspend("modal");
  assert.equal(disabled, 1);
  controller.handlePluginState("unavailable");
  assert.equal(controller.resume("modal"), false);
  assert.equal(enabled, 1);
  assert.equal(denied, 1);
  assert.equal(controller.handlePluginState("available"), true);
  assert.equal(enabled, 2);
});
