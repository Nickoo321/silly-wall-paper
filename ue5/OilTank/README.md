# OilTank: the UE 5.8 top-down tank for the Oil3D A/B

This is a headless UE 5.8 project with no assets, no .umap and no editor GUI. `AOilTankGameMode`
builds the "Ruling 2" scene (reference/briefs/COACH-oil3d-port-chat.md section 5) at BeginPlay
from `Config/OilTank.json` plus one FluidWallpaper DUMP (see [DUMP-SCHEMA.md](DUMP-SCHEMA.md)). It
captures a still or a 10 s clip and then exits. The recipe follows
reference/briefs/UE5-HEADLESS-LESSONS.md.

The C++ was written as text in a cloud session with **no compiler and no UE available**. It has
never been built. A local agent builds and renders it on Windows. Every API I could not check is
marked `// VERIFY` and listed below.

```
OilTank.uproject                 EngineAssociation 5.8, module OilTank, plugin ProceduralMeshComponent
Config/DefaultEngine.ini         map/gamemode, RT off, no GI, VSM, volumetric fog 4 px / 128 z, t.MaxFPS 72, DDC override
Config/DefaultGame.ini
Config/OilTank.json              every scene knob (table below)
Source/OilTank.Target.cs, OilTankEditor.Target.cs
Source/OilTank/OilTank.Build.cs  deps + the four OILTANK_* escape-hatch defines
Source/OilTank/OilTankGameMode.* the scene, capture, perf log, calibration readback
Source/OilTank/OilTankMesh.*     PNG loading (16-bit), marching-squares oil mesher, transient textures
Source/OilTank/OilTankSettings.* json + dump parsing
Tools/run-oiltank.ps1            the only sanctioned launcher (game check, 3-GPU-process rule, BelowNormal, timeout)
DUMP-SCHEMA.md                   what the FluidWallpaper exe must write
```

## Install, build, run

1. Copy `ue5/OilTank` to `G:\UE\OilTank`. Use G:, never D: (SMR) and never C: for the DDC.
2. **Build.** First time ~1 min here (UBA). Builds take no GPU slot; use the launcher:
   ```
   powershell -ExecutionPolicy Bypass -File G:\UE\OilTank\Tools\run-oiltank.ps1 -Build -TimeoutMinutes 60
   ```
   which runs exactly:
   ```
   G:\UE_5.8\Engine\Build\BatchFiles\Build.bat OilTankEditor Win64 Development -Project="G:\UE\OilTank\OilTank.uproject" -WaitMutex
   ```
   The output is `G:\UE\OilTank\Binaries\Win64\UnrealEditor-OilTank.dll`.
3. **Smoke test** with the synthetic field (no dump). The first launch compiles shaders for tens of minutes:
   ```
   powershell -ExecutionPolicy Bypass -File G:\UE\OilTank\Tools\run-oiltank.ps1 -Shot G:\UE\OilTank\Saved\Shots\synthetic.png -TimeoutMinutes 120
   ```
4. **A still from a dump:**
   ```
   powershell -ExecutionPolicy Bypass -File G:\UE\OilTank\Tools\run-oiltank.ps1 -Dump G:\UE\OilTank\Dumps\mc-0001\dump.json -Shot G:\UE\OilTank\Saved\Shots\mc-0001.png
   ```
   which runs exactly:
   ```
   G:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe "G:\UE\OilTank\OilTank.uproject" -game -RenderOffScreen -nosplash -unattended -nosound -log -ResX=2560 -ResY=1440 -OilDump=G:\UE\OilTank\Dumps\mc-0001\dump.json -OilShot=G:\UE\OilTank\Saved\Shots\mc-0001.png
   ```
5. **10 s clip** (300 PNGs at 30 fps of sim time; PNG writes make it slower than real time):
   add `-Clip G:\UE\OilTank\Saved\Clips\mc-0001`. With `-DumpDir <dir>` (`dump_0000.json` ...)
   the oil is rebuilt for every clip frame. With a single `-Dump`, only the motes move.
6. **Perf run.** Give `-Dump` and no `-Shot`/`-Clip`. It runs `perf_seconds` after warmup and then exits.

Other flags: `-Config <json>` sets `-OilConfig=` (a second json, e.g. a calibration variant),
`-ResX/-ResY`, `-IgnoreGames`, `-Priority`, `-Engine`, `-GpuLockScript`. Every `-Oil*` path must be
free of spaces; the script refuses otherwise.

Outputs:
- `Saved/Logs/OilTank.log`: the UE log. Our lines are `LogOilTank`, and they include the full
  settings summary plus the camera distance, f-stop, lux and mesh size.
- `Saved/Logs/oiltank-perf.txt`: one line per second with the frame number, phase, average and
  maximum frame ms, fps, GPU / game / render thread ms. The header line records whether
  FluidWallpaper.exe was running.
- `Saved/Logs/oiltank-calib.txt`: written after every still. It gives the per-channel median and
  1st percentile of the shot against the film target, and the `calib_gain` that would match.

## First-night checklist (in order)

1. The smoke test writes a PNG and no window ever appears.
2. **Calibration (trap 3).** Run with a json that sets `"debug_no_oil": true, "debug_no_motes": true`
   and either `"debug_no_water": true` or the default `calib_through_water: true`. Then
   `oiltank-calib.txt` reports `target/shot`. Set `calib_gain` to the suggested value; one
   iteration should be enough. It should come out close to 1 if my exposure formula is right.
3. **Black (three roles).** On a real dump, the oil pixels must be pure black: the `p01` in
   calib must be about (0,0,0). If the oil is grey, the water volume reaches above z = 0. Switch
   `water_mode` or fix `lfv_unit_radius_cm` (VERIFY list).
4. **Shafts.** Look at a 300% crop of open water. You should see darker streaks behind the masses,
   running along the light azimuth. If the water is uniformly lit, the volumetric fog is not
   receiving the VSM shadow; check `r.LocalFogVolume.RenderIntoVolumetricFog` or try `water_mode: "heightfog"`.
5. **Polygons.** Look at a 300% crop of an oil edge and of its floor shadow (see below).
6. **Perf.** Run once with FluidWallpaper running and say so in the report (lessons: the wallpaper costs UE about 60% of its fps).

## The scene (what is built, and how it answers the brief)

- **Units and geometry.** 1 uu = 1 cm. The frame is the water surface at z = 0, `frame_width_cm` x
  `frame_height_cm` (40 x 22.5), centred at the origin. Screen up = +X (the rise) and screen right
  = +Y. The water column runs down to z = -`tank_depth_cm` (100).
- **Floor = receiver.** A lit procedural quad at z = -depth. Its albedo is `film_png` (sRGB, mirror
  addressing) or `film_rgb`. The UVs are mapped so the floor shows the film exactly in frame,
  because perspective makes the floor's visible area larger by (L+D)/L. There is no skylight, no
  GI and no reflections, and any light, fog, sky or post actor that the Entry map carries is
  destroyed. The light is calibrated as `lux = pi * calib_gain * 1.2 / (sin(elev) * 2^bias * T)`,
  where T = exp(-tau/sin(elev)) * exp(-tau) is the water's extinction on the way down and back up
  (`calib_through_water`). An unshadowed white floor then reads 1.0 and a film floor reads as the film.
- **Oil occluder.** `height_png` is resampled (2x2 box prefilter) onto an `oil_grid` vertex grid
  (1280x720) that reaches the frame edges exactly. **Marching squares** extracts the
  `h > oil_threshold` region. The top is at z = 0, the bottom is the same triangulation at
  z = -h * `oil_thickness_cm`, and wall quads run along the iso-line, so the body is closed. The
  material is black, with specular 0, roughness 1, two-sided and two-sided shadows.
- **"No visible polygons": what I did.** The silhouette is the iso-line of the smooth field, with
  every crossing **linearly interpolated along the cell edge**. It is not the cell boundary, so
  there are no stair-steps. At 1280x720 on a 2560 px frame, each contour segment is at most ~2.8 px
  long, the turn between neighbouring segments is tiny for the smooth h the sim produces, and the
  stills are 2x supersampled (`aa: ssaa2`). Saddle cells are resolved by the cell-centre average,
  so necks do not flip or crack. The oil is flat black, so normals cannot show facets. The floor
  shadow is a projection of the same contour, softened by the VSM SMRT penumbra
  (`light_source_angle_deg`), so it is even smoother. Checked off-UE: 20 random fields gave no
  inverted triangles, a manifold top surface, and boundary edges exactly equal to the iso
  segments. If an edge still shows facets at 300%, raise `oil_grid` to [2560,1440], which is 4x
  the triangles.
- **Geometry fact the A/B must respect.** With a 1 m tank, a 22.5 cm frame and 55 deg elevation,
  the light's offset at the floor is 100 * cot(55) = 70 cm, which is 3 frame heights. **No in-frame
  oil can shadow the in-frame floor.** What shadows the visible floor is oil outside the frame. So
  `oil_surround: "mirror"` adds mirror-tiled copies of the oil (continuous across tile edges)
  wherever the shifted footprint, plus the penumbra, needs them. At the defaults that is a 4x4
  set. With `"none"` the floor is fully lit. **The hand shader must do the same thing** (mirror
  addressing for its `p + z*s` coverage lookups), or both sides must use `none`. Otherwise the A/B
  compares different scenes. Ruling 2 (e) already says that 1 m * cot(elev) lands far outside a ring.
- **Light.** One `ADirectionalLight`. Azimuth is in screen space: 0 = the light comes FROM screen
  right, 90 = from screen top, counter-clockwise. The default 210 means from lower-left, so
  shadows fall up and to the right. It is Movable, casts VSM shadows and contact shadows, has
  volumetric scattering intensity 1 with volumetric shadow on, and light-shaft occlusion on.
  Screen-space shafts only appear when the sun is on screen, which it never is top-down; the
  real shafts are volumetric. There are no other lights.
- **Water.** `water_mode: "localfog"` (default) puts an `ULocalFogVolumeComponent` squashed into a
  flat ellipsoid: centre z = -D, half-height D (so the top touches z = 0), and an XY radius of
  20x the lit reach (the top is flat to about 1 mm over the region that matters). It uses height
  fog with about 0 falloff, i.e. uniform density, and is rendered into the volumetric fog so the
  oil's VSM shadow carves the shafts. `"heightfog"` is the fallback: a global ExponentialHeightFog
  with ~uniform density, where `StartDistance` and `VolumetricFogStartDistance` equal the camera
  to surface distance, so the air above the water stays clear. That only works with the narrow
  camera. In both modes the colour is `film_rgb` normalised times `water_albedo`, the phase is
  `water_phase_g`, and the extinction is `water_optical_depth` over the column.
- **Motes.** 3000 `UInstancedStaticMeshComponent` spheres (/Engine/BasicShapes/Sphere) of 0.4 mm,
  which is about 2.5 px at focus. They are opaque (trap 6), cast no shadows, receive the oil's
  shadow and are lit only by the light. They are spread uniformly through the column
  (`mote_zmin_cm` to depth), rise up the screen at `mote_rise_cm_s`, wrap from the top back in at
  the bottom, and wander by `mote_jitter_cm`. Their positions are a pure function of sim time,
  so stills and clips are reproducible.
- **Droplets** in the dump with `depth >= droplet_min_depth` become black spheres in the column.
- **Camera.** Looks straight down (pitch -90, camera up = +X). `narrow` (default) uses a horizontal
  FOV of `camera_fov_deg` = 3 from L = (W/2)/tan(fov/2) = 764 cm, focused on the surface, with
  diaphragm (cinematic) DoF. The f-stop is solved from the thin-lens CoC so that a mote on the
  floor is `dof_px_at_floor` px across (24 px gives f/15.6 at the defaults); this matches our
  `dof_max_px` for the A/B. `ortho` uses OrthoWidth = frame width with DoF off (UE has no physical
  CoC in ortho), so it is not for the A/B.
- **Post (trap 2).** A code-spawned unbound `APostProcessVolume`: manual exposure (`exposure_bias`,
  physical-camera exposure off), `ToneCurveAmount = 0` (no filmic toe or shoulder), BlueCorrection
  0, ExpandGamut 0, local exposure neutral, and bloom, vignette, flare, motion blur, fringe and AO
  all 0. `film_grain` defaults to 0 because our grain is the reference. GrainQuantization (8-bit
  dither) and sharpen are off in the ini. The output is SDR sRGB; HDR on/off is not covered by UE
  here.
- **AA (trap 7).** `aa: "ssaa2"` (default): **no TAA/TSR, r.ScreenPercentage 200**, downsampled. The
  stills have no temporal smear on the motes. `"tsr"` is what a shipping real-time UE would use,
  so use it for the perf number. Always say which one was used.
- **Capture.** After `warmup_frames` (120; volumetric fog temporal history and VSM pages settle),
  `FScreenshotRequest` writes `-OilShot`. `-OilClip` writes `frame_0000.png` to `frame_0299.png`
  (`clip_frames` at `clip_fps`). The game then waits until every file exists with size > 0, writes
  the calibration line and exits.

## Every json knob (`Config/OilTank.json`)

| key | default | meaning |
|---|---|---|
| frame_width_cm / frame_height_cm | 40 / 22.5 | frame size at the water surface |
| tank_depth_cm | 100 | depth of the floor (receiver) |
| oil_grid | [1280,720] | mesher vertex grid (max 5120x2880) |
| oil_threshold | 0.02 | h iso value = oil boundary |
| oil_thickness_cm | 0.3 | bottom of the oil at -h * this |
| oil_surround | "mirror" | "mirror" tiles the oil around the frame for out-of-frame shadows; "none" |
| droplet_min_depth | 0.02 | dump droplets at least this deep become spheres |
| light_elev_deg / light_azim_deg | 55 / 210 | light direction (azimuth: 0 = from screen right, CCW) |
| light_source_angle_deg | 0.5357 | sun disc; penumbra = depth * tan(angle) |
| light_lux | 0 | 0 = auto-calibrated; >0 = fixed lux |
| calib_gain | 0.311 | multiply the auto lux (measured 2026-10-15: no-oil frame = film_rgb) |
| calib_through_water | true | compensate the water's extinction in the auto lux |
| light_volumetric_scattering | 1.0 | light's volumetric scattering intensity |
| contact_shadow_length | 0.05 | screen-space contact shadows (fraction of screen) |
| exposure_bias | 0 | manual exposure EV |
| film_grain | 0 | UE film grain intensity |
| aa | "ssaa2" | "ssaa2" / "tsr" / "taa" / "none" |
| water_mode | "heightfog" | "heightfog" (froxels only in the water) / "localfog" |
| water_optical_depth | 0.6 | vertical extinction tau of the full column |
| water_albedo | 0.9 | single-scattering albedo (times the normalised colour) |
| water_phase_g | 0.3 | Henyey-Greenstein g (top-down view is back-scatter: keep it low) |
| water_color | "film" | "film" or [r,g,b] |
| water_density_raw | 0 | >0 bypasses my unit guess (raw FogDensity or LFV extinction) |
| lfv_unit_radius_cm | - | removed: 5.8 fixes it at 500 cm |
| fog_grid_margin_cm | 5 | volumetric fog distance margin past the floor |
| floor_margin | 0.25 | extra floor beyond the visible area (each side, fraction) |
| floor_specular / floor_roughness | 0 / 1 | receiver BRDF |
| mote_count | 3000 | motes (0 = none) |
| mote_size_mm | 0.4 | diameter |
| mote_albedo | [0.8,0.8,0.8] | |
| mote_specular / mote_roughness | 0.3 / 0.4 | glints |
| mote_rise_cm_s | 0.6 | drift up the screen |
| mote_jitter_cm | 0.15 | smooth wander amplitude |
| mote_zmin_cm | 0.5 | shallowest mote below the surface |
| mote_seed | 1234 | |
| camera_mode | "narrow" | "narrow" / "ortho" |
| camera_fov_deg | 3 | narrow horizontal FOV |
| camera_distance_cm | 0 | >0 fixes the distance and derives the FOV |
| ortho_height_cm | 300 | ortho camera height |
| dof | true | cinematic DoF (narrow only) |
| aperture_fstop | 0 | 0 = solved from dof_px_at_floor |
| dof_px_at_floor | 24 | CoC (px) of a mote on the floor |
| sensor_width_mm | 24.89 | DoF sensor |
| dof_blade_count | 16 | 4..16 (16 is about round) |
| warmup_frames | 120 | frames before the capture |
| clip_frames / clip_fps | 300 / 30 | clip length and sim rate |
| still_sim_fps | 72 | sim step during warmup and perf runs |
| perf_seconds | 20 | perf-only run length |
| res_x | 2560 | DoF maths width (overridden by -ResX) |
| synthetic_blobs / synthetic_seed | 14 / 7 | test field when no dump is given |
| debug_no_oil / debug_no_water / debug_no_motes | false | for calibration and isolation |
| cvars | {...} | any cvar at runtime (default DepthDistributionScale 256: ~linear slices over the column) |
| exec_cmds | [] | console commands at BeginPlay, e.g. "viewmode lightingonly" (diagnostics) |

## Fairness traps (section 6), where each is handled

1. Same composition: the dump comes from the same `--shot`, and the UE still is taken at the dump's `t`.
2. Fixed exposure, no ACES curve, vignette or bloom (post volume above). Black level: our
   `shadow_floor` against UE's true 0; compare with HDR off and `sdr_gamut 0`.
3. Lit albedo receiver with calibrated light and no ambient, plus the calibration readback.
4. Opaque black oil with specular 0 and VSM. **Not done: the thin translucent edge layer.** Ours
   has one, so either switch ours off for the first A/B or accept the difference.
5. Volumetric fog GridPixelSize 4 and GridSizeZ 128 (ini). Light-shaft occlusion is on.
6. Motes are opaque meshes, so DoF blurs them. No tilt-shift.
7. SSAA2 without TSR for stills (say so); TSR for the perf number.
8. 10 s clips: `-Clip`, and `-DumpDir` to rebuild the oil every frame.
9. The shipping cost that is not in the picture: an extra UE process (GBs of VRAM, a shader
   cache), no WorkerW, no HDR path, no clocks/cycle/presets/Settings/identity. List all of it in
   the report.
10. DDC on G: (ini plus the launcher's `UE-LocalDataCachePath`).

## Local build results (2026-10-15, UE 5.8.3, VS BuildTools 17.14 / MSVC 14.44, SDK 10.0.26100)

Built and rendered on the user's RX 7900 GRE. First build 65 s (one compile-error class), rebuilds 5-8 s.
First -game launch 58 s (the engine's shipped DDC pak covers the global shaders; only the runtime
material compiled). Later runs ~10 s per still. DDC confirmed at G:\UE\OilTank\DerivedDataCache
(log: "Local: Using data cache path ../../../../UE/OilTank/DerivedDataCache: Writable"), using
ManhattanFPV's graph override (the README's original block was replaced).

Bugs found and fixed (each would have silently corrupted the A/B):
1. **Floor winding.** The floor quad (and the oil mesh's top faces) were emitted clockwise from above;
   UE showed the back face, the two-sided material flipped the up normal to -Z, and the floor got NO
   direct light. Every colour in the first renders was water in-scatter. Fixed: winding reversed.
2. **Light direction.** `SpawnActor<ADirectionalLight>(FTransform(rot))` came out as P=-79 Y=90 R=-180
   (light from screen LEFT at 79 deg instead of from the right at 55). Fixed with SetWorldRotation on the
   component after the mobility change; the log line `light: toLight=... travel(GetDirection)=...` proves it.
3. **Calibration.** calib_gain = 0.311 (verified: a no-oil, no-mote frame through the water shows film_rgb
   exactly, sRGB 255,15,131). The readback's luma ratio is wrong when a channel clips (film R = 1.0):
   calibrate on an unclipped channel.
4. **debug_no_water** rendered the whole frame black with a height fog of density 0 or 1e-6; that mode
   now spawns no fog actor.
5. **A wrong -Config path silently ran the built-in defaults**; the launcher now refuses.
6. **Water.** Default is now `heightfog`: VolumetricFogStartDistance = camera - 9.5 cm puts all 128
   froxel slices inside the 1 m column (UE adds a 9.5 cm NearOffset; VolumetricFogDistance counts from the
   start). The density was off by 2 (MatchHeightFogFactor 0.5). The `localfog` path is fixed too
   (uniform-scale 500 cm unit sphere, radial term saturated, GlobalStartDistance lowered) but not default.
7. Physics bodies were cooked for the 16 x 500k-tri oil tiles; now NoCollision.
8. `exec_cmds` json key (e.g. `["viewmode lightingonly"]`) for diagnostics, routed through the player console.

**Engine limitation (VSM + narrow FOV).** VirtualShadowMapClipmap.cpp clamps the clipmap
ResolutionLodBias at >= 0 after adding log2(FOV/viewport scale), so a 3 deg camera 7.6 m away gets
shadow texels sized for its DISTANCE (~0.5 cm on the 40 cm frame, ~40 px), not for its pixels;
r.Shadow.Virtual.ResolutionLodBiasDirectional (tried -5) changes nothing. At tank_depth 1 m the 0.93 cm
penumbra plus the floor's 24 px CoC hide it (no blocks at 300 %); at 15 cm the shadows are visibly blocky.
`camera_mode: "ortho"` (now framed right: under -game OrthoWidth acts as width/aspect) gets ~0.125 cm
texels, but UE's diaphragm DoF does nothing visible in ortho (motes stay sharp), and the auto ortho
planes lose the oil at 764 cm (use ortho_height_cm 300). CSM (r.Shadow.Virtual.Enable=0) is round but
far softer than the physical penumbra.

## Every VERIFY spot

If a define in `OilTank.Build.cs` breaks the build, set it to 0 and the code falls back:
`OILTANK_WITH_LFV` (local fog volume), `OILTANK_HAS_VFOG_START` (VolumetricFogStartDistance),
`OILTANK_PP_EXTRAS` (ToneCurveAmount, LocalExposure*, physical-exposure toggle),
`OILTANK_PERF_COUNTERS` (GPU/thread ms).

Results from the local build (2026-10-15). All four defines compile on 5.8 and stay 1.

1. LFV: header path and every field name are right. Units: extinction per METRE (yes). But the volume is a
   SPHERE with UNIFORM scale = max axis scale x 500 cm (`GetBaseVolumeSize()`), not 100 and not squashable,
   and radial/height fog combine as coverage products (radial 0 = no fog at all), and
   `r.LocalFogVolume.GlobalStartDistance` (1000 cm) hid it. Fixed in code; VSM-shadowed shafts through the
   LFV were not separately verified because heightfog is now the default.
2. `VolumetricFogStartDistance` exists (setter `SetVolumetricFogStartDistance` used). The *Color setters
   remain. NEW: `VolumetricFogDistance` is measured from the start distance.
3. Height-fog density: per 1000 uu, AND x0.5 (MatchHeightFogFactor) in the volumetric grid: FogDensity =
   2000 * tau / depth_cm.
4. Runtime UMaterial compiles and renders under -game (viewmode unlit shows the film; oil black). Only the
   5.8 deprecation of `bUsedWithInstancedStaticMeshes` (now `SetUsageByFlag`). The default Albedo is now the
   engine's WhiteSquareTexture.
5. PP fields all exist (compiled; no filmic toe visible in the calibration: film_rgb round-trips exactly).
6. The exposure formula is off by ~3.2x; calib_gain 0.311 closes it (see above).
7. `r.ScreenPercentage 200` IS honoured without TSR (5.8 kMaxResolutionFraction = 4; edges are
   anti-aliased; the SSAA2 frame costs ~3x). The froxel grid is in render pixels, so GridPixelSize is set to
   8 under ssaa2 (= 4 output px).
8. Ortho: OrthoWidth acts as width/aspect under -game (fixed); auto ortho planes lose the oil at 764 cm
   (works at 300 cm); diaphragm DoF has no visible effect in ortho; VSM is ~4x finer than the narrow camera.
9. `BatchUpdateInstancesTransforms(0, Xforms, false, true, true)` compiles; motes move in the clip.
10. `RHIGetGPUFrameCycles`, `GGameThreadTime`, `GRenderThreadTime` compile and report plausible values.
11. 16-bit grey `GetRaw(Gray, 16)` works; byte swap = 0 (native order) on this machine.
12. `SetLightSourceAngle` exists. (The light ROTATION was wrong, see bug 2.)
13. VSM: see the engine limitation above; the -2 bias is clamped away for a 3 deg camera.
14. DDC: the README's block was replaced by ManhattanFPV's graph override; it fills G:/UE/OilTank/DerivedDataCache.

## Risks for the first local build

- **Compile errors from the VERIFY APIs.** Most likely are the LFV component header and fields, and
  the material editor-only data API. They are fenced by defines or easy to delete. Expect one or
  two rounds.
- **The transient runtime material may not compile or render under -game.** The symptom is grey
  or checkered oil. The fallback is to create the material once as a tiny .uasset; that needs the
  editor, so ask the user first.
- **The water may fog the air,** which turns the oil grey. Check this first (checklist 3). Over a
  thin 1 m slab seen from 7.6 m, the froxel grid's exponential z-distribution spends most slices
  in front of the water. Try `cvars: {"r.VolumetricFog.DepthDistributionScale": "4"}`, GridSizeZ
  256, or a shorter `camera_distance_cm`. The shafts can look soft or smeared for that reason
  alone (trap 5).
- **Triangle count.** About 1M triangles per tile times 16 mirror tiles goes into the VSM shadow
  pass. Fine for stills; for the perf number, try `oil_grid` [640,360] on the surround tiles
  later (not implemented).
- **The first launch compiles shaders for tens of minutes.** Use `-TimeoutMinutes 120`.
- `FScreenshotRequest` under -RenderOffScreen is proven for stills. 300 back-to-back requests are
  not; the code waits for each one to be consumed.

## Opinion: will UE beat a hand-written top-down shader for THIS look?

**Probably not as the shipping path, but it is worth running once as a physical reference.**

This look is a 2.5D height field seen orthographically, with one light and a homogeneous medium.
For that case the hand shader can do the expensive physics *analytically and per pixel at full
resolution*: the shadow is the coverage at p + z*s with a PCSS penumbra, the shafts are a 1-D
integral along the light, and the CoC follows depth. UE does the same things with general-purpose
approximations that are weakest at exactly this scale. The volumetric fog is a 4 px froxel grid
with exponential z slices, mostly wasted on air when the subject is a thin slab far away. VSM page
resolution is picked from camera distance, not from a 3 deg FOV. The diaphragm DoF gather has its
own noise without TSR. The oil has to become 16 copies of a million-triangle mesh just to put
shadows in the right place.

UE would also add a second engine process (VRAM, shader caches) beside the wallpaper on the same
GPU, with no WorkerW, no HDR path and none of the cycle, preset or identity machinery (trap 9).
Where UE can win is believability per hour of work: real motes at real depths with real DoF and
real shadowing, which is exactly the "it needs real motes" item, and a ground-truth image to tune
our analytic terms against. So my recommendation is to use the A/B to measure what the physics
should look like (shadow offset, penumbra growth, shaft contrast, mote defocus), then implement
those in FluidWallpaper's own pass. Only if UE's frame clearly beats ours *and* the user accepts
the process cost should it go further.
