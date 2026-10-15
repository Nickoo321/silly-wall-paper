# OilTank DUMP schema (version 1)

The local FluidWallpaper exe writes one JSON per frame it wants rebuilt in UE. OilTank reads it with
`-OilDump=<path>` (a still, a perf run, or a 10 s clip of that one frame) or a numbered sequence with
`-OilDumpDir=<dir>` (`dump_0000.json`, `dump_0001.json`, ... one per clip frame at `clip_fps`).
All paths are passed space-free on the command line. Paths INSIDE the json may contain spaces.

```json
{
  "version": 1,
  "t": 123.456,
  "width": 2560,
  "height": 1440,
  "film_rgb": [0.05, 0.30, 0.40],
  "accent_rgb": [1.0, 0.40, 0.80],
  "height_png": "G:\\UE\\OilTank\\Dumps\\mc-0001\\height.png",
  "film_png": "G:\\UE\\OilTank\\Dumps\\mc-0001\\film.png",
  "droplets": [ { "x": 0.41, "y": 0.27, "r": 0.004, "depth": 0.0 } ]
}
```

| key | type | meaning |
|---|---|---|
| `version` | int | `1`. Other values are read anyway with a warning. |
| `t` | number | Sim seconds of the frame. Motes are a pure function of time, and the still is taken exactly at `t`. |
| `width`, `height` | int | The frame's pixel grid (2560x1440). The images should match it; a mismatch is logged and the uv mapping is used anyway. |
| `film_rgb` | [r,g,b] | LINEAR, the lit film colour as it is displayed (the main colour of the three roles). It is used as the floor albedo when `film_png` is empty, and it tints the water when `water_color` is `"film"`. Values above 1 are moved into the light. |
| `accent_rgb` | [r,g,b] | Linear accent colour. Logged now and kept for later stages (rims, glints). |
| `height_png` | path | REQUIRED. A **16-bit greyscale PNG** of the oil film height `h` in 0..1, where 0 means no oil and 65535 means the thickest. Same pixel grid as the frame, row 0 at the top. Oil is wherever `h > oil_threshold` (json, default 0.02). Write it smooth: the analytic field, not a thresholded mask. The silhouette is the iso-line of this field, so 8-bit or binary input brings back terraces. Byte order is detected from the data. |
| `film_png` | path or `""` | Optional. An **8-bit sRGB PNG** of the film/ink frame WITHOUT the oil, on the same grid. It becomes the albedo of the floor (the receiver). With the calibrated light, an unshadowed floor pixel shows exactly this image. |
| `droplets` | array | Optional. `x`,`y` = centre in frame uv (0..1, origin top-left, y down). `r` = radius as a fraction of the frame **width**. `depth` = 0..1 fraction of `tank_depth_cm` below the surface. Droplets with `depth < droplet_min_depth` are assumed to be in `height_png` already. Deeper ones become black spheres in the water column. |

Relative image paths resolve against the folder of the dump json.

Coordinates in UE: the frame is centred at the origin on the water surface z = 0. Screen up = world
+X (the rise), screen right = world +Y, 1 uu = 1 cm. Frame uv (u,v) maps to world
X = (0.5 - v) * frame_height_cm and Y = (u - 0.5) * frame_width_cm.
