#!/usr/bin/env python3
"""Pack the data of a region into the directory the device reads, and check it.

    pack_region.py pack  --id ID --name NAME --tiles T.mbtiles --routing DIR --places P.pidx --out Maps/ID
                         [--bbox W,S,E,N] [--source TEXT] [--archive]
    pack_region.py check Maps/ID

A region is one directory (Docs/OfflineNavigation_Design.md 4.1):

    ID/region.json     what the region is, its box, and what is in it (counts, sizes, SHA-256 of the big files)
    ID/tiles.mbtiles   vector tiles
    ID/routing/Z/X/Y.rng   the routing tiles
    ID/places.pidx     the search index

"pack" copies the files in, writes region.json and checks the result; "check" checks a directory that
is there already (after a copy to the SD card, say). The check opens every file the way the device does:
the tiles are read, every routing tile's size must match its header, the index's size must match its
header, the region's box must hold what the other files cover. Exit code 1 if anything is wrong.
--archive also writes ID.zip next to the directory, for sending it around.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import re
import shutil
import sqlite3
import struct
import sys
import zipfile

REGION_ID = re.compile(r"^[a-z0-9][a-z0-9_-]{0,47}$")
RNG_HEADER = 36
PIDX_HEADER = 36
ATTRIBUTION = "(c) OpenStreetMap contributors (ODbL)"
# the box of the routing tiles and of the places may stick out of the tiles' box by this much (degrees)
SLACK = 0.02


class Problems(list):
    """What is wrong; .warnings is what is odd but harmless."""

    def __init__(self):
        super().__init__()
        self.warnings = []

    def add(self, text):
        self.append(text)
def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def tree_bytes(path: str) -> int:
    return sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(path) for f in fs)


# ------------------------------------------------------------------------------------ reading ---
def read_tiles(path: str, problems: Problems):
    """Metadata and counts of an .mbtiles file, or None."""
    try:
        db = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
        meta = dict(db.execute("SELECT name, value FROM metadata"))
        levels = {z: n for z, n in db.execute("SELECT zoom_level, COUNT(*) FROM tiles GROUP BY zoom_level")}
        sample = db.execute("SELECT tile_data FROM tiles LIMIT 1").fetchone()
        db.close()
    except sqlite3.Error as e:
        problems.add(f"tiles: cannot read {path}: {e}")
        return None
    if not levels:
        problems.add("tiles: the file has no tiles")
        return None
    for key in ("minzoom", "maxzoom", "bounds"):
        if key not in meta:
            problems.add(f"tiles: no '{key}' in the metadata")
            return None
    with open(path, "rb") as f:
        head = f.read(20)
    if len(head) >= 20 and head[18] == 2:
        problems.add("tiles: the file is in WAL mode, which needs -wal and -shm files beside it (sqlite3 FILE 'PRAGMA journal_mode=DELETE')")
    if meta.get("format", "pbf") != "pbf":
        problems.add(f"tiles: the format is {meta.get('format')}, not pbf")
    try:
        bounds = [float(v) for v in meta["bounds"].split(",")]
        assert len(bounds) == 4 and bounds[0] < bounds[2] and bounds[1] < bounds[3]
    except (ValueError, AssertionError):
        problems.add(f"tiles: bad bounds '{meta['bounds']}'")
        return None
    lo, hi = int(meta["minzoom"]), int(meta["maxzoom"])
    if min(levels) < lo or max(levels) > hi:
        problems.add(f"tiles: tiles at zoom {min(levels)}-{max(levels)} but the metadata says {lo}-{hi}")
    if lo not in levels or hi not in levels:
        problems.add(f"tiles: no tiles at zoom {lo if lo not in levels else hi}")
    if sample and sample[0][:2] != b"\x1f\x8b" and sample[0][:1] != b"\x1a":
        problems.add("tiles: the tiles are not vector tiles")
    return {"minzoom": lo, "maxzoom": hi, "bounds": bounds, "count": sum(levels.values()), "levels": levels,
            "name": meta.get("name", "")}


def read_routing(path: str, problems: Problems):
    """Counts and the box of a directory of routing tiles, or None."""
    files = []
    zooms = set()
    for d, _, names in os.walk(path):
        for n in names:
            if n.endswith(".rng"):
                files.append(os.path.join(d, n))
    if not files:
        problems.add(f"routing: no .rng files under {path}")
        return None
    nodes = edges = size = 0
    box = [180.0, 90.0, -180.0, -90.0]
    for f in sorted(files):
        rel = os.path.relpath(f, path).split(os.sep)
        if len(rel) != 3 or not all(re.fullmatch(r"\d+", r) for r in (rel[0], rel[1], rel[2][:-4])):
            problems.add(f"routing: {os.path.relpath(f, path)} is not Z/X/Y.rng")
            continue
        zooms.add(int(rel[0]))
        data = open(f, "rb").read()
        if len(data) < RNG_HEADER or data[:4] != b"RNG1":
            problems.add(f"routing: {os.path.relpath(f, path)} is not a routing tile")
            continue
        _, hs, n, e, b, w_, s_, e_, n_ = struct.unpack_from("<HHIIIiiii", data, 4)
        if hs != RNG_HEADER or len(data) != hs + n * 12 + e * 12 + b * 16:
            problems.add(f"routing: {os.path.relpath(f, path)} has the wrong size for its header")
            continue
        nodes += n
        edges += e
        size += len(data)
        box = [min(box[0], w_ / 1e7), min(box[1], s_ / 1e7), max(box[2], e_ / 1e7), max(box[3], n_ / 1e7)]
    if len(zooms) != 1:
        problems.add(f"routing: tiles of more than one zoom level: {sorted(zooms)}")
        return None
    return {"zoom": zooms.pop(), "files": len(files), "nodes": nodes, "edges": edges, "bytes": size, "box": box}


def read_places(path: str, problems: Problems):
    """Counts and the box of a places.pidx, or None."""
    try:
        data = open(path, "rb").read()
    except OSError as e:
        problems.add(f"places: cannot read {path}: {e}")
        return None
    if len(data) < PIDX_HEADER or data[:4] != b"PIX1":
        problems.add("places: not a place index")
        return None
    _, hs, pc, kc, sb, w_, s_, e_, n_ = struct.unpack_from("<HHIIIiiii", data, 4)
    if hs < PIDX_HEADER or len(data) != hs + pc * 16 + kc * 8 + sb or (sb and data[-1] != 0):
        problems.add("places: the size does not match the header")
        return None
    if pc == 0:
        problems.add("places: the index is empty")
        return None
    return {"count": pc, "keys": kc, "bytes": len(data), "box": [w_ / 1e7, s_ / 1e7, e_ / 1e7, n_ / 1e7]}


def inside(inner, outer, slack=SLACK):
    return inner[0] >= outer[0] - slack and inner[1] >= outer[1] - slack and inner[2] <= outer[2] + slack and inner[3] <= outer[3] + slack


# ------------------------------------------------------------------------------------- checking ---
def check(directory: str):
    """Returns (problems, region) for a region directory."""
    problems = Problems()
    manifest = os.path.join(directory, "region.json")
    try:
        region = json.load(open(manifest, encoding="utf-8"))
    except (OSError, ValueError) as e:
        problems.add(f"region.json: {e}")
        return problems, None
    rid = region.get("id", "")
    if not REGION_ID.match(rid):
        problems.add(f"region.json: bad id '{rid}' (lower case letters, digits, - and _)")
    if rid != os.path.basename(os.path.normpath(directory)):
        problems.add(f"region.json: the id '{rid}' is not the name of the directory")
    if not region.get("name"):
        problems.add("region.json: no name")
    if region.get("format", 1) != 1:
        problems.add(f"region.json: format {region.get('format')} is newer than this tool knows")
    box = region.get("bbox")
    if not (isinstance(box, list) and len(box) == 4 and box[0] < box[2] and box[1] < box[3]):
        problems.add("region.json: bad bbox")
        return problems, region
    for part in ("tiles", "routing", "places"):
        if part not in region:
            problems.add(f"region.json: no '{part}'")
    if problems:
        return problems, region

    tiles = read_tiles(os.path.join(directory, region["tiles"]["file"]), problems)
    routing = read_routing(os.path.join(directory, region["routing"]["dir"]), problems)
    places = read_places(os.path.join(directory, region["places"]["file"]), problems)
    if tiles:
        if not inside(box, tiles["bounds"]):
            problems.add(f"the tiles cover {tiles['bounds']}, which does not hold the region {box}")
        if (tiles["minzoom"], tiles["maxzoom"]) != (region["tiles"]["minzoom"], region["tiles"]["maxzoom"]):
            problems.add("region.json: the zoom levels are not those of the tiles")
        if tiles["count"] != region["tiles"].get("count", tiles["count"]):
            problems.add("region.json: the tile count is not that of the tiles")
    if routing:
        if routing["zoom"] != region["routing"]["zoom"]:
            problems.add(f"region.json: routing zoom {region['routing']['zoom']}, the tiles are zoom {routing['zoom']}")
        if not inside(routing["box"], box):
            problems.warnings.append(f"the roads reach {routing['box']}, outside the region {box}")
    if places:
        if not inside(places["box"], box):
            problems.warnings.append(f"the places reach {places['box']}, outside the region {box}")
        if places["count"] != region["places"].get("count", places["count"]):
            problems.add("region.json: the place count is not that of the index")
    for part, key in (("tiles", "file"), ("places", "file")):
        want = region[part].get("sha256")
        path = os.path.join(directory, region[part][key])
        if want and os.path.exists(path) and sha256(path) != want:
            problems.add(f"{region[part][key]}: the SHA-256 is not the one in region.json (a damaged copy?)")
    return problems, region


# -------------------------------------------------------------------------------------- packing ---
def pack(a):
    if not REGION_ID.match(a.id):
        sys.exit("--id is lower case letters, digits, - and _ (at most 48)")
    if os.path.basename(os.path.normpath(a.out)) != a.id:
        sys.exit("the output directory must be named after the id")
    problems = Problems()
    tiles = read_tiles(a.tiles, problems)
    routing = read_routing(a.routing, problems)
    places = read_places(a.places, problems)
    if problems:
        print("The inputs are not good:")
        for p in problems:
            print("  -", p)
        sys.exit(1)
    box = [float(v) for v in a.bbox.split(",")] if a.bbox else tiles["bounds"]
    os.makedirs(a.out, exist_ok=True)
    shutil.copyfile(a.tiles, os.path.join(a.out, "tiles.mbtiles"))
    shutil.copyfile(a.places, os.path.join(a.out, "places.pidx"))
    target = os.path.join(a.out, "routing")
    if os.path.exists(target):
        shutil.rmtree(target)
    shutil.copytree(a.routing, target)
    region = {
        "format": 1,
        "id": a.id,
        "name": a.name,
        "version": a.version,
        "built": datetime.date.today().isoformat(),
        "bbox": box,
        "tiles": {"file": "tiles.mbtiles", "minzoom": tiles["minzoom"], "maxzoom": tiles["maxzoom"], "count": tiles["count"],
                  "bytes": os.path.getsize(os.path.join(a.out, "tiles.mbtiles")), "sha256": sha256(os.path.join(a.out, "tiles.mbtiles"))},
        "routing": {"dir": "routing", "zoom": routing["zoom"], "files": routing["files"], "nodes": routing["nodes"], "edges": routing["edges"],
                    "bytes": tree_bytes(target)},
        "places": {"file": "places.pidx", "count": places["count"], "bytes": places["bytes"], "sha256": sha256(os.path.join(a.out, "places.pidx"))},
        "attribution": ATTRIBUTION,
    }
    if a.source:
        region["source"] = a.source
    with open(os.path.join(a.out, "region.json"), "w", encoding="utf-8") as f:
        json.dump(region, f, ensure_ascii=False, indent=2)
        f.write("\n")
    problems, _ = check(a.out)
    for w in problems.warnings:
        print("  warning:", w)
    if problems:
        print("The packed region does not check out:")
        for p in problems:
            print("  -", p)
        sys.exit(1)
    total = tree_bytes(a.out)
    print(f"{a.out}: {tiles['count']} tiles, {routing['files']} routing tiles ({routing['nodes']} nodes), {places['count']} places, {total / 1048576:.1f} MB")
    if a.archive:
        zpath = os.path.normpath(a.out) + ".zip"
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_STORED) as z:
            for d, _, names in sorted(os.walk(a.out)):
                for n in sorted(names):
                    full = os.path.join(d, n)
                    z.write(full, os.path.join(a.id, os.path.relpath(full, a.out)))
        print(f"{zpath}: {os.path.getsize(zpath) / 1048576:.1f} MB")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack")
    p.add_argument("--id", required=True)
    p.add_argument("--name", required=True, help="the name the rider sees, e.g. 北京市区")
    p.add_argument("--tiles", required=True)
    p.add_argument("--routing", required=True)
    p.add_argument("--places", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--bbox", help="west,south,east,north; default: the box of the tiles")
    p.add_argument("--version", type=int, default=1)
    p.add_argument("--source", help="where the data is from, for region.json (a URL and a date, say)")
    p.add_argument("--archive", action="store_true")
    c = sub.add_parser("check")
    c.add_argument("directory")
    a = ap.parse_args()
    if a.cmd == "pack":
        pack(a)
        return
    problems, region = check(a.directory)
    for w in problems.warnings:
        print("warning:", w)
    if problems:
        for t in problems:
            print("-", t)
        sys.exit(1)
    print(f"{a.directory}: ok ({region['name']}, version {region['version']}, built {region['built']})")


if __name__ == "__main__":
    main()
