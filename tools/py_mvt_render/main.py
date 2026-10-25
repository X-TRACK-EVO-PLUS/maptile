from pb2.vector_tile_pb2 import Tile
from renderer.tile_renderer import TileRenderer, TileUtil
from renderer.style import MapStyle


def main(file):
    with open(file, "rb") as f:
        buf = f.read()
    tile = Tile()
    tile.ParseFromString(buf)
    style = MapStyle()
    renderer = TileRenderer(tile, 4096, style, zoom_level=13)
    renderer.render()
    renderer.export_to_image("x.png")
    # type_set = set()
    # for layer in tile.layers:
    #     if layer.name == "poi_label":
    #         for feature in layer.features:
    #             prop = TileUtil.build_feature_prop(layer, feature)
    #             # if "type" in prop:
    #                 # type_set.add(prop["type"])
    #             # if "sizerank" in prop:
    #             print("sizerank", prop["sizerank"].uint_value, "filterrank", prop["filterrank"].uint_value)
    #             # print(prop)
    # print(type_set)

if __name__ == "__main__":
    for file in [ "resource/26978_12415.mvt", ]:
        main(file)
