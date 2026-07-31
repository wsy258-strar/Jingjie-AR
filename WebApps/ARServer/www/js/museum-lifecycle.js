// 页面生命周期内复用访客初始化 Promise，目录重试不会重复记录浏览量。
export class MuseumLifecycle {
  constructor({ visitor } = {}) {
    if (!visitor) throw new Error("MuseumLifecycle requires visitor session");
    this.visitor = visitor;
    this.visitorBootstrapPromise = null;
  }

  bootstrapVisitorOnce() {
    if (!this.visitorBootstrapPromise) {
      this.visitorBootstrapPromise = Promise.resolve().then(() => this.visitor.bootstrap());
    }
    return this.visitorBootstrapPromise;
  }
}
