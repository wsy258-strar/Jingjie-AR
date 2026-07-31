#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
static_root="$root/WebApps/ARServer/www"
asset_root="$static_root/assets"
manifest="$root/WebApps/ARServer/config/assets-manifest.json"
catalog="$root/WebApps/ARServer/config/exhibition.json"
runtime_root="$asset_root/krp/runtime"

python3 - "$manifest" "$catalog" "$static_root" <<'PY'
import hashlib
import json
import pathlib
import sys

manifest_path = pathlib.Path(sys.argv[1])
catalog_path = pathlib.Path(sys.argv[2])
static_root = pathlib.Path(sys.argv[3]).resolve()
asset_root = static_root / "assets"

def is_below(path, parent):
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False

manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
assert isinstance(manifest, list) and manifest, "资源清单必须是非空数组"

listed = set()
for entry in manifest:
    assert set(entry) == {"path", "size", "sha256"}, f"资源清单字段异常: {entry}"
    relative = entry["path"]
    assert isinstance(relative, str) and relative and not relative.startswith("/")
    assert relative not in listed, f"资源清单重复路径: {relative}"
    listed.add(relative)
    path = (asset_root / relative).resolve()
    assert is_below(path, asset_root.resolve()), f"资源路径越界: {relative}"
    assert path.is_file(), f"资源不存在: {relative}"
    content = path.read_bytes()
    assert len(content) == entry["size"], f"资源大小不匹配: {relative}"
    assert hashlib.sha256(content).hexdigest() == entry["sha256"], \
        f"资源 SHA-256 不匹配: {relative}"

actual = {
    str(path.relative_to(asset_root))
    for path in asset_root.rglob("*")
    if path.is_file() and not is_below(path, asset_root / "krp" / "runtime")
}
assert actual == listed, \
    f"资源清单与静态目录不一致: missing={sorted(actual-listed)}, extra={sorted(listed-actual)}"

catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
references = []

def collect(value):
    if isinstance(value, dict):
        for child in value.values():
            collect(child)
    elif isinstance(value, list):
        for child in value:
            collect(child)
    elif isinstance(value, str) and value.startswith("/assets/"):
        references.append(value)

collect(catalog)
assert references, "exhibition.json 未引用任何 /assets/ 资源"
faces = ("l", "r", "f", "b", "u", "d")
for reference in references:
    expanded = [reference.replace("%s", face) for face in faces] if "%s" in reference else [reference]
    if "%s" in reference:
        assert reference.count("%s") == 1, f"cubeUrl 占位符异常: {reference}"
        assert len(expanded) == 6
    for url in expanded:
        path = (static_root / url[1:]).resolve()
        assert is_below(path, asset_root.resolve()), f"配置资源路径越界: {url}"
        assert path.is_file(), f"配置引用资源不存在: {url}"

print(f"PASS: {len(listed)} 个公开资源大小和 SHA-256 正确，{len(references)} 个配置引用可用")
PY

# krpano 为私有授权运行时，只检查部署所需文件是否存在，不读取或公开其哈希。
test -f "$runtime_root/player_krp_v2.js" || {
  printf 'krpano runtime missing: %s/player_krp_v2.js\n' "$runtime_root" >&2
  exit 1
}
test -f "$runtime_root/player_offline.xml" || {
  printf 'krpano runtime missing: %s/player_offline.xml\n' "$runtime_root" >&2
  exit 1
}
printf 'PASS: krpano 授权运行文件已部署\n'
