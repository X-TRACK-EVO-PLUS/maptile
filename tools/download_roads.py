#!/usr/bin/env python3
"""
Download Beijing road network from Overpass API in parallel small chunks.
Splits the bbox into a grid and downloads each cell concurrently.
"""

import os
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor, as_completed
from urllib.request import Request, urlopen
from urllib.parse import urlencode
import time

OVERPASS_URL = "https://overpass-api.de/api/interpreter"

# Beijing area matching our map tiles
BBOX = (39.85, 116.30, 39.97, 116.50)  # south, west, north, east
GRID_ROWS = 3
GRID_COLS = 5
MAX_WORKERS = 4  # Overpass rate limit is 4 concurrent slots

HIGHWAY_FILTER = (
    "primary|secondary|tertiary|residential|cycleway|"
    "path|track|unclassified|living_street|trunk|"
    "primary_link|secondary_link|tertiary_link|trunk_link"
)


def download_chunk(row, col, south, west, north, east):
    """Download one grid cell from Overpass API."""
    query = (
        f'[out:xml][timeout:60];'
        f'(way["highway"~"^({HIGHWAY_FILTER})$"]({south},{west},{north},{east});'
        f'node(w););out body;'
    )
    data = urlencode({"data": query}).encode("utf-8")
    req = Request(OVERPASS_URL, data=data)
    req.add_header("User-Agent", "X-MAP-routing-extract/1.0")

    for attempt in range(3):
        try:
            with urlopen(req, timeout=90) as resp:
                content = resp.read()
            return (row, col, content)
        except Exception as e:
            print(f"  Chunk ({row},{col}) attempt {attempt+1} failed: {e}")
            time.sleep(5 * (attempt + 1))

    return (row, col, None)


def merge_osm_chunks(chunks):
    """Merge multiple OSM XML chunks, deduplicating nodes and ways by id."""
    nodes = {}  # id -> Element
    ways = {}   # id -> Element

    for content in chunks:
        root = ET.fromstring(content)
        for node in root.findall("node"):
            nid = node.get("id")
            if nid not in nodes:
                nodes[nid] = node
        for way in root.findall("way"):
            wid = way.get("id")
            if wid not in ways:
                ways[wid] = way

    # Build merged XML
    osm = ET.Element("osm", version="0.6")
    for node in nodes.values():
        osm.append(node)
    for way in ways.values():
        osm.append(way)

    return ET.tostring(osm, encoding="unicode", xml_declaration=True)


def main():
    output = sys.argv[1] if len(sys.argv) > 1 else "tools/beijing_roads.osm"
    os.makedirs(os.path.dirname(output) or ".", exist_ok=True)

    south, west, north, east = BBOX
    lat_step = (north - south) / GRID_ROWS
    lon_step = (east - west) / GRID_COLS

    cells = []
    for r in range(GRID_ROWS):
        for c in range(GRID_COLS):
            s = south + r * lat_step
            w = west + c * lon_step
            n = s + lat_step
            e = w + lon_step
            cells.append((r, c, round(s, 5), round(w, 5), round(n, 5), round(e, 5)))

    total = len(cells)
    print(f"Downloading {total} chunks ({GRID_ROWS}x{GRID_COLS}) with {MAX_WORKERS} workers...")

    results = {}
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as pool:
        futures = {pool.submit(download_chunk, *cell): cell for cell in cells}
        done = 0
        for future in as_completed(futures):
            row, col, content = future.result()
            done += 1
            if content:
                results[(row, col)] = content
                print(f"  [{done}/{total}] Chunk ({row},{col}): {len(content)} bytes")
            else:
                print(f"  [{done}/{total}] Chunk ({row},{col}): FAILED")

    if not results:
        print("ERROR: No chunks downloaded successfully.")
        sys.exit(1)

    failed = total - len(results)
    if failed:
        print(f"WARNING: {failed}/{total} chunks failed")

    print("Merging chunks...")
    merged = merge_osm_chunks(results.values())

    with open(output, "w", encoding="utf-8") as f:
        f.write(merged)

    size_kb = os.path.getsize(output) / 1024
    print(f"Done: {output} ({size_kb:.0f} KB)")


if __name__ == "__main__":
    main()
