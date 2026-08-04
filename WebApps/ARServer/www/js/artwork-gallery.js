const MIN_SCALE = 1;
const MAX_SCALE = 3;
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
    this.root?.addEventListener("keydown", (event) => this.handleKeydown(event));
    if (this.stage && typeof globalThis.ResizeObserver === "function") {
      this.resizeObserver = new globalThis.ResizeObserver(() => {
        this.constrainOffsets();
        this.renderTransform();
      });
      this.resizeObserver.observe(this.stage);
    }
  }

  render() {
    const count = this.images.length;
    const multiple = count > 1;
    const source = this.images[this.currentIndex] || "";
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
    if (Math.abs(deltaX) < 50 || Math.abs(deltaX) <= Math.abs(deltaY)) return;
    if (deltaX < 0) this.next();
    else this.previous();
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
