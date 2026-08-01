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
    visitor: await import(pathToFileURL(join(target, "visitor-session.mjs")).href),
    api: await import(pathToFileURL(join(target, "api-client.mjs")).href)
  };
}

const modules = await loadVisitorSession();
const { VisitorSession } = modules.visitor;
const { ApiError } = modules.api;
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

test("心跳失败会显式标记不可用，后续定时心跳成功后恢复可用", async () => {
  let intervalCallback;
  let heartbeatCalls = 0;
  const client = {
    async request(path) {
      assert.equal(path, "/api/presence/heartbeat");
      heartbeatCalls += 1;
      if (heartbeatCalls === 1) throw new ApiError(503, "SERVICE_UNAVAILABLE", "down", "r1");
      return {};
    }
  };
  const session = new VisitorSession({
    client,
    storage: storageWith({ "ar.visitorToken": "visitor-1" }),
    setIntervalImpl(callback) { intervalCallback = callback; return 9; },
    clearIntervalImpl() {}
  });
  session.available = true;

  session.startHeartbeat();
  intervalCallback();
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(session.available, false);
  assert.equal(session.unavailable, true);

  intervalCallback();
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(heartbeatCalls, 2);
  assert.equal(session.available, true);
  assert.equal(session.unavailable, false);
});

test("并发 401 心跳只用页面原 requestId 发起一次身份恢复", async () => {
  const storage = storageWith();
  const bootstrapIds = [];
  let heartbeatCalls = 0;
  let recoveryResolve;
  const recoveryGate = new Promise((resolve) => { recoveryResolve = resolve; });
  const client = {
    async request(path, options) {
      if (path === "/api/visitors/session") {
        bootstrapIds.push(options.body.bootstrapRequestId);
        if (bootstrapIds.length === 1) return { visitorToken: "visitor-old" };
        await recoveryGate;
        return { visitorToken: "visitor-recovered" };
      }
      assert.equal(path, "/api/presence/heartbeat");
      heartbeatCalls += 1;
      throw new ApiError(401, "VISITOR_TOKEN_INVALID", "expired", "heartbeat");
    }
  };
  const session = new VisitorSession({
    client, storage, randomUUID: () => "page-original-request"
  });
  await session.bootstrap();

  const first = session.heartbeat();
  const second = session.heartbeat();
  await Promise.resolve();
  recoveryResolve();
  await Promise.all([first, second]);

  assert.equal(heartbeatCalls, 2);
  assert.deepEqual(bootstrapIds, ["page-original-request", "page-original-request"]);
  assert.equal(storage.getItem("ar.visitorToken"), "visitor-recovered");
  assert.equal(session.available, true);
  assert.equal(session.unavailable, false);
});

test("统计暂不可用时保存令牌并由并发心跳用同一 requestId 单次补记", async () => {
  const storage = storageWith();
  const bootstrapIds = [];
  let resolveRecovery;
  const recoveryGate = new Promise((resolve) => { resolveRecovery = resolve; });
  const client = {
    async request(path, options) {
      if (path === "/api/visitors/session") {
        bootstrapIds.push(options.body.bootstrapRequestId);
        if (bootstrapIds.length === 1) {
          return { visitorToken: "visitor-initial", statisticsAvailable: false };
        }
        await recoveryGate;
        return { visitorToken: "visitor-recovered", statisticsAvailable: true, totalViews: 9 };
      }
      assert.equal(path, "/api/presence/heartbeat");
      return {};
    }
  };
  const session = new VisitorSession({
    client, storage, randomUUID: () => "page-statistics-request"
  });

  const initial = await session.bootstrap();
  assert.equal(initial.statisticsAvailable, false);
  assert.equal(storage.getItem("ar.visitorToken"), "visitor-initial");
  const first = session.heartbeat();
  const second = session.heartbeat();
  await Promise.resolve();
  resolveRecovery();
  await Promise.all([first, second]);

  assert.deepEqual(bootstrapIds, ["page-statistics-request", "page-statistics-request"]);
  assert.equal(storage.getItem("ar.visitorToken"), "visitor-recovered");
  assert.equal(session.bootstrapResult.statisticsAvailable, true);
});

test("统计持续不可用时每轮心跳最多补记一次且保留待补记状态", async () => {
  let bootstrapCalls = 0;
  const session = new VisitorSession({
    client: {
      async request(path) {
        if (path === "/api/visitors/session") {
          bootstrapCalls += 1;
          return { visitorToken: "visitor-1", statisticsAvailable: false };
        }
        return {};
      }
    },
    storage: storageWith(),
    randomUUID: () => "persistent-statistics-request"
  });

  await session.bootstrap();
  await session.heartbeat();
  await session.heartbeat();
  assert.equal(bootstrapCalls, 3);
  assert.equal(session.statisticsPending, true);
});

test("bfcache 恢复使用原 requestId 恢复在线且只重建一个定时器", async () => {
  const listeners = new Map();
  const bootstrapIds = [];
  let intervals = 0;
  let clears = 0;
  const eventTarget = {
    addEventListener(type, listener) { listeners.set(type, listener); },
    removeEventListener(type) { listeners.delete(type); }
  };
  const session = new VisitorSession({
    client: {
      async request(path, options) {
        if (path === "/api/visitors/session") {
          bootstrapIds.push(options.body.bootstrapRequestId);
          return { visitorToken: "visitor-1", statisticsAvailable: true };
        }
        return {};
      }
    },
    storage: storageWith(),
    randomUUID: () => "bfcache-page-request",
    eventTarget,
    setIntervalImpl() { intervals += 1; return intervals; },
    clearIntervalImpl() { clears += 1; }
  });

  await session.bootstrap();
  session.startHeartbeat();
  listeners.get("pagehide")({ persisted: true });
  await Promise.resolve();
  assert.equal(clears, 1);
  assert.ok(listeners.has("pageshow"));
  await listeners.get("pageshow")({ persisted: true });
  await listeners.get("pageshow")({ persisted: true });

  assert.deepEqual(bootstrapIds, ["bfcache-page-request", "bfcache-page-request", "bfcache-page-request"]);
  assert.equal(intervals, 2);
  assert.ok(listeners.has("pagehide"));
  assert.ok(listeners.has("pageshow"));
});

test("bfcache 恢复等待迟到 exit 完成后才重新登记在线", async () => {
  const listeners = new Map();
  const calls = [];
  let resolveExit;
  const exitGate = new Promise((resolve) => { resolveExit = resolve; });
  const session = new VisitorSession({
    client: {
      async request(path) {
        calls.push(path);
        if (path === "/api/presence/exit") await exitGate;
        return path === "/api/visitors/session"
          ? { visitorToken: "visitor-1", statisticsAvailable: true }
          : {};
      }
    },
    storage: storageWith({ "ar.visitorToken": "visitor-1" }),
    randomUUID: () => "bfcache-order-request",
    eventTarget: {
      addEventListener(type, listener) { listeners.set(type, listener); },
      removeEventListener(type) { listeners.delete(type); }
    },
    setIntervalImpl() { return 1; },
    clearIntervalImpl() {}
  });
  await session.bootstrap();
  session.startHeartbeat();
  listeners.get("pagehide")({ persisted: true });
  const restoring = listeners.get("pageshow")({ persisted: true });
  await Promise.resolve();
  assert.deepEqual(calls, ["/api/visitors/session", "/api/presence/exit"]);
  resolveExit();
  await restoring;
  assert.deepEqual(calls, [
    "/api/visitors/session", "/api/presence/exit", "/api/visitors/session"
  ]);
});
