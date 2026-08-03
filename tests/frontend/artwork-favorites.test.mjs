import assert from "node:assert/strict";
import { mkdtemp, readFile, writeFile } from "node:fs/promises";
import { rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";
import test from "node:test";

const target = await mkdtemp(join(tmpdir(), "jingjie-ar-artwork-favorites-"));
const source = new URL("../../WebApps/ARServer/www/js/artwork-favorites.js", import.meta.url);
await writeFile(join(target, "artwork-favorites.mjs"), await readFile(source, "utf8"));
const { ArtworkFavorites } = await import(pathToFileURL(join(target, "artwork-favorites.mjs")).href);
process.once("exit", () => rmSync(target, { recursive: true, force: true }));

function storageWith(values = {}) {
  const data = new Map(Object.entries(values));
  return {
    getItem(key) { return data.has(key) ? data.get(key) : null; },
    setItem(key, value) { data.set(key, String(value)); }
  };
}

test("收藏状态按作品 ID 持久化", () => {
  const storage = storageWith();
  const favorites = new ArtworkFavorites({ storage });

  assert.equal(favorites.toggle("work-a"), true);
  assert.equal(favorites.isFavorite("work-a"), true);
  assert.equal(storage.getItem("jingjie-ar:favorite-artworks"), '["work-a"]');
  assert.equal(new ArtworkFavorites({ storage }).isFavorite("work-a"), true);
  assert.equal(favorites.toggle("work-a"), false);
});

test("无效 JSON 视为无收藏", () => {
  const favorites = new ArtworkFavorites({
    storage: storageWith({ "jingjie-ar:favorite-artworks": "not-json" })
  });

  assert.equal(favorites.isFavorite("work-a"), false);
});

test("存储读取和写入失败时退回实例内存且不抛错", () => {
  const throwingStorage = {
    getItem() { throw new Error("unavailable"); },
    setItem() { throw new Error("unavailable"); }
  };
  const favorites = new ArtworkFavorites({ storage: throwingStorage });

  assert.equal(favorites.toggle("work-a"), true);
  assert.equal(favorites.isFavorite("work-a"), true);
  assert.equal(favorites.toggle("work-a"), false);
  assert.equal(favorites.isFavorite("work-a"), false);
});
