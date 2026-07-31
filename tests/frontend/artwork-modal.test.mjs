import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

const target = await mkdtemp(join(tmpdir(), "jingjie-ar-artwork-modal-"));
const source = new URL("../../WebApps/ARServer/www/js/", import.meta.url);
await writeFile(join(target, "api-client.mjs"),
  await readFile(new URL("api-client.js", source), "utf8"));
let modalSource = await readFile(new URL("artwork-modal.js", source), "utf8");
modalSource = modalSource.replace("./api-client.js", "./api-client.mjs");
await writeFile(join(target, "artwork-modal.mjs"), modalSource);
const { ApiError } = await import(pathToFileURL(join(target, "api-client.mjs")).href);
const { ArtworkModal, fetchArtworkDetail } = await import(
  pathToFileURL(join(target, "artwork-modal.mjs")).href
);
process.once("exit", () => rmSync(target, { recursive: true, force: true }));

test("失效用户令牌不阻断公开作品详情，清理后只降级重试一次", async () => {
  const userOptions = [];
  const api = {
    async request(path, options) {
      assert.equal(path, "/api/artworks/work-1");
      userOptions.push(options.user);
      if (userOptions.length === 1)
        throw new ApiError(401, "UNAUTHORIZED", "expired", "request-1");
      return { artworkId: "work-1", title: "作品" };
    }
  };
  const auth = { token: () => "expired-token" };

  const detail = await fetchArtworkDetail({ api, auth, artworkId: "work-1" });
  assert.equal(detail.artworkId, "work-1");
  assert.deepEqual(userOptions, [true, false]);
});

test("并发触发只保留一个待登录操作，登录成功后执行一次", async () => {
  const modal = Object.create(ArtworkModal.prototype);
  let resolveLogin;
  const login = new Promise((resolve) => { resolveLogin = resolve; });
  modal.auth = { token: () => "", ensureAuthenticated: () => login };
  modal.notify = () => assert.fail("成功路径不应提示错误");
  modal.protectedActionPending = false;
  let operations = 0;

  const first = modal.runProtected(async () => { operations += 1; });
  const second = modal.runProtected(async () => { operations += 1; });
  resolveLogin("user-token");
  await Promise.all([first, second]);

  assert.equal(operations, 1);
});

test("迟到的作品操作固定使用原作品 ID 且不污染新弹窗", async () => {
  const requests = [];
  const modal = Object.create(ArtworkModal.prototype);
  modal.api = {
    async request(path, options) {
      requests.push({ path, options });
      return { liked: true, likeCount: 9 };
    }
  };
  modal.currentArtwork = { artworkId: "work-b", liked: false };
  modal.modalGeneration = 2;
  modal.updateLike = () => assert.fail("迟到响应不得更新新作品弹窗");

  await modal.toggleLike({ artworkId: "work-a", liked: false, generation: 1 });
  assert.equal(requests.length, 1);
  assert.equal(requests[0].path, "/api/artworks/work-a/likes");
});
