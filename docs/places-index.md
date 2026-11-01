# Place search index (`places.pidx`)

`tools/build_places.py` turns the vector tiles of a region into the index the firmware searches
(`App/Service/Journey/PlaceIndex.*` in X-TRACK-EVO-PLUS). The file format, the key rules and how a query
is ranked are in the header of the script and in `Docs/OfflineNavigation_Design.md` (4.2) of the firmware.

    pip install pypinyin
    python3 tools/build_places.py tests/data/beijing-mini.mbtiles tests/data/places.pidx --extra tests/data/landmarks.csv

* Places are the point features of the `pois`, `place_labels` and `public_transport` layers (Shortbread schema).
  The same name within 150 m is one place.
* Every place gets keys for its full pinyin, its initials (one key per reading for characters with several) and
  its lower-cased name, so the device needs no pinyin table. Output is deterministic.
* Shortbread has no labels for parks and palaces; `--extra` adds `name,lon,lat,kind` lines by hand. A real data
  run reads OpenStreetMap itself, where these areas have names.
* `tests/data/places.pidx` is the index of `beijing-mini.mbtiles` (with `landmarks.csv`); the firmware's tests and
  the demo board use it.
