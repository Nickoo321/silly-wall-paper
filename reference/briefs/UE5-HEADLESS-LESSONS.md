# UE5 headless lessons (from the "Manhattan map in Velicodrome" chat, 2026-10-14)

Relayed from what its Opus executor proved in logs while driving UE for the ManhattanFPV project.

## Engine and paths
- UE 5.8.3 (Epic launcher) at G:\UE_5.8. G: = NVMe SSD. D: = 5400 rpm SMR HDD: never DDC/projects.
- Reference project (WORKING TEMPLATE, read it): G:\ManhattanFPV (git). Launcher script
  G:\ManhattanFPV\Tools\run-test.ps1 (game check, shared gpu.lock, BelowNormal, timeout, lock
  release in finally, -IgnoreGames, -Priority).
- Toolchain: VS Build Tools 2022 17.14 + Windows SDK 10.0.26100; UE bundles dotnet 10 for UBT.
  UBT once needed the VS component Microsoft.Net.Component.4.8.SDK (installed already; it was a
  UAC popup, so any NEW component install needs the user first).

## Content without the GUI (what worked)
- Never opened the editor, no .umap, no editor Python, no commandlets.
- Hand-written <Proj>.uproject (EngineAssociation "5.8", one Runtime module, Plugins list),
  Source/<Module>.Build.cs + Target.cs, Config/DefaultEngine.ini with
  GameDefaultMap=/Engine/Maps/Entry and GlobalDefaultGameMode=/Script/<Module>.<GameMode>.
- The GameMode spawns everything at BeginPlay with World->SpawnActor (directional light,
  SkyAtmosphere, ExponentialHeightFog, APostProcessVolume, pawn); NewObject<> for objects; values
  from the project's own JSON in Config/. All render settings as ini cvars.
- Build: `G:\UE_5.8\Engine\Build\BatchFiles\Build.bat <Proj>Editor Win64 Development
  -Project="G:\...\X.uproject" -WaitMutex` (first build ~9 min) -> Binaries/Win64/UnrealEditor-<Module>.dll.
- UI was pure Slate in C++; no UMG, no asset cooking. Everything diffable, reproducible from logs.

## Headless capture
- `G:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe "<uproject>" -game -RenderOffScreen
  -ResX=2560 -ResY=1440 -nosplash -unattended -log <own flags>`: NO window in ~15 runs.
- Screenshots from code: FScreenshotRequest::RequestScreenshot(Path, true, false) (HighResShot works too).
- Log your own perf (stat unit values every 10 s), don't parse stat output.
- Not used: UnrealEditor-Cmd, Movie Render Queue, -ExecutePythonScript.
- GOTCHA: -ExecCmds="a b c" through a PowerShell launcher split on spaces; only the first token
  arrived. Use ini/JSON or space-free flags. -game -windowed/-fullscreen opens a window: forbidden.

## Gotchas
- First -game launch compiles shaders for tens of minutes; later runs start in seconds.
- DDC defaults to %LOCALAPPDATA%\UnrealEngine\Common\DerivedDataCache on C:. Override in
  DefaultEngine.ini: [DerivedDataCacheGraphs]/[DerivedDataCacheStores]
  ProjLocal=(Type=FileSystem, UnusedFileAge=34, PromptIfMissing=false, Path="%GAMEDIR%DerivedDataCache").
  Saved/ and Intermediate/ live under the project. UBT's small cache stays in
  C:\Users\abg77\AppData\Local\UnrealBuildTool (unavoidable, small).
- CRASH: hardware ray tracing + the user's FluidWallpaper + a game on the same RX 7900 GRE gave
  AMD driver timeouts (0x117/0x141) and a 0x7E BSOD. ALWAYS r.RayTracing=False and
  r.Lumen.HardwareRayTracing=False; frame-cap test runs (t.MaxFPS 72); never two UE processes.
- Lumen: software Lumen needs mesh distance fields (they had none, used
  r.DynamicGlobalIlluminationMethod=0, r.ReflectionMethod=2, r.Shadow.Virtual.Enable=1,
  r.AllowStaticLighting=False, r.GenerateMeshDistanceFields=False). Test RT-off first.
- Exposure: r.DefaultFeature.AutoExposure=False + manual EV on a code-spawned PostProcessVolume,
  else stills vary run to run. Bloom, motion blur, lens flare off the same way.
- The user's wallpaper running costs UE ~60% of its fps (113 -> 45 fps; priority makes no
  difference). Measure under the real condition and say so; offscreen runs do not pause the wallpaper.
- Occlusion queries without a swap chain are suspect (Cesium culled everything offscreen):
  verify with a screenshot if anything relies on occlusion.
- ExponentialHeightFog density 0.006 washed a 10 km scene white; a 1 m tank needs far denser fog.
  Untested by them: volumetric fog, orthographic/narrow-FOV cameras, Niagara, DoF, grain
  (UE 5.8 has an orthographic projection mode on camera components).

## What they would do differently
-RenderOffScreen from day one; all state in code + JSON (no assets); one launcher script (game
check, gpu.lock, BelowNormal, timeout, finally-release); decide DDC/Saved placement before the
first launch; get UAC component installs done before the no-popup rule applies; expect UE's own
timings to be invalid whenever a game or the wallpaper shares the GPU, and say so in every report.
