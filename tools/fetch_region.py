#!/usr/bin/env python3
"""Make a map region for the device from nothing but a name or a box: download, extract, pack.

    fetch_region.py --id beijing --name 北京市区 --bbbike Beijing --out Maps
    fetch_region.py --id mycity  --name 我的城市 --pbf-url https://.../city.osm.pbf --bbox W,S,E,N --out Maps
    fetch_region.py --id mycity  --name 我的城市 --pbf city.osm.pbf --out Maps

The steps, each of which can be skipped when its result is there already (--force does them again):

  osm      the OpenStreetMap extract: --bbbike NAME (a city of https://download.bbbike.org/osm/bbbike/),
           --pbf-url URL (Geofabrik and others), or --pbf FILE. A download is resumed if it stopped, and
           checked against the server's .md5 when there is one.
  tiles    the vector tiles of the box, zoom 13-14 (download_tiles.py)
  routing  the cycling network as routing tiles (extract_routing.py)
  places   the search index, from the OpenStreetMap extract (build_places.py)
  pack     <out>/<id>/ with region.json, checked (pack_region.py)

Needs pyosmium and pypinyin: pip install osmium pypinyin. The box defaults to that of the extract.
Everything the steps make before the pack is kept in --work (default ./work/<id>), so a second run
only does what is missing. The data is (c) OpenStreetMap contributors under the ODbL.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import download_tiles  # noqa: E402

BBBIKE = "https://download.bbbike.org/osm/bbbike/{n}/{n}.osm.pbf"
USER_AGENT = download_tiles.USER_AGENT
STEPS = ("osm", "tiles", "routing", "places", "pack")


def say(text: str):
    print(f"\n== {text}", flush=True)


def remote_size(url: str):
    req = urllib.request.Request(url, method="HEAD", headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as r:
        return int(r.headers.get("Content-Length", "0")) or None


def md5_of(path: str) -> str:
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download_file(url: str, path: str, opener=urllib.request.urlopen):
    """Downloads url to path, carrying on from a partial file, and checks the size and the .md5 if the server has one."""
    part = path + ".part"
    size = remote_size(url) if opener is urllib.request.urlopen else None
    have = os.path.getsize(part) if os.path.exists(part) else 0
    if size and have > size:
        os.remove(part)
        have = 0
    if not size or have < size:
        headers = {"User-Agent": USER_AGENT}
        if have:
            headers["Range"] = f"bytes={have}-"
        req = urllib.request.Request(url, headers=headers)
        with opener(req, timeout=60) as r:
            resumed = have > 0 and getattr(r, "status", 200) == 206
            with open(part, "ab" if resumed else "wb") as f:
                got = have if resumed else 0
                while True:
                    chunk = r.read(1 << 20)
                    if not chunk:
                        break
                    f.write(chunk)
                    got += len(chunk)
                    if size:
                        sys.stdout.write(f"\r  {got / 1048576:.1f} / {size / 1048576:.1f} MB ")
                        sys.stdout.flush()
        print()
    if size and os.path.getsize(part) != size:
        sys.exit(f"{url}: got {os.path.getsize(part)} bytes of {size}; run again to carry on")
    try:
        req = urllib.request.Request(url + ".md5", headers={"User-Agent": USER_AGENT})
        with opener(req, timeout=30) as r:
            want = r.read().decode().split()[0].lower()
        if len(want) == 32 and md5_of(part) != want:
            os.remove(part)
            sys.exit(f"{url}: the MD5 does not match; the download was removed, run again")
        print("  MD5 ok" if len(want) == 32 else "")
    except (urllib.error.URLError, OSError, IndexError):
        print("  (no .md5 to check against)")
    os.replace(part, path)


def run(cmd):
    print("  $", " ".join(cmd), flush=True)
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit(f"step failed ({r.returncode})")


def extract_box(pbf: str):
    try:
        import osmium
    except ImportError:
        sys.exit("reading the box of a .pbf needs pyosmium (pip install osmium); or give --bbox")
    box = osmium.io.Reader(pbf).header().box()
    if not box.valid():
        sys.exit("the extract has no box in its header; give --bbox")
    return f"{box.bottom_left.lon},{box.bottom_left.lat},{box.top_right.lon},{box.top_right.lat}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--id", required=True, help="directory name and id: lower case letters, digits, - and _")
    ap.add_argument("--name", required=True, help="the name the rider sees")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--bbbike", metavar="NAME", help="a city of download.bbbike.org, e.g. Beijing")
    src.add_argument("--pbf-url", metavar="URL")
    src.add_argument("--pbf", metavar="FILE")
    ap.add_argument("--bbox", help="west,south,east,north; default: the box of the extract")
    ap.add_argument("--out", required=True, help="the directory the region goes into: <out>/<id>/")
    ap.add_argument("--work", help="where the steps keep their results (default work/<id>)")
    ap.add_argument("--min-zoom", type=int, default=13)
    ap.add_argument("--max-zoom", type=int, default=14)
    ap.add_argument("--route-zoom", type=int, default=10)
    ap.add_argument("--rate", type=float, default=10.0, help="tile requests a second")
    ap.add_argument("--yes", action="store_true", help="go on with a big tile download")
    ap.add_argument("--skip", default="", help="steps not to do: " + ",".join(STEPS))
    ap.add_argument("--force", action="store_true", help="do the steps again even if their results are there")
    ap.add_argument("--archive", action="store_true", help="also write <out>/<id>.zip")
    a = ap.parse_args()
    skip = {s for s in a.skip.split(",") if s}
    if skip - set(STEPS):
        sys.exit(f"--skip takes {','.join(STEPS)}")
    work = a.work or os.path.join("work", a.id)
    os.makedirs(work, exist_ok=True)
    py = sys.executable
    tiles = os.path.join(work, "tiles.mbtiles")
    routing = os.path.join(work, "routing")
    places = os.path.join(work, "places.pidx")

    # ---- osm
    if a.pbf:
        pbf, source = a.pbf, os.path.basename(a.pbf)
    else:
        url = a.pbf_url or BBBIKE.format(n=a.bbbike)
        pbf, source = os.path.join(work, "extract.osm.pbf"), url
        if "osm" not in skip and (a.force or not os.path.exists(pbf)):
            say(f"osm: {url}")
            download_file(url, pbf)
        elif "osm" not in skip:
            say("osm: the extract is there")
    if not os.path.exists(pbf):
        sys.exit(f"{pbf} is not there")
    bbox = a.bbox or extract_box(pbf)
    print(f"  box {bbox}")

    # ---- tiles
    if "tiles" not in skip:
        if a.force and os.path.exists(tiles):
            os.remove(tiles)
        say("tiles")
        cmd = [py, os.path.join(HERE, "download_tiles.py"), tiles, "--bbox", bbox, "--min-zoom", str(a.min_zoom),
               "--max-zoom", str(a.max_zoom), "--rate", str(a.rate), "--name", a.id]
        if a.yes:
            cmd.append("--yes")
        run(cmd)

    # ---- routing
    if "routing" not in skip:
        if a.force or not os.path.isdir(routing):
            say("routing")
            if os.path.isdir(routing):
                import shutil
                shutil.rmtree(routing)
            run([py, os.path.join(HERE, "extract_routing.py"), pbf, routing, "--zoom", str(a.route_zoom), "--bbox", bbox])
        else:
            say("routing: there already")

    # ---- places
    if "places" not in skip:
        if a.force or not os.path.exists(places):
            say("places")
            run([py, os.path.join(HERE, "build_places.py"), places, "--pbf", pbf, "--bbox", bbox])
        else:
            say("places: there already")

    # ---- pack
    if "pack" not in skip:
        say("pack")
        cmd = [py, os.path.join(HERE, "pack_region.py"), "pack", "--id", a.id, "--name", a.name, "--tiles", tiles, "--routing", routing,
               "--places", places, "--out", os.path.join(a.out, a.id), "--bbox", bbox, "--source", source]
        if a.archive:
            cmd.append("--archive")
        run(cmd)
        print(f"\nDone. Copy {os.path.join(a.out, a.id)} to Maps/ on the SD card.")


if __name__ == "__main__":
    main()
