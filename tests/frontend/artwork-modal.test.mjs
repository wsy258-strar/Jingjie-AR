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
modalSource = modalSource.replace("./artwork-gallery.js", "./artwork-gallery.mjs");
await writeFile(join(target, "artwork-modal.mjs"), modalSource);
await writeFile(join(target, "artwork-gallery.mjs"),
  await readFile(new URL("artwork-gallery.js", source), "utf8"));
const { ApiError } = await import(pathToFileURL(join(target, "api-client.mjs")).href);
const { ArtworkModal, fetchArtworkDetail } = await import(
  pathToFileURL(join(target, "artwork-modal.mjs")).href
);
process.once("exit", () => rmSync(target, { recursive: true, force: true }));

function element() {
  const attributes = {};
  const classes = new Set();
  return {
    attributes,
    classList: {
      add: (name) => classes.add(name),
      remove: (name) => classes.delete(name),
      contains: (name) => classes.has(name)
    },
    dataset: {},
    style: {},
    hidden: false,
    textContent: "",
    value: "",
    children: [],
    addEventListener() {},
    append(...children) { this.children.push(...children); },
    appendChild(child) { this.children.push(child); },
    focus() {},
    querySelector() { return null; },
    setAttribute(name, value) { attributes[name] = String(value); }
  };
}

function tabButton(name) {
  const button = element();
  button.dataset.artworkTab = name;
  return button;
}

function modalFixture() {
  const root = element();
  const card = element();
  const tabsContainer = element();
  const tabs = [tabButton("details"), tabButton("comments")];
  root.querySelector = (selector) => {
    if (selector === ".modal-card") return card;
    if (selector === ".artwork-tabs") return tabsContainer;
    return null;
  };
  root.querySelectorAll = (selector) => selector === "[data-artwork-tab]" ? tabs : [];
  const ids = [
    "artwork-title", "artwork-gallery", "artwork-gallery-stage", "artwork-image",
    "artwork-prev", "artwork-next", "artwork-image-count", "artwork-image-status",
    "artwork-zoom-in", "artwork-zoom-out", "artwork-reset", "artwork-text",
    "artwork-comments-panel", "artwork-like", "artwork-like-symbol", "artwork-like-label",
    "artwork-like-count", "comment-list", "comments-more", "comment-form", "comment-input"
  ];
  const elements = Object.fromEntries(ids.map((id) => [id, element()]));
  const galleryTools = element();
  elements["artwork-gallery"].querySelector = (selector) =>
    selector === ".artwork-gallery-tools" ? galleryTools : null;
  elements["artwork-gallery"].textContent = "预置画廊舞台";
  const documentObject = {
    getElementById(id) { return id === "artwork-modal" ? root : elements[id] || null; },
    querySelectorAll(selector) { return selector === "[data-artwork-tab]" ? [tabButton("details"), tabButton("comments")] : []; },
    createElement: element
  };
  return { root, card, tabsContainer, elements, galleryTools, documentObject };
}

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

test("渲染详情把图片交给画廊", () => {
  const modal = Object.create(ArtworkModal.prototype);
  const calls = [];
  modal.title = { textContent: "" };
  modal.text = { textContent: "" };
  modal.galleryViewer = {
    setImages(images, title) { calls.push({ images, title }); }
  };
  modal.updateLike = () => {};

  modal.renderDetail({ title: "《启航》", text: "说明", images: ["/a.jpg", "/b.jpg"] });

  assert.deepEqual(calls, [{ images: ["/a.jpg", "/b.jpg"], title: "《启航》" }]);
});

test("移动标签更新卡片状态和 aria-selected", () => {
  const modal = Object.create(ArtworkModal.prototype);
  modal.card = { dataset: {} };
  modal.tabButtons = [tabButton("details"), tabButton("comments")];

  modal.setActiveTab("comments");

  assert.equal(modal.card.dataset.mobileTab, "comments");
  assert.equal(modal.tabButtons[1].attributes["aria-selected"], "true");
});

test("打开作品保留预置画廊舞台并使用新的评论面板 ID", async () => {
  const fixture = modalFixture();
  const originalImage = globalThis.Image;
  globalThis.Image = class { set src(value) { this.source = value; } };
  const modal = new ArtworkModal({
    api: {
      async request(path) {
        if (path.includes("/comments")) return { comments: [], nextBefore: 0 };
        return { artworkId: "work-1", title: "启航", text: "说明", images: ["/a.jpg", "/b.jpg"] };
      }
    },
    auth: { token: () => "" },
    modalManager: { open() {}, close() {} },
    notify: (message) => assert.fail(message),
    documentObject: fixture.documentObject
  });

  try {
    await modal.open("work-1");

    assert.equal(fixture.elements["artwork-gallery"].textContent, "预置画廊舞台");
    assert.deepEqual(modal.galleryViewer.images, ["/a.jpg", "/b.jpg"]);
    assert.equal(fixture.elements["artwork-image"].src, "/a.jpg");
    assert.equal(fixture.elements["artwork-comments-panel"].hidden, false);
  } finally {
    globalThis.Image = originalImage;
  }
});

test("文字热点隐藏画廊、标签、工具栏与互动区", () => {
  const fixture = modalFixture();
  const modal = new ArtworkModal({
    api: {}, auth: { token: () => "" }, modalManager: { open() {}, close() {} },
    notify: () => {}, documentObject: fixture.documentObject
  });

  modal.openText({ title: "策展说明", text: "这里是文字热点内容" });

  assert.equal(fixture.card.classList.contains("is-text-only"), true);
  assert.equal(fixture.elements["artwork-gallery"].hidden, true);
  assert.equal(fixture.tabsContainer.hidden, true);
  assert.equal(fixture.galleryTools.hidden, true);
  assert.equal(fixture.elements["artwork-comments-panel"].hidden, true);
  assert.equal(fixture.card.dataset.mobileTab, "details");
});

test("关闭弹窗会清空画廊", () => {
  const modal = Object.create(ArtworkModal.prototype);
  let clears = 0;
  modal.cancelLoad = () => {};
  modal.modalGeneration = 0;
  modal.modalManager = { close() {} };
  modal.root = {};
  modal.galleryViewer = { clear() { clears += 1; } };

  modal.close();

  assert.equal(clears, 1);
});

test("文字热点后打开作品恢复完整画廊与默认说明标签", async () => {
  const fixture = modalFixture();
  const modal = new ArtworkModal({
    api: {
      async request(path) {
        if (path.includes("/comments")) return { comments: [], nextBefore: 0 };
        return { artworkId: "work-1", title: "启航", text: "说明", images: ["/a.jpg"] };
      }
    },
    auth: { token: () => "" }, modalManager: { open() {}, close() {} },
    notify: (message) => assert.fail(message), documentObject: fixture.documentObject
  });

  modal.openText({ title: "策展说明", text: "这里是文字热点内容" });
  await modal.open("work-1");

  assert.equal(fixture.card.classList.contains("is-text-only"), false);
  assert.equal(fixture.elements["artwork-gallery"].hidden, false);
  assert.equal(fixture.tabsContainer.hidden, false);
  assert.equal(fixture.galleryTools.hidden, false);
  assert.equal(fixture.elements["artwork-comments-panel"].hidden, false);
  assert.equal(fixture.card.dataset.mobileTab, "details");
  assert.equal(modal.tabButtons[0].attributes["aria-selected"], "true");
});
