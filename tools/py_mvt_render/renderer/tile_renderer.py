from PIL import Image, ImageDraw
from pb2.vector_tile_pb2 import Tile
from typing import Dict, Union, Tuple
from .style import MapStyle

FEATURE_PROP_VALUE = Union[str, float, int, bool, None]
FEATURE_PROP = Dict[str, FEATURE_PROP_VALUE]


class TileUtil:
    @staticmethod
    def tile_value_to_py_type(value: Tile.Value) -> FEATURE_PROP_VALUE:
        if value.string_value is not None:
            return value.string_value
        elif value.float_value != 0:
            return value.float_value
        elif value.double_value  != 0:
            return value.double_value
        elif value.int_value  != 0:
            return value.int_value
        elif value.uint_value  != 0:
            return value.uint_value
        elif value.sint_value  != 0:
            return value.sint_value
        elif value.bool_value  != 0:
            return value.bool_value
        return None

    @staticmethod
    def build_feature_prop(layer: Tile.Layer, feature: Tile.Feature) -> FEATURE_PROP:
        ret = {}
        for i, tag in enumerate(feature.tags):
            if i % 2 != 0:
                continue
            ret[layer.keys[tag]] = TileUtil.tile_value_to_py_type(
                layer.values[feature.tags[i + 1]]
            )
            # ret[layer.keys[tag]] = layer.values[feature.tags[i + 1]]
            
        return ret

    @staticmethod
    def zigzag(value: int) -> int:
        return (value >> 1) ^ (-(value & 1))

    @staticmethod
    def unpack_feature_geometry_cmd(cmd) -> Tuple[int, int]:
        return cmd & 0x7, cmd >> 3


class TileRenderer:
    def __init__(
        self,
        tile_raw: Tile,
        render_size: int,
        map_style: MapStyle,
        zoom_level: int,
        color_mode: str = "RBGA",
    ):
        self._tile_raw = tile_raw
        self._render_size = render_size
        self._style = map_style
        self._img = Image.new(
            "RGBA", (self._render_size, self._render_size), self._style.get_backgroud()
        )
        self._zoom_level = zoom_level
        self._draw = ImageDraw.Draw(self._img, "RGBA")

    def render(self):
        self._render_tile(self._tile_raw)

    def export_to_image(self, file_path: str):
        self._img.save(file_path)

    def _remap_to_render_size(self, layer: Tile.Layer, value: Union[float, int]) -> Union[float, int]:
        return value * self._render_size / layer.extent

    def _render_tile(self, tile: Tile):
        for layer in tile.layers:
            self._render_layer(tile, layer)

    def _render_layer(
        self,
        tile: Tile,
        layer: Tile.Layer,
    ):
        for feature in layer.features:
            self._render_feature(tile, layer, feature)

    def _render_feature(self, tile: Tile, layer: Tile.Layer, feature: Tile.Feature):
        prop = TileUtil.build_feature_prop(layer, feature)
        if layer.name not in {"building", }:
            print(layer.name, feature.type, len(feature.geometry ), prop)
        if not self._style.get_feature_showable(layer, prop):
            return
        if feature.type == Tile.POLYGON:
            self._render_feature_polygon(tile, layer, feature, prop)
        elif feature.type == Tile.LINESTRING:
            self._render_feature_line(tile, layer, feature, prop)
        elif feature.type == Tile.POINT:
            self._render_feature_point(tile, layer, feature, prop)
    
    def _render_feature_polygon(
        self, tile: Tile, layer: Tile.Layer, feature: Tile.Feature, prop: FEATURE_PROP
    ):
        polygon_point_list = []
        o_x, o_y, x, y = 0, 0, 0, 0
        i = 0
        while i < len(feature.geometry):
            cmd, count = TileUtil.unpack_feature_geometry_cmd(feature.geometry[i])
            i += 1
            if cmd == 1:  # move to
                x = x + self._remap_to_render_size(
                    layer, TileUtil.zigzag(feature.geometry[i])
                )
                y = y + self._remap_to_render_size(
                    layer, TileUtil.zigzag(feature.geometry[i + 1])
                )
                i += 2
                o_x, o_y = x, y
                polygon_point_list.append((o_x, o_y))
            elif cmd == 2:  # Line to
                for _ in range(count):
                    x = x + self._remap_to_render_size(
                        layer, TileUtil.zigzag(feature.geometry[i])
                    )
                    y = y + self._remap_to_render_size(
                        layer, TileUtil.zigzag(feature.geometry[i + 1])
                    )
                    i += 2
                    polygon_point_list.append((x, y))
            elif cmd == 7:  # close to
                x, y = o_x, o_y
                polygon_point_list.append((o_x, o_y))
                self._draw.polygon(
                    polygon_point_list,
                    fill=self._style.get_fill_color(self._zoom_level, layer, prop),
                )
                polygon_point_list.clear()

    def _render_feature_line(
        self, tile: Tile, layer: Tile.Layer, feature: Tile.Feature, prop: FEATURE_PROP
    ):
        o_x, o_y, x, y = 0, 0, 0, 0
        i = 0
        width, color = self._style.get_line_width_color(self._zoom_level, layer, prop)
        width = self._remap_to_render_size(layer, width)
        while i < len(feature.geometry):
            cmd, count = TileUtil.unpack_feature_geometry_cmd(feature.geometry[i])
            i += 1
            if cmd == 1:  # move to
                x = x + self._remap_to_render_size(
                    layer, TileUtil.zigzag(feature.geometry[i])
                )
                y = y + self._remap_to_render_size(
                    layer, TileUtil.zigzag(feature.geometry[i + 1])
                )
                i += 2
                o_x, o_y = x, y
            elif cmd == 2:  # Line to
                for _ in range(count):
                    x = x + self._remap_to_render_size(
                        layer, TileUtil.zigzag(feature.geometry[i])
                    )
                    y = y + self._remap_to_render_size(
                        layer, TileUtil.zigzag(feature.geometry[i + 1])
                    )
                    i += 2
                    self._draw.line([(o_x, o_y), (x, y)], fill=color, width=int(width))
                    o_x, o_y = x, y

    def _render_feature_point(
        self, tile: Tile, layer: Tile.Layer, feature: Tile.Feature, prop: FEATURE_PROP
    ):
        x, y = (0,0)
        i = 0
        while i < len(feature.geometry):
            cmd, count = TileUtil.unpack_feature_geometry_cmd(feature.geometry[i])
            i += 1
            if cmd == 1:  # move to
                x = x + self._remap_to_render_size(
                    layer, TileUtil.zigzag(feature.geometry[i])
                )
                y = y + self._remap_to_render_size(
                    layer, TileUtil.zigzag(feature.geometry[i + 1])
                )
                i += 2
                self._draw.point((x, y), fill="red")
                t = self._remap_to_render_size(layer, 20)
                self._draw.ellipse([(x - t, y - t), (x + t, y + t)], fill="green", width=0)
