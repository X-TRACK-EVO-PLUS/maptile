#!/usr/bin/env python3
"""Download the vector tiles of a bounding box into an MBTiles file.

    download_tiles.py <out.mbtiles> --bbox W,S,E,N [--min-zoom 13] [--max-zoom 14]
                      [--url TEMPLATE] [--rate 10] [--workers 4] [--name NAME] [--yes]

The default server is VersaTiles (https://tiles.versatiles.org), which serves OpenStreetMap as
Shortbread tiles, the schema the map style expects. It is a free public service, so the download is
polite: a request-rate limit, an identifying User-Agent, back-off on 429/5xx (honouring Retry-After),
and a question before a download of more than 3000 tiles. For a bigger area use the versatiles tool
against the planet file, which is made for that:

    versatiles convert --bbox W,S,E,N --min-zoom 13 --max-zoom 14 https://download.versatiles.org/osm.versatiles out.mbtiles

The download can be stopped and started again: tiles already in the file (and tiles the server said
are empty) are not asked for twice. The exit code is 1 if some tiles failed, so a script knows to run
it again. Tiles are stored gzipped, as the renderer reads them.

(c) OpenStreetMap contributors, ODbL. The tiles keep that attribution in the file's metadata.
"""
from __future__ import annotations

import argparse
import gzip
import math
import os
import random
import sqlite3
import sys
import threading
import time
import urllib.error
import urllib.request
from concurrent.futures import ThreadPoolExecutor

DEFAULT_URL = "https://tiles.versatiles.org/tiles/osm/{z}/{x}/{y}"
USER_AGENT = "X-TRACK-EVO-PLUS-maptile/1.0 (+https://github.com/X-TRACK-EVO-PLUS/maptile)"
ATTRIBUTION = "(c) OpenStreetMap contributors (ODbL); tiles by VersaTiles (Shortbread schema)"
ASK_ABOVE = 3000
TIMEOUT = 20


def lon_lat_to_tile(lon: float, lat: float, zoom: int):
    n = 2**zoom
    x = int((lon + 180.0) / 360.0 * n)
    lat = max(-85.05112878, min(85.05112878, lat))
    y = int((1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n)
    return min(max(x, 0), n - 1), min(max(y, 0), n - 1)


def tile_range(bbox, zoom: int):
    """(x0, y0, x1, y1) of the tiles that cover the box at a zoom."""
    w, s, e, n = bbox
    x0, y0 = lon_lat_to_tile(w, n, zoom)
    x1, y1 = lon_lat_to_tile(e, s, zoom)
    return x0, y0, x1, y1


def tiles_of(bbox, min_zoom: int, max_zoom: int):
    for z in range(min_zoom, max_zoom + 1):
        x0, y0, x1, y1 = tile_range(bbox, z)
        for x in range(x0, x1 + 1):
            for y in range(y0, y1 + 1):
                yield z, x, y


def count_tiles(bbox, min_zoom: int, max_zoom: int) -> int:
    total = 0
    for z in range(min_zoom, max_zoom + 1):
        x0, y0, x1, y1 = tile_range(bbox, z)
        total += (x1 - x0 + 1) * (y1 - y0 + 1)
    return total


def parse_bbox(text: str):
    try:
        w, s, e, n = [float(v) for v in text.split(",")]
    except ValueError:
        raise SystemExit("--bbox is west,south,east,north in degrees")
    if not (-180 <= w < e <= 180 and -90 <= s < n <= 90):
        raise SystemExit("--bbox is west,south,east,north in degrees, west < east and south < north")
    return w, s, e, n


# ---------------------------------------------------------------------------------- the file ---
def open_mbtiles(path: str, meta: dict):
    new = not os.path.exists(path)
    db = sqlite3.connect(path, check_same_thread=False)
    # the usual rollback journal, not WAL: a WAL file needs -wal and -shm files beside it, which the device cannot
    # make on a read-only card
    db.execute("PRAGMA journal_mode=DELETE")
    db.execute("PRAGMA synchronous=NORMAL")
    db.execute("CREATE TABLE IF NOT EXISTS metadata (name TEXT, value TEXT)")
    db.execute("CREATE TABLE IF NOT EXISTS tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)")
    db.execute("CREATE UNIQUE INDEX IF NOT EXISTS tile_index ON tiles (zoom_level, tile_column, tile_row)")
    # tiles the server has none of (open sea): remembered so that a second run does not ask again
    db.execute("CREATE TABLE IF NOT EXISTS x_empty (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, PRIMARY KEY (zoom_level, tile_column, tile_row))")
    if new:
        db.executemany("INSERT INTO metadata VALUES (?, ?)", sorted(meta.items()))
    else:
        # a bigger area or more zooms than before: the metadata follows
        old = dict(db.execute("SELECT name, value FROM metadata"))
        for k in ("minzoom", "maxzoom"):
            if k in meta and k in old:
                meta[k] = str(min(int(meta[k]), int(old[k])) if k == "minzoom" else max(int(meta[k]), int(old[k])))
        db.execute("DELETE FROM metadata WHERE name IN ('minzoom','maxzoom','bounds','center')")
        db.executemany("INSERT INTO metadata VALUES (?, ?)", [(k, meta[k]) for k in ("minzoom", "maxzoom", "bounds", "center") if k in meta])
    db.commit()
    return db


def have(db):
    done = set(db.execute("SELECT zoom_level, tile_column, tile_row FROM tiles"))
    done |= set(db.execute("SELECT zoom_level, tile_column, tile_row FROM x_empty"))
    return done


# --------------------------------------------------------------------------------- the server ---
class Limiter:
    """At most `rate` requests a second over all threads."""

    def __init__(self, rate: float):
        self._gap = 1.0 / rate if rate > 0 else 0.0
        self._next = 0.0
        self._lock = threading.Lock()

    def wait(self):
        if not self._gap:
            return
        with self._lock:
            now = time.monotonic()
            at = max(now, self._next)
            self._next = at + self._gap
        if at > now:
            time.sleep(at - now)


def fetch(url: str, limiter: Limiter, retries: int, opener=urllib.request.urlopen):
    """(status, data): "ok" with the tile, "empty" when the server has none, "fail" after the retries."""
    delay = 1.0
    for attempt in range(retries + 1):
        limiter.wait()
        req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept-Encoding": "gzip"})
        retry_after = None
        try:
            with opener(req, timeout=TIMEOUT) as resp:
                data = resp.read()
                if resp.status == 204 or not data:
                    return "empty", b""
                if resp.headers.get("Content-Encoding") == "gzip" and data[:2] != b"\x1f\x8b":
                    data = gzip.compress(data)
                return "ok", data
        except urllib.error.HTTPError as e:
            if e.code in (404, 410):
                return "empty", b""
            if e.code in (429, 500, 502, 503, 504):
                try:
                    retry_after = float(e.headers.get("Retry-After", ""))
                except ValueError:
                    retry_after = None
            else:
                return "fail", b""
        except (urllib.error.URLError, TimeoutError, ConnectionError, OSError):
            pass
        if attempt < retries:
            time.sleep(min(60.0, retry_after if retry_after is not None else delay) + random.uniform(0, 0.3))
            delay *= 2
    return "fail", b""


def valid_tile(data: bytes) -> bool:
    """A tile is gzip or a protobuf message (its first byte is the tag of the layers field, 0x1a)."""
    if data[:2] == b"\x1f\x8b":
        try:
            return gzip.decompress(data)[:1] == b"\x1a"
        except (OSError, EOFError):
            return False
    return data[:1] == b"\x1a"


# ------------------------------------------------------------------------------------ the run ---
def download(out: str, bbox, min_zoom: int, max_zoom: int, url: str = DEFAULT_URL, rate: float = 10.0, workers: int = 4,
             retries: int = 4, name: str = "", quiet: bool = False, opener=urllib.request.urlopen):
    """Returns (stored, empty, failed, skipped)."""
    w, s, e, n = bbox
    meta = {
        "name": name or os.path.splitext(os.path.basename(out))[0],
        "format": "pbf",
        "type": "baselayer",
        "version": "1",
        "description": "Shortbread vector tiles of an OpenStreetMap region",
        "attribution": ATTRIBUTION,
        "minzoom": str(min_zoom),
        "maxzoom": str(max_zoom),
        "bounds": f"{w},{s},{e},{n}",
        "center": f"{(w + e) / 2:.5f},{(s + n) / 2:.5f},{min_zoom}",
    }
    db = open_mbtiles(out, meta)
    done = have(db)
    todo = [(z, x, y) for z, x, y in tiles_of(bbox, min_zoom, max_zoom) if (z, x, (1 << z) - 1 - y) not in done]
    skipped = count_tiles(bbox, min_zoom, max_zoom) - len(todo)
    lock = threading.Lock()
    counts = {"ok": 0, "empty": 0, "fail": 0}
    t0 = time.monotonic()
    limiter = Limiter(rate)

    def progress():
        if quiet:
            return
        total = len(todo)
        did = sum(counts.values())
        el = max(time.monotonic() - t0, 1e-3)
        eta = (total - did) * el / did if did else 0
        sys.stdout.write(f"\r  {did}/{total} tiles  stored {counts['ok']}  empty {counts['empty']}  failed {counts['fail']}  ETA {eta:4.0f}s ")
        sys.stdout.flush()

    def work(tile):
        z, x, y = tile
        status, data = fetch(url.format(z=z, x=x, y=y), limiter, retries, opener)
        if status == "ok" and not valid_tile(data):
            status = "fail"
        row = (z, x, (1 << z) - 1 - y)
        with lock:
            if status == "ok":
                db.execute("INSERT OR REPLACE INTO tiles VALUES (?, ?, ?, ?)", (*row, data if data[:2] == b"\x1f\x8b" else gzip.compress(data)))
            elif status == "empty":
                db.execute("INSERT OR REPLACE INTO x_empty VALUES (?, ?, ?)", row)
            counts[status] += 1
            if (counts["ok"] + counts["empty"]) % 50 == 0:
                db.commit()
            progress()

    try:
        with ThreadPoolExecutor(max_workers=max(1, workers)) as pool:
            list(pool.map(work, todo))
    finally:
        db.commit()
        db.close()
    if not quiet and todo:
        sys.stdout.write("\n")
    return counts["ok"], counts["empty"], counts["fail"], skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="the .mbtiles file to write (continued if it exists)")
    ap.add_argument("--bbox", required=True, help="west,south,east,north")
    ap.add_argument("--min-zoom", type=int, default=13)
    ap.add_argument("--max-zoom", type=int, default=14)
    ap.add_argument("--url", default=DEFAULT_URL, help="tile URL template with {z} {x} {y} (XYZ, not TMS); default VersaTiles")
    ap.add_argument("--rate", type=float, default=10.0, help="requests per second over all threads (default 10)")
    ap.add_argument("--workers", type=int, default=4)
    ap.add_argument("--retries", type=int, default=4)
    ap.add_argument("--name", default="", help="the name in the file's metadata")
    ap.add_argument("--yes", action="store_true", help="do not ask before a download of more than %d tiles" % ASK_ABOVE)
    a = ap.parse_args()
    bbox = parse_bbox(a.bbox)
    if not (0 <= a.min_zoom <= a.max_zoom <= 15):
        sys.exit("zoom levels are 0 to 15, min <= max")
    total = count_tiles(bbox, a.min_zoom, a.max_zoom)
    print(f"{total} tiles, zoom {a.min_zoom}-{a.max_zoom}, from {a.url.split('{')[0]} at {a.rate:g}/s, about {total / max(a.rate, 0.1) / 60:.0f} min")
    if total > ASK_ABOVE and not a.yes and a.url == DEFAULT_URL:
        print(f"That is a lot for a free public server. Use the versatiles tool (see --help), or --yes to go on.")
        sys.exit(2)
    ok, empty, failed, skipped = download(a.out, bbox, a.min_zoom, a.max_zoom, a.url, a.rate, a.workers, a.retries, a.name)
    size = os.path.getsize(a.out) / 1048576
    print(f"stored {ok}, empty {empty}, failed {failed}, already there {skipped}; {a.out} is {size:.1f} MB")
    if failed:
        print("Some tiles failed: run the same command again to fetch the rest.")
        sys.exit(1)


if __name__ == "__main__":
    main()
