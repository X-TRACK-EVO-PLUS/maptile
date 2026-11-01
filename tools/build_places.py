#!/usr/bin/env python3
"""Build the place search index (places.pidx) from the vector tiles of a region.

    build_places.py <places.pidx> (--tiles tiles.mbtiles | --pbf extract.osm.pbf) [--bbox W,S,E,N]
                    [--zoom Z] [--extra landmarks.csv]
The index is what the firmware's PlaceIndex (App/Service/Journey/PlaceIndex.*) searches: a table of
named places and a sorted table of keys, so that a query is a binary search and a short scan. Each
place gets up to three kinds of key (all normalised: NFKC, lower case, letters and digits only):
the full pinyin ("tiananmenguangchang"), the initials ("tamgc"), and the lower-cased name itself
(Latin names and digits, "starbucks"). A character with several readings gets a key per reading,
so the device needs no pinyin library.

Places come from one of two sources:
  --tiles  the point features of the "pois", "place_labels" and "public_transport" layers of vector tiles
           (Shortbread schema). Shortbread has no labels for parks and palaces, so it is the quick way to
           get an index for a demo region, not the way to get a good one.
  --pbf    OpenStreetMap itself (needs pyosmium: pip install osmium): named nodes, and named areas (parks,
           palaces, universities, residential compounds ...) placed at the middle of their outline.
--extra reads a CSV of "name,lon,lat,kind" lines for places to add by hand. The output is deterministic:
the same input gives the same bytes.
File layout (little endian), see Docs/OfflineNavigation_Design.md 4.2:

    header (36 bytes)  "PIX1", version u16, header_size u16, place_count u32, key_count u32,
                       string_bytes u32, bbox i32 x4 (west south east north, 1e-7 degrees)
    places             place_count x 16 bytes: lon i32, lat i32, name_off u32, kind u8, rank u8, flags u16
    keys               key_count x 8 bytes sorted by (key bytes, place id): key_off u32, place_id u32
    strings            NUL terminated UTF-8: names and keys

Needs pypinyin (pip install pypinyin).
"""
from __future__ import annotations

import argparse
import gzip
import itertools
import math
import re
import sqlite3
import struct
import sys
import unicodedata

from pypinyin import Style, lazy_pinyin, pinyin

MAGIC = b"PIX1"
VERSION = 1
HEADER_BYTES = 36

# kinds (design 4.2) and the flags that tell water from toilets inside kind 6
PLACE, SIGHT, FOOD, SHOP, BIKE, WATER_TOILET, TRANSIT, HOTEL, MEDICAL, OTHER = range(1, 11)
FLAG_WATER, FLAG_TOILET = 1, 2
KIND_NAMES = {"place": PLACE, "sight": SIGHT, "food": FOOD, "shop": SHOP, "bike": BIKE, "water": WATER_TOILET,
              "toilet": WATER_TOILET, "transit": TRANSIT, "hotel": HOTEL, "medical": MEDICAL, "other": OTHER}
RANK = {PLACE: 200, SIGHT: 180, TRANSIT: 150, BIKE: 120, WATER_TOILET: 120, HOTEL: 110, MEDICAL: 110, SHOP: 100,
        FOOD: 100, OTHER: 80}
LANDMARK_RANK = 230

SIGHT_TOURISM = {"attraction", "museum", "viewpoint", "gallery", "artwork", "zoo", "theme_park"}
HOTEL_TOURISM = {"hotel", "hostel", "guest_house", "motel", "apartment"}
FOOD_AMENITY = {"restaurant", "cafe", "fast_food", "food_court", "ice_cream", "bar", "pub", "biergarten"}
SUPPLY_SHOP = {"convenience", "supermarket", "kiosk", "beverages", "bakery"}
MEDICAL_AMENITY = {"pharmacy", "hospital", "clinic", "doctors", "dentist"}
AREA_KINDS = {"suburb", "quarter", "neighbourhood", "locality"}
SKIP_SUFFIXES = ("社区", "街道", "居委会", "委员会")
SAME_PLACE_M = 150
MAX_READINGS = 4  # keys per name: the usual reading and up to three others
MAX_KEY = 48


# ---------------------------------------------------------------------------- vector tiles ---
def _varint(b: bytes, i: int):
    r = s = 0
    while True:
        c = b[i]
        i += 1
        r |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return r, i


def _fields(b: bytes):
    i = 0
    while i < len(b):
        k, i = _varint(b, i)
        f, w = k >> 3, k & 7
        if w == 0:
            v, i = _varint(b, i)
        elif w == 2:
            n, i = _varint(b, i)
            v = b[i:i + n]
            i += n
        elif w == 5:
            v = b[i:i + 4]
            i += 4
        elif w == 1:
            v = b[i:i + 8]
            i += 8
        else:
            raise ValueError(f"wire type {w}")
        yield f, v


def _packed(b: bytes):
    out, i = [], 0
    while i < len(b):
        v, i = _varint(b, i)
        out.append(v)
    return out


def _value(b: bytes):
    for f, v in _fields(b):
        if f == 1:
            return v.decode()
        if f == 6:
            return (v >> 1) ^ -(v & 1)
        if f == 7:
            return bool(v)
        if f in (2, 3, 4, 5):
            return v
    return None


def _zigzag(n: int) -> int:
    return (n >> 1) ^ -(n & 1)


def decode_points(data: bytes):
    """Yield (layer, properties, x, y, extent) for the point features of a tile."""
    for f, layer in _fields(data):
        if f != 3:
            continue
        name, keys, vals, feats, extent = None, [], [], [], 4096
        for lf, lv in _fields(layer):
            if lf == 1:
                name = lv.decode()
            elif lf == 2:
                feats.append(lv)
            elif lf == 3:
                keys.append(lv.decode())
            elif lf == 4:
                vals.append(_value(lv))
            elif lf == 5:
                extent = lv
        for fb in feats:
            tags, gtype, geom = [], 0, []
            for ff, fv in _fields(fb):
                if ff == 2:
                    tags = _packed(fv)
                elif ff == 3:
                    gtype = fv
                elif ff == 4:
                    geom = _packed(fv)
            if gtype != 1 or not geom:
                continue
            props = {keys[tags[k]]: vals[tags[k + 1]] for k in range(0, len(tags), 2)}
            cmd, count = geom[0] & 7, geom[0] >> 3
            if cmd != 1 or count < 1:
                continue
            i = 1
            x = y = 0
            for _ in range(count):
                x += _zigzag(geom[i])
                y += _zigzag(geom[i + 1])
                i += 2
                yield name, props, x, y, extent


# ---------------------------------------------------------------------------------- places ---
def classify(layer: str, p: dict):
    """(kind, flags) of a point feature, following the table in the design document."""
    if layer == "place_labels":
        return PLACE, 0
    if layer == "public_transport":
        return TRANSIT, 0
    a, s, t = p.get("amenity"), p.get("shop"), p.get("tourism")
    if p.get("leisure") == "park" or t in SIGHT_TOURISM or p.get("historic"):
        return SIGHT, 0
    if t in HOTEL_TOURISM:
        return HOTEL, 0
    if s == "bicycle" or a in ("bicycle_repair_station", "bicycle_rental"):
        return BIKE, 0
    if a == "drinking_water":
        return WATER_TOILET, FLAG_WATER
    if a == "toilets":
        return WATER_TOILET, FLAG_TOILET
    if a in FOOD_AMENITY:
        return FOOD, 0
    if s in SUPPLY_SHOP:
        return SHOP, 0
    if a in MEDICAL_AMENITY:
        return MEDICAL, 0
    if a == "bus_station" or p.get("railway") in ("station", "halt"):
        return TRANSIT, 0
    return OTHER, 0


# ----------------------------------------------------------------------- OpenStreetMap data ---
TOWN_PLACES = AREA_KINDS | {"city", "town", "village", "hamlet", "isolated_dwelling", "city_block", "square"}
SIGHT_LEISURE = {"park", "garden", "nature_reserve", "stadium"}
SIGHT_LANDUSE = {"cemetery"}
# amenities that are things, not places to go to
NOT_PLACES = {"parking", "parking_space", "parking_entrance", "bench", "shelter", "waste_basket", "waste_disposal", "recycling",
              "bicycle_parking", "motorcycle_parking", "telephone", "post_box", "vending_machine", "atm", "charging_station",
              "fire_hydrant", "clock", "give_box", "bbq", "loading_dock", "lounger", "hunting_stand", "hookah_lounge"}
COMPOUND_LANDUSE = {"residential", "commercial", "retail"}


def osm_classify(t):
    """(kind, flags) of an OpenStreetMap object by its tags, or None if it is not a place to search for."""
    a, s, tr = t.get("amenity"), t.get("shop"), t.get("tourism")
    if t.get("place") in TOWN_PLACES:
        return PLACE, 0
    if a == "drinking_water":
        return WATER_TOILET, FLAG_WATER
    if a == "toilets":
        return WATER_TOILET, FLAG_TOILET
    if s == "bicycle" or a in ("bicycle_repair_station", "bicycle_rental"):
        return BIKE, 0
    if t.get("railway") in ("station", "halt", "subway_entrance", "tram_stop") or t.get("public_transport") == "station" or a == "bus_station" or t.get("aeroway") == "terminal":
        return TRANSIT, 0
    if tr in SIGHT_TOURISM or t.get("historic") or t.get("leisure") in SIGHT_LEISURE or t.get("landuse") in SIGHT_LANDUSE:
        return SIGHT, 0
    if tr in HOTEL_TOURISM:
        return HOTEL, 0
    if a in FOOD_AMENITY:
        return FOOD, 0
    if s in SUPPLY_SHOP:
        return SHOP, 0
    if a in MEDICAL_AMENITY or t.get("healthcare"):
        return MEDICAL, 0
    if a and a not in NOT_PLACES:
        return OTHER, 0
    if s or t.get("office") or t.get("craft") or t.get("leisure") or t.get("landuse") in COMPOUND_LANDUSE or tr:
        return OTHER, 0
    return None


def osm_name(t):
    """The name to search for, and other names that find the same place."""
    name = (t.get("name:zh") or t.get("name") or "").strip()
    if not name:
        return None, ()
    names = []
    for key in ("name", "name:zh", "alt_name", "name:en", "official_name", "short_name", "old_name"):
        for v in (t.get(key) or "").split(";"):
            v = v.strip()
            if v and v != name and v not in names:
                names.append(v)
    return name, tuple(sorted(names))


def read_pbf(path: str, bbox):
    """Named places of an OpenStreetMap extract: (name, lon, lat, kind, flags, rank, other_names)."""
    try:
        import osmium
    except ImportError:
        sys.exit("reading a .pbf needs pyosmium: pip install osmium")
    found = []

    def take(tags, lon, lat):
        if bbox and not (bbox[0] <= lon <= bbox[2] and bbox[1] <= lat <= bbox[3]):
            return
        cls = osm_classify(tags)
        name, others = osm_name(tags)
        if cls is None or not name or name.endswith(SKIP_SUFFIXES):
            return
        kind, flags = cls
        found.append((name, lon, lat, kind, flags, RANK[kind], others))

    class Places(osmium.SimpleHandler):
        def node(self, n):
            if n.tags and n.location.valid():
                take({t.k: t.v for t in n.tags}, n.location.lon, n.location.lat)

        def area(self, a):
            if not a.tags.get("name") and not a.tags.get("name:zh"):
                return
            tags = {t.k: t.v for t in a.tags}
            if osm_classify(tags) is None:
                return
            west = south = 1e9
            east = north = -1e9
            for ring in a.outer_rings():
                for ref in ring:
                    if ref.location.valid():
                        west, east = min(west, ref.lon), max(east, ref.lon)
                        south, north = min(south, ref.lat), max(north, ref.lat)
            if west <= east:
                take(tags, (west + east) / 2, (south + north) / 2)

    Places().apply_file(path, locations=True, idx="flex_mem")
    return found


def normalise(text: str) -> str:
    """The key normalisation of the design: NFKC, lower case, letters and digits only."""
    t = unicodedata.normalize("NFKC", text).lower()
    return "".join(ch for ch in t if ch.isalnum())


def _words(chunk: str):
    return re.findall(r"[A-Za-z]+|\d+", chunk)


def _tokens(name: str):
    """The syllable-sized pieces of a name: a Chinese character, or a run of Latin letters, or of digits."""
    out = []
    for m in re.finditer(r"[\u3400-\u9fff\uf900-\ufaff]|[A-Za-z]+|\d+", name):
        out.append(m.group())
    return out


def readings(name: str):
    """Up to MAX_READINGS lists of syllables for a name: the usual reading, then ones that differ in one place."""
    main = [normalise(s) for s in lazy_pinyin(name, style=Style.NORMAL, errors=_words)]
    main = [s for s in main if s]
    out = [main]
    tokens = _tokens(name)
    if len(tokens) != len(main):  # something odd in the name: the usual reading alone
        return out
    for pos, tok in enumerate(tokens):
        if not ("\u3400" <= tok <= "\u9fff" or "\uf900" <= tok <= "\ufaff"):
            continue
        try:
            choices = pinyin(tok, style=Style.NORMAL, heteronym=True)[0]
        except Exception:
            continue
        for c in choices:
            c = normalise(c)
            if c and c != main[pos]:
                variant = main[:pos] + [c] + main[pos + 1:]
                if variant not in out and len(out) < MAX_READINGS:
                    out.append(variant)
    return out
def keys_of(name: str):
    ks = []
    for syllables in readings(name):
        if not syllables:
            continue
        ks.append("".join(syllables))
        ks.append("".join(s[0] for s in syllables))
    ks.append(normalise(name))
    out = []
    for k in ks:
        k = k[:MAX_KEY]
        if k and k not in out:
            out.append(k)
    return out


def metres_between(lon1, lat1, lon2, lat2):
    dx = (lon2 - lon1) * 111320 * math.cos(math.radians((lat1 + lat2) / 2))
    dy = (lat2 - lat1) * 110574
    return math.hypot(dx, dy)


def read_tiles(path: str, zoom: int | None):
    db = sqlite3.connect(path)
    if zoom is None:
        zoom = db.execute("select max(zoom_level) from tiles").fetchone()[0]
    found = []
    for col, row, data in db.execute("select tile_column, tile_row, tile_data from tiles where zoom_level=?", (zoom,)):
        if data[:2] == b"\x1f\x8b":
            data = gzip.decompress(data)
        ty = (1 << zoom) - 1 - row
        n = 1 << zoom
        for layer, props, x, y, extent in decode_points(data):
            if layer not in ("pois", "place_labels", "public_transport"):
                continue
            name = (props.get("name") or "").strip()
            if not name or name.endswith(SKIP_SUFFIXES):
                continue
            if layer == "place_labels" and props.get("kind") not in AREA_KINDS:
                continue
            wx, wy = (col + x / extent) / n, (ty + y / extent) / n
            lon = wx * 360 - 180
            lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * wy))))
            kind, flags = classify(layer, props)
            found.append((name, lon, lat, kind, flags, RANK[kind]))
    return found


def read_extra(path: str):
    out = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            name, lon, lat, kind = [c.strip() for c in line.split(",")]
            k = KIND_NAMES[kind]
            flags = FLAG_WATER if kind == "water" else FLAG_TOILET if kind == "toilet" else 0
            out.append((name, float(lon), float(lat), k, flags, LANDMARK_RANK))
    return out


def dedupe(places):
    """The same name close by is one place; the most important entry stays."""
    kept = []
    for p in sorted(places, key=lambda p: (-p[5], p[0], p[1], p[2])):
        if any(q[0] == p[0] and metres_between(q[1], q[2], p[1], p[2]) < SAME_PLACE_M for q in kept):
            continue
        kept.append(p)
    return sorted(kept, key=lambda p: (p[0], p[1], p[2], p[3]))


# ----------------------------------------------------------------------------------- output ---
def build(places) -> bytes:
    strings = bytearray()
    offsets: dict[str, int] = {}

    def intern(s: str) -> int:
        if s not in offsets:
            offsets[s] = len(strings)
            strings.extend(s.encode("utf-8") + b"\0")
        return offsets[s]

    records = []
    pairs = set()
    for pid, place in enumerate(places):
        name, lon, lat, kind, flags, rank = place[:6]
        records.append((round(lon * 1e7), round(lat * 1e7), intern(name), kind, rank, flags))
        for k in keys_of(name):
            pairs.add((k.encode("utf-8"), pid))
        for other in (place[6] if len(place) > 6 else ()):
            for k in keys_of(other):
                pairs.add((k.encode("utf-8"), pid))
    ordered = sorted(pairs)
    key_table = [(intern(k.decode("utf-8")), pid) for k, pid in ordered]

    lons = [r[0] for r in records] or [0]
    lats = [r[1] for r in records] or [0]
    head = struct.pack("<4sHHIII4i", MAGIC, VERSION, HEADER_BYTES, len(records), len(key_table), len(strings),
                       min(lons), min(lats), max(lons), max(lats))
    assert len(head) == HEADER_BYTES
    body = b"".join(struct.pack("<iiIBBH", *r) for r in records)
    body += b"".join(struct.pack("<II", *k) for k in key_table)
    return head + body + bytes(strings)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", help="the index to write (places.pidx)")
    ap.add_argument("--tiles", help="vector tiles (.mbtiles) to read the places from")
    ap.add_argument("--pbf", help="OpenStreetMap extract (.osm.pbf) to read the places from")
    ap.add_argument("--bbox", help="west,south,east,north: keep only the places inside")
    ap.add_argument("--zoom", type=int, help="tile zoom to read (default: the highest in the file)")
    ap.add_argument("--extra", help="CSV of name,lon,lat,kind lines to add (kind: place sight food shop bike water toilet transit hotel medical other)")
    a = ap.parse_args()
    if not a.tiles and not a.pbf:
        sys.exit("give --tiles or --pbf")
    bbox = tuple(float(v) for v in a.bbox.split(",")) if a.bbox else None
    places = []
    if a.tiles:
        places += [p for p in read_tiles(a.tiles, a.zoom) if not bbox or (bbox[0] <= p[1] <= bbox[2] and bbox[1] <= p[2] <= bbox[3])]
    if a.pbf:
        places += read_pbf(a.pbf, bbox)
    n_tiles = len(places)
    if a.extra:
        places += [p for p in read_extra(a.extra) if not bbox or (bbox[0] <= p[1] <= bbox[2] and bbox[1] <= p[2] <= bbox[3])]
    places = dedupe(places)
    data = build(places)
    with open(a.out, "wb") as f:
        f.write(data)
    kinds: dict[int, int] = {}
    for p in places:
        kinds[p[3]] = kinds.get(p[3], 0) + 1
    print(f"{len(places)} places ({n_tiles} read), {len(data) / 1024:.0f} KiB; kinds "
          + " ".join(f"{k}={v}" for k, v in sorted(kinds.items())), file=sys.stderr)


if __name__ == "__main__":
    main()
