#!/usr/bin/env python3
"""The overview of a whole country: the coarse zoom levels of a national .mbtiles, as one small .mbtiles file.

  pack_overview.py --out Maps/overview.mbtiles [--max-zoom 10] NATIONAL.mbtiles
                   [--patch EXTRA.mbtiles --patch-box W,S,E,N [--patch-from-zoom 6]]

The map draws it (Maps/overview.mbtiles on the SD card) where a region's tiles have none: below the region's zoom
range, or outside the area it covers. It has no routing and no search index and is no region.

--patch is for an area the national file lacks (Taiwan is not in the mainland extract): a tile of EXTRA at
--patch-from-zoom or higher whose centre is inside --patch-box replaces the national tile, and EXTRA's tiles that
the national file does not have are added.
"""
import argparse
import gzip
import math
import os
import sqlite3
import sys


def tile_centre(z, x, row):
    n = 1 << z
    y = n - 1 - row  # mbtiles rows count from the south
    lon = (x + 0.5) / n * 360 - 180
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * (y + 0.5) / n))))
    return lon, lat


def gz(data):
    return data if data[:2] == b"\x1f\x8b" else gzip.compress(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("national")
    ap.add_argument("--out", required=True)
    ap.add_argument("--max-zoom", type=int, default=10)
    ap.add_argument("--patch")
    ap.add_argument("--patch-box", help="W,S,E,N")
    ap.add_argument("--patch-from-zoom", type=int, default=6)
    a = ap.parse_args()
    if bool(a.patch) != bool(a.patch_box):
        sys.exit("--patch and --patch-box go together")
    if os.path.exists(a.out):
        os.remove(a.out)
    src = sqlite3.connect(f"file:{a.national}?mode=ro", uri=True)
    db = sqlite3.connect(a.out)
    db.execute("PRAGMA journal_mode=DELETE")
    db.execute("PRAGMA synchronous=OFF")
    db.execute("CREATE TABLE metadata (name TEXT, value TEXT)")
    db.execute("CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)")
    wanted = {}
    if a.patch:
        w, s, e, n = (float(v) for v in a.patch_box.split(","))
        extra = sqlite3.connect(f"file:{a.patch}?mode=ro", uri=True)
        for z, x, row, data in extra.execute("SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles WHERE zoom_level BETWEEN ? AND ?",
                                             (a.patch_from_zoom, a.max_zoom)):
            lon, lat = tile_centre(z, x, row)
            if w <= lon <= e and s <= lat <= n:
                wanted[(z, x, row)] = data
    replaced = set()
    count = 0
    batch = []
    for z, x, row, data in src.execute("SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles WHERE zoom_level <= ?", (a.max_zoom,)):
        key = (z, x, row)
        if key in wanted:
            data = wanted[key]
            replaced.add(key)
        batch.append((z, x, row, gz(data)))
        if len(batch) >= 2000:
            db.executemany("INSERT INTO tiles VALUES (?, ?, ?, ?)", batch)
            count += len(batch)
            batch = []
    for key, data in wanted.items():
        if key not in replaced:
            batch.append((*key, gz(data)))
    db.executemany("INSERT INTO tiles VALUES (?, ?, ?, ?)", batch)
    count += len(batch)
    db.execute("CREATE UNIQUE INDEX tile_index ON tiles (zoom_level, tile_column, tile_row)")
    bounds = dict(src.execute("SELECT name, value FROM metadata")).get("bounds", "-180,-85,180,85")
    meta = {
        "name": "overview",
        "format": "pbf",
        "type": "baselayer",
        "version": "1",
        "description": "Shortbread vector tiles of OpenStreetMap, the coarse levels of a whole country",
        "attribution": "(c) OpenStreetMap contributors (ODbL)",
        "minzoom": "0",
        "maxzoom": str(a.max_zoom),
        "bounds": bounds,
    }
    db.executemany("INSERT INTO metadata VALUES (?, ?)", sorted(meta.items()))
    db.commit()
    db.execute("VACUUM")
    db.close()
    print(f"{a.out}: {count} tiles, {len(replaced)} replaced by the patch, {len(wanted) - len(replaced)} added, "
          f"{os.path.getsize(a.out) / 1048576:.0f} MB")


if __name__ == "__main__":
    main()
