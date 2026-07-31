// 展馆前端的统一 API 客户端：请求头、响应信封和令牌失效处理集中在此处。
export const VISITOR_TOKEN_KEY = "ar.visitorToken";
export const USER_TOKEN_KEY = "ar.userToken";

export class ApiError extends Error {
  constructor(status, code, message, requestId) {
    super(message || "API request failed");
    this.name = "ApiError";
    this.status = status;
    this.code = code || "API_ERROR";
    this.requestId = requestId || "";
  }
}

function defaultStorage() {
  return typeof globalThis.sessionStorage === "undefined" ? null : globalThis.sessionStorage;
}

function joinUrl(baseUrl, path) {
  if (!baseUrl || /^https?:\/\//i.test(path)) return path;
  return baseUrl.replace(/\/$/, "") + "/" + path.replace(/^\//, "");
}

function isJsonBody(body) {
  return body !== null && typeof body === "object" &&
    !(typeof FormData !== "undefined" && body instanceof FormData) &&
    !(typeof Blob !== "undefined" && body instanceof Blob) &&
    !(typeof URLSearchParams !== "undefined" && body instanceof URLSearchParams);
}

async function readResponseBody(response) {
  const text = await response.text();
  if (!text) return { value: null, text: "" };
  try {
    return { value: JSON.parse(text), text };
  } catch (_) {
    return { value: null, text };
  }
}

export class ApiClient {
  constructor({ baseUrl = "", fetchImpl = globalThis.fetch, storage = defaultStorage() } = {}) {
    if (typeof fetchImpl !== "function") throw new Error("fetch is unavailable");
    this.baseUrl = baseUrl;
    this.fetchImpl = fetchImpl;
    this.storage = storage;
  }

  request(path, {
    method = "GET", body, visitor = false, user = false, signal, keepalive = false
  } = {}) {
    const headers = { Accept: "application/json" };
    const visitorToken = visitor && this.storage ? this.storage.getItem(VISITOR_TOKEN_KEY) : null;
    const userToken = user && this.storage ? this.storage.getItem(USER_TOKEN_KEY) : null;
    if (visitorToken) headers["X-Visitor-Token"] = visitorToken;
    if (userToken) headers.Authorization = "Bearer " + userToken;

    let requestBody = body;
    if (isJsonBody(body)) {
      headers["Content-Type"] = "application/json";
      requestBody = JSON.stringify(body);
    }
    const options = { method, headers, signal, keepalive };
    if (typeof requestBody !== "undefined") options.body = requestBody;

    return this.fetchImpl(joinUrl(this.baseUrl, path), options)
      .then(async (response) => {
        const parsed = await readResponseBody(response);
        const envelope = parsed.value && typeof parsed.value === "object" ? parsed.value : null;
        const failed = !response.ok || (envelope && envelope.success === false);
        if (failed) {
          if (response.status === 401 && user && this.storage)
            this.storage.removeItem(USER_TOKEN_KEY);
          const message = envelope && envelope.message ? envelope.message :
            (parsed.text || "HTTP " + response.status);
          throw new ApiError(response.status,
            envelope && envelope.code ? envelope.code : "HTTP_" + response.status,
            message, envelope && envelope.requestId ? envelope.requestId : "");
        }
        if (envelope && envelope.success === true) return envelope.data === undefined ? null : envelope.data;
        return parsed.value;
      });
  }
}
