import math
from typing import Tuple
from dataclasses import dataclass

@dataclass
class Location:
    longitude: float
    latitude: float

    def get_tile_x_y(self, zoom) -> Tuple[int, int]:
        x = math.floor((self.longitude + 180) / 360 * math.pow(2, zoom))
        y = math.floor((1 - math.log(math.tan(self.latitude * math.pi / 180) +
                                     1 / math.cos(self.latitude * math.pi / 180))/math.pi) / 2 * math.pow(2, zoom))
        return x, y


if __name__ == '__main__':
    loc = Location(121.49261, 31.241)
    print(loc.get_tile_x_y(13))
