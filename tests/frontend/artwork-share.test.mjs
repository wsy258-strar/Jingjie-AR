import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

const target = await mkdtemp(join(tmpdir(), "jingjie-ar-artwork-share-"));
const source = new URL("../../WebApps/ARServer/www/js/artwork-share.js", import.meta.url);
await writeFile(join(target, "artwork-share.mjs"), await readFile(source, "utf8"));
const { buildArtworkShareUrl, copyArtworkShareLink } = await import(pathToFileURL(join(target, "artwork-share.mjs")).href);
process.once("exit", () => rmSync(target, { recursive: true, force: true }));

test("分享 URL 覆盖 artwork 参数并保留其他参数", () => {
  assert.equal(
    buildArtworkShareUrl("https://example.test/?from=poster&artwork=old", "a b"),
    "https://example.test/?from=poster&artwork=a+b"
  );
});

test("Clipboard API 成功时复制分享 URL", async () => {
  let copiedUrl;
  const copied = await copyArtworkShareLink({
    navigatorObject: { clipboard: { async writeText(url) { copiedUrl = url; } } },
    url: "https://x/?artwork=a"
  });

  assert.equal(copied, true);
  assert.equal(copiedUrl, "https://x/?artwork=a");
});

test("Clipboard API 被拒绝时使用临时文本框复制", async () => {
  const textarea = {
    select() { this.selected = true; },
    remove() { this.removed = true; }
  };
  let appended;
  const copied = await copyArtworkShareLink({
    navigatorObject: { clipboard: { async writeText() { throw new Error("denied"); } } },
    documentObject: {
      createElement(tagName) { assert.equal(tagName, "textarea"); return textarea; },
      body: { appendChild(node) { appended = node; } },
      execCommand(command) { assert.equal(command, "copy"); return true; }
    },
    url: "https://x/?artwork=a"
  });

  assert.equal(copied, true);
  assert.equal(appended, textarea);
  assert.equal(textarea.value, "https://x/?artwork=a");
  assert.equal(textarea.selected, true);
  assert.equal(textarea.removed, true);
});

test("没有 document 时无法回退复制会优雅返回 false", async () => {
  const copied = await copyArtworkShareLink({
    navigatorObject: { clipboard: { async writeText() { throw new Error("denied"); } } },
    url: "https://x/?artwork=a"
  });

  assert.equal(copied, false);
});
