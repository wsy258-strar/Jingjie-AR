// 用户会话与匿名访客会话严格分离：仅作品写操作使用用户令牌。
import { ApiError, USER_TOKEN_KEY } from "./api-client.js";

function defaultStorage() {
  return typeof globalThis.sessionStorage === "undefined" ? null : globalThis.sessionStorage;
}

export class AuthSession {
  constructor({ client = null, storage = defaultStorage(), onAuthenticationRequired = null } = {}) {
    this.client = client;
    this.storage = storage;
    this.onAuthenticationRequired = onAuthenticationRequired;
    this.pendingAuthentication = null;
  }

  async ensureAuthenticated() {
    const existing = this.token();
    if (existing) return existing;
    if (this.pendingAuthentication) return this.pendingAuthentication;

    this.pendingAuthentication = Promise.resolve()
      .then(() => this.onAuthenticationRequired ? this.onAuthenticationRequired() : undefined)
      .then(() => {
        const token = this.token();
        if (token) return token;
        throw new ApiError(401, "AUTHENTICATION_REQUIRED",
          "请先登录后再进行此操作", "");
      })
      .finally(() => { this.pendingAuthentication = null; });
    return this.pendingAuthentication;
  }

  async authenticate(username, password) {
    if (!this.client) {
      throw new ApiError(503, "AUTH_SERVICE_UNAVAILABLE", "认证服务不可用", "");
    }
    const result = await this.client.request("/api/auth", {
      method: "POST",
      body: { username, password }
    });
    if (!result || typeof result.token !== "string" || !result.token) {
      throw new ApiError(503, "AUTH_TOKEN_MISSING", "认证响应缺少令牌", "");
    }
    if (this.storage) this.storage.setItem(USER_TOKEN_KEY, result.token);
    return result;
  }

  clear() {
    if (this.storage) this.storage.removeItem(USER_TOKEN_KEY);
  }

  token() {
    return this.storage ? this.storage.getItem(USER_TOKEN_KEY) || "" : "";
  }
}
