#!/usr/bin/env python3
"""
extract_routing.py for inputs that do not fit in memory: a country, or the whole planet.

    extract_routing_large.py <input.osm.pbf> <output_dir> [--zoom 10] [--bbox W,S,E,N]
                             [--work DIR] [--min-component 500] [--keep-work]

It makes the same .rng tiles as extract_routing.py (same roads, same junctions, same files, give or
take a metre in an edge length), but keeps its intermediate results on disk and in numpy arrays, so
it needs a few GB of memory and some tens of GB of disk, not tens of GB of memory. It needs
pyosmium and numpy; the island pruning needs scipy.

The stages, each of which leaves its result in --work and is skipped when it is already there
(an interrupted run goes on from the last finished stage; --force starts over):

  1 scan      one pass over the PBF; the rideable ways with the coordinates of their nodes -> ways files
  2 junctions the nodes that are a junction or the end of a way (the graph nodes) -> junctions.bin
  3 edges     the edges between graph nodes, split by the tile row of their start -> rows/<y>.bin
  4 islands   which graph nodes belong to a strongly connected network of at least --min-component nodes -> keep.bin
  5 tiles     one row at a time: the .rng files
"""
import argparse
import os
import shutil
import struct
import sys
import time
from array import array

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_routing as er  # noqa: E402

BUCKETS = 64  # junction counting is done on the ids of one residue at a time
CHUNK_REFS = 6_000_000  # node references handled at a time in the numpy stages
FLUSH_REFS = 4_000_000  # node references buffered in the scan before they are written
REVERSE_BIT = 1 << 15  # oneway=-1, kept next to the flags of er._make_flags() (which use bits 0 to 9)

EDGE_DT = np.dtype([("s", "<i8"), ("d", "<i8"), ("sy", "<i4"), ("sx", "<i4"), ("dy", "<i4"), ("dx", "<i4"),
                    ("dist", "<u2"), ("flags", "<u2"), ("kind", "u1")])  # kind 0: an edge, 1: only the node d
NODE_DT = np.dtype([("lon", "<i4"), ("lat", "<i4"), ("first", "<u4")])
RNG_EDGE_DT = np.dtype([("target", "<u4"), ("dist", "<u2"), ("flags", "<u2"), ("elev", "<i2"), ("next", "<u2")])
BORDER_DT = np.dtype([("node", "<u4"), ("tx", "<u2"), ("ty", "<u2"), ("lon", "<u4"), ("lat", "<i4")])


def log(msg):
    print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def deg7(v):
    """int(degrees * 1e7) of a coordinate held as an int32 in 1e-7 degrees, as write_rng() does it."""
    return np.trunc((v / 1e7) * 1e7).astype(np.int64)


def tile_of(y7, x7, zoom):
    """er.lat_lon_to_pixel() + er.pixel_to_tile() on arrays of 1e-7 degrees."""
    size = er.TILE_SIZE << zoom
    lat = y7 / 1e7
    lon = x7 / 1e7
    fx = (lon + 180.0) / 360.0
    s = np.sin(lat * np.pi / 180.0)
    fy = 0.5 - np.log((1 + s) / (1 - s)) / (4 * np.pi)
    px = np.clip(fx * size + 0.5, 0, size - 1).astype(np.int64)
    py = np.clip(fy * size + 0.5, 0, size - 1).astype(np.int64)
    return px // er.TILE_SIZE, py // er.TILE_SIZE


def haversine(lat1, lon1, lat2, lon2):
    dlat = np.radians(lat2 - lat1)
    dlon = np.radians(lon2 - lon1)
    a = np.sin(dlat / 2) ** 2 + np.cos(np.radians(lat1)) * np.cos(np.radians(lat2)) * np.sin(dlon / 2) ** 2
    return 6371000 * 2 * np.arctan2(np.sqrt(a), np.sqrt(1 - a))


class Work:
    """The files of one run."""

    def __init__(self, path):
        self.path = path
        os.makedirs(path, exist_ok=True)

    def p(self, *parts):
        return os.path.join(self.path, *parts)

    def done(self, stage):
        return os.path.exists(self.p(stage + ".done"))

    def finish(self, stage):
        open(self.p(stage + ".done"), "w").close()


# --- 1 scan ---
def scan(pbf, work, bbox):
    try:
        import osmium
    except ImportError:
        sys.exit("reading a .pbf needs pyosmium: pip install osmium")
    files = {n: open(work.p(n + ".bin"), "wb") for n in ("refs", "y", "x", "flags", "nn")}
    buf = {"refs": array("q"), "y": array("i"), "x": array("i"), "flags": array("H"), "nn": array("I")}
    stat = {"ways": 0, "kept": 0, "refs": 0, "t": time.time()}

    def flush():
        for k, a in buf.items():
            a.tofile(files[k])
            del a[:]

    class Scan(osmium.SimpleHandler):
        def way(self, w):
            if w.tags.get("highway") not in er.HIGHWAY_CLASS:
                return
            stat["ways"] += 1
            tags = {t.k: t.v for t in w.tags}
            rec = er._way_record(tags, [0, 0])
            if rec is None:
                return
            flags = er._make_flags(rec) | (REVERSE_BIT if rec["reverse"] else 0)
            if bbox is None:
                runs = [[(n.ref, n.location.y, n.location.x) for n in w.nodes if n.location.valid()]]
            else:
                pts = [(n.ref, n.location.lat, n.location.lon) for n in w.nodes if n.location.valid()]
                runs = [[(r, round(la * 1e7), round(lo * 1e7)) for r, la, lo in run] for run in er._runs_inside(pts, bbox)]
            for run in runs:
                if len(run) < 2:
                    continue
                stat["kept"] += 1
                stat["refs"] += len(run)
                for r, y, x in run:
                    buf["refs"].append(r)
                    buf["y"].append(y)
                    buf["x"].append(x)
                buf["flags"].append(flags)
                buf["nn"].append(len(run))
            if len(buf["refs"]) >= FLUSH_REFS:
                flush()
            if stat["ways"] % 1_000_000 == 0:
                log(f"  {stat['ways']} highway ways read, {stat['kept']} rideable, {stat['refs']} node references")

    idx = work.p("nodes.idx")
    if os.path.exists(idx):
        os.remove(idx)
    log(f"scan {pbf}")
    Scan().apply_file(pbf, locations=True, idx="sparse_file_array," + idx)
    flush()
    for f in files.values():
        f.close()
    if os.path.exists(idx):
        os.remove(idx)  # tens of GB for a country
    log(f"scan: {stat['ways']} highway ways, {stat['kept']} rideable, {stat['refs']} node references")
    if not stat["kept"]:
        sys.exit("no roads found in the input")


# --- 2 junctions ---
def junctions(work):
    refs = np.memmap(work.p("refs.bin"), dtype="<i8", mode="r")
    nn = np.fromfile(work.p("nn.bin"), dtype="<u4").astype(np.int64)
    offs = np.zeros(len(nn) + 1, dtype=np.int64)
    np.cumsum(nn, out=offs[1:])
    # ids are written to one file per residue so that each can be counted by itself
    bk = [open(work.p(f"bucket{b:02d}.bin"), "wb") for b in range(BUCKETS)]
    for i in range(0, len(refs), 8 * CHUNK_REFS):
        c = np.asarray(refs[i:i + 8 * CHUNK_REFS])
        b = c & (BUCKETS - 1)
        order = np.argsort(b, kind="stable")
        counts = np.bincount(b, minlength=BUCKETS)
        c = c[order]
        pos = 0
        for k in range(BUCKETS):
            c[pos:pos + counts[k]].tofile(bk[k])
            pos += counts[k]
    for f in bk:
        f.close()
    ends = np.unique(np.concatenate([np.asarray(refs[offs[:-1]]), np.asarray(refs[offs[1:] - 1])]))
    parts = []
    for b in range(BUCKETS):
        path = work.p(f"bucket{b:02d}.bin")
        a = np.fromfile(path, dtype="<i8")
        os.remove(path)
        u, c = np.unique(a, return_counts=True)
        parts.append(np.union1d(u[c > 1], ends[(ends & (BUCKETS - 1)) == b]))
    j = np.concatenate(parts)
    j.sort()
    j.tofile(work.p("junctions.bin"))
    log(f"junctions: {len(j)} graph nodes of {len(refs)} node references")


# --- 3 edges ---
def edges(work, zoom):
    j = np.fromfile(work.p("junctions.bin"), dtype="<i8")
    flags_all = np.fromfile(work.p("flags.bin"), dtype="<u2")
    nn = np.fromfile(work.p("nn.bin"), dtype="<u4").astype(np.int64)
    offs = np.zeros(len(nn) + 1, dtype=np.int64)
    np.cumsum(nn, out=offs[1:])
    refs_f = np.memmap(work.p("refs.bin"), dtype="<i8", mode="r")
    y_f = np.memmap(work.p("y.bin"), dtype="<i4", mode="r")
    x_f = np.memmap(work.p("x.bin"), dtype="<i4", mode="r")
    rows = work.p("rows")
    shutil.rmtree(rows, ignore_errors=True)
    os.makedirs(rows)
    total = 0
    w0 = 0
    nways = len(nn)
    while w0 < nways:
        w1 = int(np.searchsorted(offs, offs[w0] + CHUNK_REFS, side="right")) - 1
        w1 = min(max(w1, w0 + 1), nways)
        r0, r1 = int(offs[w0]), int(offs[w1])
        ids = np.asarray(refs_f[r0:r1])
        y = np.asarray(y_f[r0:r1])
        x = np.asarray(x_f[r0:r1])
        off = offs[w0:w1] - r0
        n = nn[w0:w1]
        fl = flags_all[w0:w1]
        k = np.searchsorted(j, ids)
        k[k >= len(j)] = len(j) - 1
        is_graph = j[k] == ids
        # the length of every way up to each of its points
        lat = y / 1e7
        lon = x / 1e7
        d = np.zeros(len(ids))
        d[:-1] = haversine(lat[:-1], lon[:-1], lat[1:], lon[1:])
        d[off + n - 1] = 0  # not the step from a way to the next one
        cs = np.concatenate(([0.0], np.cumsum(d)))
        pos = np.flatnonzero(is_graph)
        wid = np.searchsorted(off, pos, side="right") - 1
        same = wid[1:] == wid[:-1]
        a = pos[:-1][same]
        b = pos[1:][same]
        f = fl[wid[:-1][same]]
        dist = np.minimum(cs[b] - cs[a], 65535).astype(np.int64)
        oneway = (f & 1).astype(bool)
        rev_only = oneway & ((f & REVERSE_BIT) != 0)
        # per segment: forward and backward, or one of them for a oneway
        s2 = np.empty(2 * len(a), dtype=np.int64)
        d2 = np.empty(2 * len(a), dtype=np.int64)
        s2[0::2] = np.where(rev_only, b, a)
        d2[0::2] = np.where(rev_only, a, b)
        s2[1::2] = b
        d2[1::2] = a
        valid = np.ones(2 * len(a), dtype=bool)
        valid[1::2] = ~oneway
        s2, d2 = s2[valid], d2[valid]
        dist2 = np.repeat(dist, 2)[valid]
        f2 = np.repeat(f & (REVERSE_BIT - 1), 2)[valid]
        rec = np.zeros(len(s2), dtype=EDGE_DT)
        rec["s"], rec["d"] = ids[s2], ids[d2]
        rec["sy"], rec["sx"], rec["dy"], rec["dx"] = y[s2], x[s2], y[d2], x[d2]
        rec["dist"], rec["flags"] = dist2, f2
        stx, sty = tile_of(rec["sy"], rec["sx"], zoom)
        dtx, dty = tile_of(rec["dy"], rec["dx"], zoom)
        cross = (stx != dtx) | (sty != dty)
        out = [(rec, sty)]
        if cross.any():
            r1_ = rec[cross].copy()
            r1_["kind"] = 1
            out.append((r1_, dty[cross]))
        for recs, rowkey in out:
            order = np.argsort(rowkey, kind="stable")
            recs, rowkey = recs[order], rowkey[order]
            ys, first = np.unique(rowkey, return_index=True)
            bounds = list(first) + [len(recs)]
            for i, row in enumerate(ys):
                with open(os.path.join(rows, f"{row}.bin"), "ab") as fh:
                    recs[bounds[i]:bounds[i + 1]].tofile(fh)
        total += len(rec)
        w0 = w1
        log(f"edges: {w0}/{nways} ways, {total} edges")
    log(f"edges: {total} edges")


# --- 4 islands ---
def islands(work, min_nodes):
    """Keep the graph nodes of the strongly connected parts of at least min_nodes (see extract_routing.prune_islands)."""
    j = np.fromfile(work.p("junctions.bin"), dtype="<i8")
    if min_nodes <= 0:
        np.ones(len(j), dtype=bool).tofile(work.p("keep.bin"))
        return
    try:
        from scipy.sparse import coo_matrix
        from scipy.sparse.csgraph import connected_components
    except ImportError:
        sys.exit("--min-component needs scipy (pip install scipy), or give --min-component 0")
    a_parts, b_parts = [], []
    for name in sorted(os.listdir(work.p("rows")), key=lambda s: int(s.split(".")[0])):
        rec = np.fromfile(work.p("rows", name), dtype=EDGE_DT)
        rec = rec[(rec["kind"] == 0) & (((rec["flags"] >> 8) & 3) != 0) & (((rec["flags"] >> 1) & 7) != 0)]  # the edges a bicycle can use
        a_parts.append(np.searchsorted(j, rec["s"]).astype(np.int32))
        b_parts.append(np.searchsorted(j, rec["d"]).astype(np.int32))
    a = np.concatenate(a_parts)
    b = np.concatenate(b_parts)
    n = len(j)
    # duplicates (two roads between the same junctions) are summed, which does not matter here
    g = coo_matrix((np.ones(len(a), dtype=np.int8), (a, b)), shape=(n, n)).tocsr()
    del a, b, a_parts, b_parts
    count, labels = connected_components(g, directed=True, connection="strong")
    size = np.bincount(labels)
    keep = size[labels] >= min_nodes
    keep.tofile(work.p("keep.bin"))
    kept_parts = int((size >= min_nodes).sum())
    log(f"islands: kept {int(keep.sum())} of {n} nodes in {kept_parts} network(s); dropped {count - kept_parts} smaller than {min_nodes}")


# --- 5 tiles ---
def write_tile(path, e, extra, tx, ty, zoom):
    """The .rng file of one tile from the edges that start in it (e) and the nodes that are only reached from outside (extra)."""
    refs = np.concatenate([e["s"], e["d"], extra["d"]])
    ys = np.concatenate([e["sy"], e["dy"], extra["dy"]])
    xs = np.concatenate([e["sx"], e["dx"], extra["dx"]])
    uniq, first = np.unique(refs, return_index=True)
    ny, nx = ys[first], xs[first]
    n = len(uniq)
    si = np.searchsorted(uniq, e["s"])
    di = np.searchsorted(uniq, e["d"])
    dtx, dty = tile_of(e["dy"], e["dx"], zoom)
    cross = (dtx != tx) | (dty != ty)
    # the edge table: the edges of node 0, then of node 1, ...; in the order the edges were found
    order = np.argsort(si, kind="stable")
    counts = np.bincount(si, minlength=n)
    starts = np.cumsum(counts) - counts
    table = np.zeros(len(e), dtype=RNG_EDGE_DT)
    table["target"] = di[order]
    table["dist"] = e["dist"][order]
    table["flags"] = e["flags"][order]
    table["next"] = 1
    has = counts > 0
    table["next"][(starts + counts - 1)[has]] = 0xFFFF
    nodes = np.zeros(n, dtype=NODE_DT)
    nodes["lon"], nodes["lat"] = deg7(nx), deg7(ny)
    nodes["first"] = np.where(has, starts, 0xFFFFFFFF)
    border = np.zeros(int(cross.sum()), dtype=BORDER_DT)
    border["node"] = si[cross]
    border["tx"], border["ty"] = dtx[cross], dty[cross]
    border["lon"] = (deg7(e["dx"][cross]) & 0xFFFFFFFF)
    border["lat"] = deg7(e["dy"][cross])
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(er.RNG_MAGIC)
        f.write(struct.pack("<HH", er.RNG_VERSION, er.RNG_HEADER_SIZE))
        f.write(struct.pack("<III", n, len(table), len(border)))
        f.write(struct.pack("<iiii", int(nodes["lon"].min()), int(nodes["lat"].min()), int(nodes["lon"].max()), int(nodes["lat"].max())))
        nodes.tofile(f)
        table.tofile(f)
        border.tofile(f)
    return n, len(table), os.path.getsize(path)


def tiles(work, out_dir, zoom):
    j = np.fromfile(work.p("junctions.bin"), dtype="<i8")
    keep = np.fromfile(work.p("keep.bin"), dtype=bool)
    files = nodes_total = edges_total = size_total = 0
    names = sorted(os.listdir(work.p("rows")), key=lambda s: int(s.split(".")[0]))
    for name in names:
        ty = int(name.split(".")[0])
        rec = np.fromfile(work.p("rows", name), dtype=EDGE_DT)
        ks = keep[np.searchsorted(j, rec["s"])]
        kd = keep[np.searchsorted(j, rec["d"])]
        rec = rec[np.where(rec["kind"] == 0, ks & kd, kd)]
        if not len(rec):
            continue
        is_edge = rec["kind"] == 0
        stx, _ = tile_of(rec["sy"], rec["sx"], zoom)
        dtx, _ = tile_of(rec["dy"], rec["dx"], zoom)
        own = np.where(is_edge, stx, dtx)  # the tile that a record belongs to
        order = np.argsort(own, kind="stable")
        rec, own, is_edge = rec[order], own[order], is_edge[order]
        for tx in np.unique(own):
            sel = own == tx
            g, ge = rec[sel], is_edge[sel]
            size = write_tile(os.path.join(out_dir, str(zoom), str(tx), f"{ty}.rng"), g[ge], g[~ge], int(tx), ty, zoom)
            files += 1
            nodes_total += size[0]
            edges_total += size[1]
            size_total += size[2]
        log(f"tiles: row {ty} done, {files} files")
    print("\nSummary:")
    print(f"  Files: {files}")
    print(f"  Total nodes: {nodes_total}")
    print(f"  Total edges: {edges_total}")
    print(f"  Total size: {size_total / 1024:.1f} KB")


def main():
    ap = argparse.ArgumentParser(description="Extract the cycling routing network of a large PBF extract")
    ap.add_argument("input", help="Input PBF (.osm.pbf)")
    ap.add_argument("output_dir", help="Output directory for the .rng files")
    ap.add_argument("--zoom", type=int, default=10)
    ap.add_argument("--bbox", help="west,south,east,north: keep only the roads inside")
    ap.add_argument("--min-component", type=int, default=500)
    ap.add_argument("--work", help="where the stages keep their files (default <output_dir>.work)")
    ap.add_argument("--force", action="store_true", help="redo every stage")
    ap.add_argument("--keep-work", action="store_true", help="do not delete the work directory at the end")
    args = ap.parse_args()
    bbox = tuple(float(v) for v in args.bbox.split(",")) if args.bbox else None
    if bbox is not None and (len(bbox) != 4 or bbox[0] >= bbox[2] or bbox[1] >= bbox[3]):
        sys.exit("--bbox is west,south,east,north")
    work_dir = args.work or args.output_dir.rstrip("/") + ".work"
    if args.force:
        shutil.rmtree(work_dir, ignore_errors=True)
    work = Work(work_dir)
    stages = [
        ("scan", lambda: scan(args.input, work, bbox)),
        ("junctions", lambda: junctions(work)),
        ("edges", lambda: edges(work, args.zoom)),
        ("islands", lambda: islands(work, args.min_component)),
    ]
    for name, run in stages:
        if work.done(name):
            log(f"{name}: done before")
            continue
        run()
        work.finish(name)
    tiles(work, args.output_dir, args.zoom)
    if not args.keep_work:
        shutil.rmtree(work_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
