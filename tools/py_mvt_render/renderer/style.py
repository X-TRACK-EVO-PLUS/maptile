from pb2.vector_tile_pb2 import Tile
from typing import Dict, Union, Tuple, List

FEATURE_PROP_VALUE = Union[str, float, int, bool, None]
FEATURE_PROP = Dict[str, FEATURE_PROP_VALUE]


class MapStyle:
    def __init__(self):
        self._layer_fill_color = {
            "water": lambda a, b: "hsl(205, 76%, 67%)",
            "landuse": self._get_layer_landusb_fill_color,
            "building": self._get_layer_building_fill_color,
        }
        self._landuse_fill_color_class_type = {
            "park": "hsl(78, 51%, 73%)",
            "pitch": "hsl(78, 51%, 73%)",
            "hospital": "hsl(0, 56%, 89%)",
            "school": "hsl(25, 45%, 85%)",
            "default": "hsl(0, 0%, 100%)",
        }
        self._road_line_width = {
            "motorway": ((5, 0.5), (12, 3), (18, 30)),
            "trunk": ((5, 0.5), (12, 3), (18, 30)),
            "primary": ((5, 0.5), (12, 3), (18, 30)),
            "secondary": ((5, 0.01), (12, 2), (18, 24)),
            "tertiary": ((5, 0.01), (12, 2), (18, 24)),
            "street": ((5, 0), (12, 0.5), (18, 24)),
            "street_limited": ((5, 0), (12, 0.5), (18, 12)),
            "motorway_link": ((5, 0), (12, 0.5), (18, 12)),
            "service": ((5, 0), (12, 0), (18, 10)),
            "track": ((5, 0), (12, 0), (18, 10)),
            "link": ((5, 0), (12, 0), (18, 10)),
            "default": ((5, 0), (12, 0), (18, 10)),
        }
        self._road_line_color = {
            "street": "hsl(38, 100%, 98%)",
            "street_limited": "hsl(38, 100%, 98%)",
            "service": "hsl(38, 100%, 98%)",
            "track": "hsl(38, 100%, 98%)",
            "link": "hsl(38, 100%, 98%)",
            "default": "hsl(0, 0%, 100%)",
        }
        self._road_color = ["red", "blue", "orange", "green", "purple", "yellow"]

    def get_backgroud(self) -> str:
        return "hsl(38, 43%, 89%)"

    def get_layer_type(self, layer: Tile.Layer) -> str:
        if layer.name in {
            "building", 'landuse'
        }:
            return "fill"
        if layer.name in {
            "road",
        }:
            return "line"

    @staticmethod
    def map(map_data: List[Tuple[int, int]], zoom) -> float:
        if zoom < map_data[0][0]:
            return map_data[0][1]
        if zoom >= map_data[-1][0]:
            return map_data[-1][1]
        for i in range(len(map_data) -1 ):
            if map_data[i][0] <= zoom < map_data[i + 1][0]:
                return map_data[i][1] + (zoom - map_data[i][0]) * (map_data[i + 1][1] - map_data[i][1]) / (map_data[i + 1][0] - map_data[i][0])
        return 0.0

    def get_line_width_color(
        self, zoom: int, layer: Tile.Layer, feature_prop: FEATURE_PROP
    ) -> Tuple[float, str]:
        if layer.name == "road":
            return self._get_road_line_width_color(zoom, layer, feature_prop)
        return 0, "black"

    def _get_road_line_width_color(self, zoom: int, layer:Tile.Layer, feature_prop: FEATURE_PROP) -> Tuple[float, str]:

        map_data = self._road_line_width[feature_prop['class']] if feature_prop["class"] in self._road_line_width else self._road_line_width["default"]
        width = MapStyle.map(map_data, zoom) * 2
        color = self._road_line_color[feature_prop['class']] if feature_prop['class'] in self._road_line_color else self._road_line_color['default']
        color = self._road_color[0]
        self._road_color = self._road_color[1:] + self._road_color[:1]
        return width, color

    def get_fill_color(
        self, zoom: int, layer: Tile.Layer, feature_prop: FEATURE_PROP
    ) -> str:
        if layer.name in self._layer_fill_color:
            return self._layer_fill_color[layer.name](layer, feature_prop)
        return self._get_default_fill_color(layer, feature_prop)

    def _get_default_fill_color(
        self, layer: Tile.Layer, feature_prop: FEATURE_PROP
    ) -> str:
        return "red"

    def _get_layer_landusb_fill_color(
        self, layer: Tile.Layer, feature_prop: FEATURE_PROP
    ) -> str:
        if feature_prop["class"] in self._landuse_fill_color_class_type:
            return self._landuse_fill_color_class_type[feature_prop["class"]]
        return self._landuse_fill_color_class_type["default"]

    def _get_layer_building_fill_color(
        self, layer: Tile.Layer, feature_prop: FEATURE_PROP
    ) -> str:
        if (
            feature_prop["type"] != "building:part"
            and feature_prop["underground"] == "false"
        ):
            return "hsl(38, 28%, 77%)"
        return self._get_default_fill_color(layer, feature_prop)

    def get_feature_showable(self, layer: Tile.Layer, feature_prop: FEATURE_PROP) -> bool:
        if layer.name == 'road' and feature_prop["type"] in {"traffic_signals", "waterway"}:
            return False
        if layer.name == 'poi_label':
            return False
        return True
        