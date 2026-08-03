// 统一维护模态焦点栈：只暴露顶层弹窗，并让键盘焦点留在当前对话框内。
const FOCUSABLE_SELECTOR = [
  "a[href]", "button:not([disabled])", "input:not([disabled])",
  "textarea:not([disabled])", "select:not([disabled])", '[tabindex]:not([tabindex="-1"])'
].join(",");

function showModal(modal, visible) {
  modal.setAttribute("aria-hidden", visible ? "false" : "true");
  modal.classList.toggle("is-open", visible);
  modal.inert = !visible;
}

export class ModalFocusManager {
  constructor({ documentObject = document, backgroundSelector = ".museum-shell" } = {}) {
    this.document = documentObject;
    this.background = documentObject.querySelector(backgroundSelector);
    this.stack = [];
    this.handleKeydown = this.handleKeydown.bind(this);
    documentObject.addEventListener("keydown", this.handleKeydown);
  }

  open(modal, { initialFocus = null, onEscape = null } = {}) {
    const current = this.stack[this.stack.length - 1];
    if (current && current.modal === modal) return;
    if (current) {
      current.modal.inert = true;
      current.modal.setAttribute("aria-hidden", "true");
    }
    if (this.background) this.background.inert = true;
    const entry = {
      modal,
      onEscape,
      restoreFocus: this.document.activeElement
    };
    this.stack.push(entry);
    showModal(modal, true);
    const target = initialFocus || this.focusableElements(modal)[0] || modal;
    if (target && typeof target.focus === "function") target.focus();
  }

  close(modal) {
    const entry = this.stack[this.stack.length - 1];
    if (!entry || entry.modal !== modal) return false;
    this.stack.pop();
    showModal(modal, false);
    const previous = this.stack[this.stack.length - 1];
    if (previous) {
      previous.modal.inert = false;
      previous.modal.setAttribute("aria-hidden", "false");
    } else if (this.background) {
      this.background.inert = false;
    }
    if (entry.restoreFocus && typeof entry.restoreFocus.focus === "function")
      entry.restoreFocus.focus();
    return true;
  }

  focusableElements(modal) {
    return Array.from(modal.querySelectorAll(FOCUSABLE_SELECTOR)).filter((element) => {
      const hiddenAncestor = typeof element.closest === "function"
        ? element.closest('[hidden],[inert],[aria-hidden="true"]') : null;
      return !hiddenAncestor && !element.hidden && !element.disabled &&
        element.getAttribute?.("aria-hidden") !== "true";
    });
  }

  handleKeydown(event) {
    const entry = this.stack[this.stack.length - 1];
    if (!entry) return;
    if (event.key === "Escape") {
      if (entry.onEscape) {
        event.preventDefault();
        entry.onEscape();
      }
      return;
    }
    if (event.key !== "Tab") return;
    const focusable = this.focusableElements(entry.modal);
    if (!focusable.length) {
      event.preventDefault();
      entry.modal.focus();
      return;
    }
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (event.shiftKey && this.document.activeElement === first) {
      event.preventDefault();
      last.focus();
    } else if (!event.shiftKey && this.document.activeElement === last) {
      event.preventDefault();
      first.focus();
    } else if (!entry.modal.contains?.(this.document.activeElement)) {
      event.preventDefault();
      first.focus();
    }
  }
}
