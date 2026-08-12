const MIN_SCALE = 1;
const MAX_SCALE = 3;
const IMMERSIVE_MAX_SCALE = 4;
const DOUBLE_TAP_DELAY = 280;
const DOUBLE_TAP_DISTANCE = 24;
const SWIPE_DISTANCE = 50;
const ZOOM_FACTOR = 1.2;
const INTERACTIVE_SELECTOR = "button, input, textarea, select, a";

export function clampScale(value) {
  return Math.min(MAX_SCALE, Math.max(MIN_SCALE, Number(value) || MIN_SCALE));
}

export function clampOffset(value, overflow) {
  const limit = Math.max(0, Number(overflow) || 0) / 2;
  return Math.min(limit, Math.max(-limit, Number(value) || 0));
}

export class ArtworkGallery {
  constructor(elements = {}) {
    Object.assign(this, elements);
    this.imageFactory = elements.imageFactory || (() => new Image());
    this.images = [];
    this.currentIndex = 0;
    this.scale = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    this.pointer = null;
    this.immersiveScale = 1;
    this.immersiveOffsetX = 0;
    this.immersiveOffsetY = 0;
    this.pointers = new Map();
    this.singleTapTimer = null;
    this.lastTap = null;
    this.pinchStartDistance = 0;
    this.pinchStartScale = 1;
    this.savedScrollTop = 0;
    this.now = elements.now || (() => Date.now());
    this.setTimer = elements.setTimeout || ((callback, delay) => globalThis.setTimeout(callback, delay));
    this.clearTimer = elements.clearTimeout || ((timer) => globalThis.clearTimeout(timer));
    this.mobileMediaQuery = elements.mobileMediaQuery ||
      globalThis.matchMedia?.("(max-width: 820px)") || null;
    this.mobileCheck = elements.isMobile || (() => Boolean(this.mobileMediaQuery?.matches));
    this.bindEvents();
  }

  setImages(images, title) {
    this.images = Array.isArray(images)
      ? images.filter((value) => typeof value === "string" && value) : [];
    this.title = title || "作品图片";
    this.currentIndex = 0;
    this.resetView();
    this.render();
  }

  clear() { this.setImages([], ""); }
  next() { return this.goTo(this.currentIndex + 1); }
  previous() { return this.goTo(this.currentIndex - 1); }

  goTo(index) {
    if (index < 0 || index >= this.images.length || index === this.currentIndex) return false;
    this.currentIndex = index;
    this.resetView();
    this.resetImmersiveView();
    this.render();
    return true;
  }

  zoomIn() {
    this.scale = clampScale(this.scale * ZOOM_FACTOR);
    this.renderTransform();
    return this.scale;
  }

  zoomOut() {
    this.scale = clampScale(this.scale / ZOOM_FACTOR);
    if (this.scale === 1) this.resetView();
    else {
      this.constrainOffsets();
      this.renderTransform();
    }
    return this.scale;
  }

  resetView() {
    this.scale = 1;
    this.offsetX = 0;
    this.offsetY = 0;
    this.renderTransform();
  }

  bindEvents() {
    this.previousButton?.addEventListener("click", () => this.previous());
    this.nextButton?.addEventListener("click", () => this.next());
    this.zoomInButton?.addEventListener("click", () => this.zoomIn());
    this.zoomOutButton?.addEventListener("click", () => this.zoomOut());
    this.resetButton?.addEventListener("click", () => this.resetView());
    this.stage?.addEventListener("pointerdown", (event) => this.handlePointerDown(event));
    this.stage?.addEventListener("pointermove", (event) => this.handlePointerMove(event));
    this.stage?.addEventListener("pointerup", (event) => this.handlePointerUp(event));
    this.stage?.addEventListener("pointercancel", (event) => this.handlePointerCancel(event));
    this.stage?.addEventListener("lostpointercapture", (event) => this.handlePointerCancel(event));
    this.image?.addEventListener("dragstart", (event) => event.preventDefault());
    this.image?.addEventListener("click", (event) => {
      if (event.detail === 0 && this.mobileCheck()) this.openImmersive();
    });
    this.image?.addEventListener("keydown", (event) => {
      if ((event.key === "Enter" || event.key === " ") && this.mobileCheck()) {
        event.preventDefault();
        this.openImmersive();
      }
    });
    this.root?.addEventListener("keydown", (event) => this.handleKeydown(event));
    this.immersiveStage?.addEventListener("pointerdown", (event) =>
      this.handleImmersivePointerDown(event));
    this.immersiveStage?.addEventListener("pointermove", (event) =>
      this.handleImmersivePointerMove(event));
    this.immersiveStage?.addEventListener("pointerup", (event) =>
      this.handleImmersivePointerUp(event));
    this.immersiveStage?.addEventListener("pointercancel", (event) =>
      this.handleImmersivePointerCancel(event));
    this.immersiveStage?.addEventListener("lostpointercapture", (event) =>
      this.handleImmersivePointerCancel(event));
    this.immersiveRoot?.addEventListener("pointerdown", (event) => {
      if (event.target === this.immersiveRoot) this.handleImmersivePointerDown(event);
    });
    this.immersiveRoot?.addEventListener("pointermove", (event) => {
      if (event.target === this.immersiveRoot) this.handleImmersivePointerMove(event);
    });
    this.immersiveRoot?.addEventListener("pointerup", (event) => {
      if (event.target === this.immersiveRoot) this.handleImmersivePointerUp(event);
    });
    this.immersiveRoot?.addEventListener("pointercancel", (event) => {
      if (event.target === this.immersiveRoot) this.handleImmersivePointerCancel(event);
    });
    this.immersiveImage?.addEventListener("dragstart", (event) => event.preventDefault());
    this.immersiveCloseButton?.addEventListener("click", () => this.closeImmersive());
    const handleMobileChange = () => this.handleMobileChange();
    if (this.mobileMediaQuery?.addEventListener)
      this.mobileMediaQuery.addEventListener("change", handleMobileChange);
    else this.mobileMediaQuery?.addListener?.(handleMobileChange);
    if (this.stage && typeof globalThis.ResizeObserver === "function") {
      this.resizeObserver = new globalThis.ResizeObserver(() => {
        this.handleMobileChange();
        this.constrainOffsets();
        this.renderTransform();
        if (this.isImmersive()) {
          this.constrainImmersiveOffsets();
          this.renderImmersiveTransform();
        }
      });
      this.resizeObserver.observe(this.stage);
      if (this.immersiveStage) this.resizeObserver.observe(this.immersiveStage);
    }
  }

  handleMobileChange() {
    if (this.isImmersive() && !this.mobileCheck()) this.closeImmersive();
    this.updateImageOpenerAccessibility();
  }

  render() {
    const count = this.images.length;
    const multiple = count > 1;
    const source = this.images[this.currentIndex] || "";
    this.updateImageOpenerAccessibility();
    if (this.image) {
      this.image.src = source;
      this.image.alt = this.title || "作品图片";
      this.image.onerror = () => {
        if (this.images[this.currentIndex] === source && this.status) {
          this.status.textContent = "图片暂时无法加载";
          this.status.hidden = false;
        }
      };
    }
    if (this.status) {
      this.status.textContent = "";
      this.status.hidden = true;
    }
    if (this.counter) {
      this.counter.textContent = multiple ? `${this.currentIndex + 1} / ${count}` : "";
      this.counter.hidden = !multiple;
    }
    if (this.previousButton) {
      this.previousButton.hidden = !multiple;
      this.previousButton.disabled = !multiple || this.currentIndex === 0;
    }
    if (this.nextButton) {
      this.nextButton.hidden = !multiple;
      this.nextButton.disabled = !multiple || this.currentIndex === count - 1;
    }
    for (const index of [this.currentIndex - 1, this.currentIndex + 1]) {
      if (this.images[index]) this.preload(this.images[index]);
    }
    this.renderTransform();
    if (this.isImmersive()) this.renderImmersive();
  }

  preload(source) {
    const image = this.imageFactory();
    image.src = source;
  }

  renderTransform() {
    if (!this.image) return;
    this.image.style.transform =
      "translate3d(" + this.offsetX + "px," + this.offsetY + "px,0) scale(" + this.scale + ")";
  }

  openImmersive() {
    if (!this.images.length || !this.immersiveRoot || !this.mobileCheck() || this.isImmersive())
      return false;
    this.savedScrollTop = Number(this.scrollContainer?.scrollTop) || 0;
    this.resetImmersiveView();
    this.showImmersiveLayer?.();
    this.immersiveRoot.setAttribute("aria-hidden", "false");
    this.renderImmersive();
    return true;
  }

  closeImmersive() {
    const wasOpen = this.isImmersive();
    this.clearSingleTap();
    for (const pointerId of this.pointers.keys())
      this.releaseImmersivePointerCapture(pointerId);
    this.pointers.clear();
    this.lastTap = null;
    this.pinchStartDistance = 0;
    this.pinchStartScale = 1;
    this.resetImmersiveView();
    if (wasOpen) this.hideImmersiveLayer?.();
    this.immersiveRoot?.setAttribute("aria-hidden", "true");
    if (wasOpen && this.scrollContainer) this.scrollContainer.scrollTop = this.savedScrollTop;
    return wasOpen;
  }

  isImmersive() {
    return this.immersiveRoot?.getAttribute?.("aria-hidden") === "false";
  }

  resetImmersiveView() {
    this.immersiveScale = 1;
    this.immersiveOffsetX = 0;
    this.immersiveOffsetY = 0;
    this.renderImmersiveTransform();
  }

  renderImmersive() {
    if (!this.immersiveImage) return;
    this.immersiveImage.src = this.images[this.currentIndex] || "";
    this.immersiveImage.alt = this.title || "作品图片";
    this.renderImmersiveTransform();
  }

  renderImmersiveTransform() {
    if (!this.immersiveImage) return;
    this.immersiveImage.style.transform = "translate3d(" + this.immersiveOffsetX + "px," +
      this.immersiveOffsetY + "px,0) scale(" + this.immersiveScale + ")";
  }

  handleImmersivePointerDown(event) {
    if (!this.isImmersive() || event.target?.closest?.(INTERACTIVE_SELECTOR)) return;
    const pointer = {
      id: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      x: event.clientX,
      y: event.clientY,
      offsetX: this.immersiveOffsetX,
      offsetY: this.immersiveOffsetY,
      moved: false,
      multiTouch: false
    };
    this.pointers.set(event.pointerId, pointer);
    this.immersiveStage?.setPointerCapture?.(event.pointerId);
    if (this.pointers.size === 2) {
      this.cancelTapSequence();
      for (const active of this.pointers.values()) active.multiTouch = true;
      this.pinchStartDistance = this.immersivePointerDistance();
      this.pinchStartScale = this.immersiveScale;
    }
  }

  handleImmersivePointerMove(event) {
    const pointer = this.pointers.get(event.pointerId);
    if (!pointer) return;
    pointer.x = event.clientX;
    pointer.y = event.clientY;
    const moved = Math.hypot(pointer.x - pointer.startX, pointer.y - pointer.startY) >
      DOUBLE_TAP_DISTANCE;
    if (moved && !pointer.moved) this.cancelTapSequence();
    pointer.moved = pointer.moved || moved;
    if (this.pointers.size >= 2 && this.pinchStartDistance > 0) {
      this.immersiveScale = Math.min(IMMERSIVE_MAX_SCALE, Math.max(MIN_SCALE,
        this.pinchStartScale * this.immersivePointerDistance() / this.pinchStartDistance));
      if (this.immersiveScale === 1) {
        this.immersiveOffsetX = 0;
        this.immersiveOffsetY = 0;
      } else {
        this.constrainImmersiveOffsets();
      }
      this.renderImmersiveTransform();
      event.preventDefault?.();
      return;
    }
    if (this.pointers.size !== 1 || this.immersiveScale === 1) return;
    this.immersiveOffsetX = clampOffset(
      pointer.offsetX + event.clientX - pointer.startX,
      this.immersiveOverflow("width")
    );
    this.immersiveOffsetY = clampOffset(
      pointer.offsetY + event.clientY - pointer.startY,
      this.immersiveOverflow("height")
    );
    this.renderImmersiveTransform();
    event.preventDefault?.();
  }

  handleImmersivePointerUp(event) {
    const pointer = this.pointers.get(event.pointerId);
    if (!pointer) return;
    const pointerCount = this.pointers.size;
    this.pointers.delete(event.pointerId);
    this.releaseImmersivePointerCapture(event.pointerId);
    if (pointerCount > 1 || pointer.multiTouch) {
      if (this.pointers.size < 2) {
        this.pinchStartDistance = 0;
        this.rebaseRemainingImmersivePointer();
      }
      return;
    }
    const deltaX = event.clientX - pointer.startX;
    const deltaY = event.clientY - pointer.startY;
    if (Math.hypot(deltaX, deltaY) > DOUBLE_TAP_DISTANCE) this.cancelTapSequence();
    if (this.immersiveScale === 1 && Math.abs(deltaX) >= SWIPE_DISTANCE &&
      Math.abs(deltaX) > Math.abs(deltaY)) {
      if (deltaX < 0) this.next();
      else this.previous();
      return;
    }
    if (!pointer.moved && Math.hypot(deltaX, deltaY) <= DOUBLE_TAP_DISTANCE)
      this.handleImmersiveTap(event.clientX, event.clientY);
  }

  handleImmersivePointerCancel(event) {
    if (!this.pointers.has(event.pointerId)) return;
    this.pointers.delete(event.pointerId);
    this.releaseImmersivePointerCapture(event.pointerId);
    if (this.pointers.size < 2) {
      this.pinchStartDistance = 0;
      this.rebaseRemainingImmersivePointer();
    }
  }

  handleImmersiveTap(x, y) {
    const tap = { x, y, time: this.now() };
    if (this.lastTap && tap.time - this.lastTap.time <= DOUBLE_TAP_DELAY &&
      Math.hypot(tap.x - this.lastTap.x, tap.y - this.lastTap.y) <= DOUBLE_TAP_DISTANCE) {
      this.clearSingleTap();
      this.lastTap = null;
      this.immersiveScale = this.immersiveScale === 1 ? 2 : 1;
      this.immersiveOffsetX = 0;
      this.immersiveOffsetY = 0;
      this.renderImmersiveTransform();
      return;
    }
    this.clearSingleTap();
    this.lastTap = tap;
    this.singleTapTimer = this.setTimer(() => {
      this.singleTapTimer = null;
      this.lastTap = null;
      if (this.immersiveScale === 1) this.closeImmersive();
    }, DOUBLE_TAP_DELAY);
  }

  clearSingleTap() {
    if (this.singleTapTimer !== null) this.clearTimer(this.singleTapTimer);
    this.singleTapTimer = null;
  }

  cancelTapSequence() {
    this.clearSingleTap();
    this.lastTap = null;
  }

  rebaseRemainingImmersivePointer() {
    if (this.pointers.size !== 1) return;
    const remaining = this.pointers.values().next().value;
    remaining.startX = remaining.x;
    remaining.startY = remaining.y;
    remaining.offsetX = this.immersiveOffsetX;
    remaining.offsetY = this.immersiveOffsetY;
    remaining.moved = false;
  }

  updateImageOpenerAccessibility() {
    if (!this.image?.setAttribute) return;
    if (this.images.length && this.mobileCheck()) {
      this.image.setAttribute("role", "button");
      this.image.setAttribute("tabindex", "0");
      this.image.setAttribute("aria-label", "打开作品大图");
      return;
    }
    this.image.removeAttribute?.("role");
    this.image.removeAttribute?.("tabindex");
    this.image.removeAttribute?.("aria-label");
  }

  immersivePointerDistance() {
    const [first, second] = [...this.pointers.values()];
    return first && second ? Math.hypot(second.x - first.x, second.y - first.y) : 0;
  }

  constrainImmersiveOffsets() {
    this.immersiveOffsetX = clampOffset(
      this.immersiveOffsetX, this.immersiveOverflow("width"));
    this.immersiveOffsetY = clampOffset(
      this.immersiveOffsetY, this.immersiveOverflow("height"));
  }

  releaseImmersivePointerCapture(pointerId) {
    if (!this.immersiveStage?.releasePointerCapture) return;
    if (!this.immersiveStage.hasPointerCapture || this.immersiveStage.hasPointerCapture(pointerId))
      this.immersiveStage.releasePointerCapture(pointerId);
  }

  immersiveOverflow(dimension) {
    const property = dimension === "width" ? "Width" : "Height";
    const stageSize = Number(this.immersiveStage?.[`client${property}`]) ||
      Number(this.immersiveStage?.[dimension]) || 0;
    const imageSize = Number(this.immersiveImage?.[`client${property}`]) ||
      Number(this.immersiveImage?.[dimension]) || stageSize;
    return Math.max(0, imageSize * this.immersiveScale - stageSize);
  }

  constrainOffsets() {
    this.offsetX = clampOffset(this.offsetX, this.overflow("width"));
    this.offsetY = clampOffset(this.offsetY, this.overflow("height"));
  }

  handlePointerDown(event) {
    if (!this.images.length || event.target?.closest?.(INTERACTIVE_SELECTOR)) return;
    this.pointer = {
      id: event.pointerId,
      startX: event.clientX,
      startY: event.clientY,
      startedOnImage: event.target === this.image,
      offsetX: this.offsetX,
      offsetY: this.offsetY
    };
    this.stage?.setPointerCapture?.(event.pointerId);
  }

  handlePointerMove(event) {
    if (!this.isActivePointer(event) || this.scale === 1) return;
    const deltaX = event.clientX - this.pointer.startX;
    const deltaY = event.clientY - this.pointer.startY;
    this.offsetX = clampOffset(this.pointer.offsetX + deltaX, this.overflow("width"));
    this.offsetY = clampOffset(this.pointer.offsetY + deltaY, this.overflow("height"));
    this.renderTransform();
    event.preventDefault?.();
  }

  handlePointerUp(event) {
    if (!this.isActivePointer(event)) return;
    const pointer = this.pointer;
    this.pointer = null;
    this.releasePointerCapture(event.pointerId);
    if (this.scale !== 1) return;
    const deltaX = event.clientX - pointer.startX;
    const deltaY = event.clientY - pointer.startY;
    if (Math.abs(deltaX) >= SWIPE_DISTANCE && Math.abs(deltaX) > Math.abs(deltaY)) {
      if (deltaX < 0) this.next();
      else this.previous();
      return;
    }
    if (pointer.startedOnImage && Math.hypot(deltaX, deltaY) <= DOUBLE_TAP_DISTANCE &&
      this.mobileCheck()) this.openImmersive();
  }

  handlePointerCancel(event) {
    if (!this.isActivePointer(event)) return;
    this.releasePointerCapture(event.pointerId);
    this.pointer = null;
  }

  releasePointerCapture(pointerId) {
    if (!this.stage?.releasePointerCapture) return;
    if (!this.stage.hasPointerCapture || this.stage.hasPointerCapture(pointerId))
      this.stage.releasePointerCapture(pointerId);
  }

  handleKeydown(event) {
    const actions = {
      ArrowLeft: () => this.previous(),
      ArrowRight: () => this.next(),
      "+": () => this.zoomIn(),
      "-": () => this.zoomOut(),
      "0": () => this.resetView()
    };
    const action = actions[event.key];
    if (!action) return;
    event.preventDefault();
    action();
  }

  isActivePointer(event) {
    return Boolean(this.pointer && this.pointer.id === event.pointerId);
  }

  overflow(dimension) {
    const stageSize = Number(this.stage?.[`client${dimension === "width" ? "Width" : "Height"}`]) ||
      Number(this.stage?.[dimension]) || 0;
    const imageSize = Number(this.image?.[`client${dimension === "width" ? "Width" : "Height"}`]) ||
      Number(this.image?.[dimension]) || stageSize;
    return Math.max(0, imageSize * this.scale - stageSize);
  }
}
