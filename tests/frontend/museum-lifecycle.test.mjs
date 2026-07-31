import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

const target = await mkdtemp(join(tmpdir(), "jingjie-ar-lifecycle-"));
const source = new URL("../../WebApps/ARServer/www/js/museum-lifecycle.js", import.meta.url);
await writeFile(join(target, "museum-lifecycle.mjs"), await readFile(source, "utf8"));
const { MuseumLifecycle } = await import(pathToFileURL(join(target, "museum-lifecycle.mjs")).href);
process.once("exit", () => rmSync(target, { recursive: true, force: true }));

test("同一页面生命周期的目录重试复用唯一访客 bootstrap", async () => {
  let calls = 0;
  const lifecycle = new MuseumLifecycle({
    visitor: { async bootstrap() { calls += 1; return { visitorToken: "visitor-1" }; } }
  });

  const first = lifecycle.bootstrapVisitorOnce();
  const retry = lifecycle.bootstrapVisitorOnce();
  assert.equal(first, retry);
  assert.equal((await retry).visitorToken, "visitor-1");
  assert.equal((await lifecycle.bootstrapVisitorOnce()).visitorToken, "visitor-1");
  assert.equal(calls, 1);
});
