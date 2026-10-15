# oil3d-cloud — reference frames from the cloud smoke test (2026-10-15)

`contact.png`: six Oil3DWallpaper frames rendered by the CLOUD executor, not on the user's GPU:
mingw-w64 build of `src_oil3d/` (branch `oil3d`, the commit that added this folder), run under
wine 9 + vkd3d + Mesa lavapipe (CPU Vulkan) with Microsoft's `d3dcompiler_47.dll` (real fxc,
vs_5_1/ps_5_1). Every frame: `--seed 1234 --shot-delay 15 --shot-size 960x540`, SDR (`--hdr off`).

| tile | stack (`--ini`, in order) |
|---|---|
| home | `reference/configs/oil3d-default.ini` (= monotone-post-0924 + Layout - Few giants) |
| Magenta Cyan | monotone-post-0924, Layout - Few giants, Scheme - Magenta Cyan |
| Bright Triad | monotone-post-0924, Layout - Few giants, Scheme - Bright Triad |
| acid-rise-12 | acid-rise-12, Layout - Few giants, acid-rise-12-tone-half |
| two-hue | acid-rise-2hue, Layout - Few giants |
| flat | as Magenta Cyan, plus `--shade flat` |

Use: diff the first real-GPU shots of the same commands against these. Expect float-level
differences (different rasteriser / transcendental precision), not a different picture; a
different picture means a driver or build difference worth chasing. Like every screenshot in
this repo they are rough hue/composition checks, never luminance truth (CALIBRATION.md).
