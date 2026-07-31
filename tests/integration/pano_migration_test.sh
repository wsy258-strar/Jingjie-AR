#!/usr/bin/env bash
set -euo pipefail

tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT
source_root=${PANO_SOURCE:-pano/html}

python3 scripts/migrate_pano.py \
  --source "$source_root" \
  --config "$tmp_dir/exhibition.json" \
  --assets "$tmp_dir/assets" \
  --manifest "$tmp_dir/assets-manifest.json"

python3 - "$tmp_dir" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
data = json.loads((root / "exhibition.json").read_text(encoding="utf-8"))

def _paths(value):
    if isinstance(value, dict):
        for key, child in value.items():
            if ((key.endswith("Url") or key in {"url", "preview"})
                    and isinstance(child, str) and child):
                yield child
            elif key == "images" and isinstance(child, list):
                yield from (item for item in child if isinstance(item, str))
            yield from _paths(child)
    elif isinstance(value, list):
        for child in value:
            yield from _paths(child)

assert data["exhibition"]["id"] == "19491365"
assert data["exhibition"]["title"] == "画叙勤廉·浙江美术馆馆藏作品展"
assert data["exhibition"]["defaultSceneId"] == "76196992"
assert len(data["scenes"]) == 9
assert sum(len(scene["hotspots"]) for scene in data["scenes"]) == 41
assert len(list((root / "assets/pano").glob("*/*_[bdflru].jpg"))) == 54
assert len(list((root / "assets/pano").glob("*/preview.jpg"))) == 9
assert len(list((root / "assets/pano").glob("*/thumb.jpg"))) == 9
qihang = next(a for a in data["artworks"] if a["title"] == "《启航》")
assert qihang["images"] == ["/assets/illustration/qihang.jpg"]
assert "何红舟  黄发祥" in qihang["text"]
assert all(path.startswith("/assets/") for path in _paths(data))
PY

# Source-authored XML/category resource paths, rather than filename conventions,
# must determine both emitted URLs and files copied to the deployment assets.
custom_source="$tmp_dir/custom-source"
cp -a "$source_root" "$custom_source"
python3 - "$custom_source" <<'PY'
import pathlib
import shutil
import sys

source = pathlib.Path(sys.argv[1])
index = source / "index.html"
text = index.read_text(encoding="utf-8")
replacements = {
    "/pano/15949056/preview.jpg": "/pano/custom/entry-preview.jpg",
    "/pano/15949056/15949056_%s.jpg": "/pano/custom/entry-face-%s.jpg",
    '"thumb": "/pano/15949056/thumb.jpg"': '"thumb": "/pano/custom/entry-thumb.jpg"',
}
for old, new in replacements.items():
    assert text.count(old) == 1, (old, text.count(old))
    text = text.replace(old, new)
index.write_text(text, encoding="utf-8")

custom = source / "assets/pano/custom"
custom.mkdir(parents=True)
shutil.copy2(source / "assets/pano/15949056/preview.jpg", custom / "entry-preview.jpg")
shutil.copy2(source / "assets/pano/15949056/thumb.jpg", custom / "entry-thumb.jpg")
for face in "bdflru":
    shutil.copy2(source / "assets/pano/15949056" / ("15949056_%s.jpg" % face),
                 custom / ("entry-face-%s.jpg" % face))
PY
python3 scripts/migrate_pano.py \
  --source "$custom_source" \
  --config "$tmp_dir/custom-exhibition.json" \
  --assets "$tmp_dir/custom-assets" \
  --manifest "$tmp_dir/custom-assets-manifest.json"
python3 - "$tmp_dir" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
data = json.loads((root / "custom-exhibition.json").read_text(encoding="utf-8"))
scene = next(scene for scene in data["scenes"] if scene["sceneId"] == "76196992")
assert scene["previewUrl"] == "/assets/pano/custom/entry-preview.jpg"
assert scene["cubeUrl"] == "/assets/pano/custom/entry-face-%s.jpg"
assert scene["thumbnailUrl"] == "/assets/pano/custom/entry-thumb.jpg"
for filename in ["entry-preview.jpg", "entry-thumb.jpg"] + ["entry-face-%s.jpg" % face for face in "bdflru"]:
    assert (root / "custom-assets/pano/custom" / filename).is_file(), filename
PY

# All unsafe output combinations must be rejected before cleanup or writes.  The
# test uses only a temporary copy and confirms its source data is byte-identical.
safety_source="$tmp_dir/safety-source"
cp -a "$source_root" "$safety_source"
before_hashes=$(sha256sum "$safety_source/index.html" "$safety_source/assets/pano/15949056/preview.jpg")
for conflict in source assets inside-source config-in-assets manifest-in-assets; do
  safe_root="$tmp_dir/safe-$conflict"
  case "$conflict" in
    source)
      assets="$safety_source"
      config="$safe_root/exhibition.json"
      manifest="$safe_root/assets-manifest.json"
      ;;
    assets)
      assets="$safety_source/assets"
      config="$safe_root/exhibition.json"
      manifest="$safe_root/assets-manifest.json"
      ;;
    inside-source)
      assets="$safety_source/derived-assets"
      config="$safe_root/exhibition.json"
      manifest="$safe_root/assets-manifest.json"
      ;;
    config-in-assets)
      assets="$safe_root/assets"
      config="$safe_root/assets/exhibition.json"
      manifest="$safe_root/assets-manifest.json"
      ;;
    manifest-in-assets)
      assets="$safe_root/assets"
      config="$safe_root/exhibition.json"
      manifest="$safe_root/assets/assets-manifest.json"
      ;;
  esac
  if python3 scripts/migrate_pano.py \
      --source "$safety_source" --assets "$assets" --config "$config" --manifest "$manifest"; then
    echo "unsafe output accepted: $conflict" >&2
    exit 1
  fi
  test "$before_hashes" = "$(sha256sum "$safety_source/index.html" "$safety_source/assets/pano/15949056/preview.jpg")"
done

# Every pair of outputs must be disjoint before parsing, cleanup, or writes.
# Each case starts with a sentinel and must leave no newly created output behind.
for conflict in config-equals-manifest assets-inside-config assets-inside-manifest; do
  safe_root="$tmp_dir/output-pair-$conflict"
  sentinel="$safe_root/sentinel.txt"
  mkdir -p "$safe_root"
  printf '%s\n' "$conflict" > "$sentinel"
  case "$conflict" in
    config-equals-manifest)
      assets="$safe_root/assets"
      config="$safe_root/shared.json"
      manifest="$safe_root/shared.json"
      outputs=("$assets" "$config")
      ;;
    assets-inside-config)
      config="$safe_root/config-target"
      assets="$config/assets"
      manifest="$safe_root/assets-manifest.json"
      outputs=("$assets" "$manifest")
      ;;
    assets-inside-manifest)
      config="$safe_root/exhibition.json"
      manifest="$safe_root/manifest-target"
      assets="$manifest/assets"
      outputs=("$config" "$assets")
      ;;
  esac
  if python3 scripts/migrate_pano.py \
      --source "$safety_source" --assets "$assets" --config "$config" --manifest "$manifest"; then
    echo "unsafe output pair accepted: $conflict" >&2
    exit 1
  fi
  test "$(cat "$sentinel")" = "$conflict"
  for output in "${outputs[@]}"; do
    test ! -e "$output"
  done
done

# Source mapping is deliberately one-to-one: missing records, duplicates, and
# scene/pano disagreement are data errors rather than opportunities to guess.
python3 - <<'PY'
import importlib.util

spec = importlib.util.spec_from_file_location("migrate_pano", "scripts/migrate_pano.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
raw = {"1": {"id": 1, "panoId": 2}}
category = [{"scenes": [{"id": 1, "panoId": 2, "thumb": "/pano/2/thumb.jpg"}]}]
valid_scene = ('<scene name="s_1" scene_id="1" pano_id="2">'
               '<preview url="/pano/2/preview.jpg"/><image><cube url="/pano/2/cube_%s.jpg"/>'
               '</image></scene>')

def rejects(xml, categories):
    try:
        module.scene_resources("<krpano>" + xml + "</krpano>", raw, categories)
    except ValueError:
        return
    raise AssertionError("invalid source mapping was accepted")

rejects(valid_scene.replace('<preview url="/pano/2/preview.jpg"/>', ""), category)
rejects(valid_scene + valid_scene, category)
rejects(valid_scene, [{"scenes": [{"id": 1, "panoId": 3, "thumb": "/pano/3/thumb.jpg"}]}])
try:
    module.unique_mapping([{"id": 1, "panoId": 2}, {"id": 1, "panoId": 3}], "test")
except ValueError:
    pass
else:
    raise AssertionError("duplicate initial-state scene was accepted")
PY

echo "PASS: pano migration"
