import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

async function loadModules() {
  const target = await mkdtemp(join(tmpdir(), "jingjie-ar-api-client-"));
  const source = new URL("../../WebApps/ARServer/www/js/", import.meta.url);
  for (const name of ["api-client", "visitor-session", "auth-session"]) {
    let content = await readFile(new URL(name + ".js", source), "utf8");
    content = content.replaceAll("./api-client.js", "./api-client.mjs");
    await writeFile(join(target, name + ".mjs"), content);
  }
  return {
    target,
    api: await import(pathToFileURL(join(target, "api-client.mjs")).href)
  };
}

const modules = await loadModules();
const { ApiClient, ApiError } = modules.api;
process.once("exit", () => rmSync(modules.target, { recursive: true, force: true }));

function storageWith(values = {}) {
  const data = new Map(Object.entries(values));
  return {
    getItem(key) { return data.has(key) ? data.get(key) : null; },
    setItem(key, value) { data.set(key, String(value)); },
    removeItem(key) { data.delete(key); }
  };
}

function response(status, body, contentType = "application/json") {
  return {
    ok: status >= 200 && status < 300,
    status,
    headers: { get(name) { return name.toLowerCase() === "content-type" ? contentType : null; } },
    async text() { return body; }
  };
}

test("请求按需附带彼此独立的访客与用户令牌", async () => {
  const storage = storageWith({ "ar.visitorToken": "visitor-1", "ar.userToken": "user-1" });
  let received;
  const client = new ApiClient({
    storage,
    fetchImpl: async (url, options) => {
      received = { url, options };
      return response(200, '{"success":true,"data":{"ok":true},"message":""}');
    }
  });

  assert.deepEqual(await client.request("/api/example", { visitor: true, user: true }), { ok: true });
  assert.equal(received.url, "/api/example");
  assert.equal(received.options.headers["X-Visitor-Token"], "visitor-1");
  assert.equal(received.options.headers.Authorization, "Bearer user-1");
});

test("success false 响应抛出包含稳定字段的 ApiError", async () => {
  const client = new ApiClient({
    storage: storageWith(),
    fetchImpl: async () => response(200,
      '{"success":false,"code":"INVALID_INPUT","message":"输入无效","requestId":"req-1"}')
  });

  await assert.rejects(client.request("/api/example"), (error) => {
    assert.ok(error instanceof ApiError);
    assert.equal(error.status, 200);
    assert.equal(error.code, "INVALID_INPUT");
    assert.equal(error.requestId, "req-1");
    return true;
  });
});

test("401 仅清除用户令牌并能处理空的非 JSON 响应", async () => {
  const storage = storageWith({ "ar.visitorToken": "visitor-1", "ar.userToken": "user-1" });
  const client = new ApiClient({
    storage,
    fetchImpl: async () => response(401, "unauthorized", "text/plain")
  });

  await assert.rejects(client.request("/api/artworks/a/likes", { user: true }), ApiError);
  assert.equal(storage.getItem("ar.visitorToken"), "visitor-1");
  assert.equal(storage.getItem("ar.userToken"), null);
});

test("空成功响应返回 null 而不是 JSON 解析错误", async () => {
  const client = new ApiClient({
    storage: storageWith(),
    fetchImpl: async () => response(204, "", "")
  });

  assert.equal(await client.request("/api/example"), null);
});
