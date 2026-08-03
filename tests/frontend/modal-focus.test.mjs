import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

const target = await mkdtemp(join(tmpdir(), "jingjie-ar-modal-focus-"));
const source = new URL("../../WebApps/ARServer/www/js/modal-focus.js", import.meta.url);
await writeFile(join(target, "modal-focus.mjs"), await readFile(source, "utf8"));
const { ModalFocusManager } = await import(pathToFileURL(join(target, "modal-focus.mjs")).href);
process.once("exit", () => rmSync(target, { recursive: true, force: true }));

function node(name) {
  return {
    name,
    inert: false,
    hidden: false,
    attributes: new Map(),
    classList: { toggle() {} },
    setAttribute(key, value) { this.attributes.set(key, value); },
    focus() { documentObject.activeElement = this; },
    querySelectorAll() { return []; }
  };
}

const background = node("background");
const documentObject = {
  activeElement: node("origin"),
  querySelector() { return background; },
  addEventListener() {}
};

test("嵌套模态只暴露顶层并在关闭后恢复背景和焦点", () => {
  const manager = new ModalFocusManager({ documentObject, backgroundSelector: ".museum-shell" });
  const origin = documentObject.activeElement;
  const artwork = node("artwork");
  const artworkFocus = node("artwork-focus");
  const login = node("login");
  const loginFocus = node("login-focus");

  manager.open(artwork, { initialFocus: artworkFocus });
  assert.equal(background.inert, true);
  assert.equal(documentObject.activeElement, artworkFocus);
  manager.open(login, { initialFocus: loginFocus });
  assert.equal(artwork.inert, true);
  assert.equal(artwork.attributes.get("aria-hidden"), "true");
  manager.close(login);
  assert.equal(artwork.inert, false);
  assert.equal(artwork.attributes.get("aria-hidden"), "false");
  manager.close(artwork);
  assert.equal(background.inert, false);
  assert.equal(documentObject.activeElement, origin);
});

test("Tab 在顶层模态首尾焦点之间循环", () => {
  const manager = new ModalFocusManager({ documentObject, backgroundSelector: ".museum-shell" });
  const modal = node("modal");
  const first = node("first");
  const last = node("last");
  modal.querySelectorAll = () => [first, last];
  manager.open(modal, { initialFocus: first });
  documentObject.activeElement = last;
  let prevented = false;
  manager.handleKeydown({ key: "Tab", shiftKey: false, preventDefault() { prevented = true; } });
  assert.equal(prevented, true);
  assert.equal(documentObject.activeElement, first);
  manager.close(modal);
});

test("焦点循环排除隐藏区域中的后代控件", () => {
  const manager = new ModalFocusManager({ documentObject, backgroundSelector: ".museum-shell" });
  const modal = node("modal");
  const visible = node("visible");
  const hiddenDescendant = node("hidden-descendant");
  hiddenDescendant.closest = () => ({ hidden: true });
  modal.querySelectorAll = () => [visible, hiddenDescendant];
  assert.deepEqual(manager.focusableElements(modal), [visible]);
});

test("焦点循环排除 aria-hidden 标签面板中的控件", () => {
  const manager = new ModalFocusManager({ documentObject, backgroundSelector: ".museum-shell" });
  const modal = node("modal");
  const visible = node("visible");
  const hiddenTabControl = node("hidden-tab-control");
  hiddenTabControl.closest = (selector) => selector.includes('[aria-hidden="true"]')
    ? { attributes: new Map([["aria-hidden", "true"]]) } : null;
  modal.querySelectorAll = () => [visible, hiddenTabControl];
  modal.contains = (element) => element === visible || element === hiddenTabControl;

  manager.open(modal, { initialFocus: visible });
  documentObject.activeElement = visible;
  let prevented = false;
  manager.handleKeydown({ key: "Tab", shiftKey: false, preventDefault() { prevented = true; } });

  assert.equal(prevented, true);
  assert.equal(documentObject.activeElement, visible);
  manager.close(modal);
});
