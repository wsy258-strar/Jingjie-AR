// 作品弹窗只使用安全 DOM 属性写入后端内容，并协调登录后的一次性待操作重试。
import { ApiError } from "./api-client.js";
import { ArtworkFavorites } from "./artwork-favorites.js";
import { ArtworkGallery } from "./artwork-gallery.js";
import { buildArtworkShareUrl, copyArtworkShareLink } from "./artwork-share.js";

function formatCount(value) {
  const count = Number(value);
  return Number.isFinite(count) && count >= 0 ? String(count) : "0";
}

export async function fetchArtworkDetail({ api, auth, artworkId, signal } = {}) {
  const path = `/api/artworks/${encodeURIComponent(artworkId)}`;
  const withUser = Boolean(auth.token());
  try {
    return await api.request(path, { user: withUser, signal });
  } catch (error) {
    // 详情是公开接口；可选用户令牌失效时降级为匿名读取，不打断游客浏览。
    if (withUser && error instanceof ApiError && error.status === 401)
      return api.request(path, { user: false, signal });
    throw error;
  }
}

export class ArtworkModal {
  constructor({
    api, auth, modalManager, notify,
    favorites,
    copyShareLink,
    locationObject = globalThis.location,
    documentObject = document
  } = {}) {
    this.api = api;
    this.auth = auth;
    this.modalManager = modalManager;
    this.notify = notify;
    this.document = documentObject;
    this.favorites = favorites || new ArtworkFavorites();
    this.locationObject = locationObject;
    this.copyShareLink = copyShareLink || ((artworkId) => copyArtworkShareLink({
      navigatorObject: this.document.defaultView?.navigator || globalThis.navigator,
      documentObject: this.document,
      url: buildArtworkShareUrl(this.locationObject, artworkId)
    }));
    this.root = documentObject.getElementById("artwork-modal");
    this.card = this.root.querySelector(".modal-card");
    this.title = documentObject.getElementById("artwork-title");
    this.gallery = documentObject.getElementById("artwork-gallery");
    this.galleryTools = this.gallery.querySelector(".artwork-gallery-tools");
    this.galleryViewer = new ArtworkGallery({
      root: this.gallery,
      stage: documentObject.getElementById("artwork-gallery-stage"),
      image: documentObject.getElementById("artwork-image"),
      previousButton: documentObject.getElementById("artwork-prev"),
      nextButton: documentObject.getElementById("artwork-next"),
      counter: documentObject.getElementById("artwork-image-count"),
      status: documentObject.getElementById("artwork-image-status"),
      zoomInButton: documentObject.getElementById("artwork-zoom-in"),
      zoomOutButton: documentObject.getElementById("artwork-zoom-out"),
      resetButton: documentObject.getElementById("artwork-reset")
    });
    this.text = documentObject.getElementById("artwork-text");
    this.detailsPanel = documentObject.getElementById("artwork-details-panel");
    this.interactions = documentObject.getElementById("artwork-comments-panel");
    this.actionBar = this.root.querySelector(".artwork-action-bar");
    this.likeButton = documentObject.getElementById("artwork-like");
    this.likeCount = documentObject.getElementById("artwork-like-count");
    this.favoriteButton = documentObject.getElementById("artwork-favorite");
    this.favoriteLabel = documentObject.getElementById("artwork-favorite-label");
    this.commentJumpButton = documentObject.getElementById("artwork-comment-jump");
    this.commentCount = documentObject.getElementById("artwork-comment-count");
    this.shareButton = documentObject.getElementById("artwork-share");
    this.commentsTitle = documentObject.getElementById("comments-title");
    this.commentsTotal = documentObject.getElementById("artwork-comments-total");
    this.commentList = documentObject.getElementById("comment-list");
    this.commentsMore = documentObject.getElementById("comments-more");
    this.commentsScroller = this.root.querySelector(".artwork-comments-scroll");
    this.commentsSection = this.commentsScroller;
    this.composer = this.root.querySelector(".artwork-comment-composer");
    this.commentForm = documentObject.getElementById("comment-form");
    this.commentInput = documentObject.getElementById("comment-input");
    this.currentArtwork = null;
    this.nextBefore = 0;
    this.loadController = null;
    this.protectedActionPending = false;
    this.modalGeneration = 0;
    this.bindEvents();
  }

  bindEvents() {
    this.root.querySelectorAll("[data-artwork-close]").forEach((element) => {
      element.addEventListener("click", () => this.close());
    });
    this.likeButton.addEventListener("click", () => {
      if (!this.currentArtwork) return;
      const context = this.artworkContext();
      this.runProtected(() => this.toggleLike(context));
    });
    this.favoriteButton.addEventListener("click", () => this.toggleFavorite());
    this.commentJumpButton.addEventListener("click", () => this.scrollToComments());
    this.shareButton.addEventListener("click", () => this.shareCurrentArtwork());
    this.commentsMore.addEventListener("click", () => this.loadComments(false));
    this.commentForm.addEventListener("submit", (event) => {
      event.preventDefault();
      const content = this.commentInput.value.trim();
      if (!content) {
        this.notify("评论内容不能为空");
        this.commentInput.focus();
        return;
      }
      const context = this.artworkContext();
      this.runProtected(() => this.submitComment(context, content));
    });
  }

  async open(artworkId) {
    this.cancelLoad();
    const generation = ++this.modalGeneration;
    this.modalManager.open(this.root, {
      initialFocus: this.card,
      onEscape: () => this.close()
    });
    this.currentArtwork = null;
    this.nextBefore = 0;
    this.title.textContent = "作品加载中…";
    this.text.textContent = "";
    this.galleryViewer.clear();
    this.commentList.textContent = "";
    this.commentInput.value = "";
    this.commentsMore.hidden = true;
    this.updateLike(false, 0);
    this.updateCommentCount(0);
    this.updateFavorite(false);
    this.card.classList.remove("is-text-only");
    this.gallery.hidden = false;
    this.galleryTools.hidden = false;
    this.setPanelAccessibility(this.detailsPanel, false);
    this.setPanelAccessibility(this.actionBar, false);
    this.setPanelAccessibility(this.interactions, false);
    this.setPanelAccessibility(this.commentsTitle, false);
    this.setPanelAccessibility(this.commentsScroller, false);
    this.setPanelAccessibility(this.composer, false);
    this.loadController = new AbortController();

    try {
      const detail = await fetchArtworkDetail({
        api: this.api, auth: this.auth, artworkId, signal: this.loadController.signal
      });
      if (generation !== this.modalGeneration) return;
      this.currentArtwork = detail;
      this.renderDetail(detail);
      await this.loadComments(true, this.artworkContext());
    } catch (error) {
      if (error && error.name === "AbortError") return;
      if (generation !== this.modalGeneration) return;
      this.title.textContent = "作品暂时无法加载";
      this.notify(error.message || "作品暂时无法加载");
    }
  }

  openText(hotspot) {
    this.cancelLoad();
    ++this.modalGeneration;
    this.modalManager.open(this.root, {
      initialFocus: this.card,
      onEscape: () => this.close()
    });
    this.currentArtwork = null;
    this.title.textContent = hotspot.title || "展览信息";
    this.text.textContent = hotspot.text || "";
    this.galleryViewer.clear();
    this.commentList.textContent = "";
    this.commentInput.value = "";
    this.commentsMore.hidden = true;
    this.updateLike(false, 0);
    this.updateCommentCount(0);
    this.updateFavorite(false);
    this.card.classList.add("is-text-only");
    this.gallery.hidden = true;
    this.galleryTools.hidden = true;
    this.setPanelAccessibility(this.detailsPanel, false);
    this.setPanelAccessibility(this.actionBar, true);
    this.setPanelAccessibility(this.interactions, true);
    this.setPanelAccessibility(this.commentsTitle, true);
    this.setPanelAccessibility(this.commentsScroller, true);
    this.setPanelAccessibility(this.composer, true);
  }

  renderDetail(detail) {
    this.title.textContent = detail.title || "未命名作品";
    this.text.textContent = detail.text || "";
    this.galleryViewer.setImages(detail.images, detail.title || "未命名作品");
    this.updateLike(detail.liked, detail.likeCount);
    this.updateCommentCount(detail.commentCount);
    this.updateFavorite(this.favorites.isFavorite(detail.artworkId));
  }

  setPanelAccessibility(panel, hidden) {
    if (!panel) return;
    panel.hidden = hidden;
    panel.inert = hidden;
    panel.setAttribute("aria-hidden", String(hidden));
  }

  updateLike(liked, count) {
    if (this.currentArtwork) {
      this.currentArtwork.liked = Boolean(liked);
      this.currentArtwork.likeCount = Number(formatCount(count));
    }
    this.likeButton.setAttribute("aria-pressed", liked ? "true" : "false");
    this.likeButton.setAttribute("aria-label", liked ? "取消点赞作品" : "点赞作品");
    this.likeCount.textContent = formatCount(count);
  }

  updateCommentCount(count) {
    const value = formatCount(count);
    if (this.currentArtwork) this.currentArtwork.commentCount = Number(value);
    this.commentCount.textContent = value;
    this.commentsTotal.textContent = value;
  }

  updateFavorite(checked) {
    this.favoriteButton.setAttribute("aria-pressed", String(Boolean(checked)));
    this.favoriteLabel.textContent = checked ? "已收藏" : "收藏";
  }

  toggleFavorite() {
    if (!this.currentArtwork) return;
    this.updateFavorite(this.favorites.toggle(this.currentArtwork.artworkId));
  }

  async shareCurrentArtwork() {
    if (!this.currentArtwork) return;
    let copied = false;
    try {
      copied = await this.copyShareLink(this.currentArtwork.artworkId);
    } catch (_) {}
    this.notify(copied ? "展品链接已复制" : "展品链接复制失败");
  }

  scrollToComments() {
    this.commentsSection.scrollIntoView({ behavior: "smooth", block: "start" });
  }

  artworkContext() {
    return this.currentArtwork ? {
      artworkId: this.currentArtwork.artworkId,
      liked: Boolean(this.currentArtwork.liked),
      generation: this.modalGeneration
    } : null;
  }

  isCurrent(context) {
    return Boolean(context && this.currentArtwork &&
      context.generation === this.modalGeneration &&
      context.artworkId === this.currentArtwork.artworkId);
  }

  async toggleLike(context) {
    if (!context) return;
    const method = context.liked ? "DELETE" : "POST";
    const result = await this.api.request(
      `/api/artworks/${encodeURIComponent(context.artworkId)}/likes`, { method, user: true }
    );
    if (this.isCurrent(context)) this.updateLike(result.liked, result.likeCount);
  }

  async loadComments(reset, context = this.artworkContext()) {
    if (!context || !this.isCurrent(context)) return;
    const artworkId = context.artworkId;
    const before = reset ? 0 : this.nextBefore;
    const query = before ? `?before=${encodeURIComponent(before)}&limit=20` : "?limit=20";
    try {
      const result = await this.api.request(
        `/api/artworks/${encodeURIComponent(artworkId)}/comments${query}`
      );
      if (!this.isCurrent(context)) return;
      const comments = Array.isArray(result.comments) ? result.comments : [];
      const items = this.createCommentItems(comments, reset);
      const nextBefore = Number(result.nextBefore) || 0;
      if (reset) this.commentList.replaceChildren(...items);
      else this.commentList.append(...items);
      this.nextBefore = nextBefore;
      this.commentsMore.hidden = !nextBefore;
      if (reset && this.commentsScroller) this.commentsScroller.scrollTop = 0;
    } catch (error) {
      if (this.isCurrent(context)) this.notify(error.message || "评论暂时无法加载");
    }
  }

  createCommentItems(comments, reset) {
    const items = [];
    if (!comments.length && reset) {
      const empty = this.document.createElement("p");
      empty.className = "empty-comments";
      empty.textContent = "还没有评论。";
      items.push(empty);
    }
    for (const comment of comments) {
      const item = this.document.createElement("article");
      item.className = "comment-item";
      const author = this.document.createElement("strong");
      const body = this.document.createElement("p");
      author.textContent = comment.username || "访客";
      body.textContent = comment.content || "";
      item.append(author, body);
      items.push(item);
    }
    return items;
  }

  async submitComment(context, content) {
    if (!context) return;
    await this.api.request(`/api/artworks/${encodeURIComponent(context.artworkId)}/comments`, {
      method: "POST", body: { content }, user: true
    });
    if (!this.isCurrent(context)) return;
    const currentCount = Number(this.currentArtwork.commentCount) || 0;
    this.updateCommentCount(currentCount + 1);
    if (this.commentInput.value.trim() === content) this.commentInput.value = "";
    await this.loadComments(true, context);
  }

  async runProtected(operation) {
    if (this.protectedActionPending) return;
    this.protectedActionPending = true;
    const hadToken = Boolean(this.auth.token());
    try {
      await this.auth.ensureAuthenticated();
      await operation();
    } catch (error) {
      if (hadToken && error instanceof ApiError && error.status === 401) {
        try {
          await this.auth.ensureAuthenticated();
          await operation();
          return;
        } catch (retryError) {
          if (!(retryError && retryError.code === "LOGIN_CANCELLED"))
            this.notify(retryError.message || "操作失败，请稍后重试");
          return;
        }
      }
      if (!(error && error.code === "LOGIN_CANCELLED"))
        this.notify(error.message || "操作失败，请稍后重试");
    } finally {
      this.protectedActionPending = false;
    }
  }

  cancelLoad() {
    if (this.loadController) this.loadController.abort();
    this.loadController = null;
  }

  close() {
    this.cancelLoad();
    ++this.modalGeneration;
    this.modalManager.close(this.root);
    this.currentArtwork = null;
    this.commentInput.value = "";
    this.galleryViewer.clear();
  }
}
