import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

async function loadVisitorSession() {
  const target = await mkdtemp(join(tmpdir(), "jingjie-ar-visitor-session-"));
  const source = new URL("../../WebApps/ARServer/www/js/", import.meta.url);
  for (const name of ["api-client", "visitor-session"]) {
    let content = await readFile(new URL(name + ".js", source), "utf8");
    content = content.replaceAll("./api-client.js", "./api-client.mjs");
    await writeFile(join(target, name + ".mjs"), content);
  }
  return {
    target,
    visitor: await import(pathToFileURL(join(target, "visitor-session.mjs")).href)
  };
}

const modules = await loadVisitorSession();
const { VisitorSession } = modules.visitor;
process.once("exit", () => rmSync(modules.target, { recursive: true, force: true }));

function storageWith(values = {}) {
  const data = new Map(Object.entries(values));
  return {
    getItem(key) { return data.has(key) ? data.get(key) : null; },
    setItem(key, value) { data.set(key, String(value)); },
    removeItem(key) { data.delete(key); }
  };
}

test("每次初始化生成新的 requestId，成功后保存访客令牌", async () => {
  const storage = storageWith();
  const ids = ["boot-1", "boot-2"];
  const payloads = [];
  const client = {
    async request(path, options) {
      assert.equal(path, "/api/visitors/session");
      payloads.push(options.body);
      return { visitorToken: "visitor-" + payloads.length, totalViews: payloads.length };
    }
  };
  const session = new VisitorSession({ client, storage, randomUUID: () => ids.shift() });

  assert.equal((await session.bootstrap()).visitorToken, "visitor-1");
  assert.equal((await session.bootstrap()).visitorToken, "visitor-2");
  assert.deepEqual(payloads, [
    { bootstrapRequestId: "boot-1" },
    { bootstrapRequestId: "boot-2" }
  ]);
  assert.equal(storage.getItem("ar.visitorToken"), "visitor-2");
  assert.equal(session.available, true);
});

test("初始化的传输重试复用同一个 requestId", async () => {
  const payloads = [];
  const client = {
    async request(path, options) {
      payloads.push(options.body.bootstrapRequestId);
      if (payloads.length === 1) throw new TypeError("network down");
      return { visitorToken: "visitor-1" };
    }
  };
  const session = new VisitorSession({
    client,
    storage: storageWith(),
    randomUUID: () => "boot-retry",
    retryDelay: () => Promise.resolve()
  });

  await session.bootstrap();
  assert.deepEqual(payloads, ["boot-retry", "boot-retry"]);
});

test("初始化失败只标记服务不可用，不阻断调用方", async () => {
  const session = new VisitorSession({
    client: { async request() { throw new Error("offline"); } },
    storage: storageWith(),
    randomUUID: () => "boot-fail",
    retryDelay: () => Promise.resolve()
  });

  assert.equal(await session.bootstrap(), null);
  assert.equal(session.available, false);
  assert.equal(session.unavailable, true);
});

test("无法生成 requestId 时也降级为统计暂不可用", async () => {
  const session = new VisitorSession({
    client: { async request() { throw new Error("不应发起请求"); } },
    storage: storageWith(),
    randomUUID: () => { throw new Error("crypto unavailable"); }
  });

  assert.equal(await session.bootstrap(), null);
  assert.equal(session.available, false);
  assert.equal(session.unavailable, true);
});

test("心跳只建立一组定时器，pagehide 使用 keepalive 退出", async () => {
  const calls = [];
  const listeners = new Map();
  let intervals = 0;
  let cleared = 0;
  const eventTarget = {
    addEventListener(type, listener) { listeners.set(type, listener); },
    removeEventListener(type) { listeners.delete(type); }
  };
  const session = new VisitorSession({
    client: { async request(path, options) { calls.push({ path, options }); return {}; } },
    storage: storageWith({ "ar.visitorToken": "visitor-1" }),
    eventTarget,
    setIntervalImpl() { intervals += 1; return 7; },
    clearIntervalImpl() { cleared += 1; }
  });

  session.startHeartbeat();
  session.startHeartbeat();
  assert.equal(intervals, 1);
  listeners.get("pagehide")();
  await Promise.resolve();
  assert.equal(cleared, 1);
  assert.deepEqual(calls[0], {
    path: "/api/presence/exit",
    options: { method: "POST", visitor: true, keepalive: true }
  });
});
