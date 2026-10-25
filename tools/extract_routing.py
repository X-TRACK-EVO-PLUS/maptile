#!/usr/bin/env python3
"""
Extract routing network from OSM XML and output .rng binary files.

Usage:
    python3 tools/extract_routing.py tools/beijing_roads.osm example/resource/routing --zoom 10
"""

import argparse
import math
import os
import struct
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

# --- Constants ---

TILE_SIZE = 256

HIGHWAY_CLASS = {
    "motorway": 0, "motorway_link": 0,
    "trunk": 1, "trunk_link": 1,
    "primary": 2, "primary_link": 2,
    "secondary": 3, "secondary_link": 3,
    "tertiary": 4, "tertiary_link": 4,
    "residential": 5, "living_street": 5, "unclassified": 5,
    "cycleway": 6,
    "path": 7, "track": 7, "pedestrian": 7,
}

SURFACE_MAP = {
    "paved": 0, "asphalt": 0, "concrete": 0, "concrete:plates": 0,
    "concrete:lanes": 0, "paving_stones": 0, "sett": 0, "cobblestone": 0,
    "metal": 0, "wood": 0,
    "unpaved": 1, "compacted": 1, "fine_gravel": 1, "dirt": 1,
    "earth": 1, "mud": 1, "sand": 1, "grass": 1,
    "gravel": 2, "pebblestone": 2,
}

BICYCLE_ACCESS = {
    "no": 0, "dismount": 1, "yes": 2, "designated": 3, "permissive": 2,
}


# --- Tile math (Web Mercator) ---

def lat_lon_to_pixel(lat, lon, zoom):
    """Convert lat/lon to pixel XY at given zoom level."""
    map_size = TILE_SIZE << zoom
    x = (lon + 180.0) / 360.0
    sin_lat = math.sin(lat * math.pi / 180.0)
    y = 0.5 - math.log((1 + sin_lat) / (1 - sin_lat)) / (4 * math.pi)
    px = int(min(max(x * map_size + 0.5, 0), map_size - 1))
    py = int(min(max(y * map_size + 0.5, 0), map_size - 1))
    return px, py


def pixel_to_tile(px, py):
    return px // TILE_SIZE, py // TILE_SIZE


def haversine_m(lat1, lon1, lat2, lon2):
    """Haversine distance in meters."""
    R = 6371000
    dlat = math.radians(lat2 - lat1)
    dlon = math.radians(lon2 - lon1)
    a = (math.sin(dlat / 2) ** 2 +
         math.cos(math.radians(lat1)) * math.cos(math.radians(lat2)) *
         math.sin(dlon / 2) ** 2)
    return R * 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))


# --- OSM Parsing ---

def parse_osm(filepath):
    """Parse OSM XML, return nodes dict and ways list."""
    print(f"Parsing {filepath}...")
    tree = ET.parse(filepath)
    root = tree.getroot()

    nodes = {}
    for elem in root.findall("node"):
        nid = int(elem.get("id"))
        lat = float(elem.get("lat"))
        lon = float(elem.get("lon"))
        nodes[nid] = (lat, lon)

    ways = []
    for elem in root.findall("way"):
        tags = {t.get("k"): t.get("v") for t in elem.findall("tag")}
        hw = tags.get("highway")
        if hw not in HIGHWAY_CLASS:
            continue

        nd_refs = [int(nd.get("ref")) for nd in elem.findall("nd")]
        # Filter out refs with missing nodes
        nd_refs = [r for r in nd_refs if r in nodes]
        if len(nd_refs) < 2:
            continue

        oneway = tags.get("oneway", "no") in ("yes", "true", "1", "-1")
        reverse = tags.get("oneway") == "-1"
        surface = SURFACE_MAP.get(tags.get("surface", ""), 3)
        bicycle = tags.get("bicycle", "")
        if bicycle in BICYCLE_ACCESS:
            bike_access = BICYCLE_ACCESS[bicycle]
        elif hw == "motorway" or hw == "motorway_link":
            bike_access = 0
        elif hw == "cycleway":
            bike_access = 3
        else:
            bike_access = 2

        bridge = 1 if tags.get("bridge", "no") != "no" else 0
        tunnel = 1 if tags.get("tunnel", "no") != "no" else 0

        ways.append({
            "nodes": nd_refs,
            "road_class": HIGHWAY_CLASS[hw],
            "oneway": oneway,
            "reverse": reverse,
            "surface": surface,
            "bicycle_access": bike_access,
            "bridge": bridge,
            "tunnel": tunnel,
        })

    print(f"  Parsed {len(nodes)} nodes, {len(ways)} ways")
    return nodes, ways


# --- Graph Building ---

def build_graph(nodes, ways):
    """
    Build a routing graph. Split ways at intersections.
    Returns: graph_nodes dict {node_id: (lat, lon)}, edges list.
    """
    # Count how many ways reference each node -> intersections have count > 1
    node_usage = defaultdict(int)
    for way in ways:
        for nid in way["nodes"]:
            node_usage[nid] += 1
        # Endpoints are always graph nodes
        node_usage[way["nodes"][0]] += 1
        node_usage[way["nodes"][-1]] += 1

    # Graph nodes = intersections + endpoints
    graph_node_ids = set()
    for way in ways:
        graph_node_ids.add(way["nodes"][0])
        graph_node_ids.add(way["nodes"][-1])
        for nid in way["nodes"][1:-1]:
            if node_usage[nid] > 1:
                graph_node_ids.add(nid)

    # Build edges by splitting ways at graph nodes
    edges = []
    for way in ways:
        wnodes = way["nodes"]
        seg_start = 0

        for i in range(1, len(wnodes)):
            if wnodes[i] in graph_node_ids:
                # Compute segment distance
                dist = 0.0
                for j in range(seg_start, i):
                    lat1, lon1 = nodes[wnodes[j]]
                    lat2, lon2 = nodes[wnodes[j + 1]]
                    dist += haversine_m(lat1, lon1, lat2, lon2)

                dist_m = int(min(dist, 65535))
                flags = _make_flags(way)

                # Forward edge
                edges.append((wnodes[seg_start], wnodes[i], dist_m, flags))

                # Reverse edge (if not oneway)
                if not way["oneway"]:
                    edges.append((wnodes[i], wnodes[seg_start], dist_m, flags))
                elif way["reverse"]:
                    # oneway=-1: reverse direction only
                    edges.pop()  # remove forward
                    edges.append((wnodes[i], wnodes[seg_start], dist_m, flags))

                seg_start = i

    graph_nodes = {nid: nodes[nid] for nid in graph_node_ids}
    print(f"  Graph: {len(graph_nodes)} nodes, {len(edges)} edges")
    return graph_nodes, edges


def _make_flags(way):
    flags = 0
    if way["oneway"]:
        flags |= (1 << 0)
    flags |= (way["road_class"] & 0x7) << 1
    flags |= (way["surface"] & 0x3) << 4
    if way["bridge"]:
        flags |= (1 << 6)
    if way["tunnel"]:
        flags |= (1 << 7)
    flags |= (way["bicycle_access"] & 0x3) << 8
    return flags


# --- Tile Splitting ---

def split_into_tiles(graph_nodes, edges, zoom):
    """Split graph into tiles at given zoom level."""
    # Assign each node to a tile
    node_tile = {}
    for nid, (lat, lon) in graph_nodes.items():
        px, py = lat_lon_to_pixel(lat, lon, zoom)
        tx, ty = pixel_to_tile(px, py)
        node_tile[nid] = (tx, ty)

    # Group nodes and edges by tile
    tile_nodes = defaultdict(set)
    tile_edges = defaultdict(list)
    border_edges = []  # (from_tile, to_tile, from_node, to_node, dist, flags)

    for nid, tile in node_tile.items():
        tile_nodes[tile].add(nid)

    for src, dst, dist, flags in edges:
        src_tile = node_tile.get(src)
        dst_tile = node_tile.get(dst)
        if src_tile is None or dst_tile is None:
            continue
        if src_tile == dst_tile:
            tile_edges[src_tile].append((src, dst, dist, flags))
        else:
            # Cross-tile edge: add to source tile as border
            tile_edges[src_tile].append((src, dst, dist, flags))
            tile_nodes[src_tile].add(dst)  # need dst node coords in src tile too
            border_edges.append((src_tile, dst_tile, src, dst, dist, flags))

    print(f"  Split into {len(tile_nodes)} tiles, {len(border_edges)} cross-tile edges")
    return tile_nodes, tile_edges, border_edges, node_tile


# --- .rng Binary Output ---

RNG_MAGIC = b"RNG1"
RNG_VERSION = 1
RNG_HEADER_SIZE = 36  # 4 + 2+2 + 4+4+4 + 4+4+4+4


def write_rng(filepath, graph_nodes, tile_node_ids, tile_edge_list,
              border_list, node_tile_map, tile_xy):
    """Write a single .rng file for one tile."""
    # Build local node index
    local_nodes = sorted(tile_node_ids)
    node_to_local = {nid: i for i, nid in enumerate(local_nodes)}

    # Build adjacency: for each local node, list of edges
    adj = defaultdict(list)
    for src, dst, dist, flags in tile_edge_list:
        if src in node_to_local and dst in node_to_local:
            adj[node_to_local[src]].append((node_to_local[dst], dist, flags))

    # Build edge table with next_edge linked list
    edge_table = []  # (target, dist, flags, elev_gain, next_edge_offset)
    node_first_edge = [0xFFFFFFFF] * len(local_nodes)

    for local_id in range(len(local_nodes)):
        edges_for_node = adj.get(local_id, [])
        for i, (target, dist, flags) in enumerate(edges_for_node):
            edge_idx = len(edge_table)
            if node_first_edge[local_id] == 0xFFFFFFFF:
                node_first_edge[local_id] = edge_idx
            else:
                # Link previous edge to this one
                prev_idx = edge_idx - 1
                # Find the last edge for this node and update its next
                # Walk back to set next_edge
                pass
            edge_table.append([target, dist, flags, 0, 0xFFFF])

        # Set up linked list
        node_edges_start = node_first_edge[local_id]
        if node_edges_start != 0xFFFFFFFF:
            count = len(edges_for_node)
            for j in range(count - 1):
                edge_table[node_edges_start + j][4] = 1  # offset to next
            edge_table[node_edges_start + count - 1][4] = 0xFFFF  # end

    # Build border node entries
    # For cross-tile edges, we store the local source node index,
    # the destination tile coords, and the destination node's lon/lat
    # (encoded as int32 * 1e7) so the router can find it in the remote tile.
    border_entries = []
    for src_tile, dst_tile, src_node, dst_node, dist, flags in border_list:
        if src_tile == tile_xy and src_node in node_to_local:
            dst_lat, dst_lon = graph_nodes[dst_node]
            border_entries.append((
                node_to_local[src_node],
                dst_tile[0], dst_tile[1],
                int(dst_lon * 1e7),  # remote node lon
                int(dst_lat * 1e7),  # remote node lat
            ))

    # Compute bbox
    lats = []
    lons = []
    for nid in local_nodes:
        lat, lon = graph_nodes[nid]
        lats.append(lat)
        lons.append(lon)

    if not lats:
        return

    min_lon = int(min(lons) * 1e7)
    min_lat = int(min(lats) * 1e7)
    max_lon = int(max(lons) * 1e7)
    max_lat = int(max(lats) * 1e7)

    node_count = len(local_nodes)
    edge_count = len(edge_table)
    border_count = len(border_entries)

    os.makedirs(os.path.dirname(filepath), exist_ok=True)

    with open(filepath, "wb") as f:
        # Header (32 bytes)
        f.write(RNG_MAGIC)
        f.write(struct.pack("<HH", RNG_VERSION, RNG_HEADER_SIZE))
        f.write(struct.pack("<III", node_count, edge_count, border_count))
        f.write(struct.pack("<iiii", min_lon, min_lat, max_lon, max_lat))

        # Node table (12 bytes each)
        for i, nid in enumerate(local_nodes):
            lat, lon = graph_nodes[nid]
            f.write(struct.pack("<iiI",
                                int(lon * 1e7),
                                int(lat * 1e7),
                                node_first_edge[i]))

        # Edge table (12 bytes each)
        for target, dist, flags, elev, next_off in edge_table:
            f.write(struct.pack("<IHHhH",
                                target,
                                min(dist, 65535),
                                flags,
                                elev,
                                next_off))

        # Border nodes (20 bytes each: local_node_id, tile_x, tile_y, remote_lon, remote_lat)
        for node_id, tx, ty, remote_lon, remote_lat in border_entries:
            f.write(struct.pack("<IHHI",
                                node_id,
                                tx, ty,
                                remote_lon & 0xFFFFFFFF))
            f.write(struct.pack("<i", remote_lat))


def main():
    parser = argparse.ArgumentParser(description="Extract routing network from OSM XML")
    parser.add_argument("input", help="Input OSM XML file")
    parser.add_argument("output_dir", help="Output directory for .rng files")
    parser.add_argument("--zoom", type=int, default=10, help="Tile zoom level (default: 10)")
    args = parser.parse_args()

    nodes, ways = parse_osm(args.input)
    graph_nodes, edges = build_graph(nodes, ways)
    tile_nodes, tile_edges, border_edges, node_tile = split_into_tiles(
        graph_nodes, edges, args.zoom)

    total_nodes = 0
    total_edges = 0
    total_files = 0
    total_bytes = 0

    for (tx, ty), node_ids in sorted(tile_nodes.items()):
        edge_list = tile_edges.get((tx, ty), [])
        borders = [b for b in border_edges if b[0] == (tx, ty)]

        filepath = os.path.join(args.output_dir, str(args.zoom), str(tx), f"{ty}.rng")
        write_rng(filepath, graph_nodes, node_ids, edge_list,
                  borders, node_tile, (tx, ty))

        if os.path.exists(filepath):
            size = os.path.getsize(filepath)
            total_bytes += size
            total_nodes += len(node_ids)
            total_edges += len(edge_list)
            total_files += 1
            print(f"  {filepath}: {len(node_ids)} nodes, {len(edge_list)} edges, {size} bytes")

    print(f"\nSummary:")
    print(f"  Files: {total_files}")
    print(f"  Total nodes: {total_nodes}")
    print(f"  Total edges: {total_edges}")
    print(f"  Total size: {total_bytes / 1024:.1f} KB")


if __name__ == "__main__":
    main()
