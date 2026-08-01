import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

async function loadModules() {
  const target = await mkdtemp(join(tmpdir(), "jingjie-ar-auth-session-"));
  const source = new URL("../../WebApps/ARServer/www/js/", import.meta.url);
  for (const name of ["api-client", "auth-session"]) {
    let content = await readFile(new URL(name + ".js", source), "utf8");
    content = content.replaceAll("./api-client.js", "./api-client.mjs");
    await writeFile(join(target, name + ".mjs"), content);
  }
  return {
    target,
    api: await import(pathToFileURL(join(target, "api-client.mjs")).href),
    auth: await import(pathToFileURL(join(target, "auth-session.mjs")).href)
  };
}

const modules = await loadModules();
const { ApiError } = modules.api;
const { AuthSession } = modules.auth;
process.once("exit", () => rmSync(modules.target, { recursive: true, force: true }));

function storageWith(values = {}) {
  const data = new Map(Object.entries(values));
  return {
    getItem(key) { return data.has(key) ? data.get(key) : null; },
    setItem(key, value) { data.set(key, String(value)); },
    removeItem(key) { data.delete(key); }
  };
}

test("认证成功仅保存用户令牌并提交 JSON 凭据", async () => {
  const storage = storageWith({ "ar.visitorToken": "visitor-1" });
  let received;
  const session = new AuthSession({
    storage,
    client: {
      async request(path, options) {
        received = { path, options };
        return { token: "user-1", username: "alice" };
      }
    }
  });

  assert.deepEqual(await session.authenticate("alice", "secret"), { token: "user-1", username: "alice" });
  assert.equal(received.path, "/api/auth");
  assert.deepEqual(received.options, { method: "POST", body: { username: "alice", password: "secret" } });
  assert.equal(storage.getItem("ar.userToken"), "user-1");
  assert.equal(storage.getItem("ar.visitorToken"), "visitor-1");
});

test("已有用户令牌可直接通过认证检查", async () => {
  const session = new AuthSession({ storage: storageWith({ "ar.userToken": "user-1" }) });
  assert.equal(await session.ensureAuthenticated(), "user-1");
});

test("没有用户令牌时认证检查要求界面登录，clear 不影响访客令牌", async () => {
  const storage = storageWith({ "ar.visitorToken": "visitor-1" });
  let requested = 0;
  const session = new AuthSession({
    storage,
    onAuthenticationRequired() { requested += 1; }
  });

  await assert.rejects(session.ensureAuthenticated(), (error) => {
    assert.ok(error instanceof ApiError);
    assert.equal(error.status, 401);
    assert.equal(error.code, "AUTHENTICATION_REQUIRED");
    return true;
  });
  assert.equal(requested, 1);
  session.clear();
  assert.equal(storage.getItem("ar.visitorToken"), "visitor-1");
});

test("退出登录先请求服务端撤销，成功后才清理本地用户令牌", async () => {
  const storage = storageWith({ "ar.userToken": "user-1", "ar.visitorToken": "visitor-1" });
  const calls = [];
  const session = new AuthSession({
    storage,
    client: { async request(path, options) { calls.push({ path, options }); return {}; } }
  });

  assert.equal(await session.logout(), true);
  assert.deepEqual(calls, [{ path: "/api/auth/logout", options: { method: "POST", user: true } }]);
  assert.equal(storage.getItem("ar.userToken"), null);
  assert.equal(storage.getItem("ar.visitorToken"), "visitor-1");
});

test("退出请求失败时保留本地令牌以明确表示撤销未完成", async () => {
  const storage = storageWith({ "ar.userToken": "user-1" });
  const session = new AuthSession({
    storage,
    client: { async request() { throw new TypeError("network down"); } }
  });

  await assert.rejects(session.logout(), /network down/);
  assert.equal(storage.getItem("ar.userToken"), "user-1");
});
