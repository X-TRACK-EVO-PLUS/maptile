#!/usr/bin/env python3
"""Tests of the data tools: tools/download_tiles.py, extract_routing.py, build_places.py, pack_region.py, fetch_region.py.

    python3 -m unittest discover -s tests/tools -v        (from the maptile directory)

They run on the beijing-mini test data and on a local HTTP server that plays the tile server, so they need
no network. build_places.py needs pypinyin (pip install pypinyin); the tests that read a .pbf need
pyosmium (pip install osmium) and are skipped without it.
"""
import gzip
import hashlib
import http.server
import json
import os
import shutil
import sqlite3
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.error
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TOOLS = os.path.join(ROOT, "tools")
DATA = os.path.join(ROOT, "tests", "data")
MINI = os.path.join(DATA, "beijing-mini.mbtiles")
sys.path.insert(0, TOOLS)

import download_tiles  # noqa: E402
import fetch_region  # noqa: E402
import pack_region  # noqa: E402

try:
    import osmium  # noqa: F401
    HAVE_OSMIUM = True
except ImportError:
    HAVE_OSMIUM = False
try:
    import pypinyin  # noqa: F401
    HAVE_PINYIN = True
except ImportError:
    HAVE_PINYIN = False

MINI_BOX = (116.35, 39.90, 116.43, 39.94)  # what the mini file says it covers (its metadata)


def _tiles_box():
    """A box that makes the downloader ask for every tile the mini file has (it has a few more than its metadata says)."""
    import math
    db = sqlite3.connect(MINI)
    xs = [x for x, in db.execute("select tile_column from tiles where zoom_level=14")]
    ys = [(1 << 14) - 1 - r for r, in db.execute("select tile_row from tiles where zoom_level=14")]
    n = 1 << 14
    lat = lambda y: math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * y / n))))  # noqa: E731
    eps = 1e-4
    return (min(xs) / n * 360 - 180 + eps, lat(max(ys) + 1) + eps, (max(xs) + 1) / n * 360 - 180 - eps, lat(min(ys)) - eps)


TILES_BOX = _tiles_box()
def run(*args, expect=0, env=None):
    r = subprocess.run([sys.executable, *args], capture_output=True, text=True, env=env)
    if expect is not None:
        assert r.returncode == expect, f"{args}: exit {r.returncode}\n{r.stdout}\n{r.stderr}"
    return r


class TileServer:
    """Plays the tile server from the mini file: XYZ URLs, with some tiles misbehaving on request."""

    def __init__(self):
        db = sqlite3.connect(MINI)
        self.tiles = {(z, x, (1 << z) - 1 - r): d for z, x, r, d in db.execute("select zoom_level, tile_column, tile_row, tile_data from tiles")}
        self.requests = []
        self.flaky = {}  # (z,x,y) -> [codes to answer with before the real answer]
        self.bad = set()  # tiles that answer with rubbish
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_GET(self):
                try:
                    z, x, y = [int(v) for v in self.path.strip("/").split("/")[-3:]]
                except ValueError:
                    self.send_error(400)
                    return
                outer.requests.append((z, x, y))
                codes = outer.flaky.get((z, x, y))
                if codes:
                    code = codes.pop(0)
                    self.send_response(code)
                    if code == 429:
                        self.send_header("Retry-After", "0")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                if (z, x, y) in outer.bad:
                    body = b"<html>not a tile</html>"
                elif (z, x, y) in outer.tiles:
                    body = outer.tiles[(z, x, y)]
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.httpd.server_address[1]}/{{z}}/{{x}}/{{y}}"
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


class Tmp(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="maptile-tools-")
        self.addCleanup(shutil.rmtree, self.tmp, True)

    def path(self, *p):
        return os.path.join(self.tmp, *p)


# ------------------------------------------------------------------------------ download_tiles ---
class DownloadTiles(Tmp):
    def setUp(self):
        super().setUp()
        self.server = TileServer()
        self.addCleanup(self.server.close)

    def get(self, out, **kw):
        args = dict(url=self.server.url, rate=0, workers=4, retries=3, quiet=True)
        args.update(kw)
        return download_tiles.download(out, TILES_BOX, 13, 14, **args)

    def test_tile_arithmetic(self):
        # Tiananmen at zoom 14 (OpenStreetMap's slippy map numbers)
        self.assertEqual(download_tiles.lon_lat_to_tile(116.3975, 39.9087, 14), (13489, 6208))
        x0, y0, x1, y1 = download_tiles.tile_range(MINI_BOX, 14)
        self.assertEqual(download_tiles.count_tiles(MINI_BOX, 14, 14), (x1 - x0 + 1) * (y1 - y0 + 1))
        self.assertEqual(download_tiles.count_tiles(MINI_BOX, 13, 14), sum(1 for _ in download_tiles.tiles_of(MINI_BOX, 13, 14)))
        # the whole world is one tile at zoom 0, and the poles do not break it
        self.assertEqual(download_tiles.count_tiles((-180, -90, 180, 90), 0, 0), 1)
        self.assertEqual(download_tiles.count_tiles((-180, -90, 180, 90), 3, 3), 64)

    def test_bbox_is_checked(self):
        self.assertEqual(download_tiles.parse_bbox("1,2,3,4"), (1, 2, 3, 4))
        for bad in ("1,2,3", "a,b,c,d", "3,2,1,4", "1,4,3,2", "-200,0,10,10", "0,0,10,100"):
            with self.assertRaises(SystemExit):
                download_tiles.parse_bbox(bad)

    def test_downloads_every_tile_the_server_has_and_writes_the_metadata(self):
        out = self.path("t.mbtiles")
        ok, empty, failed, skipped = self.get(out, name="mini")
        self.assertEqual((failed, skipped), (0, 0))
        self.assertEqual(ok, len(self.server.tiles))
        self.assertEqual(ok + empty, download_tiles.count_tiles(TILES_BOX, 13, 14))
        db = sqlite3.connect(out)
        meta = dict(db.execute("select name, value from metadata"))
        self.assertEqual(meta["name"], "mini")
        self.assertEqual((meta["format"], meta["minzoom"], meta["maxzoom"]), ("pbf", "13", "14"))
        self.assertEqual(meta["bounds"], ",".join(str(v) for v in TILES_BOX))
        self.assertIn("OpenStreetMap", meta["attribution"])
        # stored the way the renderer reads them: gzipped, in TMS rows, the same bytes as the original
        rows = {(z, x, r): d for z, x, r, d in db.execute("select zoom_level, tile_column, tile_row, tile_data from tiles")}
        orig = {(z, x, (1 << z) - 1 - y): d for (z, x, y), d in self.server.tiles.items()}
        self.assertEqual(rows, orig)
        problems = pack_region.Problems()
        self.assertEqual(pack_region.read_tiles(out, problems)["count"], ok)
        self.assertEqual(list(problems), [])
        self.assertEqual(sqlite3.connect(out).execute("PRAGMA journal_mode").fetchone()[0], "delete")
        self.assertFalse(os.path.exists(out + "-wal") or os.path.exists(out + "-shm"))

    def test_a_second_run_asks_for_nothing(self):
        out = self.path("t.mbtiles")
        self.get(out)
        n = len(self.server.requests)
        ok, empty, failed, skipped = self.get(out)
        self.assertEqual((ok, empty, failed), (0, 0, 0))
        self.assertEqual(skipped, download_tiles.count_tiles(TILES_BOX, 13, 14))
        self.assertEqual(len(self.server.requests), n)

    def test_a_run_that_stopped_carries_on(self):
        out = self.path("t.mbtiles")
        some = sorted(self.server.tiles)[:5]
        for key in some:
            self.server.flaky[key] = [500] * 10  # these fail every time (more than the retries)
        ok, empty, failed, _ = self.get(out, retries=1)
        self.assertEqual(failed, 5)
        self.server.flaky.clear()
        before = len(self.server.requests)
        ok2, empty2, failed2, skipped = self.get(out)
        self.assertEqual((ok2, failed2), (5, 0))
        self.assertEqual(len(self.server.requests) - before, 5)  # only the five
        self.assertEqual(skipped, download_tiles.count_tiles(TILES_BOX, 13, 14) - 5)

    def test_back_off_on_busy_and_server_errors(self):
        out = self.path("t.mbtiles")
        a, b, c = sorted(self.server.tiles)[:3]
        self.server.flaky[a] = [429, 429]
        self.server.flaky[b] = [503]
        self.server.flaky[c] = [502, 500]
        ok, _, failed, _ = self.get(out)
        self.assertEqual(failed, 0)
        self.assertEqual(ok, len(self.server.tiles))
        self.assertEqual(sum(1 for r in self.server.requests if r == a), 3)

    def test_a_tile_that_is_not_a_tile_is_a_failure_and_not_stored(self):
        out = self.path("t.mbtiles")
        key = sorted(self.server.tiles)[0]
        self.server.bad.add(key)
        ok, _, failed, _ = self.get(out)
        self.assertEqual(failed, 1)
        self.assertEqual(ok, len(self.server.tiles) - 1)
        db = sqlite3.connect(out)
        self.assertIsNone(db.execute("select 1 from tiles where zoom_level=? and tile_column=? and tile_row=?", (key[0], key[1], (1 << key[0]) - 1 - key[2])).fetchone())

    def test_other_errors_are_not_retried(self):
        out = self.path("t.mbtiles")
        key = sorted(self.server.tiles)[0]
        self.server.flaky[key] = [403]
        ok, _, failed, _ = self.get(out)
        self.assertEqual(failed, 1)
        self.assertEqual(sum(1 for r in self.server.requests if r == key), 1)

    def test_an_unreachable_server_fails_every_tile(self):
        out = self.path("t.mbtiles")
        ok, empty, failed, _ = download_tiles.download(out, TILES_BOX, 14, 14, url="http://127.0.0.1:1/{z}/{x}/{y}", rate=0, retries=0, quiet=True)
        self.assertEqual((ok, empty), (0, 0))
        self.assertEqual(failed, download_tiles.count_tiles(TILES_BOX, 14, 14))

    def test_a_wider_box_adds_to_the_file_and_the_metadata_follows(self):
        out = self.path("t.mbtiles")
        download_tiles.download(out, TILES_BOX, 14, 14, url=self.server.url, rate=0, quiet=True)
        download_tiles.download(out, TILES_BOX, 13, 14, url=self.server.url, rate=0, quiet=True)
        meta = dict(sqlite3.connect(out).execute("select name, value from metadata"))
        self.assertEqual((meta["minzoom"], meta["maxzoom"]), ("13", "14"))
        levels = dict(sqlite3.connect(out).execute("select zoom_level, count(*) from tiles group by 1"))
        self.assertEqual(set(levels), {13, 14})

    def test_the_command_line_asks_before_a_big_download(self):
        out = self.path("t.mbtiles")
        r = run(os.path.join(TOOLS, "download_tiles.py"), out, "--bbox", "115,39,117,41", "--min-zoom", "13", "--max-zoom", "14", expect=2)
        self.assertIn("--yes", r.stdout)
        self.assertFalse(os.path.exists(out))

    def test_the_command_line_reports_failures_by_exit_code(self):
        out = self.path("t.mbtiles")
        key = sorted(self.server.tiles)[0]
        self.server.flaky[key] = [500] * 20
        args = [os.path.join(TOOLS, "download_tiles.py"), out, "--bbox", ",".join(str(v) for v in TILES_BOX), "--url", self.server.url,
                "--rate", "1000", "--retries", "0"]
        r = run(*args, expect=1)
        self.assertIn("run the same command again", r.stdout)
        self.server.flaky.clear()
        run(*args, expect=0)


# ----------------------------------------------------------------------------------- fetch_region ---
class RangeServer:
    """A file server with Range support and an .md5, for the extract download."""

    def __init__(self, data: bytes, md5=True, cut_first=0, md5_value=None):
        self.data = data
        self.requests = []
        self.cut = cut_first  # the first answer ends after this many bytes
        outer = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *a):
                pass

            def do_HEAD(self):
                self.send_response(200 if self.path == "/x.osm.pbf" else 404)
                self.send_header("Content-Length", str(len(outer.data)))
                self.end_headers()

            def do_GET(self):
                outer.requests.append((self.path, self.headers.get("Range")))
                if self.path == "/x.osm.pbf.md5":
                    if not md5:
                        self.send_error(404)
                        return
                    body = ((md5_value or hashlib.md5(outer.data).hexdigest()) + "  x.osm.pbf\n").encode()
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                start = 0
                rng = self.headers.get("Range")
                if rng:
                    start = int(rng.split("=")[1].split("-")[0])
                body = outer.data[start:]
                self.send_response(206 if rng else 200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                if outer.cut and not rng:
                    self.wfile.write(body[: outer.cut])
                    outer.cut = 0
                    self.wfile.flush()
                    self.connection.close()
                    return
                self.wfile.write(body)

        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.httpd.server_address[1]}/x.osm.pbf"
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


class FetchRegion(Tmp):
    def test_a_download_is_checked_against_the_md5(self):
        data = os.urandom(300000)
        srv = RangeServer(data)
        self.addCleanup(srv.close)
        out = self.path("x.osm.pbf")
        fetch_region.download_file(srv.url, out)
        self.assertEqual(open(out, "rb").read(), data)
        self.assertFalse(os.path.exists(out + ".part"))

    def test_a_download_that_broke_off_is_carried_on(self):
        data = os.urandom(300000)
        srv = RangeServer(data, cut_first=120000)
        self.addCleanup(srv.close)
        out = self.path("x.osm.pbf")
        with self.assertRaises((SystemExit, Exception)):
            fetch_region.download_file(srv.url, out)
        self.assertTrue(os.path.exists(out + ".part"))
        self.assertLess(os.path.getsize(out + ".part"), len(data))
        fetch_region.download_file(srv.url, out)
        self.assertEqual(open(out, "rb").read(), data)
        self.assertTrue(any(rng and rng.startswith("bytes=") for _, rng in srv.requests))

    def test_a_download_with_the_wrong_md5_is_thrown_away(self):
        srv = RangeServer(os.urandom(50000), md5_value="0" * 32)
        self.addCleanup(srv.close)
        out = self.path("x.osm.pbf")
        with self.assertRaises(SystemExit):
            fetch_region.download_file(srv.url, out)
        self.assertFalse(os.path.exists(out))
        self.assertFalse(os.path.exists(out + ".part"))

    def test_a_server_without_an_md5_is_fine(self):
        data = os.urandom(50000)
        srv = RangeServer(data, md5=False)
        self.addCleanup(srv.close)
        out = self.path("x.osm.pbf")
        fetch_region.download_file(srv.url, out)
        self.assertEqual(open(out, "rb").read(), data)

    def test_pack_from_prepared_steps(self):
        """fetch_region.py with the steps before the pack done: the pack and its check."""
        work = self.path("work")
        os.makedirs(work)
        shutil.copyfile(MINI, os.path.join(work, "tiles.mbtiles"))
        shutil.copytree(os.path.join(DATA, "routing"), os.path.join(work, "routing"))
        shutil.copyfile(os.path.join(DATA, "places.pidx"), os.path.join(work, "places.pidx"))
        open(os.path.join(work, "extract.osm.pbf"), "wb").write(b"not used")
        out = self.path("Maps")
        run(os.path.join(TOOLS, "fetch_region.py"), "--id", "mini", "--name", "小样例", "--pbf", os.path.join(work, "extract.osm.pbf"),
            "--bbox", "116.35,39.90,116.43,39.94", "--work", work, "--out", out, "--skip", "tiles,routing,places", "--archive")
        problems, region = pack_region.check(os.path.join(out, "mini"))
        self.assertEqual(list(problems), [])
        self.assertEqual(region["name"], "小样例")
        self.assertTrue(zipfile.is_zipfile(os.path.join(out, "mini.zip")))
        with zipfile.ZipFile(os.path.join(out, "mini.zip")) as z:
            self.assertIn("mini/region.json", z.namelist())

    def test_unknown_step_is_refused(self):
        run(os.path.join(TOOLS, "fetch_region.py"), "--id", "x", "--name", "x", "--pbf", "nothing", "--out", self.tmp, "--skip", "flying", expect=1)


# ------------------------------------------------------------------------------------ pack_region ---
class PackRegion(Tmp):
    def pack(self, rid="mini", out=None, **kw):
        out = out or self.path("Maps", rid)
        args = [os.path.join(TOOLS, "pack_region.py"), "pack", "--id", rid, "--name", "小样例", "--tiles", MINI, "--routing", os.path.join(DATA, "routing"),
                "--places", os.path.join(DATA, "places.pidx"), "--out", out]
        for k, v in kw.items():
            args += [f"--{k}", v]
        return run(*args, expect=None), out

    def test_pack_and_check(self):
        r, out = self.pack()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        region = json.load(open(os.path.join(out, "region.json"), encoding="utf-8"))
        self.assertEqual(region["id"], "mini")
        self.assertEqual(region["bbox"], list(MINI_BOX))
        self.assertEqual(region["tiles"]["count"], 34)
        self.assertEqual(region["routing"]["files"], 4)
        self.assertEqual(region["routing"]["zoom"], 10)
        self.assertGreater(region["places"]["count"], 4000)
        self.assertEqual(region["tiles"]["sha256"], pack_region.sha256(os.path.join(out, "tiles.mbtiles")))
        self.assertEqual(run(os.path.join(TOOLS, "pack_region.py"), "check", out).returncode, 0)

    def test_the_id_and_the_directory_agree(self):
        r, _ = self.pack(rid="Bad Id")
        self.assertNotEqual(r.returncode, 0)
        r, _ = self.pack(rid="mini", out=self.path("Maps", "other"))
        self.assertNotEqual(r.returncode, 0)

    def check(self, d):
        return list(pack_region.check(d)[0])

    def packed(self):
        r, out = self.pack()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return out

    def test_check_finds_what_is_wrong(self):
        out = self.packed()
        self.assertEqual(self.check(out), [])

        # a missing file
        shutil.copytree(out, self.path("a", "mini"))
        os.remove(self.path("a", "mini", "places.pidx"))
        self.assertTrue(any("places" in p for p in self.check(self.path("a", "mini"))))

        # a routing tile cut short
        shutil.copytree(out, self.path("b", "mini"))
        f = self.path("b", "mini", "routing", "10", "842", "387.rng")
        data = open(f, "rb").read()
        open(f, "wb").write(data[:-20])
        self.assertTrue(any("wrong size" in p for p in self.check(self.path("b", "mini"))))

        # a copy that changed a byte of the tiles
        shutil.copytree(out, self.path("c", "mini"))
        f = self.path("c", "mini", "tiles.mbtiles")
        data = bytearray(open(f, "rb").read())
        data[-5] ^= 0xFF
        open(f, "wb").write(bytes(data))
        self.assertTrue(any("SHA-256" in p or "tiles:" in p for p in self.check(self.path("c", "mini"))))

        # a manifest that lies
        for name, edit in (
            ("d", lambda r: r.update(bbox=[0, 0, 1, 1])),
            ("e", lambda r: r.update(id="Other")),
            ("f", lambda r: r.update(format=2)),
            ("g", lambda r: r.pop("places")),
            ("h", lambda r: r["tiles"].update(maxzoom=15)),
        ):
            shutil.copytree(out, self.path(name, "mini"))
            mf = self.path(name, "mini", "region.json")
            region = json.load(open(mf, encoding="utf-8"))
            edit(region)
            json.dump(region, open(mf, "w", encoding="utf-8"))
            self.assertNotEqual(self.check(self.path(name, "mini")), [], name)

        # no manifest, a manifest that is not JSON
        os.makedirs(self.path("i", "mini"))
        self.assertTrue(self.check(self.path("i", "mini")))
        open(self.path("i", "mini", "region.json"), "w").write("{")
        self.assertTrue(self.check(self.path("i", "mini")))

    def test_tiles_in_wal_mode_are_refused(self):
        wal = self.path("wal.mbtiles")
        shutil.copyfile(MINI, wal)
        db = sqlite3.connect(wal)
        self.assertEqual(db.execute("PRAGMA journal_mode=WAL").fetchone()[0], "wal")
        db.close()
        problems = pack_region.Problems()
        pack_region.read_tiles(wal, problems)
        self.assertTrue(any("WAL" in p for p in problems))
        db = sqlite3.connect(wal)
        db.execute("PRAGMA journal_mode=DELETE")
        db.close()
        problems = pack_region.Problems()
        self.assertIsNotNone(pack_region.read_tiles(wal, problems))
        self.assertEqual(list(problems), [])

    def test_inputs_that_are_not_good_are_not_packed(self):
        bad = self.path("junk.mbtiles")
        open(bad, "wb").write(b"junk")
        r = run(os.path.join(TOOLS, "pack_region.py"), "pack", "--id", "mini", "--name", "x", "--tiles", bad, "--routing", os.path.join(DATA, "routing"),
                "--places", os.path.join(DATA, "places.pidx"), "--out", self.path("Maps", "mini"), expect=None)
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse(os.path.exists(self.path("Maps", "mini", "region.json")))


# ------------------------------------------------------------------------------- build_places ---
@unittest.skipUnless(HAVE_PINYIN, "needs pypinyin")
class BuildPlaces(Tmp):
    def build(self, out, *extra):
        return run(os.path.join(TOOLS, "build_places.py"), out, "--tiles", MINI, "--extra", os.path.join(DATA, "landmarks.csv"), *extra)

    def test_the_committed_index_is_what_the_tool_makes(self):
        out = self.path("p.pidx")
        self.build(out)
        self.assertEqual(open(out, "rb").read(), open(os.path.join(DATA, "places.pidx"), "rb").read())

    def test_the_output_is_the_same_every_time_and_the_file_is_well_formed(self):
        a, b = self.path("a.pidx"), self.path("b.pidx")
        self.build(a)
        self.build(b)
        self.assertEqual(open(a, "rb").read(), open(b, "rb").read())
        problems = pack_region.Problems()
        info = pack_region.read_places(a, problems)
        self.assertEqual(list(problems), [])
        self.assertGreater(info["keys"], info["count"] * 2)
        # keys sorted by (bytes, place id)
        d = open(a, "rb").read()
        _, hs, pc, kc, sb = struct.unpack_from("<HHIII", d, 4)
        base = hs + pc * 16
        strings = base + kc * 8
        prev = None
        for i in range(kc):
            off, pid = struct.unpack_from("<II", d, base + i * 8)
            end = d.index(b"\0", strings + off)
            cur = (d[strings + off:end], pid)
            if prev:
                self.assertLessEqual(prev, cur)
            prev = cur

    def test_a_box_keeps_only_the_places_inside(self):
        full, small = self.path("f.pidx"), self.path("s.pidx")
        self.build(full)
        self.build(small, "--bbox", "116.38,39.91,116.40,39.93")
        a, b = pack_region.read_places(full, pack_region.Problems()), pack_region.read_places(small, pack_region.Problems())
        self.assertLess(b["count"], a["count"])
        self.assertGreaterEqual(b["box"][0], 116.38 - 1e-6)
        self.assertLessEqual(b["box"][2], 116.40 + 1e-6)

    def test_keys_for_readings_initials_and_latin_names(self):
        import build_places as bp
        ks = bp.keys_of("故宫博物院")
        self.assertIn("gugongbowuyuan", ks)
        self.assertIn("ggbwy", ks)
        self.assertIn("故宫博物院", ks)
        self.assertIn("starbucks", bp.keys_of("Starbucks"))
        # a character with two readings gets a key for each
        ks = bp.keys_of("重庆")
        self.assertIn("chongqing", ks)
        self.assertIn("zhongqing", ks)
        self.assertLessEqual(len(bp.keys_of("长" * 12)), 2 * bp.MAX_READINGS + 1)
        self.assertEqual(bp.normalise("McDonald's  Ｗｉｄｅ"), "mcdonaldswide")

    def test_nothing_to_read_is_an_error(self):
        r = run(os.path.join(TOOLS, "build_places.py"), self.path("x.pidx"), expect=None)
        self.assertNotEqual(r.returncode, 0)

    def test_osm_tags_become_kinds(self):
        import build_places as bp
        c = bp.osm_classify
        self.assertEqual(c({"amenity": "drinking_water"}), (bp.WATER_TOILET, bp.FLAG_WATER))
        self.assertEqual(c({"amenity": "toilets"}), (bp.WATER_TOILET, bp.FLAG_TOILET))
        self.assertEqual(c({"shop": "bicycle"})[0], bp.BIKE)
        self.assertEqual(c({"tourism": "museum"})[0], bp.SIGHT)
        self.assertEqual(c({"leisure": "park"})[0], bp.SIGHT)
        self.assertEqual(c({"railway": "station"})[0], bp.TRANSIT)
        self.assertEqual(c({"amenity": "cafe"})[0], bp.FOOD)
        self.assertEqual(c({"amenity": "pharmacy"})[0], bp.MEDICAL)
        self.assertEqual(c({"place": "suburb"})[0], bp.PLACE)
        self.assertEqual(c({"landuse": "residential"})[0], bp.OTHER)
        self.assertIsNone(c({"amenity": "parking"}))
        self.assertIsNone(c({"highway": "residential"}))
        self.assertIsNone(c({}))
        self.assertEqual(bp.osm_name({"name": "A", "name:en": "B", "alt_name": "C;D"}), ("A", ("B", "C", "D")))
        self.assertEqual(bp.osm_name({"name:en": "B"}), (None, ()))


# ----------------------------------------------------------------------------- extract_routing ---
OSM_XML = """<?xml version="1.0"?>
<osm version="0.6">
  <node id="1" lat="39.9000" lon="116.3900"/>
  <node id="2" lat="39.9000" lon="116.3950"/>
  <node id="3" lat="39.9000" lon="116.4000"/>
  <node id="4" lat="39.9050" lon="116.3950"/>
  <node id="5" lat="39.9100" lon="116.3950"/>
  <node id="6" lat="39.9000" lon="116.5000"/>
  <way id="10"><nd ref="1"/><nd ref="2"/><nd ref="3"/><tag k="highway" v="residential"/><tag k="name" v="A Road"/></way>
  <way id="11"><nd ref="2"/><nd ref="4"/><nd ref="5"/><tag k="highway" v="tertiary"/><tag k="oneway" v="yes"/></way>
  <way id="12"><nd ref="3"/><nd ref="6"/><tag k="highway" v="cycleway"/></way>
  <way id="13"><nd ref="1"/><nd ref="4"/><tag k="highway" v="footway"/></way>
  <way id="14"><nd ref="1"/><nd ref="5"/><tag k="building" v="yes"/></way>
</osm>
"""


class ExtractRouting(Tmp):
    def extract(self, *extra, src=None):
        src = src or self.path("t.osm")
        if not os.path.exists(src):
            open(src, "w").write(OSM_XML)
        out = self.path("routing")
        r = run(os.path.join(TOOLS, "extract_routing.py"), src, out, "--zoom", "10", *(extra if "--min-component" in extra else ("--min-component", "0", *extra)), expect=None)
        return r, out

    def tiles(self, out):
        info = pack_region.read_routing(out, pack_region.Problems())
        return info

    def test_roads_become_a_tile_whose_size_matches_its_header(self):
        r, out = self.extract()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        problems = pack_region.Problems()
        info = pack_region.read_routing(out, problems)
        self.assertEqual(list(problems), [])
        self.assertEqual(info["zoom"], 10)
        # junction 2 splits the first road; the footway and the building are not for bicycles
        self.assertGreaterEqual(info["nodes"], 5)
        self.assertGreater(info["edges"], 4)

    def test_the_same_input_gives_the_same_bytes(self):
        _, a = self.extract()
        first = {os.path.relpath(os.path.join(d, f), a): open(os.path.join(d, f), "rb").read() for d, _, fs in os.walk(a) for f in fs}
        shutil.rmtree(a)
        _, b = self.extract()
        second = {os.path.relpath(os.path.join(d, f), b): open(os.path.join(d, f), "rb").read() for d, _, fs in os.walk(b) for f in fs}
        self.assertEqual(first, second)

    def test_a_box_cuts_roads_where_they_leave_it(self):
        _, out = self.extract()
        full = self.tiles(out)
        shutil.rmtree(out)
        r, out = self.extract("--bbox", "116.38,39.89,116.46,39.915")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        cut = self.tiles(out)
        self.assertLess(cut["nodes"], full["nodes"])  # node 6 (lon 116.5) is outside
        self.assertLessEqual(cut["box"][2], 116.46)

    def test_islands_are_dropped(self):
        # a road of two junctions far from the others, and the service roads that lead nowhere
        xml = OSM_XML.replace("</osm>", '''  <node id="20" lat="39.9300" lon="116.4000"/>
  <node id="21" lat="39.9310" lon="116.4000"/>
  <node id="22" lat="39.9320" lon="116.4000"/>
  <way id="30"><nd ref="20"/><nd ref="21"/><nd ref="22"/><tag k="highway" v="residential"/></way>
  <way id="31"><nd ref="1"/><nd ref="22"/><tag k="highway" v="service"/><tag k="service" v="parking_aisle"/></way>
  <way id="32"><nd ref="4"/><nd ref="21"/><tag k="highway" v="residential"/><tag k="access" v="private"/></way>
</osm>''')
        src = self.path("i.osm")
        open(src, "w").write(xml)
        r, out = self.extract(src=src)
        with_island = self.tiles(out)
        shutil.rmtree(out)
        r, out = self.extract("--min-component", "4", src=src)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("dropped 1", r.stdout)
        without = self.tiles(out)
        self.assertLess(without["nodes"], with_island["nodes"])
        shutil.rmtree(out)
        r, _ = self.extract("--min-component", "100", src=src)  # nothing is that big
        self.assertNotEqual(r.returncode, 0)

    def test_a_bad_box_or_no_roads_stops_it(self):
        r, _ = self.extract("--bbox", "1,2,3")
        self.assertNotEqual(r.returncode, 0)
        r, _ = self.extract("--bbox", "10,10,11,11")  # nothing there
        self.assertNotEqual(r.returncode, 0)

    @unittest.skipUnless(HAVE_OSMIUM, "needs pyosmium")
    def test_a_pbf_gives_the_same_network_as_the_xml(self):
        import osmium
        pbf = self.path("t.osm.pbf")
        w = osmium.SimpleWriter(pbf)
        for nid, lat, lon in ((1, 39.9, 116.39), (2, 39.9, 116.395), (3, 39.9, 116.4), (4, 39.905, 116.395), (5, 39.91, 116.395), (6, 39.9, 116.5)):
            w.add_node(osmium.osm.mutable.Node(id=nid, location=(lon, lat)))
        for wid, refs, tags in ((10, [1, 2, 3], {"highway": "residential", "name": "A Road"}), (11, [2, 4, 5], {"highway": "tertiary", "oneway": "yes"}),
                                (12, [3, 6], {"highway": "cycleway"}), (13, [1, 4], {"highway": "footway"})):
            w.add_way(osmium.osm.mutable.Way(id=wid, nodes=refs, tags=tags))
        w.close()
        _, a = self.extract(src=self.path("t.osm"))
        xml = {os.path.relpath(os.path.join(d, f), a): open(os.path.join(d, f), "rb").read() for d, _, fs in os.walk(a) for f in fs}
        shutil.rmtree(a)
        r, b = self.extract(src=pbf)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        got = {os.path.relpath(os.path.join(d, f), b): open(os.path.join(d, f), "rb").read() for d, _, fs in os.walk(b) for f in fs}
        self.assertEqual(got, xml)


if __name__ == "__main__":
    unittest.main()
