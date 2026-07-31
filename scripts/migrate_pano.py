#!/usr/bin/env python3
"""Deterministically migrate the exhibition data embedded in pano index.html.

The input is JavaScript-shaped data, not executable input.  This module accepts
only strings, numbers, arrays, objects, literal values, string concatenation,
and references to earlier ``*Text`` constants.
"""

import argparse
import hashlib
import json
import re
import shutil
import sys
import xml.etree.ElementTree as element_tree
from pathlib import Path


TEXT_DECLARATION = re.compile(
    r"\bconst\s+([A-Za-z][A-Za-z0-9_]*Text)\s*=", re.MULTILINE)
INITIAL_STATE = re.compile(r"\bwindow\.__INITIAL_STATE__\s*=", re.MULTILINE)
ASSET_PREFIXES = {
    "/pano/": "pano/",
    "/illstration/": "illustration/",
    "/hotspot/": "hotspot/",
    "/music/": "music/",
}


class ParseError(ValueError):
    """Raised when the restricted JavaScript data grammar is not met."""


class TokenStream:
    """Small lexer for the data-only JavaScript subset used by the source page."""

    def __init__(self, source, offset=0):
        self.source = source
        self.position = offset
        self.buffered = None

    def peek(self):
        if self.buffered is None:
            self.buffered = self._next()
        return self.buffered

    def take(self):
        token = self.peek()
        self.buffered = None
        return token

    def _next(self):
        source = self.source
        length = len(source)
        while self.position < length and source[self.position].isspace():
            self.position += 1
        if self.position >= length:
            return ("eof", "", self.position)
        start = self.position
        char = source[self.position]
        if char in "{}[],:;+":
            self.position += 1
            return (char, char, start)
        if char in "\"'`":
            return ("string", self._read_string(char), start)
        if char.isdigit() or (char == "-" and self.position + 1 < length and
                              source[self.position + 1].isdigit()):
            match = re.match(r"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?",
                             source[self.position:])
            if not match:
                raise ParseError("invalid number at character %d" % start)
            value = match.group(0)
            self.position += len(value)
            return ("number", value, start)
        if char.isalpha() or char in "_$":
            match = re.match(r"[A-Za-z_$][A-Za-z0-9_$]*", source[self.position:])
            value = match.group(0)
            self.position += len(value)
            return ("identifier", value, start)
        raise ParseError("unsupported JavaScript token at character %d" % start)

    def _read_string(self, quote):
        source = self.source
        self.position += 1
        chars = []
        while self.position < len(source):
            char = source[self.position]
            self.position += 1
            if char == quote:
                value = "".join(chars)
                if quote == "`" and "${" in value:
                    raise ParseError("template interpolation is not permitted")
                return value
            if char != "\\":
                chars.append(char)
                continue
            if self.position >= len(source):
                raise ParseError("unterminated escape sequence")
            escaped = source[self.position]
            self.position += 1
            simple = {"b": "\b", "f": "\f", "n": "\n", "r": "\r",
                      "t": "\t", "v": "\v", "0": "\0"}
            if escaped in simple:
                chars.append(simple[escaped])
            elif escaped == "u":
                digits = source[self.position:self.position + 4]
                if len(digits) != 4 or not re.match(r"^[0-9A-Fa-f]{4}$", digits):
                    raise ParseError("invalid unicode escape")
                chars.append(chr(int(digits, 16)))
                self.position += 4
            elif escaped == "x":
                digits = source[self.position:self.position + 2]
                if len(digits) != 2 or not re.match(r"^[0-9A-Fa-f]{2}$", digits):
                    raise ParseError("invalid hexadecimal escape")
                chars.append(chr(int(digits, 16)))
                self.position += 2
            elif escaped in "\n\r":
                if escaped == "\r" and self.position < len(source) and source[self.position] == "\n":
                    self.position += 1
            else:
                chars.append(escaped)
        raise ParseError("unterminated string")


class DataParser:
    """Parser for a deliberately non-executable JavaScript data subset."""

    def __init__(self, source, offset=0, constants=None):
        self.tokens = TokenStream(source, offset)
        self.constants = constants or {}

    def parse_value(self):
        value = self._parse_atom()
        while self.tokens.peek()[0] == "+":
            self.tokens.take()
            right = self._parse_atom()
            if not isinstance(value, str) or not isinstance(right, str):
                raise ParseError("only string concatenation is permitted")
            value += right
        return value

    def _parse_atom(self):
        kind, value, position = self.tokens.take()
        if kind == "string":
            return value
        if kind == "number":
            return float(value) if any(marker in value for marker in ".eE") else int(value)
        if kind == "identifier":
            literals = {"true": True, "false": False, "null": None}
            if value in literals:
                return literals[value]
            if value in self.constants:
                return self.constants[value]
            raise ParseError("unknown identifier %r at character %d" % (value, position))
        if kind == "{":
            return self._parse_object()
        if kind == "[":
            return self._parse_array()
        raise ParseError("expected data value at character %d" % position)

    def _parse_object(self):
        result = {}
        if self.tokens.peek()[0] == "}":
            self.tokens.take()
            return result
        while True:
            kind, key, position = self.tokens.take()
            if kind not in ("string", "identifier", "number"):
                raise ParseError("expected object key at character %d" % position)
            self._expect(":")
            result[str(key)] = self.parse_value()
            delimiter = self.tokens.take()[0]
            if delimiter == "}":
                return result
            if delimiter != ",":
                raise ParseError("expected ',' or '}' in object")
            if self.tokens.peek()[0] == "}":
                self.tokens.take()
                return result

    def _parse_array(self):
        result = []
        if self.tokens.peek()[0] == "]":
            self.tokens.take()
            return result
        while True:
            result.append(self.parse_value())
            delimiter = self.tokens.take()[0]
            if delimiter == "]":
                return result
            if delimiter != ",":
                raise ParseError("expected ',' or ']' in array")
            if self.tokens.peek()[0] == "]":
                self.tokens.take()
                return result

    def _expect(self, kind):
        actual = self.tokens.take()
        if actual[0] != kind:
            raise ParseError("expected %r at character %d" % (kind, actual[2]))


def parse_source(index_path):
    """Read permitted text constants and initial state without evaluating JavaScript."""
    source = index_path.read_text(encoding="utf-8")
    constants = {}
    for match in TEXT_DECLARATION.finditer(source):
        parser = DataParser(source, match.end(), constants)
        value = parser.parse_value()
        if not isinstance(value, str):
            raise ParseError("%s must be a string" % match.group(1))
        parser._expect(";")
        constants[match.group(1)] = value
    state_match = INITIAL_STATE.search(source)
    if not state_match:
        raise ParseError("window.__INITIAL_STATE__ not found")
    parser = DataParser(source, state_match.end(), constants)
    state = parser.parse_value()
    parser._expect(";")
    if not isinstance(state, dict):
        raise ParseError("window.__INITIAL_STATE__ must be an object")
    return state


def asset_url(value):
    """Prefix a source-authored local resource path with ``/assets/``."""
    source_path = source_asset_path(value)
    if not source_path:
        return ""
    for source_prefix, destination_prefix in ASSET_PREFIXES.items():
        if source_path.startswith(source_prefix):
            return "/assets/" + destination_prefix + source_path[len(source_prefix):]
    raise ValueError("unsupported asset URL %r" % value)


def source_asset_path(value):
    """Return the local portion of an allowed source asset URL without renaming it."""
    if not value:
        return ""
    if not isinstance(value, str):
        raise ValueError("asset URL must be a string")
    for source_prefix in ASSET_PREFIXES:
        position = value.find(source_prefix)
        if position >= 0:
            return value[position:]
    raise ValueError("unsupported asset URL %r" % value)


def normalize_hotspot(raw, pano_to_scene, artwork_by_signature):
    """Convert a source hotspot by the shape of its payload, never its numeric type."""
    data = raw.get("data")
    base = {
        "hotspotId": str(raw["id"]),
        "title": raw.get("title", ""),
        "ath": float(raw["ath"]),
        "atv": float(raw["atv"]),
        "iconUrl": asset_url(raw.get("iconUrl", "")),
    }
    if isinstance(data, dict) and str(data.get("panoId", "")) not in ("", "0"):
        base.update(type="scene", targetSceneId=pano_to_scene[str(data["panoId"])])
        return base
    if isinstance(data, list) and data and all(
            isinstance(item, dict) and "image" in item and "text" in item for item in data):
        signature = json.dumps({"title": raw.get("title", ""), "data": data},
                               ensure_ascii=False, sort_keys=True)
        artwork_id = artwork_by_signature.setdefault(signature, str(raw["id"]))
        base.update(type="artwork", artworkId=artwork_id)
        return base
    if raw.get("type") == 4 and isinstance(data, str):
        base.update(type="text", text=data)
        return base
    if isinstance(data, dict) and str(data.get("panoId", "")) == "0":
        base.update(type="inactive", rawData=data)
        return base
    raise ValueError("无法识别热点 %s" % raw.get("id"))


def scene_views(xml_root):
    """Extract source-authored view settings from the initial-state krpano XML."""
    views = {}
    for pano in xml_root.findall("./config/panos/pano"):
        view = pano.find("view")
        if view is None:
            continue
        scene_id = pano.attrib["name"].removeprefix("s_")
        views[scene_id] = {
            "hlookat": float(view.attrib["hlookat"]),
            "vlookat": float(view.attrib["vlookat"]),
            "fov": float(view.attrib["fov"]),
        }
    return views


def unique_mapping(entries, label):
    """Build a scene-id map while rejecting duplicate scene or pano identities."""
    by_scene = {}
    seen_panos = set()
    for entry in entries:
        scene_id = str(entry["id"])
        pano_id = str(entry["panoId"])
        if scene_id in by_scene or pano_id in seen_panos:
            raise ValueError("duplicate %s sceneId/panoId: %s/%s" %
                             (label, scene_id, pano_id))
        by_scene[scene_id] = entry
        seen_panos.add(pano_id)
    return by_scene


def scene_resources(xml_text, raw_scene_by_id, categories):
    """Read scene URLs from XML and thumbnail URLs from category scene records."""
    root = element_tree.fromstring(xml_text)
    xml_by_scene = {}
    xml_panos = set()
    for scene in root.findall("./scene"):
        scene_id = scene.attrib.get("scene_id")
        pano_id = scene.attrib.get("pano_id")
        if not scene_id or not pano_id or scene.attrib.get("name") != "s_" + scene_id:
            raise ValueError("invalid XML scene identity")
        if scene_id in xml_by_scene or pano_id in xml_panos:
            raise ValueError("duplicate XML sceneId/panoId: %s/%s" % (scene_id, pano_id))
        previews = scene.findall("./preview")
        cubes = scene.findall("./image/cube")
        if len(previews) != 1 or len(cubes) != 1:
            raise ValueError("missing or duplicate XML preview/cube for scene %s" % scene_id)
        preview_url = previews[0].attrib.get("url")
        cube_url = cubes[0].attrib.get("url")
        if not preview_url or not cube_url:
            raise ValueError("missing XML preview/cube URL for scene %s" % scene_id)
        xml_by_scene[scene_id] = {
            "panoId": pano_id,
            "preview": preview_url,
            "cube": cube_url,
        }
        xml_panos.add(pano_id)

    thumbnails = {}
    thumb_panos = set()
    for category in categories:
        for scene in category.get("scenes", []):
            scene_id = str(scene.get("id", ""))
            pano_id = str(scene.get("panoId", ""))
            thumb = scene.get("thumb")
            if not scene_id or not pano_id or not thumb:
                raise ValueError("invalid category scene thumbnail record")
            if scene_id in thumbnails or pano_id in thumb_panos:
                raise ValueError("duplicate category sceneId/panoId: %s/%s" %
                                 (scene_id, pano_id))
            thumbnails[scene_id] = {"panoId": pano_id, "thumb": thumb}
            thumb_panos.add(pano_id)

    if set(raw_scene_by_id) != set(xml_by_scene) or set(raw_scene_by_id) != set(thumbnails):
        raise ValueError("source scene records do not match XML/category scene records")
    resources = {}
    for scene_id, raw_scene in raw_scene_by_id.items():
        pano_id = str(raw_scene["panoId"])
        if xml_by_scene[scene_id]["panoId"] != pano_id or thumbnails[scene_id]["panoId"] != pano_id:
            raise ValueError("scene/pano mismatch for scene %s" % scene_id)
        resources[scene_id] = {
            "preview": xml_by_scene[scene_id]["preview"],
            "cube": xml_by_scene[scene_id]["cube"],
            "thumb": thumbnails[scene_id]["thumb"],
        }
    return root, resources


def truthy_source(value):
    return value in (True, 1, "1", "true", "True")


def copied_asset_paths(raw_scenes, resources):
    """Yield the exact source paths used by the source-authored resource records."""
    yielded = set()

    def add_url(url):
        path = source_asset_path(url)
        if path and path not in yielded:
            yielded.add(path)
            return path
        return None

    for scene in raw_scenes:
        scene_id = str(scene["id"])
        resource = resources[scene_id]
        added = add_url(resource["preview"])
        if added:
            yield added
        cube = source_asset_path(resource["cube"])
        if cube.count("%s") != 1:
            raise ValueError("cube URL must contain exactly one %%s placeholder for scene %s" % scene_id)
        for face in "bdflru":
            added = add_url(cube.replace("%s", face))
            if added:
                yield added
        added = add_url(resource["thumb"])
        if added:
            yield added
        sound = scene.get("sound", {})
        added = add_url(sound.get("url", "")) if sound.get("url") else None
        if added:
            yield added
        for hotspot in scene.get("hotspot", []):
            added = add_url(hotspot.get("iconUrl", "")) if hotspot.get("iconUrl") else None
            if added:
                yield added
            if isinstance(hotspot.get("data"), list):
                for item in hotspot["data"]:
                    added = add_url(item["image"])
                    if added:
                        yield added


def copy_assets(source_assets, destination_assets, source_paths):
    """Copy exactly the referenced, redistributable static assets."""
    for directory in ("pano", "illustration", "hotspot", "music"):
        target = destination_assets / directory
        if target.exists():
            shutil.rmtree(target)
    for source_path in source_paths:
        destination_url = asset_url(source_path)
        relative_destination = Path(destination_url.removeprefix("/assets/"))
        source_relative = source_path.removeprefix("/")
        source_file = source_assets / source_relative
        if not source_file.is_file():
            raise FileNotFoundError("referenced asset is missing: %s" % source_file)
        destination_file = destination_assets / relative_destination
        destination_file.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_file, destination_file)


def asset_manifest(assets_root):
    entries = []
    for path in sorted(file_path for file_path in assets_root.rglob("*") if file_path.is_file()):
        relative = path.relative_to(assets_root).as_posix()
        if relative.split("/", 1)[0] not in {"pano", "illustration", "hotspot", "music"}:
            continue
        entries.append({
            "path": relative,
            "size": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        })
    return entries


def is_within(path, parent):
    """Whether *path* is equal to or contained by *parent*."""
    try:
        path.relative_to(parent)
        return True
    except ValueError:
        return False


def validate_paths(source, config_path, assets_path, manifest_path):
    """Resolve all paths and reject every output that could overwrite source data."""
    source = source.resolve()
    source_assets = (source / "assets").resolve()
    config_path = config_path.resolve()
    assets_path = assets_path.resolve()
    manifest_path = manifest_path.resolve()
    if not source.is_dir() or not source_assets.is_dir():
        raise ValueError("source and source/assets must be existing directories")
    for label, output in (("assets", assets_path), ("config", config_path),
                          ("manifest", manifest_path)):
        if is_within(output, source) or is_within(source, output):
            raise ValueError("unsafe %s path overlaps source: %s" % (label, output))
    output_paths = (("config", config_path), ("assets", assets_path),
                    ("manifest", manifest_path))
    for index, (left_label, left_path) in enumerate(output_paths):
        for right_label, right_path in output_paths[index + 1:]:
            if is_within(left_path, right_path) or is_within(right_path, left_path):
                raise ValueError("unsafe %s and %s paths overlap: %s / %s" %
                                 (left_label, right_label, left_path, right_path))
    return source, config_path, assets_path, manifest_path


def migrate(source, config_path, assets_path, manifest_path):
    source, config_path, assets_path, manifest_path = validate_paths(
        source, config_path, assets_path, manifest_path)
    state = parse_source(source / "index.html")
    product = state["data"]["product"]
    property_data = product["property"]
    raw_scenes = product["config"]["scenes"]
    raw_scene_by_id = unique_mapping(raw_scenes, "initial-state")
    pano_to_scene = {str(scene["panoId"]): str(scene["id"]) for scene in raw_scenes}
    xml_root, resources = scene_resources(state["xml"], raw_scene_by_id,
                                          product["config"].get("category", []))
    views = scene_views(xml_root)
    artworks = []
    artwork_by_signature = {}
    scenes = []

    for raw_scene in raw_scenes:
        scene_id = str(raw_scene["id"])
        pano_id = str(raw_scene["panoId"])
        if scene_id not in views:
            raise ValueError("missing view for scene %s" % scene_id)
        normalized_hotspots = []
        for raw_hotspot in raw_scene.get("hotspot", []):
            normalized = normalize_hotspot(raw_hotspot, pano_to_scene, artwork_by_signature)
            normalized_hotspots.append(normalized)
            if normalized["type"] != "artwork" or normalized["artworkId"] != str(raw_hotspot["id"]):
                continue
            data = raw_hotspot["data"]
            texts = {item["text"] for item in data}
            if len(texts) != 1:
                raise ValueError("artwork hotspot %s has inconsistent text" % raw_hotspot["id"])
            artworks.append({
                "artworkId": normalized["artworkId"],
                "title": raw_hotspot.get("title", ""),
                "text": data[0]["text"],
                "images": [asset_url(item["image"]) for item in data],
            })
        sound = raw_scene.get("sound", {})
        normalized_scene = {
            "sceneId": scene_id,
            "panoId": pano_id,
            "name": raw_scene.get("name", ""),
            "previewUrl": asset_url(resources[scene_id]["preview"]),
            "cubeUrl": asset_url(resources[scene_id]["cube"]),
            "thumbnailUrl": asset_url(resources[scene_id]["thumb"]),
            "musicUrl": asset_url(sound.get("url", "")),
            "musicVolume": float(sound.get("volume", 0)),
            "musicAutoplay": truthy_source(sound.get("autoPlay", False)),
            "musicLoop": truthy_source(sound.get("isLoop", False)),
            "view": views[scene_id],
            "hotspots": normalized_hotspots,
        }
        scenes.append(normalized_scene)

    output = {
        "exhibition": {
            "id": str(property_data["id"]),
            "title": property_data["name"],
            "remark": property_data.get("remark", ""),
            "defaultSceneId": str(raw_scenes[0]["id"]),
        },
        "artworks": artworks,
        "scenes": scenes,
    }
    copy_assets(source / "assets", assets_path, copied_asset_paths(raw_scenes, resources))
    config_path.parent.mkdir(parents=True, exist_ok=True)
    config_path.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(asset_manifest(assets_path), ensure_ascii=False, indent=2) + "\n",
                             encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--assets", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    arguments = parser.parse_args(argv)
    try:
        migrate(arguments.source, arguments.config, arguments.assets, arguments.manifest)
    except (OSError, ParseError, ValueError, element_tree.ParseError) as error:
        print("pano migration failed: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
