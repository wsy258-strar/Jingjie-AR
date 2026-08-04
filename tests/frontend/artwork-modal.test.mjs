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
modalSource = modalSource.replace("./artwork-favorites.js", "./artwork-favorites.mjs");
modalSource = modalSource.replace("./artwork-share.js", "./artwork-share.mjs");
await writeFile(join(target, "artwork-modal.mjs"), modalSource);
await writeFile(join(target, "artwork-gallery.mjs"),
  await readFile(new URL("artwork-gallery.js", source), "utf8"));
await writeFile(join(target, "artwork-favorites.mjs"),
  await readFile(new URL("artwork-favorites.js", source), "utf8"));
await writeFile(join(target, "artwork-share.mjs"),
  await readFile(new URL("artwork-share.js", source), "utf8"));
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
    inert: false,
    textContent: "",
    value: "",
    children: [],
    addEventListener() {},
    append(...children) { this.children.push(...children); },
    appendChild(child) { this.children.push(child); },
    replaceChildren(...children) { this.children = children; },
    focus() {},
    scrollIntoView() {},
    querySelector() { return null; },
    setAttribute(name, value) { attributes[name] = String(value); }
  };
}

function modalFixture() {
  const root = element();
  const card = element();
  const actionBar = element();
  const commentsScroller = element();
  const composer = element();
  root.querySelector = (selector) => {
    if (selector === ".modal-card") return card;
    if (selector === ".artwork-action-bar") return actionBar;
    if (selector === ".artwork-comments-scroll") return commentsScroller;
    if (selector === ".artwork-comment-composer") return composer;
    return null;
  };
  root.querySelectorAll = () => [];
  const ids = [
    "artwork-title", "artwork-gallery", "artwork-gallery-stage", "artwork-image",
    "artwork-prev", "artwork-next", "artwork-image-count", "artwork-image-status",
    "artwork-zoom-in", "artwork-zoom-out", "artwork-reset", "artwork-text",
    "artwork-details-panel", "artwork-comments-panel", "artwork-like",
    "artwork-like-count", "artwork-favorite", "artwork-favorite-label",
    "artwork-comment-jump", "artwork-comment-count", "artwork-share",
    "comments-title", "artwork-comments-total", "comment-list", "comments-more",
    "comment-form", "comment-input"
  ];
  const elements = Object.fromEntries(ids.map((id) => [id, element()]));
  const galleryTools = element();
  elements["artwork-gallery"].querySelector = (selector) =>
    selector === ".artwork-gallery-tools" ? galleryTools : null;
  elements["artwork-gallery"].textContent = "预置画廊舞台";
  const documentObject = {
    getElementById(id) { return id === "artwork-modal" ? root : elements[id] || null; },
    createElement: element
  };
  return { root, card, actionBar, commentsScroller, composer, elements, galleryTools, documentObject };
}

function commentsModal({ request, nextBefore = 0, scrollTop = 0 } = {}) {
  const modal = Object.create(ArtworkModal.prototype);
  modal.api = { request };
  modal.currentArtwork = { artworkId: "work-a", liked: false, commentCount: 3 };
  modal.modalGeneration = 1;
  modal.nextBefore = nextBefore;
  modal.commentList = element();
  modal.commentsMore = element();
  modal.commentsScroller = { scrollTop };
  modal.commentInput = element();
  modal.document = { createElement: element };
  modal.notify = (message) => assert.fail(message);
  return modal;
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

test("作品详情同步画廊、点赞、评论数与本地收藏状态", () => {
  const modal = Object.create(ArtworkModal.prototype);
  const calls = [];
  modal.currentArtwork = { artworkId: "work-a" };
  modal.title = { textContent: "" };
  modal.text = { textContent: "" };
  modal.galleryViewer = {
    setImages(images, title) { calls.push({ images, title }); }
  };
  modal.likeButton = element();
  modal.likeCount = element();
  modal.commentCount = element();
  modal.commentsTotal = element();
  modal.favoriteButton = element();
  modal.favoriteLabel = element();
  modal.favorites = { isFavorite: (artworkId) => artworkId === "work-a" };

  modal.renderDetail({
    artworkId: "work-a", title: "《启航》", text: "说明",
    liked: false, likeCount: 12, commentCount: 3, images: ["/a.jpg", "/b.jpg"]
  });

  assert.deepEqual(calls, [{ images: ["/a.jpg", "/b.jpg"], title: "《启航》" }]);
  assert.equal(modal.likeCount.textContent, "12");
  assert.equal(modal.commentCount.textContent, "3");
  assert.equal(modal.commentsTotal.textContent, "3");
  assert.equal(modal.favoriteButton.attributes["aria-pressed"], "true");
  assert.equal(modal.favoriteLabel.textContent, "已收藏");
});

test("收藏只写本地状态并同步按钮", () => {
  const modal = Object.create(ArtworkModal.prototype);
  modal.currentArtwork = { artworkId: "work-a" };
  modal.favoriteButton = element();
  modal.favoriteLabel = element();
  const toggled = [];
  modal.favorites = {
    toggle(artworkId) {
      toggled.push(artworkId);
      return true;
    }
  };

  modal.toggleFavorite();

  assert.deepEqual(toggled, ["work-a"]);
  assert.equal(modal.favoriteButton.attributes["aria-pressed"], "true");
  assert.equal(modal.favoriteLabel.textContent, "已收藏");
});

test("分享当前作品复制深链接并通知", async () => {
  const modal = Object.create(ArtworkModal.prototype);
  modal.currentArtwork = { artworkId: "work-a" };
  modal.copyShareLink = async (artworkId) => artworkId === "work-a";
  const notifications = [];
  modal.notify = (message) => notifications.push(message);

  await modal.shareCurrentArtwork();

  assert.deepEqual(notifications, ["展品链接已复制"]);
});

test("分享复制返回失败时通知用户", async () => {
  const modal = Object.create(ArtworkModal.prototype);
  modal.currentArtwork = { artworkId: "work-a" };
  modal.copyShareLink = async () => false;
  const notifications = [];
  modal.notify = (message) => notifications.push(message);

  await modal.shareCurrentArtwork();

  assert.deepEqual(notifications, ["展品链接复制失败"]);
});

test("分享复制异常时降级为失败通知", async () => {
  const modal = Object.create(ArtworkModal.prototype);
  modal.currentArtwork = { artworkId: "work-a" };
  modal.copyShareLink = async () => { throw new Error("clipboard unavailable"); };
  const notifications = [];
  modal.notify = (message) => notifications.push(message);

  await modal.shareCurrentArtwork();

  assert.deepEqual(notifications, ["展品链接复制失败"]);
});

test("评论跳转平滑定位到评论区", () => {
  const modal = Object.create(ArtworkModal.prototype);
  let options;
  modal.commentsSection = {
    scrollIntoView(value) { options = value; }
  };

  modal.scrollToComments();

  assert.deepEqual(options, { behavior: "smooth", block: "start" });
});

test("打开作品保留预置画廊舞台并恢复完整互动区域", async () => {
  const fixture = modalFixture();
  const originalImage = globalThis.Image;
  globalThis.Image = class { set src(value) { this.source = value; } };
  const modal = new ArtworkModal({
    api: {
      async request(path) {
        if (path.includes("/comments")) return { comments: [], nextBefore: 0 };
        return {
          artworkId: "work-1", title: "启航", text: "说明",
          likeCount: 2, commentCount: 4, images: ["/a.jpg", "/b.jpg"]
        };
      }
    },
    auth: { token: () => "" },
    modalManager: { open() {}, close() {} },
    notify: (message) => assert.fail(message),
    favorites: { isFavorite: () => false, toggle: () => false },
    documentObject: fixture.documentObject
  });

  try {
    await modal.open("work-1");

    assert.equal(fixture.elements["artwork-gallery"].textContent, "预置画廊舞台");
    assert.deepEqual(modal.galleryViewer.images, ["/a.jpg", "/b.jpg"]);
    assert.equal(fixture.elements["artwork-image"].src, "/a.jpg");
    assert.equal(fixture.elements["artwork-comments-panel"].hidden, false);
    assert.equal(fixture.actionBar.hidden, false);
    assert.equal(fixture.composer.hidden, false);
    assert.equal(fixture.elements["artwork-comment-count"].textContent, "4");
  } finally {
    globalThis.Image = originalImage;
  }
});

test("文字热点隐藏画廊、互动栏、评论区与固定评论输入区", () => {
  const fixture = modalFixture();
  const modal = new ArtworkModal({
    api: {}, auth: { token: () => "" }, modalManager: { open() {}, close() {} },
    notify: () => {}, documentObject: fixture.documentObject
  });

  modal.openText({ title: "策展说明", text: "这里是文字热点内容" });

  assert.equal(fixture.card.classList.contains("is-text-only"), true);
  assert.equal(fixture.elements["artwork-gallery"].hidden, true);
  assert.equal(fixture.galleryTools.hidden, true);
  assert.equal(fixture.actionBar.hidden, true);
  assert.equal(fixture.actionBar.inert, true);
  assert.equal(fixture.elements["artwork-comments-panel"].hidden, true);
  assert.equal(fixture.elements["artwork-comments-panel"].inert, true);
  assert.equal(fixture.elements["artwork-comments-panel"].attributes["aria-hidden"], "true");
  assert.equal(fixture.elements["comments-title"].hidden, true);
  assert.equal(fixture.commentsScroller.hidden, true);
  assert.equal(fixture.composer.hidden, true);
  assert.equal(fixture.composer.inert, true);
});

test("关闭弹窗会清空画廊与评论草稿", () => {
  const modal = Object.create(ArtworkModal.prototype);
  let clears = 0;
  modal.cancelLoad = () => {};
  modal.modalGeneration = 0;
  modal.modalManager = { close() {} };
  modal.root = {};
  modal.commentInput = { value: "未提交草稿" };
  modal.galleryViewer = { clear() { clears += 1; } };

  modal.close();

  assert.equal(clears, 1);
  assert.equal(modal.commentInput.value, "");
});

test("文字热点后打开作品恢复画廊、互动栏、评论区与 composer", async () => {
  const fixture = modalFixture();
  const modal = new ArtworkModal({
    api: {
      async request(path) {
        if (path.includes("/comments")) return { comments: [], nextBefore: 0 };
        return {
          artworkId: "work-1", title: "启航", text: "说明",
          likeCount: 1, commentCount: 2, images: ["/a.jpg"]
        };
      }
    },
    auth: { token: () => "" }, modalManager: { open() {}, close() {} },
    notify: (message) => assert.fail(message),
    favorites: { isFavorite: () => true, toggle: () => false },
    documentObject: fixture.documentObject
  });

  modal.openText({ title: "策展说明", text: "这里是文字热点内容" });
  await modal.open("work-1");

  assert.equal(fixture.card.classList.contains("is-text-only"), false);
  assert.equal(fixture.elements["artwork-gallery"].hidden, false);
  assert.equal(fixture.galleryTools.hidden, false);
  assert.equal(fixture.actionBar.hidden, false);
  assert.equal(fixture.actionBar.inert, false);
  assert.equal(fixture.elements["artwork-comments-panel"].hidden, false);
  assert.equal(fixture.elements["artwork-details-panel"].inert, false);
  assert.equal(fixture.elements["artwork-comments-panel"].inert, false);
  assert.equal(fixture.elements["artwork-comments-panel"].attributes["aria-hidden"], "false");
  assert.equal(fixture.elements["comments-title"].hidden, false);
  assert.equal(fixture.commentsScroller.hidden, false);
  assert.equal(fixture.composer.hidden, false);
  assert.equal(fixture.composer.inert, false);
});

test("切换作品、进入文字热点和关闭弹窗都会清空未提交评论", async () => {
  const fixture = modalFixture();
  const modal = new ArtworkModal({
    api: {
      async request(path) {
        if (path.includes("/comments")) return { comments: [], nextBefore: 0 };
        const artworkId = path.endsWith("work-b") ? "work-b" : "work-a";
        return {
          artworkId, title: artworkId, text: "说明",
          likeCount: 0, commentCount: 0, images: []
        };
      }
    },
    auth: { token: () => "" }, modalManager: { open() {}, close() {} },
    notify: (message) => assert.fail(message),
    favorites: { isFavorite: () => false, toggle: () => false },
    documentObject: fixture.documentObject
  });

  await modal.open("work-a");
  fixture.elements["comment-input"].value = "作品 A 草稿";
  await modal.open("work-b");
  assert.equal(fixture.elements["comment-input"].value, "");

  fixture.elements["comment-input"].value = "作品 B 草稿";
  modal.openText({ title: "策展说明", text: "内容" });
  assert.equal(fixture.elements["comment-input"].value, "");

  await modal.open("work-a");
  fixture.elements["comment-input"].value = "关闭前草稿";
  modal.close();
  assert.equal(fixture.elements["comment-input"].value, "");
  await modal.open("work-b");
  assert.equal(fixture.elements["comment-input"].value, "");
});

test("作品请求完成前清零旧互动状态且隐藏旧分页入口", async () => {
  const fixture = modalFixture();
  let resolveDetail;
  const detailPending = new Promise((resolve) => { resolveDetail = resolve; });
  fixture.elements["artwork-like-count"].textContent = "99";
  fixture.elements["artwork-comment-count"].textContent = "88";
  fixture.elements["artwork-comments-total"].textContent = "88";
  fixture.elements["artwork-favorite"].setAttribute("aria-pressed", "true");
  fixture.elements["comments-more"].hidden = false;
  const modal = new ArtworkModal({
    api: {
      async request(path) {
        if (path.includes("/comments")) return { comments: [], nextBefore: 0 };
        return detailPending;
      }
    },
    auth: { token: () => "" }, modalManager: { open() {}, close() {} },
    notify: (message) => assert.fail(message),
    favorites: { isFavorite: () => false, toggle: () => false },
    documentObject: fixture.documentObject
  });

  const opening = modal.open("work-1");

  assert.equal(fixture.elements["artwork-like-count"].textContent, "0");
  assert.equal(fixture.elements["artwork-comment-count"].textContent, "0");
  assert.equal(fixture.elements["artwork-comments-total"].textContent, "0");
  assert.equal(fixture.elements["artwork-favorite"].attributes["aria-pressed"], "false");
  assert.equal(fixture.elements["comments-more"].hidden, true);

  resolveDetail({
    artworkId: "work-1", title: "启航", text: "说明",
    likeCount: 5, commentCount: 6, images: []
  });
  await opening;
});

test("成功评论只增加一次后端详情评论数", async () => {
  const requests = [];
  const modal = commentsModal({
    request: async (path, options) => {
      requests.push({ path, options });
      return path.includes("?limit=20") ? { comments: [], nextBefore: 0 } : {};
    }
  });
  modal.commentInput.value = "很好";
  modal.updateCommentCount = (count) => { modal.currentArtwork.commentCount = count; };

  await modal.submitComment(modal.artworkContext(), "很好");

  assert.equal(modal.currentArtwork.commentCount, 4);
  assert.equal(modal.commentInput.value, "");
  assert.equal(requests.filter(({ options }) => options?.method === "POST").length, 1);
});

test("评论发布失败不增加评论数也不清空输入", async () => {
  const modal = commentsModal({ request: async () => { throw new Error("发布失败"); } });
  modal.commentInput.value = "保留内容";
  modal.updateCommentCount = (count) => { modal.currentArtwork.commentCount = count; };

  await assert.rejects(
    modal.submitComment(modal.artworkContext(), "保留内容"),
    /发布失败/
  );

  assert.equal(modal.currentArtwork.commentCount, 3);
  assert.equal(modal.commentInput.value, "保留内容");
});

test("迟到的评论发布成功不增加新作品评论数", async () => {
  let resolvePost;
  const postPending = new Promise((resolve) => { resolvePost = resolve; });
  const modal = commentsModal({ request: async () => postPending });
  const oldContext = modal.artworkContext();
  modal.updateCommentCount = () => assert.fail("迟到发布不得更新新作品计数");
  modal.loadComments = () => assert.fail("迟到发布不得刷新新作品评论");

  const submitting = modal.submitComment(oldContext, "旧作品评论");
  modal.currentArtwork = { artworkId: "work-b", liked: false, commentCount: 7 };
  modal.modalGeneration = 2;
  resolvePost({});
  await submitting;

  assert.equal(modal.currentArtwork.commentCount, 7);
});

test("重载第一页评论在成功渲染后才回到顶部", async () => {
  let resolveRequest;
  const pending = new Promise((resolve) => { resolveRequest = resolve; });
  const modal = commentsModal({ request: async () => pending, scrollTop: 160 });

  const loading = modal.loadComments(true, modal.artworkContext());
  assert.equal(modal.commentsScroller.scrollTop, 160, "请求期间不能提前改变用户位置");
  resolveRequest({
    comments: [{ username: "访客", content: "新评论" }],
    nextBefore: 0
  });
  await loading;

  assert.equal(modal.commentsScroller.scrollTop, 0);
});

test("追加评论不改变已有滚动位置", async () => {
  const modal = commentsModal({
    nextBefore: 10,
    scrollTop: 120,
    request: async () => ({ comments: [{ username: "访客", content: "更早评论" }], nextBefore: 0 })
  });

  await modal.loadComments(false, modal.artworkContext());

  assert.equal(modal.commentsScroller.scrollTop, 120);
});

test("评论请求失败时不改变已有滚动位置", async () => {
  const notifications = [];
  const modal = commentsModal({
    scrollTop: 88,
    request: async () => { throw new Error("网络异常"); }
  });
  modal.notify = (message) => notifications.push(message);

  await modal.loadComments(true, modal.artworkContext());

  assert.equal(modal.commentsScroller.scrollTop, 88);
  assert.deepEqual(notifications, ["网络异常"]);
});

test("重载第一页评论失败时保留旧评论、游标和加载更多状态", async () => {
  const notifications = [];
  const modal = commentsModal({
    nextBefore: 42,
    scrollTop: 88,
    request: async () => { throw new Error("网络异常"); }
  });
  const oldComment = { textContent: "旧评论" };
  modal.commentList.children = [oldComment];
  modal.commentsMore.hidden = false;
  modal.notify = (message) => notifications.push(message);

  await modal.loadComments(true, modal.artworkContext());

  assert.deepEqual(modal.commentList.children, [oldComment]);
  assert.equal(modal.nextBefore, 42);
  assert.equal(modal.commentsMore.hidden, false);
  assert.equal(modal.commentsScroller.scrollTop, 88);
  assert.deepEqual(notifications, ["网络异常"]);
});

test("重载第一页评论成功后原子替换旧评论与分页状态", async () => {
  const modal = commentsModal({
    nextBefore: 42,
    scrollTop: 88,
    request: async () => ({
      comments: [{ username: "新访客", content: "最新评论" }],
      nextBefore: 0
    })
  });
  modal.commentList.children = [{ textContent: "旧评论" }];
  modal.commentsMore.hidden = false;

  await modal.loadComments(true, modal.artworkContext());

  assert.equal(modal.commentList.children.length, 1);
  assert.equal(modal.commentList.children[0].className, "comment-item");
  assert.equal(modal.nextBefore, 0);
  assert.equal(modal.commentsMore.hidden, true);
  assert.equal(modal.commentsScroller.scrollTop, 0);
});

test("迟到的评论成功响应不污染切换后的作品弹窗", async () => {
  let resolveRequest;
  const pending = new Promise((resolve) => { resolveRequest = resolve; });
  const modal = commentsModal({ request: async () => pending, scrollTop: 160 });
  const oldContext = modal.artworkContext();

  const loading = modal.loadComments(true, oldContext);
  modal.currentArtwork = { artworkId: "work-b", liked: false };
  modal.modalGeneration = 2;
  resolveRequest({
    comments: [{ username: "旧作品访客", content: "不应显示" }],
    nextBefore: 0
  });
  await loading;

  assert.equal(modal.commentsScroller.scrollTop, 160);
  assert.equal(modal.commentList.children.length, 0);
});
