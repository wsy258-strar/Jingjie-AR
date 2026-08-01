import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

const target = await mkdtemp(join(tmpdir(), "jingjie-ar-lifecycle-"));
const source = new URL("../../WebApps/ARServer/www/js/museum-lifecycle.js", import.meta.url);
await writeFile(join(target, "museum-lifecycle.mjs"), await readFile(source, "utf8"));
const { MuseumLifecycle } = await import(pathToFileURL(join(target, "museum-lifecycle.mjs")).href);
process.once("exit", () => rmSync(target, { recursive: true, force: true }));

test("同一页面生命周期的目录重试复用唯一访客 bootstrap", async () => {
  let calls = 0;
  const lifecycle = new MuseumLifecycle({
    visitor: { async bootstrap() { calls += 1; return { visitorToken: "visitor-1" }; } }
  });

  const first = lifecycle.bootstrapVisitorOnce();
  const retry = lifecycle.bootstrapVisitorOnce();
  assert.equal(first, retry);
  assert.equal((await retry).visitorToken, "visitor-1");
  assert.equal((await lifecycle.bootstrapVisitorOnce()).visitorToken, "visitor-1");
  assert.equal(calls, 1);
});

test("bfcache 恢复等待访客在线恢复并只重建一个计数轮询", async () => {
  const listeners = new Map();
  const listenerCounts = new Map();
  const order = [];
  let bootstrapCalls = 0;
  let intervalCalls = 0;
  let clearCalls = 0;
  const visitor = {
    async bootstrap() {
      bootstrapCalls += 1;
      return { visitorToken: "visitor-1" };
    },
    async waitForPageRestore() { order.push("visitor-restored"); }
  };
  const lifecycle = new MuseumLifecycle({
    visitor,
    eventTarget: {
      addEventListener(type, listener) {
        listeners.set(type, listener);
        listenerCounts.set(type, (listenerCounts.get(type) || 0) + 1);
      }
    },
    refreshCounters: async () => { order.push("counters-refreshed"); },
    setIntervalImpl() { intervalCalls += 1; return intervalCalls; },
    clearIntervalImpl() { clearCalls += 1; }
  });

  await lifecycle.bootstrapVisitorOnce();
  lifecycle.startCounterPolling();
  lifecycle.startCounterPolling();
  assert.equal(intervalCalls, 1);
  assert.equal(listenerCounts.get("pagehide"), 1);
  assert.equal(listenerCounts.get("pageshow"), 1);

  listeners.get("pagehide")({ persisted: true });
  assert.equal(clearCalls, 1);
  await listeners.get("pageshow")({ persisted: false });
  assert.deepEqual(order, []);

  await listeners.get("pageshow")({ persisted: true });
  await listeners.get("pageshow")({ persisted: true });
  assert.deepEqual(order, [
    "visitor-restored", "counters-refreshed",
    "visitor-restored", "counters-refreshed"
  ]);
  assert.equal(intervalCalls, 2);
  assert.equal(bootstrapCalls, 1);
});

test("bfcache 恢复未完成时再次离开会使迟到的计数恢复失效", async () => {
  const listeners = new Map();
  let resolveRestore;
  const restoreGate = new Promise((resolve) => { resolveRestore = resolve; });
  let intervals = 0;
  let refreshes = 0;
  const lifecycle = new MuseumLifecycle({
    visitor: {
      async bootstrap() { return {}; },
      waitForPageRestore() { return restoreGate; }
    },
    eventTarget: {
      addEventListener(type, listener) { listeners.set(type, listener); }
    },
    refreshCounters: async () => { refreshes += 1; },
    setIntervalImpl() { intervals += 1; return intervals; },
    clearIntervalImpl() {}
  });
  lifecycle.startCounterPolling();
  listeners.get("pagehide")({ persisted: true });
  const restoring = listeners.get("pageshow")({ persisted: true });
  await Promise.resolve();
  listeners.get("pagehide")({ persisted: true });
  resolveRestore();
  await restoring;

  assert.equal(lifecycle.counterTimer, null);
  assert.equal(intervals, 1);
  assert.equal(refreshes, 0);
});
