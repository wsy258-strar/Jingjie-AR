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

echo "PASS: pano migration"
