from dataclasses import dataclass
from enum import StrEnum
from typing import Optional


class  StyleLayerType(StrEnum):
    FILL = 'fill'
    LINE = 'line'
    SYMBOL = 'symbol'
    CIRCLE = 'circle'
    HEATMAP = 'heatmap'
    FILL_EXTRUSION = 'fill-extrusion'
    RASTER = 'raster'
    HILLSHADE = 'hillshade'
    MODEL = 'model'
    BACKGROUND = 'background'
    SKY = 'sky'
    SLOT = 'slot'

class StyleLayerFilter:
    pass

class StyleLayerPaint:
    pass

@dataclass
class StyleLayer:
    layer_type: StyleLayerType
    filter: Optional[StyleLayerFilter]
    layout: Optional[str]
    minzomm: Optional[int]
    maxzoom: Optional[int]
    metadata: Optional[str]
    paint: Optional[StyleLayerPaint]
    slot: Optional[str]
    source: Optional[str]

