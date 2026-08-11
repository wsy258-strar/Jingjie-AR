// 音频控件状态以 HTMLAudioElement 的真实属性和事件为唯一来源。
export class AudioControlState {
  constructor({ audio, button }) {
    this.audio = audio;
    this.button = button;
    this.sync = this.sync.bind(this);
    this.events = ["play", "pause", "ended", "error", "emptied"];
    this.events.forEach((type) => audio.addEventListener(type, this.sync));
    this.sync();
  }

  sync() {
    const hasSource = Boolean(this.audio.src);
    const playing = hasSource && this.audio.paused === false && this.audio.ended === false;
    const label = !hasSource ? "当前场景暂无音乐" : playing ? "暂停讲解" : "播放讲解";
    this.button.disabled = !hasSource;
    this.button.classList.toggle("is-playing", playing);
    this.button.setAttribute("aria-label", label);
    this.button.title = label;
    return playing;
  }

  setUnavailable() {
    this.button.disabled = true;
    this.button.classList.toggle("is-playing", false);
    this.button.setAttribute("aria-label", "当前场景暂无音乐");
    this.button.title = "当前场景暂无音乐";
  }

  destroy() {
    this.events.forEach((type) => this.audio.removeEventListener(type, this.sync));
  }
}
