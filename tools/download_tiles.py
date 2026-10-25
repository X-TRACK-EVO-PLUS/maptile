#!/usr/bin/env python3
"""
Download vector tiles for a bounding box and save as MBTiles.
Auto-selects the fastest tile server. Multi-threaded downloading.
"""

import sqlite3
import urllib.request
import sys
import math
import gzip
import os
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed

TILE_SERVERS = [
    {
        "name": "VersaTiles",
        "url": "https://tiles.versatiles.org/tiles/osm/{z}/{x}/{y}",
    },
    {
        "name": "OpenFreeMap",
        "url": "https://tiles.openfreemap.org/planet/{z}/{x}/{y}.pbf",
    },
    {
        "name": "MapTiler (demo key)",
        "url": "https://api.maptiler.com/tiles/v3/{z}/{x}/{y}.pbf?key=get_your_own_OpIi9ZULNHzrESv6T2vL",
    },
]

# Test tile: Beijing, zoom 14
TEST_TILE = (14, 13489, 6208)
TIMEOUT = 15
MAX_RETRIES = 2
TILE_URL = ""
NUM_WORKERS = 8


def lon_lat_to_tile(lon, lat, zoom):
    n = 2 ** zoom
    x = int((lon + 180.0) / 360.0 * n)
    lat_rad = math.radians(lat)
    y = int((1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) / 2.0 * n)
    return x, y


def probe_server(server):
    """Download a test tile and return (speed_bytes_per_sec, data_size) or None on failure."""
    z, x, y = TEST_TILE
    url = server["url"].format(z=z, x=x, y=y)
    req = urllib.request.Request(url, headers={"User-Agent": "X-MAP-Demo/1.0"})
    try:
        t0 = time.time()
        with urllib.request.urlopen(req, timeout=8) as resp:
            data = resp.read()
        elapsed = time.time() - t0
        if len(data) == 0:
            return None
        speed = len(data) / elapsed if elapsed > 0 else 0
        return speed, len(data)
    except Exception:
        return None


def select_best_server():
    """Probe all servers and return the fastest one."""
    global TILE_URL
    print("Probing tile servers...")
    results = []
    for server in TILE_SERVERS:
        name = server["name"]
        sys.stdout.write(f"  {name:25s} ... ")
        sys.stdout.flush()
        result = probe_server(server)
        if result:
            speed, size = result
            print(f"{format_size(size):>8s}  {format_size(int(speed))}/s  ✓")
            results.append((speed, server))
        else:
            print(f"{'':>8s}  {'':>10s}  ✗ failed")

    if not results:
        print("\nAll servers failed!")
        sys.exit(1)

    results.sort(key=lambda r: r[0], reverse=True)
    best = results[0][1]
    print(f"\n→ Using: {best['name']} ({NUM_WORKERS} threads)\n")
    TILE_URL = best["url"]
    return best


def create_mbtiles(output_path):
    db = sqlite3.connect(output_path, check_same_thread=False)
    db.execute("CREATE TABLE IF NOT EXISTS metadata (name TEXT PRIMARY KEY, value TEXT)")
    db.execute("""CREATE TABLE IF NOT EXISTS tiles (
        zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB,
        UNIQUE(zoom_level, tile_column, tile_row))""")
    # Clean up any duplicate metadata rows from old runs
    db.execute("DELETE FROM metadata")
    db.execute("INSERT OR REPLACE INTO metadata VALUES ('name', 'demo')")
    db.execute("INSERT OR REPLACE INTO metadata VALUES ('format', 'pbf')")
    db.execute("INSERT OR REPLACE INTO metadata VALUES ('type', 'overlay')")
    db.commit()
    return db


def download_tile(z, x, y):
    url = TILE_URL.format(z=z, x=x, y=y)
    req = urllib.request.Request(url, headers={"User-Agent": "X-MAP-Demo/1.0"})
    for attempt in range(MAX_RETRIES):
        try:
            with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
                data = resp.read()
                if len(data) > 0:
                    return data
                return None
        except Exception:
            if attempt < MAX_RETRIES - 1:
                time.sleep(0.5)
    return None


def format_time(seconds):
    if seconds < 60:
        return f"{seconds:.0f}s"
    elif seconds < 3600:
        return f"{seconds / 60:.0f}m{seconds % 60:.0f}s"
    else:
        return f"{seconds / 3600:.0f}h{(seconds % 3600) / 60:.0f}m"


def format_size(bytes_val):
    if bytes_val < 1024:
        return f"{bytes_val}B"
    elif bytes_val < 1024 * 1024:
        return f"{bytes_val / 1024:.1f}KB"
    else:
        return f"{bytes_val / 1024 / 1024:.1f}MB"


def count_tiles(min_lon, min_lat, max_lon, max_lat, min_zoom, max_zoom):
    total = 0
    for z in range(min_zoom, max_zoom + 1):
        x_min, y_min = lon_lat_to_tile(min_lon, max_lat, z)
        x_max, y_max = lon_lat_to_tile(max_lon, min_lat, z)
        total += (x_max - x_min + 1) * (y_max - y_min + 1)
    return total


class ProgressTracker:
    """Thread-safe progress tracker with stable single-line output."""

    def __init__(self, total):
        self.total = total
        self.done = 0
        self.failed = 0
        self.bytes_downloaded = 0
        self.start_time = time.time()
        self.current_zoom = 0
        self.current_zoom_total = 0
        self.current_zoom_done = 0
        self.lock = threading.Lock()
        self._last_line_len = 0

    def update(self, nbytes, ok):
        with self.lock:
            self.done += 1
            self.current_zoom_done += 1
            if ok:
                self.bytes_downloaded += nbytes
            else:
                self.failed += 1
            self._print()

    def set_zoom(self, z, z_total):
        with self.lock:
            self.current_zoom = z
            self.current_zoom_total = z_total
            self.current_zoom_done = 0
            self._print()

    def _print(self):
        elapsed = time.time() - self.start_time
        if elapsed < 0.05:
            return

        pct = self.done / self.total * 100 if self.total > 0 else 0
        speed = self.bytes_downloaded / elapsed if elapsed > 0 else 0
        eta = (self.total - self.done) / (self.done / elapsed) if self.done > 0 else 0

        bar_len = 30
        filled = int(bar_len * self.done / self.total) if self.total > 0 else 0
        bar = "█" * filled + "░" * (bar_len - filled)

        line = (f"  Z{self.current_zoom:2d} "
                f"[{bar}] {pct:5.1f}%  "
                f"{self.done}/{self.total}  "
                f"{format_size(int(speed))}/s  "
                f"ETA {format_time(eta):>6s}  "
                f"fail:{self.failed}")

        # Pad to overwrite previous line fully
        padded = line.ljust(self._last_line_len)
        self._last_line_len = len(line)
        sys.stdout.write(f"\r{padded}")
        sys.stdout.flush()

    def finish(self):
        sys.stdout.write("\n")
        sys.stdout.flush()


def main():
    if len(sys.argv) < 2:
        print("Usage: download_tiles.py <output> [bbox] [min_zoom] [max_zoom] [threads]")
        print("  output: path ending in .mbtiles (database) or directory path (MVT files)")
        print("  bbox format: min_lon,min_lat,max_lon,max_lat")
        print("  Default: Beijing Tiananmen area, zoom 10-15, 8 threads")
        print()
        print("Examples:")
        print("  download_tiles.py tiles.mbtiles                    # MBTiles database")
        print("  download_tiles.py ./maps                           # MVT files: ./maps/z/x/y.mvt")
        sys.exit(1)

    output = sys.argv[1]
    bbox = sys.argv[2] if len(sys.argv) > 2 else "116.3,39.85,116.5,39.95"
    min_zoom = int(sys.argv[3]) if len(sys.argv) > 3 else 10
    max_zoom = int(sys.argv[4]) if len(sys.argv) > 4 else 15

    global NUM_WORKERS
    if len(sys.argv) > 5:
        NUM_WORKERS = int(sys.argv[5])

    mvt_mode = not output.endswith(".mbtiles")

    min_lon, min_lat, max_lon, max_lat = map(float, bbox.split(","))
    total = count_tiles(min_lon, min_lat, max_lon, max_lat, min_zoom, max_zoom)

    print(f"Bbox: {min_lon},{min_lat} → {max_lon},{max_lat}")
    print(f"Zoom: {min_zoom}-{max_zoom}")
    print(f"Total tiles: {total}")
    print(f"Output: {output} ({'MVT files' if mvt_mode else 'MBTiles'})")
    print()

    select_best_server()

    db = None
    db_lock = threading.Lock()

    if not mvt_mode:
        db = create_mbtiles(output)
        db.execute(f"INSERT OR REPLACE INTO metadata VALUES ('minzoom', '{min_zoom}')")
        db.execute(f"INSERT OR REPLACE INTO metadata VALUES ('maxzoom', '{max_zoom}')")
        db.execute(f"INSERT OR REPLACE INTO metadata VALUES ('bounds', '{bbox}')")
        db.commit()

    progress = ProgressTracker(total)

    def process_tile(z, x, y):
        data = download_tile(z, x, y)
        if data:
            if mvt_mode:
                # Decompress if gzipped, save raw protobuf
                raw = data
                if data[:2] == b'\x1f\x8b':
                    raw = gzip.decompress(data)
                tile_dir = os.path.join(output, str(z), str(x))
                os.makedirs(tile_dir, exist_ok=True)
                tile_path = os.path.join(tile_dir, f"{y}.mvt")
                with open(tile_path, 'wb') as f:
                    f.write(raw)
                progress.update(len(raw), True)
            else:
                tms_y = (1 << z) - 1 - y
                if data[:2] != b'\x1f\x8b':
                    data = gzip.compress(data)
                with db_lock:
                    db.execute("INSERT OR REPLACE INTO tiles VALUES (?, ?, ?, ?)",
                               (z, x, tms_y, data))
                progress.update(len(data), True)
        else:
            progress.update(0, False)

    with ThreadPoolExecutor(max_workers=NUM_WORKERS) as pool:
        for z in range(min_zoom, max_zoom + 1):
            x_min, y_min = lon_lat_to_tile(min_lon, max_lat, z)
            x_max, y_max = lon_lat_to_tile(max_lon, min_lat, z)
            z_count = (x_max - x_min + 1) * (y_max - y_min + 1)

            progress.set_zoom(z, z_count)

            futures = []
            for x in range(x_min, x_max + 1):
                for y in range(y_min, y_max + 1):
                    futures.append(pool.submit(process_tile, z, x, y))

            for f in as_completed(futures):
                f.result()

            if db:
                with db_lock:
                    db.commit()

    progress.finish()
    if db:
        db.close()

    elapsed = time.time() - progress.start_time
    if mvt_mode:
        # Count total size of MVT directory
        total_size = sum(
            os.path.getsize(os.path.join(dp, f))
            for dp, _, fns in os.walk(output) for f in fns
        ) / 1024 / 1024
    else:
        total_size = os.path.getsize(output) / 1024 / 1024
    print(f"Done! {progress.done - progress.failed}/{total} tiles, "
          f"{total_size:.1f}MB, {format_time(elapsed)}, {progress.failed} failed")


if __name__ == "__main__":
    main()
