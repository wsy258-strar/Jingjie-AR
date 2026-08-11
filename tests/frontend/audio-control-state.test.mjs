import assert from "node:assert/strict";
import test from "node:test";
import { AudioControlState } from "../../WebApps/ARServer/www/js/audio-control-state.js";

class FakeClassList {
  constructor() {
    this.values = new Set();
  }

  toggle(name, force) {
    if (force) this.values.add(name);
    else this.values.delete(name);
  }

  contains(name) {
    return this.values.has(name);
  }
}

class FakeEventTarget {
  constructor() {
    this.listeners = new Map();
  }

  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(listener);
  }

  removeEventListener(type, listener) {
    this.listeners.set(type, (this.listeners.get(type) || []).filter((item) => item !== listener));
  }

  dispatch(type) {
    for (const listener of this.listeners.get(type) || []) listener({ type, target: this });
  }
}

class FakeAudio extends FakeEventTarget {
  constructor() {
    super();
    this.src = "";
    this.paused = true;
    this.ended = false;
  }
}

class FakeButton {
  constructor() {
    this.classList = new FakeClassList();
    this.attributes = new Map();
    this.disabled = false;
    this.title = "";
  }

  setAttribute(name, value) {
    this.attributes.set(name, String(value));
  }

  getAttribute(name) {
    return this.attributes.get(name) || null;
  }
}

test("音频事件只在真实播放时显示播放状态", () => {
  const audio = new FakeAudio();
  const button = new FakeButton();
  const state = new AudioControlState({ audio, button });

  state.sync();
  assert.equal(button.disabled, true);
  assert.equal(button.title, "当前场景暂无音乐");

  audio.src = "/audio/guide.mp3";
  state.sync();
  assert.equal(button.disabled, false);
  assert.equal(button.classList.contains("is-playing"), false);
  assert.equal(button.title, "播放讲解");

  audio.dispatch("play");
  assert.equal(button.classList.contains("is-playing"), false, "paused 的 play 事件不应猜测播放状态");

  audio.paused = false;
  audio.dispatch("play");
  assert.equal(button.classList.contains("is-playing"), true);
  assert.equal(button.title, "暂停讲解");

  audio.paused = true;
  audio.dispatch("pause");
  assert.equal(button.classList.contains("is-playing"), false);

  audio.paused = false;
  audio.ended = false;
  audio.dispatch("play");
  audio.paused = true;
  audio.ended = true;
  audio.dispatch("ended");
  assert.equal(button.classList.contains("is-playing"), false);

  audio.ended = false;
  audio.paused = false;
  audio.dispatch("play");
  audio.paused = true;
  audio.dispatch("error");
  assert.equal(button.classList.contains("is-playing"), false);

  audio.paused = false;
  audio.dispatch("play");
  audio.paused = true;
  audio.src = "";
  audio.dispatch("emptied");
  assert.equal(button.classList.contains("is-playing"), false);
  assert.equal(button.disabled, true);

  state.destroy();
});
