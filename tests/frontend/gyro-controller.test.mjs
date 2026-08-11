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

const { GyroController } = loaded.module;

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

  assert.equal(await controller.autoEnable(), true);
  controller.suspend("modal");
  controller.suspend("drawer");
  controller.resume("modal");
  assert.equal(enabled, 1);
  controller.resume("drawer");
  assert.equal(enabled, 2);
  assert.equal(disabled, 1);
});
