# Worklog — Kimi session (started 2026-07-24)

Continuation of the Claude-built project. See NOTES.md for full history.

## Goals (user, 2026-07-24)

1. **Job 1 (now):** Make the native app's look match the Wallpaper Engine
   original — but better. User reference photos: WE has big slow blobby masses
   with fine marbling *inside* them (multi-scale, layered, "organic"). Our app:
   vortices spin too fast; lowering viscosity hits the main splats more than the
   vortices; overall lacks "organity". Try settings first, code later if needed.
2. **Job 2 (later):** Rework the Scenes feature. Known bug: hue-band scenes
   (e.g. Ember) leak out-of-band colors (green) — emitted splat colors and the
   hue-shift wheel are not both constrained to the band. Claude's band
   implementation only partially constrains.

## Safety

- Source + settings backup: `backup-pre-rework-2026-07-24/` (src, assets,
  reference, build files, NOTES.md, settings.ini.snapshot).
- **Correction to NOTES.md:** the app does NOT hot-reload settings.ini from
  disk. Live-apply only works from the Settings window (writes
  `g_renderer->Config()` directly, src/settings.cpp). Hand-edited ini requires
  an app restart. Config load is once at startup (src/main.cpp:1019).

## Iteration log

### Iter 1 (2026-07-24) — WE-parity structure settings
Baseline: user's ini (colorful=0 red/blue palette, hue band 66±70, post
sat/con/bri 1.30/1.61/1.25, sim_res 512, vorticity 27, pressure_diffusion 0.39,
pressure_iterations 30, max_brightness 0.70, decay 0.985/0.20, sat_restore 0.90).

Changed toward reference/project.json values (structure only, colors untouched):
sim_res 512->256, vorticity 27->48, pressure_diffusion 0.39->0.85,
pressure_iterations 30->20, max_brightness 0.70->1.35,
decay_threshold 0.20->0.29, decay_fast 0.985->1.0, saturation_restore 0.90->0.93.

Hypothesis: sim_res 512 is the main cause of "vortices too fast / no big
blobs"; max_brightness 0.70 flattens layering. ### Iter 2 (2026-07-24) — color: monochrome fix
User: vortices much better after iter 1. Remaining: colors too monochrome,
saturation too high, hue-shift wheel too fast, emitted colors change too slowly.
Root cause found: colorful=0 (palette mode, 5 fixed red/blue colors) + hue band
66°±70 pinning one mood. WE runs colorful=1 with continuously rotating emission.
Changes: colorful 0->1, color_cycle_period 19->10 (faster emission rotation),
hue_range 70->180 (full wheel), post_saturation 1.30->1.14, post_contrast
1.61->1.34, post_brightness 1.25->1.06, post_hue 0->14 (WE author panel values),
hdr saturation 1.70->1.20, hueshift_step 65->40, linger 6.5->10, off_time 10->20.
Applied via ini edit + restart (PID 19712). Result: more vibrant, user
confirmed rotating colors work. New direction emerged -> mood system plan.

## Mood system plan (approved 2026-07-24)

Plan file: .kimi-code session plans dir (fire-riri-williams-rictor.md).
Key user directives: bloom never; sim_res/dye_res freeze at MAX (512/4096);
every mood recipe formalized only after a game-style calibration quiz
(black level "barely visible logo", saturation swatches, Neon blowout target).
User monitor: X27U W1 (OLED, HDR, 240 Hz).
New engine feature planned: shadow floor (bottom knee) - adjustable gray floor
instead of crush-to-black, per-mood, keeps marbling visible in dark regions.

### Iter 3 (2026-07-24) — step 1: freeze resolutions at max
sim_res 256->512 (user directive "just max it out"), vorticity 48->24 to
compensate (feature scale ~halves when sim res doubles; 512+48 was the
original "vortices too fast" complaint). Restarted (PID 19532).
Result: (awaiting user motion check)

### Step 2 (2026-07-24) — shadow floor ("bottom knee") engine feature
New display-pass feature: near-black lifts smoothly toward a neutral gray
floor (keeps dark marbling visible on OLED; 0 = off = old behavior).
- shaders.h: display CB gained float4 shadow (x=floor, y=knee); PSMain lifts
  after the response curve, before m/HDR knee (28->32 root constants).
- fluid.h: FluidConfig.shadowFloor=0, shadowKnee=0.15. fluid.cpp: root sig
  {0,0,32}, three draw sites + BuildDisplayConstants(Ex) widened to 32.
- main.cpp: ini keys [color] shadow_floor / shadow_knee (load+save).
- settings.cpp: two sliders in col 3, section "Shadow floor (dark marbling)".
Built OK, app restarted (PID 26896), floor=0 verified visually unchanged.
NOTE: build fails with LNK1104 if the exe is running — always Stop-Process
FluidWallpaper before build.bat.

## SESSION STATE DUMP (2026-07-24, saved at user request before context limit)

**Where we are:** Steps 1-2 of the approved mood-system plan done. App running
PID 26896 with sim_res=512 (frozen max), vorticity=24, iter-2 color settings,
shadow floor built but =0 (off).

**OPEN QUESTIONS TO ASK USER NEXT SESSION (they asked to be re-asked):**
1. Motion check: is sim_res 512 + vorticity 24 as calm as the 256 version they
   liked? If twitchy, tune curl/velocity_diffusion at 512 — NEVER resolution
   (user directive: res frozen at max 512/4096 forever).
2. Shadow-floor sliders (Settings col 3, "Shadow floor (dark marbling)"):
   does the gray-lift behave as they imagined?

**Remaining plan steps (plan file approved, in session plans dir
fire-riri-williams-rictor.md):**
3. Calibration harness: --calibrate flag rendering quiz patterns behind icons
   (black-level "barely visible logo", saturation swatches, Neon blowout
   ladder). Reuse M1 --gradient infrastructure.
4. Quiz round 1 (Neon) with user -> write Neon.ini.
5. Engine primitives: CommandHueShift(targetDeg,dur)+ReleaseHueShift in
   fluid.cpp (reuse glide machinery ~fluid.cpp:1281-1315, gate scheduler);
   store m_darkPct + getters CoverageDarkPct/ScreenTooFull/FieldAvgHue from
   the 48x27 readback (ProcessCoverage ~fluid.cpp:1389-1431).
6. Conductor (new src/moods.cpp/h + CMakeLists): moods\*.ini overlays,
   DWELL->SHIFT_BRIDGE->EMIT_BRIDGE->RETURN_HOME state machine, replaces
   interlude block main.cpp:1186-1243, tray "Moods" submenu, [moods] ini
   section, migrate [cycle] enabled->[moods].
7. Quiz round 2 (Clouds) -> Clouds.ini.
8. Verify full Neon<->Clouds cycle with user.
9. Update WORKLOG.md + NOTES.md.

**Key constraints:** bloom NEVER; sim/dye res NEVER changed (frozen 512/4096);
every mood recipe needs user quiz sign-off; monitor X27U W1 OLED HDR 240Hz.
**HARD REQ (user, quiz round 1):** everything — esp. the shadow floor — must
look good with Windows HDR **on AND off**. Verify both before finalizing any
mood. Quiz page 1 confirmed accurate in both modes; user sees ALL 10 strips
(0.16-6.4 nits) in both -> floor is a taste choice, not a visibility limit.
Conversion: quiz strips are linear scRGB (1.0=80 nits); shadow_floor slider is
gamma-space dye units -> gamma_floor ≈ linear^(1/2.2).
**Safety:** backup-pre-rework-2026-07-24/ has pre-session source+ini snapshot.
### Step 3+4 (2026-07-24) — calibration harness + quiz round 1 (Neon)
--calibrate N flag (pages: 1 black level, 2 saturation, 3 blowout ladder) via
kGradientSrc page branch; reuses gradient path (skips sim resources).
Quiz answers (recorded in CALIBRATION.md as reusable benchmarks):
- black floor: user sees ALL strips HDR on+off; chose strips 2-4 ->
  shadow_floor=0.10 gamma (0.48 nits), knee 0.15.
- saturation: columns 5-6 -> post_saturation=1.70.
- brightness: HDR perception saturates ~400-500 nits on large areas (ABL);
  "Neon as bright as possible" -> peak_nits=1000 (small cores ride
  small-window headroom; ABL handles big areas).
Wrote %APPDATA%\FluidWallpaper\moods\Neon.ini (full recipe, no res/fps keys).
Applied same values to live settings.ini; restarted normal mode (PID 25304).
Result: (awaiting user eyeball — check Neon punch AND dark-region marbling,
HDR on; then HDR off via Win+Alt+B)

**Traps:** ini edits need app restart (no hot reload from disk); screenshots
of HDR tone-map dark; build needs exe stopped first.

## Steps 5-6 (2026-07-25) — engine primitives + mood conductor

- fluid.cpp/h: CommandHueShift(target,dur)/ReleaseHueShift(returnHome),
  HueAngleDeg, coverage getters (m_darkPct/m_avgHue, CoverageDarkPct,
  FieldAvgHueDeg, SetCoverageWanted). Display CB widened to 32 floats.
- src/moods.cpp/h (new, in CMakeLists): conductor state machine
  DWELL -> SHIFT (hue bridge glide) -> EMIT (discrete flip at t=0.4) ->
  RETURN (release bridge at t=0.7) -> dwell. LerpLook() lerps ~40 look
  floats over the transition; FlipDiscrete() flips bools/palette/ints.
  BeginTransition computes the bridge angle from FieldAvgHueDeg so the old
  field lands on the next mood's hue center. Moods are partial ini overlays
  in %APPDATA%\FluidWallpaper\moods\*.ini; sim/dye res, fps, mirror stripped.
  [moods] ini section (enabled, dwell_minutes, transition_seconds=4, jitter,
  early_switch_darkpct, min_dwell_seconds, base_mood); [cycle]->[moods]
  migration. Composition-aware early switch: sustained dark field + min
  dwell -> go early.
- Old interlude engine RIPPED OUT of main loop + tray + Settings window.
  g_cycle* globals + scenes.cpp interlude UI remain (Job 2 rework pending).
- Settings window: "Mood cycling" sliders (dwell 1-30min, transition 2-60s,
  jitter 0-0.5), System checkbox "Mood cycling", "Next mood" button next to
  Scenes. Tray: Moods submenu (toggle, next, per-mood radio).

### Bug fixes during bring-up
1. SNAP: ReleaseHueShift(false) and the !hsEnabled branch of UpdateHueShift
   zeroed m_hueAngle instantly -> whole-screen color snap (Clouds has
   hueshift off). Fixed: both now glide to the next full turn.
2. First "next mood" was a no-op: s_current defaulted to alphabetical 0
   (Clouds) while live config is Neon. Fixed: base_mood default "Neon".
3. BeginTransition forces SetCoverageWanted(true) (bridge needs fresh hue).

### Feature: emission/hue-shift coordination (user suggestion)
WheelHue() counter-rotates emission by the commanded hue angle while a
command is active, so fresh dye lands in the intended visible band during
transitions instead of clashing with the rotated field. Sign verified
against CssHueRotate(+m_hueAngle) display matrix; HSVtoRGB wrap-safe.

### Clouds v2 (physics-weighted per user: "look from physics, not grading")
Applied agent proposal (cloud-quiz-round2-proposal.md) to
moods\Clouds.ini + embedded kCloudsIni: vorticity 32, dye_diffusion 0.40,
splat_radius 0.75, decay 0.20/0.98, sat_restore 0.85, max_brightness 1.40,
post sat/con/bri 0.85/1.10/1.10, hue 35±45, cycle 30s, HDR comp neutral.
peak_nits deliberately NOT in mood ini (shell-global wiring open; quiz
round 5 decides). STILL pending quiz round 2 sign-off.

### CALIBRATION.md correction (user)
SDR blowout page: user reports literally ALL 10 bands (80-1500 nits) looked
identical in SDR — not simple clipping at SDR white. Recorded as open
anomaly (likely Windows SDR brightness remapping); SDR quiz judgments are
relative-look-only, never absolute nits.

## STATE DUMP 2026-07-25 (~4:20 AM, user asleep; PC auto-shutdown 6:09 AM)

- App autostarts with Windows (registry). Latest build has everything above.
- Mood cycling verified via scripted self-test: ini [moods] enabled=1,
  dwell_minutes=1, conductor auto-transitioned Neon->Clouds (see screenshots
  mood_before/after in %TEMP% if kept). After verification ini restored to
  enabled=0 — user toggles when ready.
- NEXT SESSION: quiz round 2 with user (protocol in
  cloud-quiz-round2-proposal.md section c — 6 A/B rounds, one param group
  each, start from v2). Then HDR on+off checklist (section d), then mark
  Clouds calibrated, then Job 2 (Scenes rework; green-leak bug: emitted
  colors + hue-shift wheel must both respect the band).
- User prefs to remember: hidden taskbar (OLED) -> put controls in the
  Settings window, not just tray. Physics over color grading. 4s transition
  preferred. Ask before screenshots only occasionally.
- Unanswered: motion at sim_res 512/vorticity 24 (calm enough?);
  shadow-floor look judgment.

## 2026-07-25 (midday) — Job 2 prep: scenes cleanup + structural leak fix

- Autostart-after-shutdown verified: app came up on boot with the new build.
- **Green-leak structural fix** (Job 2 core bug): UpdateHueShift scheduler is
  now suppressed whenever hueRange < 179, not just when hsEnabled=0. A narrow
  band can no longer be post-rotated out of band by ANY config path (mood,
  scene, or hand edit). Color motion in banded looks comes from emission-side
  drift (color_cycle_period) only. Commanded shifts (mood bridges) are
  unaffected — they bypass the scheduler.
- scenes.cpp: dead interlude "Auto-cycling" section removed (window is now
  pure scene manager, 700x300). g_cycle* globals, [cycle] ini read/write,
  CMD_CYCLE_* tray IDs all deleted. The [cycle]->[moods] ini migration in
  InitMoods stays for one-time upgrades; orphaned [cycle] keys on disk are
  harmless.
- Still pending for Job 2 proper: decide what "Scenes" means next to moods
  (scenes = manual one-shot looks, moods = auto-cycled recipes? merge?),
  needs user input.

## 2026-07-25 (afternoon, user away) — mechanism retune + baroclinic + 7 moods

**WE mechanism extraction (subagent, verified vs reference/project.json):**
the cloudy depth comes from (1) decay_fast=1.0 — faint haze never culled,
stacks into strata; (2) ZERO dye diffusion; (3) wanderer_brightness 0.1
(ours was 0.72 — 7x too hot); (4) vorticity high ON A PERSISTENT field;
(5) emboss shading (we already have it verbatim). Applied to Clouds/Storm:
decay_fast 0.98->1.0, dye_diffusion 0.22->0.0, sat_restore 0.85->0.93,
wanderer_brightness ->0.30, vorticity ->20, wanderer_speed ->250,
max_brightness ->1.20, hue 10+-18 (yellow cap), post_hue=0 (post_hue
rotates OUTSIDE the hue band — was the green-leak vector in Clouds v2).

**Video reference (user's WE recording):** pace ~10s evolution timescale;
cotton puffs with internal marbling; continuous ~30s hue lap. User notes:
"moves as a front, like oil on water"; "paddle through water OK, straight
lines not"; "add resistance from other forms so wakes bend".

**idleBrightness engine param:** MultipleSplats burst intensity was
hardcoded x1.5 (10x wanderer paint) = the "suns" in user's WE shot.
FluidConfig.idleBrightness (default 1.5 = old behavior, Neon unchanged),
ini [behavior] idle_brightness, settings slider, LerpLook entry.
Clouds/Storm/new moods use 0.5.

**Baroclinic torque (user suggestion, engine feature):** CSVorticity gains
a dye-gradient term: force += baroclinic * rho * perp(grad rho), making
wakes bend around dye masses. New SrcC (t2) SRV slot in the compute root
sig (6->7 params), SimCB 19->20 DWORDs, FluidConfig.baroclinic (default
0 = off, Neon untouched), ini [sim] baroclinic, slider, LerpLook.
First guess 60 in Clouds/Storm/new moods. **NOT YET VISUALLY VERIFIED**
(screenshots started failing ~13:25, "handle is invalid" — display
asleep/locked while user away). If it looks wrong at 5:23 check: baroclinic
magnitude first (try 20 / 150), then sign (perp direction).

**7 new moods** (all on the tuned mechanism set, all hueshift=0):
Pluto (cream/tan/rust, sat 0.55), Abyss (deep blue-teal, very dark),
Ember Giant (dark planet, deep-red bands, user's Google-find ref),
Violet Front (video's purple phase), Verdant (moss green),
Glacier (pale cyan, bright hazy), Solar Wind (golden streams).
All pending quiz sign-off per hard rule.

**State:** settings.ini restored from settings.ini.neon-backup (Neon live,
PID at the time 9696). 10 moods on disk: Abyss, Clouds, Ember Giant,
Glacier, Neon, Pluto, Solar Wind, Storm, Verdant, Violet Front.
Cron wake set 5:23 PM EDT today (01KYD3TP07FP3FG4N70FA857X3) to re-engage.
**Deferred:** hue-linger wheel profile (drift->rest->drift, user's WE
"palette fills up" behavior); off-screen sim margin experiment; per-mood
peak_nits wiring.

## 2026-07-25 (evening, user gaming) — UI coherence pass (subagent)

- Settings window: hover tooltips on all 52 sliders + 18 checkboxes
  (dark-themed, 300px wrap). SliderDef/CheckDef gained a `tip` field.
- Mood label now shows "Mood: A -> B" during transitions (MoodsNextIndex()).
- Settings window auto-rebuilds when the mood changes (500ms timer check,
  skipped while user is mid-drag) — sliders always show the active mood's
  real values.
- 5:23 cron cancelled (user returned early).
- Workflow note for the mood tour: tune sliders -> say "save <Name>" -> I
  sync settings.ini into moods\Name.ini -> then switch. Conductor never
  writes settings.ini; FinishTransition overwrites the live config.
- flowSpeed field exists in FluidConfig (unused, default 1.0) — finish
  wiring only if wanted later.

## 2026-07-25 (evening) — moods+scenes unified into one managed system (subagent)

- ONE folder: moods\*.ini for everything. presets\ migrated in once
  ([moods] migrated=1 guard; presets folder left on disk). 18 recipes now.
  Scenes window = "Looks" manager over the same folder; tray Presets
  submenu also reads moods\. 
- Backend: WriteConfigToIni(path,cfg,includeShell) extracted in main.cpp
  (shell=false skips sim_res/dye_res/fps_limit/mirror_second for moods).
  moods.cpp: MoodsSaveCurrent/MoodsCreateFromLive/MoodsDeleteCurrent/
  MoodsIsSkipped/MoodsSetSkipped (skip=name1;name2 in settings.ini,
  honored when auto-advancing; force still works), lock cache
  (MoodsLocksKey), cached mood config (MoodsCachedConfig),
  MoodsRefreshUiCache, rescan-by-name.
- Settings window: mood bar on top (name, "In cycle" checkbox, Save/New/
  Delete with confirm). Every slider+checkbox label: ● = mood file locks
  this key, ○ = inherited, trailing * = live value differs from the saved
  mood value (dirty, offsetof-based field mapping, epsilon = slider
  step/2). Zero moods = buttons disabled.
- Repo IS a git repo — all work is uncommitted local changes; nothing
  committed by us. backup-pre-rework-2026-07-24/ also still intact.
- New PID 7004.

## 2026-07-25 (late evening) — reframe: moods as DIRECTOR + Journey mode

**User reframe (important):** moods were never meant as static themed
scenes. The desired system is a color DIRECTOR: keep WE's organic hue
journey but curate the trajectory — reliably good families, ugly zones
(e.g. jarring greens vs violet) never emitted. The journey is the feature;
the dwell must hold one coherent family at a time; transitions between
families are the show. Also: dynamic black levels — density is a second
trajectory axis (bright dense phases vs deliberate dark intermissions with
emission off, then the reveal). Meta-directive: don't implement literal
suggestions; consider the desired outcome, try, verify direction.

**Journey mode (src/journey.h/.cpp, in CMake):** journeys\*.txt legs:
`hueCenter hueRange darkFloor dwellSec emit [blendSec] [glideSec]`.
A mood opts in with [journey] file=<Name> in its ini. Legs: GLIDE_TO_MID
(glideSec) -> BLEND_HOLD (blendSec, both families marbled = the "stoner
look" the user wants time in) -> GLIDE -> DWELL. emit=0 = dark
intermission (wanderers/idle off, field decays). After N loops (ini
[journey] loops=2) normal mood dwell resumes. Coherence: CommandHueShift
held all journey (never released mid-journey); WheelHue counter-rotation
keeps new dye in-family; scheduler auto-suppressed in narrow bands.
Ships: Journey Aurora (adjacent-hue arc) + Journey Stoner (high-contrast
families, 12s glides, 20s blend holds). Mood label shows leg i/n + "blend".
Mood dirs: 20 mood inis, 2 journey files. PID 36432.

**Evaluation criteria agreed with user:** does the field hold ONE coherent
family per leg; are blends the enjoyable part; any ugly zone ever; do dark
intermissions feel intentional. All journeys/moods still pending user
sign-off (hard rule) + HDR on/off check.

## 2026-07-25 (night) — journey v2: shiftDeg + settings redesign + adopt fix

**User's color-choreography model:** resultant color = emitted hue + global
field shift, managed per mood. Example: emit purple, shift field -35 (old
purple reads blue), emit blue, shift +35 (old blue reads purple) — two-color
harmonies with in-family emission only; 3 colors drift richer. WE also has
continuous hue movement as a baseline feature.

**Journey v2:** 8th leg field shiftDeg (signed, relative). Present && !=0 ->
GLIDE applies CommandHueShift(held+shiftDeg, glideSec); band is
emission-only (WheelHue counter-rotation lands it — no fluid.cpp change).
==0 -> band change only. Absent -> v1 glide-to-center. Blend midpoint =
held+shiftDeg/2. Held angle accumulates unwrapped; v1 legs after shift legs
target the nearest hueCenter-equivalent (no snap). Ships Journey Duet
(blue/purple duet) alongside Aurora + Stoner. 21 mood inis, 3 journeys.

**Adopt fix:** scene-apply (Looks window / tray) now syncs the conductor:
MoodsAdoptPath sets s_current, resets dwell, JourneyAttach — fixes stale
label + journeys never running when entering via scene-apply.

**Settings window redesigned (agent):** paged nav (17 categories left,
page content right), resizable 760x640 (min 640x480), vscroll panels,
bottom utility area always visible; all features preserved (tooltips,
markers, mood bar, auto-rebuild keeps page). Tray Moods submenu shows
skipped moods grayed (owner-draw, still clickable for forcing).

**Disabled moods:** already a feature = "In cycle" checkbox (skip-list).

## 2026-07-26 (user in Dota) — away-work batch 2

- **Journey verified empirically**: spaced screenshots show the dark
  intermission -> magenta reveal on schedule. Director confirmed working.
- **User steering captured**: "dyes bordering orange are amazing" (hue
  ~5-15 = Ember/Storm zone); Stoner reworked gold-forward (45 gold -> pink
  -> green -> orange-red -> dark -> violet, Stoner.v1.bak kept); Journey
  Magenta created to preserve the approved magenta look.
- **hue_linger=0.30** set in all banded moods (Neon exempt, full wheel).
- **Per-mood peak_nits wired**: mood ini [hdr] peak_nits overrides during
  the dwell; absent -> restores settings.ini value. Applies on transition
  finish, scene-adoption, startup. Session-only.
- **Mood color pass BLOCKED**: Dota covers + auto-pauses the wallpaper.
  Resume when user is back (QUIZ.md section 2).
- **QUIZ.md written**: full evaluation protocol (journeys, static moods,
  texture sanity, housekeeping, HDR on+off).
- **Bench restored**: settings.ini = Neon backup + [moods]
  base_mood=Journey Stoner, transition 13s, jitter 0.10, cycling off.
- **Open for user**: Mood 1-6 auto-files (from the New button) keep or
  delete. Dota-behind capture: CopyFromScreen sees only Dota + wallpaper
  auto-pauses; PrintWindow experiment still untried (low odds).
- awk-based mood->settings merge pattern (no python on this box).

## 2026-07-26 (quiz round, part 1) — boot-apply fix + Stoner v3

- Quiz start: Stoner v2 read as "random colors, ununified" (user). Diagnosis:
  legs zigzagged the wheel (45->300->120->20) = unrelated families; fixed v3
  = MONOTONIC climb (45->120->240->dark->300->20) so families travel, plus
  post_saturation 0.70 in the stub (tonal unity, their pastel reference).
- **MoodsApplyBase** (main.cpp/moods.cpp): at boot, if base_mood was
  explicitly persisted (scene-apply or manual pick), the base mood's recipe
  overlays settings.ini. Absent key = user's own settings, never stomped.
  Fixes: journey/static mood stubs never applied at startup (bench texture
  leaked, e.g. Neon 1.70 sat under a pastel stub).
- Mood color pass launched (8 flagships, base_mood flip + 75s develop + sc,
  no bench clobbering thanks to boot-apply). Results to be reviewed below.
- Screenshots during games: captured The Finals / Dota instead of the
  wallpaper twice — wallpaper auto-pauses on fullscreen apps. User told to
  ping when back at desktop.

## 2026-07-26 — renderer suspend in-game (free RAM)

- When pause-on-fullscreen/maximized holds >20s: renderer shuts down fully
  (swapchain, all sim/dye/mirror/analyzer textures, heaps, PSOs, queue,
  device) = RAM+VRAM freed for games. Shutdown() previously released
  NOTHING (relied on process exit) — rewritten idempotent + re-initable.
- Resume on game exit: full re-Init with the suspend-time config snapshot,
  HDR options re-resolved, colorspace reasserted, coverage restored.
  Manual pause never suspends. Explorer-restart + mirror lifecycle are
  suspend-aware. Verified live against the user's actual Dota 2 session
  (the 20s trigger fired, process healthy) + a --test-suspend hook run.
- Caveat: settings edits made while suspended take effect next launch
  (persisted to ini, just not live-applied to the snapshot).
- Mood color pass was ended early by the user; base_mood=Journey Stoner.

## 2026-07-26 (late) — settings UI overlap fixed (agent-10)

- "Text bunching over itself": three real bugs — mood bar font/baseline
  collision ("MoodJourney Stoner" jammed), journey status text clipped in
  too-small static, and resize ghosting (missing CS_HREDRAW/CS_VREDRAW +
  unbatched SetWindowPos). All fixed in settings.cpp; drag-torture tested
  clean at default/min/mid sizes. PID 11540.
- User asked for PC shutdown after this entry. Pending on return: QUIZ.md
  evaluation (journeys + static moods + HDR on/off). Everything else is in
  git as uncommitted changes; app autostarts on boot.

## 2026-08-29 — OilWallpaper PoC: lava-lamp oil blobs (agent-11)

- New standalone target **OilWallpaper** (src_oil/, separate from the fluid
  app; FluidWallpaper sources untouched). Same shell patterns: WorkerW attach
  (primary monitor only), tray (Pause / Palette radio Acid+Royal / Exit),
  Explorer-restart reattach, single-instance mutex. No ini/moods/settings.
- Renderer: ONE fullscreen-triangle pixel shader, no compute. 64-blob
  metaball field (r²/d² with compact tail cutoff 0.08 + per-blob clamp 3.5),
  influence-weighted palette colors, rim = max(sdf estimate (field−1)/|∇field|,
  field band) so merged masses keep saturated interiors with dark outlines,
  gloss from the analytic field gradient, 3-octave fbm marbling ±15%,
  gradient + blotch + grain backdrop. CPU sim: pseudo-curl drift +
  radius-proportional buoyancy (few sinkers), ±15% radius breathing, wrap.
- Automation flags: `--shot path.bmp [--shot-delay N] [--palette 0|1]`
  (GPU readback → BMP, then exits); log at %TEMP%\OilWallpaper.log (share-read).
  Needed because screenshots during the user's Dota session capture the game,
  not the desktop.
- Verified: both palettes shot at 25/40/70 s (build/oil-3.png, oil-royal.png,
  oil-final.png) — distinct rimmed blobs, necks/splits, droplets, no clump-lock.
  Tray commands (palette switch, pause) exercised via posted WM_COMMAND.
  ~57 fps at 2560×1440 (RX 7900 GRE, vsync-capped).
- Follow-ups if ported: HDR/scRGB output (PoC is SDR R8G8B8A8_UNORM only),
  settings/ini layer, pause-on-fullscreen, second-monitor mirror.

## 2026-08-29 — AcidWallpaper PoC: "liquid acid" oil-and-ink (subagent)

- New standalone target **AcidWallpaper** (src_acid/, copied from the
  OilWallpaper shell; src/ and src_oil/ untouched, only CMakeLists.txt
  gained a target). Same shell/flags: `--shot x.bmp`, `--shot-delay N`,
  `--palette 0|1`, `--console`; log at %TEMP%\AcidWallpaper.log.
- Look: posterized metaball blobs — flat banded interiors (softened-floor
  quantize), thin bright rim at the field==1 boundary (Gaussian on
  sdf=(field−1)/|∇field|, direct color, no bloom), dark meniscus band,
  hash speckle hugging interfaces, film grain. Palettes: 0 = Coral
  (bright flat coral blobs, deep teal/dark-amber mottled medium, cyan
  rims), 1 = Royal (red-orange vein web + dark hole-blobs on deep purple,
  hot-orange rims; seeded as a diagonal band for the dendritic look).
- Gotcha (also in AGENTS.md): accumulate the analytic field gradient only
  where the per-blob weight is UNclamped — the min(w,3.5) clamp flattens
  the field but 2r²/d³ keeps exploding toward blob centers, dragging sdf→0
  there and flooding centers with rim color ("donut" artifact). Coverage
  must come from the field, not sdf (sdf diverges where |∇field|→0).
- Verified via GPU-readback captures, 6 shader rounds: final
  build/acid-p0-final.png + acid-p1-final.png, polish round
  build/acid-p0b.png + acid-p1b.png (flatter/brighter interiors, moodier
  medium, rim-hugging speckle; hole-ier web with thinner strands).
  ~57 fps at 2560×1440. SDR only; HDR is a follow-up (same as oil).

## 2026-09-15 — Color parity vs the WE original (session with Opus subagents)

User verdict: the modded WE version (reference/script.js) looked awesome, the
port's colors "are ass". Priority: WE-look parity first, oil-as-render-mode
after (see OIL-REVIEW.md for the oil PoC review + recommendation).

- **Ground truth extracted from Wallpaper Engine's config.json** into
  WE-LIVE-PROPERTIES.json (live per-monitor property values of "Fluid Custom")
  and WE-PRESETS.json (saved presets). WE right-panel color adjust =
  wec_sa/con/brs/hue on 0..100 (50 neutral); user's look = 57/67/53/54 ->
  saturate 1.14, contrast 1.34, brightness 1.06, hue +14.4 deg. WE global fps
  cap is 240 (wallpaper ran ~100 fps). Monitor layout corrected: the fluid
  runs full-bleed on the PRIMARY X27U (WE calls that slot "Monitor1").
- **Reference captures** of the live WE original: reference/shots/ (3-min
  series + 3 bursts + workshop gif frames; contact sheets + gifs tracked,
  raw frames gitignored). Screenshots clip the panel's P3 green; hue and
  composition only. Look: extreme saturation, true black + hue-tinted haze,
  hard dark rims, global hue steps every 10-20 s.
- **COLOR-AUDIT.md** (Opus): the port's pipeline is a faithful translation;
  the bad colors are configuration: moods conductor on (recipes desaturate,
  grey-lift, hue-lock), shadow_floor=0.10 neutral grey add, hue band vs full
  wheel, gamut=2 BT.2020 vs WE's Display-P3, post_contrast drift 1.27 vs
  1.34, peak_nits gain firing only on already-clipped pixels, decay_fast
  0.614 vs WE's 1.0 (erases the accumulate-then-ignite faint field).
  Code-level: no clamp before the CSS filter matrix (canvas was 8-bit) and
  no clamp between primitives (Skia clamps after every colour-matrix stage —
  verified in cc/paint/render_surface_filters.cc).
- **Offscreen `--shot` mode** (Opus): headless deterministic render, see
  main.cpp header comment; configs in reference/configs/, sheets via
  reference/configs/ab.py + blind.py. LESSON: never run two shot processes
  at once — the OLED went grey (GPU starvation of DWM/WE); shot mode now
  yields per frame (`--shot-yield`, default 2 ms; 25 ms ~= 30 fps wall while
  the user games). Long batches: build/shots/batch.cmd detached.
- **Code changes**: display shader evaluates the CSS chain primitive by
  primitive with saturate() after each (input clamp first); shadow floor is
  now chroma-preserving (scales the pixel's own colour, no grey add); HDR
  expansion gain is driven by the RAW dye intensity and scales all channels
  in linear light (hue/sat preserved, "fluorescent" headroom) — identity at
  peak_nits=0; InitMoods no longer applies the default base mood's
  peak_nits when moods are off and no base_mood is persisted.
- **A/B step 1** (live config vs parity config, same seed): user preferred
  parity ("B looked better"). Going forward: BLIND A/B/C/D sheets, key kept
  in build/shots/blindN-key.txt until the user picks.
- Pending: blind sheet of s1 parity / s2 +clamps / s3 +chroma shadow 0.10 /
  s4 +HDR 700 nits knee 0.7; on-screen verification with HDR on (screenshots
  can't show the P3 green); then fix presets\WE Original.ini to carry every
  parity key; decide moods policy (moods must not go below parity sat).

## 2026-09-16 — WE parity landed: config + 256 grid; pour test; eyes

- Blind sheet 1 (s1 parity / s2 +clamps / s3 +chroma shadow / s4 +HDR700): user
  "probably B and D, can barely tell" = chroma shadow 0.10 and HDR 700 — both are
  invisible in SDR PNGs by construction; judged on the panel instead.
- Live settings.ini = reference/configs/we-look-live.ini (parity + clamps + shadow
  0.10 + peak 700 knee 0.70). Backup of the old live ini:
  %APPDATA%\FluidWallpaper\settings-backup-2026-09-16.ini.
- **Motion**: user's WE pour (reference/shots/photos/we-pour-*.jpg) shows big
  vortices feeding the cursor, droplet curls, marbled cores. The port's pour was a
  flat textureless disc expanding radially. Added `--shot-pour X,Y,START,DUR` to
  shot mode; matrix 512/20it vs 256/20it vs 512/40it vs 512/vort48: pressure
  iterations irrelevant; **only sim_res 256 (vorticity 48) reproduces the WE
  curls/droplets/marbling**. Live + defaults switched to 256/48; AGENTS.md freeze
  note updated (moods/presets still never carry sim_res/dye_res).
  User: "never using kimi k3 again this is heat".
- presets\WE Original.ini + moods\WE Original.ini rebuilt as true parity (every
  key; no sim_res); builtin kWEOriginalIni in main.cpp matches; fluid.h default
  gamut 2 -> 1 (P3). Settings label "Shadow floor (colour lift)".
- Open: "eyes" (tiny vortex-trapped dye pockets of an older hue) read stronger in
  the port than in WE. Candidates: HDR peak gain lifting small saturated cores,
  crisp 4096 dye + shading ring (WE's canvas scaling softened them), chroma
  shadow lift. Knobs: peak_nits 0, dye_diffusion 0.02-0.05, shadow_floor 0.
- Next: oil as a render mode (OIL-REVIEW.md phases 0-1).

## 2026-09-16 (late) — Liquid Acid look inside FluidWallpaper (in progress)

- Oil end goal redefined by the user with three refs (reference/shots/photos/
  liquid-acid-ref-*.jpg, "Liquid Acid" visual pack): macro oil on inked water.
  Design in reference/shots/README.md. Lava-lamp metaballs (src_oil phase 0,
  committed d483044 as donor code) are NOT it.
- Opus agent is building it as a render style in the fluid app: `[look]
  style=liquid_acid`, `[liquid_acid]` section; ink style on the fluid dye +
  metaball oil layer (positive discs/webs + negative holes/bubbles) advected by
  a 64x36 velocity readback (async, one frame late, same pattern as the coverage
  governor). Builds in build2/ only; regression shot of style=fluid is
  byte-identical to the pre-change build (regress-base vs regress-after).
- First frame (build2/shots/a1.png) vs refs: ink layer is on the right track
  (teal marbled filaments on near-black) but too speckly/posterised; oil layer
  far too sparse (a few small discs, ~8% coverage vs ~70% in ref 1), rims dark
  only (refs show a thin bright ink-coloured rim outside a dark line), almost no
  bubble swarms. Feedback sent to the agent; iterating.
- Iterations a1..a12 (build2/shots, 1280x720, same seed) driven by ref-vs-port sheets
  (reference/configs/refsheet.py). Fixes in order: compact Wyvill metaball kernel;
  big-disc/web seeding; atan2(0,0) NaN blacking out oil (the "black blotches");
  clustered log-normal bubble swarms; holes composite as ink; flat oil fill; ink
  ramp teal/orange; seam rings softened (the "bevel" was seams, not shading).
- Palettes A/B/C at 1920x1080 x t=60/90/120 (build2/shots/sheet?-NNN.png,
  liquid-acid-sheet.png, liquid-acid-evolution.png). Inis tracked as
  reference/configs/liquid-acid-{a,b,c}.ini. Cost +1.04 ms/frame at 1440p (upper
  bound, includes headless blocking readback). Fluid look byte-identical
  (regress-base == regress-final, md5 8069de7b...).
- Agent's own caveats: oil more geometric than ref 2's web; ink_mix 0.95 bypasses the
  hue cycler / moods hue in this look (decide before shipping); swarms are screen-space
  not advected; oil_hdr/rim_hdr default 0 (oil sits at SDR white); settings page,
  mirror and mood overlays untested at runtime.
- User on the palette sheet: "This is cool. Maybe make the colors slightly more
  opposite in hue, more often." -> follow-up: ink hue locked to the oil complement
  (+/- span) and a slow shared hue sweep so the pair stays opposite while cycling.
- OLED dimmed to 30/100 via DDC/CI at the user's request (tools/oled-brightness.ps1);
  restore with `powershell -File tools\oled-brightness.ps1 100`.
- Hue follow-up: first attempt (matrix hue-rotate sweep) went muddy (olive ink / pastel oil);
  replaced by ink-complement lock + sweep through curated vivid pairs with the ink's dark base
  kept in stops 1/2. A/B build2/shots/hue-ab.png (A fixed a12, B lock+sweep: yellow/purple ->
  orange/teal -> magenta/green over 60..120 s). Sent to the user; awaiting verdict + agent's
  key/cost report.
- User asked for (3) `style=ink` (ink-in-water, shared with liquid_acid's ink path) and (4) a
  separate 3D ink sim side project. Fable planners writing reference/INK2D-PLAN.md and
  reference/INK3D-PLAN.md; Opus executors after review. refs: reference/shots/photos/
  ink-in-water-ref-*.jpg. GPU lock rule added to AGENTS.md (build2/shots/gpu.lock).
- Sweep landed: `[liquid_acid]` ink_complement_lock=1 (default), ink_complement_span=40,
  hue_sweep_period=0 (off; 90 for the A/B), sweep_pair_N_oil/ink overrides; 5 curated
  pairs (vermillion/teal, red/cyan-green, magenta/green, gold/violet, lime/purple); blobs
  carry a palette index; meniscus halo gated on |grad|. liquid-acid-a/b/c.ini pin lock=0
  so they render unchanged. Cost corrected (earlier table was inflated ~1.9x by a
  concurrent render): fluid baseline, +0.87 ms full acid, +1.01 ms with lock+sweep at 1440p.
  Open: gold/violet pair = big bright yellow area at SDR white (ABL exposure, unchecked on
  panel); swept ink has no oil-hue bands (dark-base rule) — A keeps orange marbling.

## 2026-09-17 — session-limit cutoff; 3D parked; 2D ink finishing

- Both Opus executors died at the 5-hour session limit (~23:30 on 09-16). User: stagger
  subagents, one at a time; "3D can be next week's job".
- **3D ink (src_ink3d/, build3/) parked as WIP**: M0 passes (raymarch, jitter fixed the
  banding); M1 drop rendered as a striped streak — bisected to the per-frame unsharp
  `sharpen` pass running unbounded (bi-sharp0-040.png shows a clean falling blob with it
  off). Resume point: bound/disable sharpen, re-run M1 gates, then M2 (vorticity/buoyancy
  tuned to the plume look) and timings. Plan: reference/INK3D-PLAN.md.
- **2D ink**: the "entry orb" that ate ~20 renders was the periodic drop EMITTER's first
  drop (interval 9 s, timer primed at 0.3..1.0 x interval) landing at t~6-7 s, not the
  Gaussian halo — found by reading UpdateDrops. Shot inis now use drops=0 (QueueDrop still
  works); shipped inis keep drops=1. Final 1080p sheet rendering (paper / inverted /
  acid-water); acid-water tile drifted to ~90% oil coverage — to fix from liquid-acid-a.ini.
- User on sheet-acidwater-120: "the teal outline is too perfect, uniform and flat". Follow-up
  (with the coverage re-base, one pass): modulate rim width + halo brightness along the
  boundary with low-frequency position/time noise (breaks and thickenings), and make the
  halo intensity follow the ink brightness under it (it is refracted ink), for all liquid_acid.

## 2026-09-17 — `style=ink` (ink in water), shared with liquid_acid

- Third display PSO (`#ifdef INK` in `kDisplaySrc`), same pattern as LIQUID_ACID.
  Shared `InkWater()` block does Beer-Lambert `T=exp(-k*thick*a)` on the RAW dye,
  4-tap edge darkening for folds, paper vs INVERTED output; exposed both as
  `[look] style=ink` and as `[liquid_acid] ink_mode=water`. `style=fluid` shot is
  byte-identical before and after (md5 10E36EBF… / -hdr 414B4321…).
- Sim-side, style-agnostic: `[sim] gravity` (dye-weighted, in `CSVorticity`),
  `gravity_pow`, `gravity_blur`; `[drops]` emitter with `InjectDrop`/`QueueDrop`
  and `--shot-drop X,Y,T[,VY]`. `Splat()` split into `SplatVelocity`/`SplatDye`.
- Iteration verdicts (build2/shots/ink/, single-variable, seed 1234):
  vorticity **48 -> 12** is the single biggest win (48 = smoke puff in 5 s,
  4 = glassy symmetric); velocity_diffusion **0.998** makes the pair stall
  mid-frame instead of hitting the floor; density_diffusion **0.9997** (0.999 =
  pale by 13 s, 1.0 = never clears); gravity **2** with gravity_blur 3 (30-100
  piles up on the bottom edge, blur 0 seeds grid-scale fringe); drop
  `asymmetry` 0.35 gives the refs' unequal lobes from two cheap 256-grid
  impulses around one dye stamp; ink density k 3 (paper) / 4.5 (inverted).
- LESSON, cost me ~8 renders: the "blown-out orb at the injection point" that
  survived every tail/impulse/kernel change was **a second scheduled drop** —
  `[drops] interval=9` primes at `interval*(0.3+0.7*RandF())`, so the emitter's
  own first drop landed at t≈6-7 s right where the scripted one entered. Shot
  inis that test a single drop must set `drops=0`; `--shot-drop` still works.
  Two features survive from that hunt and are keepers: `impulse_spread` (the
  velocity impulse is wider than the dye stamp) and the DROP_COMPACT splat PSO
  (finite-support drop kernel). The `[ink] motion_lo/motion_hi` HDR gate is
  also in, defaulted on, `motion_opacity` off.
- Cost at 2560x1440, 2880 frames, one render at a time, nothing else running:
  fluid 26.1 s vs the SAME ini + `style=ink` 26.4 s = **+0.10 ms/frame**. The
  ink inis themselves run FASTER than the parity look (16.3 s) because they
  turn off wanderers/idle splats/dart, i.e. far fewer 4096-res dye passes.
  acid + ink_mode=water 20.9 s (different sim; not comparable to fluid).
- Open: liquid-acid-water.ini has ~90% oil coverage — every `[liquid_acid]`
  population key is identical to liquid-acid-a.ini, but that population was
  tuned against the busy WE-parity flow and merges on the calm ink flow. Needs
  its own iteration (blob_count ~64, threshold ~0.95, support_scale ~1.7).
  Panel check with HDR on AND off still owed.

## 2026-09-17 — rim variation keys + liquid-acid-water population re-tune

- **`[liquid_acid] rim_vary` / `rim_ink_follow`** (both default **0** = the shipped
  uniform rim; liquid-acid-a/b/c are unchanged unless the keys are set). In the OIL
  block of `kDisplaySrc` (`src/shaders.h`), just before the dark-rim Gaussian:
  `rim_vary` drives one low-frequency `AcidFbm` of position (+ a slow time drift),
  contrast-stretched, that multiplies BOTH the rim half-width and the meniscus
  half-width (0.4x..1.9x) and the halo intensity (0..1.55x), so the outline thickens,
  thins and dies out along stretches. `rim_ink_follow` scales the halo by the dye
  brightness sampled one tap OUTSIDE the isoline along -grad (the halo is refracted
  ink): 0.55x over clear water .. 1.45x over bright ink — a floor, not zero, because
  the halo is also the oil edge's own refraction. New cbuffer slot `laP12`
  (AcidParamsGPU 336 -> 352 bytes), parsed in main.cpp, two Settings sliders.
- Balance took one extra render: at `0.25 + 1.05*smoothstep` the halo only ever got
  DIMMER than the current look (ink_mode=water is mostly clear water), which reads as
  "weaker", not "varied". The shipped gains make the bright stretches ~= the old
  uniform strength and the dead stretches ~0.15 alpha.
- **liquid-acid-water.ini population is now its own** (was a copy of palette A's):
  blob_count 96->64, disc_frac .16->.10, web_frac .32->.20, bubble_frac .28->.40,
  disc 0.160-0.380 -> 0.300-0.420, web 0.070-0.200 -> 0.120-0.210, threshold
  0.55->0.65, support_scale 2.00->1.60. Oil-region coverage **96% -> ~41%** (a12 is
  ~50%): big discs with ink channels between them, holes clustered on the oil instead
  of confetti over the whole frame. Nothing else touched (repulsion/flow keys as-is).
- Shots (1920x1080, seed 1234, `--shot-drop 960,150,70`, t=90, `--hdr on`) in
  build2/shots/rim/: r1 = rim keys off, r2 = first gains, r3 = shipped gains
  (`rim_vary 0.7`, `rim_ink_follow 0.8`, ini build2/shots/rim/r2.ini). Sheets:
  rim-ab.png, rim-refsheet.png.
- `style=fluid` regression: we-look-live.ini 60 s 2560x1440 md5
  **10E36EBF1A74EDFE609065D757300054** before and after (build2/shots/rim/regress-fluid.png).
- Rim follow-up landed (cf554e8). User: "new rim looks good". Verified rim_vary/rim_ink_follow
  on palette A's banded ink with one headless render (build2/shots/rim/a-rim-090.png): halo
  brightens where bright filaments meet the edge, fades on dark stretches, no artefacts.
  Applied 0.7/0.8 to liquid-acid-a/b/c and -a-sweep inis.
- Rim softened to 0.4/0.5 everywhere (user: "not the harder one"), 5669297. OLED set to 0
  via DDC (panels in standby; readback 0/0, re-check when awake).
- Research (Opus, web only): reference/research/LIQUID-ACID-RESEARCH.md — no official
  breakdown exists; genre setup = backlit glass dish, oil on dyed water, macro from above
  (a transmission image -> Beer-Lambert ink is right); rim = meniscus refraction, dark
  inside / bright outside, bright half is concentrated background colour (rim_ink_follow
  -> ~1.0); discs vs webs are one dewetting-coverage parameter, not three fracs; palettes
  sampled as hex; grade = crushed warm toe, grain in shadows, per-channel clipping.
- "Odd combos" of ink physics + WE automation (build2/shots/combo/, inis derived from
  we-look-live): c3 = style=ink inverted, chroma 1, full WE emission -> opaque marbled
  paint-pour look (no clear water), sent to user. c1/c5 (WE + gravity / + drops) dim at
  t=75 (parity dark phase). c2/c4 (drops-only emission under the parity chain) render
  nearly black: one dim drop by 40 s — the periodic emitter + fluid colour path needs a
  debug pass (governor? PickSplatColor brightness? interval?).
- Automation-on-ink combos that WORK (user's intent: the WE automated emission driving the
  ink physics, not one scripted drop): ink-auto-drops.ini (style=ink inverted, chroma 1,
  hue-cycled drops density 6 every ~4 s + gravity 2, no WE emission -> coloured drop caps in
  clear water), ink-auto-bursts.ini (WE idle bursts + darts, no wanderers, parity fade
  0.999 so water clears, gravity 2 -> coloured ink clouds with curls on black),
  ink-auto-bursts-drops.ini (both). Drops in the WE colour path need density ~6 because
  WE palette colours are scaled 0.15 and single splats never "ignite". Sheet:
  build2/shots/combo/auto-sheet.png. c3 (full WE emission) = opaque paint-pour, no water.
- User pick on the combos: ink-auto-bursts (WE bursts driving ink, clear water) and the
  paint-pour (full WE emission through the ink render) are "the best for now", preferring the
  crowded paint-pour -> tracked as reference/configs/ink-pour.ini. To be judged on the panel
  later today via tools\panel-check.ps1 -Ini reference\configs\ink-pour.ini (then -Restore).
- User: "the random colors just don't have the same pop or intent" (hue-cycled ink combos).
  Config-only fix: chroma=0 and a complementary pair in tint_thin/tint_thick (veils one
  colour, dense cores the other). Rendered pour + bursts x {teal/vermillion, violet/red,
  yellow/magenta}: build2/shots/combo/duo-*.png, sheets duo-pour-sheet / duo-bursts-sheet.
  Sent; awaiting pick. If approved: rotate the pair through the acid sweep list over time
  (small code change reusing the liquid_acid pair list).
- User picked 3 duotones (pour teal/vermillion, pour yellow/magenta, bursts yellow/magenta) but
  "the red one looks washed out". Root cause: my combo inis (from we-look-live) left
  hueshift_enabled=1, which post-rotates the WHOLE frame with the CSS matrix (desaturating)
  — so tints drifted (yellow/magenta rendered blue/green at t=75). Proof: same tints on
  ink-inverted.ini rendered true. Fixed configs: reference/configs/ink-duo-*.ini
  (hueshift off, grading neutral). Note: in SDR PNGs dim veils of a yellow tint read olive;
  cores get the HDR lift on the panel.

## 2026-09-17 — looks as runtime modes, duotone pair rotation, acid polish

- **Looks switch live now.** The gap was ONLY the display PSO: `CreateAcidBuffers()`
  (blob/param/InkCB upload rings) and the 64x36 velocity readback were already created
  unconditionally, and `Frame()` already seeds the acid blobs lazily — but
  `m_psoLiquidAcid` / `m_psoInk` were compiled in `CreateDevice` only for the look that
  was enabled at startup, so `DisplayPso()` silently fell back to the fluid PSO.
  New `FluidRenderer::EnsureLookResources()` (idempotent, GPU-idle-waits like
  `SetResolutions`) compiles whichever variant the current `[look]` needs; the
  `makeGfx` lambda became `FluidRenderer::MakeGraphicsPso()` so both paths build the
  PSO identically. Called from `ApplyPreset` (main.cpp) and from the settings
  checkbox handler; the two look checkboxes are now mutually exclusive and no longer
  say "(restart)". Switching a look OFF needs nothing — the fluid path reads none of
  that state.
- **`WriteConfigToIni` now writes `[look] style` plus full `[ink]`, `[drops]` and
  `[liquid_acid]` blocks** (and deletes the int forms `[look] ink` / `liquid_acid` /
  `[liquid_acid] ink_water` so they cannot contradict the string form). Before this a
  saved preset round-tripped the fluid keys and dropped the look entirely. Key names
  cross-checked against the parser: no key parsed-but-not-written and none written-but-
  not-parsed.
- **`--shot-preset <ini> AT <sec>`**: headless look-switch verification through the
  tray's own `ApplyPreset`. Two 1280x720 frames in build2/shots/modes/:
  `switch-to-ink.png` (we-look-live -> "Ink - duo pour teal" at t=20, captured t=60 —
  teal/vermillion duotone paint-pour, correct) and `switch-to-fluid.png`
  (ink-inverted -> we-look-live at t=20, captured t=60 — WE-parity neon fluid, correct).
- **reference/presets/** (new, 10 tray-friendly copies: WE parity, 3 Liquid Acid, 6 Ink).
  The tray "Presets" menu enumerates the MOODS folder, so these have to be copied into
  `%APPDATA%\FluidWallpaper\moods\` by hand to appear — nothing was written there.
  `we-look-live.ini` gained `[look] style=fluid` (a no-op for the default, but without
  it a preset can never switch BACK to fluid: overlays are partial by design).
- **`[ink] pair_sweep_period`** (s, default 0 = off): cross-fades `tint_thin`/`tint_thick`
  through `LiquidAcidConfig::sweepInk`/`sweepOil` with the same `HsvLerp3` +
  smoothstep the acid palette sweep uses. Thin = ink anchor, thick = oil anchor —
  exactly the assignment the user's hand-picked duotone inis already use, so veils stay
  darker than cores. Slider in the Ink group. Sheet: build2/shots/modes/duo-sweep.png
  (ink-duo-pour-teal-vermillion + period 120: t=75 gold/violet, t=105 lime/purple).
- **Liquid Acid polish**, three keys, all defaulting to the current behaviour:
  `rim_order=1` puts the dark band one half-width INSIDE the isoline and the bright
  caustic one half-width OUTSIDE it (adjacent, never overlapping — Micromachines
  13(7):1021); `grain_shadow_weight` multiplies grain by `(1-luma)^2`;
  `toe_tint` lifts the darkest pixels toward a low-value version of the ink ramp's mid
  hue. Sheets: acid-polish-ab.png, acid-polish-zoom.png (2x crop — the rim change is
  only readable zoomed), acid-polish-refsheet.png.
- `style=fluid` regression: we-look-live 60 s 2560x1440 md5
  **10E36EBF1A74EDFE609065D757300054** (-hdr 414B4321…), unchanged.
- User marked two flat patches on the teal duotone pour (a smooth fresh plume, a fading haze):
  both are the RGB midpoint of complementary tints = grey-brown "mud". New `[ink]` keys
  (default off): `veil_floor` (faint dye -> background) and `tint_mid_dip` (blend dips toward
  dark around its midpoint, like real ink). A hue-arc blend was tried first and rejected (the
  midpoint became lime, a third colour). A/B: build2/shots/mud/mud-ab2.png (0.12 / 0.7).
  Fluid md5 unchanged. Also: build2.cmd (vswhere + vcvars + cmake) — run it from PowerShell.
- User: "maybe just a switch to background colour, not black" -> tint_mid_dip now dips OPACITY so
  the midpoint shows paper_color (4449681); paper_color is the background for the inverted
  look (black in the shipped inis, any colour works). Demo build2/shots/mud/mud-ab4.png (deep
  purple, dip 1.0). Fluid md5 unchanged. Awaiting verdict; shipped duotone inis untouched.
- **Random layering exploration (Sonnet explorer, 87e4c39).** `tools/explore.py` + `tools/explore-space-layering.json`:
  mutate a base ini per a knob space (random keys + curated groups, shuffle-without-replacement
  per group), render each variant headless under gpu.lock, write `<out>-NN.ini/.png`, a labeled
  sheet and a JSON record. Batch `layering1` (seed 7, 12 tiles, 960x540, t=75 s) cycled the
  water / duo / bands bases with: sparse mono (grey/white/black) ink under coloured oil, colour
  moved between ink and oil, black/white "element" switches, paper_color navy/plum/black, dip 1.
  Tiles were sent to the user one at a time with the mutated keys. Sheet:
  build2/shots/explore/layering1-sheet.png. Own read of the sheet: 01 (orange oil over white
  ink, warm black) and 10 (red oil over white ink on navy) are the strongest — coloured oil on
  monochrome ink is the combo that works; 04 (black oil, rims only, white ink) is a curious
  third. 02/05/08/11 (dip 1.0 on dark backgrounds) went to mud or near-black; 09 was a
  coverage failure (ink_mode=water + complement lock off -> oil fills the frame); 06 read as
  white paper with black dots (inverted ink lost under white oil). Pillow: installed to the
  user's site-packages; ab.py/refsheet.py/explore.py prefer it, scratchpad copy is the fallback.
- **"Real oil" pass on Liquid Acid (Fable executor).** User on a 2x crop of white oil over teal
  ink: "it just looks super dull and fake"; then, from refs 4-7, "there is no boundary" /
  "some scenes have no boundary, and some do". The shipped oil was a hard cut-out with a
  1-2 px stroke in a FIXED `meniscus_color`, which drew a teal line around black shapes on
  mono ink for no reason. Eight `[liquid_acid]` keys, EVERY one defaulting to the old look:
  `oil_thin_edge` (film thickness `thk = smoothstep(0, edgeW, sdf)`; the disc now fades over
  `edgeW` and its colour slides toward `oil * saturate(0.30 + 1.10*ink)` — thin oil goes
  darker and takes the ink's hue, never brighter), `oil_edge_frac` (that band as a fraction
  of the LOCAL lens radius: the Wyvill kernel gives `R ~= 0.78/|grad|` for free, clamped to
  0.02..0.35 because |grad| collapses on a merged mass and an unclamped R blew the halo into
  frame-sized pale lobes), `refraction_width` (rim_width multiplier, 0 = the shipped 7),
  `oil_specular` (broad Blinn lobe + Fresnel off a gradient+fbm normal, deliberately weak —
  the rig is backlit; also lifts `m` under `oil_hdr`), `oil_iridescence` (thin-film hue ramp
  on the fbm thickness, strongest where thin), `swarm_lens` (a trapped droplet is a HOLE IN
  THE FILM: soft edge sized to the droplet, thin-oil fringe outside it, softened ring, one
  offset highlight — `AcidSwarm` now also returns the winning droplet's normal and radius),
  `oil_glow` (diffuse spill of the oil's colour into the ink, never a line),
  `meniscus_from_ink` (the boundary becomes EMERGENT: colour = the local ink lifted in HSV,
  width = a few % of the lens radius, weight = a dye tap taken OUTSIDE the isoline, and the
  same weight gates the dark hairline; `meniscus_color` survives only as the legacy fallback).
  So on liquid-acid-a's teal ink a wide soft teal halo appears (ref 7's regime) and on the
  mono/near-black inks the halo and the hairline both vanish (refs 4/5/6) — no foreign cyan
  line anywhere. Grain is boosted inside the halo band (the refs' halo is visibly grainy).
  `AcidCB` gained laP13/laP14 (352 -> 384 bytes) and one more raw-string split in shaders.h.
  Suggested "oil-real" set: oil_thin_edge 0.70, oil_edge_frac 0.14, oil_specular 0.35,
  oil_iridescence 0.20, swarm_lens 0.80, meniscus_from_ink 0.90, oil_glow 0.45,
  refraction_width 22. Sheets (base / real / stronger / weaker, 960x540, t=75, seed 1234,
  --hdr on): build2/shots/oil/sheet-l03.png, sheet-laa.png, sheet-mono.png (layering1-09 and
  tile9-01, base vs real), and the user's crop case at 2x in build2/shots/oil/crop2x-l03.png.
  Shipped copies to compare: reference/configs/liquid-acid-a-real.ini, layering1-09-real.ini.
  `style=fluid` regression: we-look-live 60 s 2560x1440 md5
  **10E36EBF1A74EDFE609065D757300054** (-hdr 414B4321...), unchanged.
- **Transparent coloured oil (Opus executor).** User: "I suppose transparent and colored oil is
  the next step? Black oil seems figured out." Until now the oil was a FILL: whatever the ink did
  under a disc, the disc hid it. Five more `[liquid_acid]` keys, all inert at their defaults:
  `oil_transparency` (0 = the shipped fill) turns the oil into an ABSORBING FILM over the already
  refracted ink -- Beer-Lambert per channel, `T = exp(-oil_absorb * thickness * (1 - oilHue))`
  off the oil's own hue normalised to its brightest channel, so an orange film passes red and
  eats blue, and a near-black oil normalises to ~0 and absorbs everything (a neutral-density
  film: that is why the approved black-oil family survives the key). The dish is BACKLIT, so
  over dead-black ink nothing is transmitted and a physically honest film would go black -- the
  scatter term `oilC * (1 - T_avg)` is the light scattered inside the film itself and is what
  keeps a thick disc reading as its own colour there; it vanishes at the rim where `T -> 1`.
  Thickness is the SAME proxy the edge work uses (`thk = smoothstep(0, edgeW, sdf)`, edgeW a
  fraction of the local lens radius `R ~= 0.78/|grad|`), so the thin edge is automatically the
  clearest part of the film -- what `oil_thin_edge` was approximating by hand -- times
  `oil_film_bump` (a slow fbm) so the film has islands of thick and thin instead of one even
  pane of glass. `oil_refract_body` (uv units) displaces the ink lookup across the WHOLE body
  along the gradient of (field + fbm), capped to a unit vector, so the marbling wobbles as it
  passes under; the existing edge refraction is untouched. `oil_ink_blur` defocuses the ink under
  the film with four diagonal dye taps weighted by `thk * cov`, taken BEFORE the ink pipeline so
  the bands, seams and water absorption all inherit the defocus. HDR: `oil_hdr` is now weighted
  by `filmOp = 1 - T_avg`, so a transparent pixel keeps the INK's own `m` and only the scattered
  part is driven to the oil level. Holes (negative blobs) and trapped droplets are untouched --
  both show pure ink, no film. `AcidCB` gained laP15/laP16 (384 -> 416 bytes).
  A/B sheets (960x540, t=75, seed 1234, --hdr on): build2/shots/oil-trans/sheet-laa.png
  (opaque / 0.50 / 0.85 / glass 0.60 with body refraction + defocus), sheet-water.png,
  sheet-tile9.png, and crop2x-laa.png (2x on a disc edge + the drip neck). What they show:
  on liquid-acid-a the flat vermillion becomes a mottled film whose thin islands go amber where
  the ink ramp under it is warm; on ink_mode=water the pale ink plume runs straight THROUGH the
  orange discs, tinted, while the oil over empty black water just deepens; tile9-08 survives --
  the black holes stay pure black and the sheet gains depth, at a modest cost in overall level.
  Shipped: reference/configs/liquid-acid-a-glass.ini, liquid-acid-water-glass.ini.
  Regression: build2/shots/oil-trans/laa-t00-075.png is BIT-IDENTICAL to the previous commit's
  build2/shots/oil/laa-real-075.png (md5 32e9b585...), i.e. the five keys at 0 are a no-op on
  the acid path too. `style=fluid`: we-look-live 60 s 2560x1440 md5
  **10E36EBF1A74EDFE609065D757300054** (-hdr 414B432114EEB0BC68432226FB081E70), unchanged.

- **Screen mirroring / kaleidoscope, `[mirror]` (Opus executor).** User, showing two 4-fold
  mirrored "Liquid Acid" loops: "mirroring the sim ... as a random wallpaper would be cool,
  shouldn't be hard". It is one uv transform at the TOP of the display pixel shader
  (`MirrorFold` in the shared part of `kDisplaySrc`), applied before the dye sample, before the
  acid metaball field (`pp` derives from `uv`) and before `InkWater` -- so all three looks fold
  together and blobs, rims, swarms, speckle and bands all mirror WITH the dye instead of
  floating over it. No macro and no fourth PSO: `mode = 0` returns `uv` untouched before doing
  any arithmetic, which is why `style=fluid` stays bit-identical. Keys, all inert at default:
  `mode` (0 off / 1 horizontal / 2 vertical / 3 quad / 4 kaleidoscope), `segments` (kaleidoscope
  wedges, default 6), `source` (which quarter of the SIM you see and that gets copied around:
  +1 right half, +2 bottom half), `center_x`/`center_y` (also accepts the hand-written
  `center = "x y"`), `rotate_period`, `drift`, `soft`.
  How the fold maps: each folded axis sends the FOLD LINE to the middle of the sim and the
  screen edge to the outer edge of the chosen half, so the shown half is stretched to fill its
  mirrored tile -- the full sim resolution lands on screen and nothing is wasted. `soft` is a
  SMOOTH ABS (`sqrt(d*d+s*s)-s`) on the fold itself rather than a second sample of the whole
  look: a mirror is already continuous in value at the seam, what gives it away is the reversed
  gradient, and rounding the fold removes exactly that for one sqrt. `drift` is three
  incommensurate sines of time (an fbm with no table), clamped to 0.2..0.8 so a tile never
  collapses. The kaleidoscope folds the ANGLE in aspect-corrected space (`af = |wrap(a,2seg) -
  seg|`, every other wedge reflected, so a wedge edge is a mirror and never a jump) and scales
  the radius to the sim's central DISC -- without that the far corners all sample outside
  [0,1]^2 and the clamp sampler smears one row of texels into radial streaks (first kaleidoscope
  render did exactly that).
  Plumbing: `MirrorConfig` in fluid.h, `[mirror]` load/write in main.cpp, a "Mirror" settings
  page, and a 12-DWORD ROOT-CONSTANT block at b3 (root params 6 -> 7). Root constants, not an
  upload ring, because unlike AcidCB/InkCB this block IS read by all three display PSOs and must
  be bound on every display draw (all four sites: swapchain, offscreen shot, analyzer,
  second-monitor mirror -- the last two pass their own w/h for the aspect).
  Pointer mapping: the sim still runs full-size, so `HandleInput` sends the pointer through a
  CPU twin of the same fold -- the pixel you point at is showing some source pixel and that is
  where the dye goes, so clicking a mirrored copy maps back to the source. The drag impulse is
  signed by `d(source)/d(screen)` per axis so a drag in a reflected tile pushes the way it
  looks. NOT mapped, by design: the kaleidoscope's impulse direction (ill-defined under
  rotation, flips stay 1), `i.pos.xy` (the paper vignette and film grain stay screen-space, as
  on real film), and the one-pixel `fwidth()` spike exactly on a seam.
  Overlay presets: `[mirror]` is display-only and look-agnostic, so it is the one block worth
  shipping PARTIAL. `reference/presets/Mirror - quad (overlay).ini`, `... kaleidoscope 6
  (overlay).ini`, `... off (overlay).ini` and `reference/configs/mirror-quad.ini` carry nothing
  else; applied from the tray they fold whatever look is running and leave every other key
  alone. VERIFIED at runtime: `--shot-preset reference/configs/mirror-quad.ini AT 30` on the WE
  look comes back 4-fold mirrored at t=60 with no PSO rebuild (build2/shots/mirror/ovl-060.png).
  Sheets (960x540, seed 1234, --hdr on, --shot-delay 60, --shot-yield 2):
  **build2/shots/mirror/sheet-mirror.png** -- WE off / WE quad / WE quad via the overlay at t30 /
  WE kaleidoscope 6 / Liquid Acid A real oil quad / layering1-09 quad / layering1-09 kaleidoscope
  6 -- and **sheet-drift.png**, ink-duo-pour-teal-vermillion quad with `drift 0.5` at t=30 and
  t=90 (the seam has visibly walked off centre by t=90). What they show: the quad turns the ink
  pour into a Rorschach butterfly and is the strongest of the modes on every look; the
  kaleidoscope reads beautifully on the high-contrast WE look (a clean 6-fold rosette) and is
  nearly wasted on layering1-09, whose near-solid magenta oil has little structure to fold.
  Cost is a handful of ALU per pixel.
  Left for the sweep (PROGRESS.md "End state"): `MirrorFold` already returns the signed distance
  to the nearest fold line alongside the source uv, and the line's definition is the one cbuffer
  block, so a future side weight can reuse the distance without touching the mirror modes.
  Regression: `style=fluid` we-look-live 60 s 2560x1440 seed 1234 --hdr on md5
  **10E36EBF1A74EDFE609065D757300054** (-hdr 414B432114EEB0BC68432226FB081E70), unchanged --
  re-checked after the kaleidoscope radius fix as well.

- **LAVA LAMP "rise" mode + a 12-slot palette sweep + the film's chroma as a key
  (Fable executor).** User, on the tile9 family ("it's 12 different colors. Can be a
  system for changing colors once in a while. Maybe they spawn off screen and go up
  slowly maybe?"), then: "Do you see the lava lamp aesthetic now?" Motion first, in
  `StepAcidBlobs`: `rise_speed` (uv/s) is a constant upward term on the TARGET velocity,
  so the damping relaxation still smooths it and the fluid can still shove a blob
  sideways; holes are water, not oil, and climb at 0.7x, which is what makes a trapped
  bubble creep ACROSS the disc it sits in instead of riding it like a painted dot.
  `rise_wobble` adds two incommensurate sines on the blob's own s1/s2 curl phases, so no
  two blobs sway together. `rise_respawn` re-enters a blob that has climbed clear of the
  top BELOW the bottom edge at a fresh x and a fresh radius from its own kind's range --
  a teleport, not a spawn, so the population and the field density never move. In respawn
  mode the y wrap is off entirely (it would have bounced the re-entered blob straight back
  to the top, which is the first thing it did) and the only guard left is a floor on how
  far below the frame an eddy may push a blob. The stagger the mode needs is already
  there: `SeedAcidBlobs` spreads every kind over the full height, so the column is
  populated from frame one instead of one batch marching up and leaving a bare screen.
  Determinism: respawn draws come from a private xorshift seeded off the population's own
  seed, so a `--shot` replays exactly and the fluid's `rand()` sequence is untouched.
  `rise_stretch` makes a moving blob a teardrop for almost nothing -- an ANISOTROPIC
  kernel, `q.y` scaled by `1/(1+s)` before the distance and the gradient carrying the
  matching `1/(1+s)^2`, with `s` uploaded per blob in the previously unused `AcidBlobGPU
  .b.w` (speed relative to the rise speed, scaled by how small the blob is, capped at 0.8
  -- past that a metaball stops reading as a blob and starts reading as a smear).
  `rise_bottom_light` is one vertical ramp on the oil (not on the ink: the glass is not
  lit, the wax is), applied before the film so it also feeds the scattered light, and
  carried into `oil_hdr` so the base of the lamp is the HOT part and not merely the pale
  part.
  Colour: the user clarified that "the hue of the ENTIRE screen can shift", so there is
  no per-blob colour anywhere. Two global options, both keyed, both CPU-side on `effOil`
  in `UploadAcidConstants`. `hue_rotate_period` rotates the whole oil family's hue in HSV
  (S/V preserved); `hue_sweep_period` keeps the curated list, now up to 12 entries
  (`sweep_count`, `sweep_oil_N`/`sweep_ink_N`; the shipped `sweep_pair_N_*` spelling is
  still read first, and the short form is what gets written). The important part for the
  user's favourite palettes: a sweep entry's oil value may be THREE floats (one anchor,
  the other three shades derived as before) or TWELVE (the four `oil_color_N` shades
  verbatim). The eight tile9 palettes are hand-picked four-shade sets and had to reach
  the screen exactly as they were rendered, not re-derived from a single anchor and toned
  down. When no entry carries a full set the old anchor path runs untouched. Plus
  `oil_saturation`, one vividness multiplier whatever fed the palette.
  `post_chroma` / `post_lift` close out the transparent-film work: the film costs 10-20%
  perceptual chroma and some lightness, and rather than re-author every palette for it
  the FINAL acid composite is scaled -- chroma about the pixel's own luma (hue and luma
  survive), then a luma multiplier. Measured on tile9-08 at t=75, OKLab means over the
  non-dark pixels: opaque L .663 C .194; film 0.5 + `post_chroma 1.2` / `post_lift 1.05`
  L .618 C .199; at `post_lift 1.12` L .641 C .205. So the shader key reproduces the
  post-processed preview -- chroma is fully back, lightness lands a few % short because
  the film also deepens the darks. The user's pick (transparency 0.5 with the chroma
  held) is now the shipped default of the whole glass family, at 1.2 / 1.08.
  Coverage note: the tile9 bases sit at threshold ~0.47-0.56 with repulsion 0.90 and NO
  rise. Under a constant rise the blobs bunch as they climb and that pair floods the
  frame into one continent (rendered: the oil filled ~90% and the black holes were all
  that moved). The rise inis use threshold 0.85 with the tile9 repulsion, which puts the
  coverage back where the tile9 tiles sit while leaving the lobes separate enough to
  merge and neck apart on the way up.
  Configs `reference/configs/acid-rise-12.ini` (the 8 tile9 palettes in hue order, 900 s
  per trip) and `acid-rise-rotate.ini` (continuous rotation, 600 s per turn), shipped as
  presets "Liquid Acid - rising colours.ini" / "- rising hue rotation.ini".
  Sheets (960x540, seed 1234, --hdr on, t = 20/60/100/140):
  **build2/shots/rise/sheet-rise.png** (base with the rise and the sweep off / rise +
  8-palette sweep / rise + hue rotation) and **sheet-chroma.png** (the film A/B above).
  What they show: the sweep column walks hot pink -> magenta while black masses leave the
  top and fresh oil climbs in under the bottom edge, and the bubbles stay BLACK through
  every hue, exactly as asked -- the ink is mono, only the oil takes the colour. The
  rotation column is the honest version of the caveat in the code: orange -> OLIVE ->
  green, and the green is a 104-nit full-frame mean where the sweep sits at 34, so on the
  OLED it wants `oil_saturation` or `post_lift` pulled down. `AcidCB` gained laP17 (416 ->
  432 bytes) and one more raw-string split in shaders.h.
  Also, the manual pause (tray `CMD_PAUSE`) used to freeze the LAST FRAME on the panel:
  `FluidRenderer::PresentBlack()` now clears and presents ONE black frame on the
  transition into a manual pause (the second monitor too) and then the shell stops
  presenting as before, so a panel left paused goes dark for no extra GPU work. A
  FULLSCREEN pause deliberately does not do this -- that wallpaper is behind the game
  anyway and clearing it would flash black when the game exits. `CMD_PAUSE_ON = 8` /
  `CMD_PAUSE_OFF = 9` are explicit, non-toggling ids so `tools/away-pause.ps1` (new
  `-Explicit` switch) can stop tracking state; `CMD_PAUSE = 1` stays the toggle for the
  tray menu. NOT verified on the panel -- a `--shot` run has no swap chain and never
  reaches this path; the reasoning is from the code and the user's live exe has to be
  rebuilt and relaunched by them before `-Explicit` does anything (an older exe ignores
  8/9 silently).
  Regressions: the acid path is a NO-OP at the new defaults --
  `liquid-acid-a-real.ini` 960x540 t=75 comes back md5 **32E9B585A5B061536188420C88BE4DB6**,
  bit-identical to the previous commit's build2/shots/oil-trans/laa-t00-075.png. That
  took a fix: the field loop's `dot(q, q)` had been expanded by hand to
  `q.x*q.x + qy*qy` for the anisotropy, which moved the rounding and changed every acid
  frame; written as `dot()` on the scaled vector it is identical again (`q.y / 1` is
  exact). `style=fluid`: we-look-live 60 s 2560x1440 seed 1234 --hdr on md5
  **10E36EBF1A74EDFE609065D757300054** (-hdr 414B432114EEB0BC68432226FB081E70), unchanged.

- **WIP CHECKPOINT (Fable executor, 2026-09-17 ~19:00) — DROPLET PARTICLE SIM.** User pivot:
  the procedural swarms "look png'd on... they need to be simulated and attached to the oil".
  DONE: `[liquid_acid] droplets` particle sim (`src/fluid.cpp` `StepAcidDroplets` /
  `AcidFieldAt`, state + keys in `src/fluid.h`, ini parse/write in `src/main.cpp`, 10 sliders in
  `src/settings.cpp`). Particles carry x/y, a DRAWN radius `r` relaxing toward a target `rt`, and
  a `mergeTo` link -- nothing is ever born, merged or removed in one frame (the user's
  "flickering"): birth is r=0 growing in, dissolution is rt=0 shrinking out, coalescence sets the
  survivor's rt to the area-conserving sqrt(r1^2+r2^2) while the absorbed one's rt goes to 0 and
  its centre is pulled in, so the metaball union necks them. Kind 0 = water trapped in oil
  (negative weight), kind 1 = oil on open ink (positive, oil-coloured) -- BOTH swarm kinds
  replaced. Motion: the local oil's own velocity (soft-max over blob kernels) for kind 0, the
  fluid for kind 1, plus Brownian jitter, gradient confinement to the right side of the isoline,
  attraction / contact repulsion / coalescence over a 64x36 uniform grid, a radius cap that
  pinches off a satellite, and `droplet_life` dissolution. Rendered by being ADDED INTO the same
  metaball `field`/`grad` before the threshold (shaders.h, new `AcidDrops` t2 / `DropCells` t4
  root SRVs at graphics root params 7/8, `laP18`/`laP19`, AcidCB 432 -> 464 B); the hole weight
  scales with the LOCAL blob field so a droplet punches through thick oil too. `droplets` > 0
  forces `swarm_holes`/`swarm_drops` to 0 -- one system.
  VERIFICATION COMPLETE: HLSL compiled without errors. Droplet sim cost at 2560x1440: -0.81 ms/frame
  (acid-rise-12.ini 20.19s, base.ini 23.69s, 4320 frames). MD5 regression (we-look-live.ini 60s
  2560x1440): **PASS** (10e36ebf1a74edfe609065d757300054). Strip renders (no-pop check):
  strip-060.png / strip-061.png. A/B sheet: build2/shots/droplets/sheet-droplets.png (1944x1130).
  Every droplet key defaults to off, so any ini that does not name them is unchanged -- but the
  two shipped RISING presets were deliberately changed (droplets 1500, swarm_holes/swarm_drops 0),
  so "shipped inis byte-identical" is NOT true of those two. style=fluid holds its md5.

- **Ring / plume artefacts: diagnosed, partly fixed (Fable, 2026-09-17 ~19:50, ee4c045).**
  Both are ONE bug and it predates the droplets: everything keyed on `sdf = (field - thresh) /
  |grad|` is wrong wherever the field dips TOWARD the threshold without crossing it. There
  `field - thresh` and `|grad|` both collapse, the quotient lands wherever it likes, and
  `thk = smoothstep(0, edgeW, sdf)` concludes the film has thinned to nothing -- so the thin-edge
  model paints `oilC * saturate(0.30 + 1.10*inkC)` (over black ink: 0.30x, DARK) in a ring around
  a bubble that never punched through, with oil still inside it. The plume is the same thing at
  another scale: `lensR = 0.78/|grad|` pins to its 0.35 ceiling on a shallow plateau, so
  `edgeW` hits its 0.060 cap = ~32 px at 540p, and the iridescence and the glow (both keyed on
  1-thk) light its middle. NOT the rim and NOT the halo: gating those changed nothing.
  Measured, one 960x540 t=90 render of acid-rise-12 each (build2/shots/droplets/):
  `gate.png` droplet gate alone -- no change, so the rings are not stranded droplets.
  `iso.png` isoOk on rim+halo at 2.5..7.0 -- no change, confirming the above.
  `thk.png` + `iso2` isoOk on `thk` at 1.35..2.20 -- rings mostly GONE (best result), but the
  hard blend put a bright seam around the plume.
  `iso3.png` isoOk on the lensR ceiling -- rings back AND the seam; reverted.
  `iso4.png` (SHIPPED) isoOk on `thk` at 1.35..6.00 -- no seam, but too soft to kill the rings.
  So the fix is right and only the blend is wrong. NEXT: keep the tight 1.35..2.20 band for the
  RING case but make the fallback continuous -- blend `thk` toward `cov` in the same units the
  thin edge already uses (e.g. clamp `edgeW` by `isoOk * edgeW + (1-isoOk) * rim_width*2` so the
  band narrows instead of the value jumping), then re-render `iso2` and check the seam. One
  render per attempt, ~55 s; this is a tune-and-look loop and does not need Opus.

- **Droplet contribution gate + isoline-trust gate + shot-series sub-second names (Fable, 2026-09-17, ee4c045).** Chasing hollow dark rings (oil still inside) and a soft dark plume beside a nearly-merged blob: a droplet on the wrong side of the interface, or a droplet below a pixel across, now fades its whole contribution (field AND gradient) to zero via a CPU-side gate carried in the droplet's `.w`; `isoOk` (built from the display quad's own two derivatives, ~1 on every real edge) gates the dark rim, the bright halo and the film-thickness proxy wherever the signed-distance term collapses without a true threshold crossing. Also: `--shot-series` now names sub-second series in TENTHS (0600/0605/0610) so 60/60.5/61 no longer collide as 060/061/061 and silently overwrite a capture; one more raw-string split in shaders.h for MSVC's 16380-byte cap. Rings/plume reduced, not gone — see the "Ring / plume artefacts" entry above for the measured follow-up.
  Verification: renders pending — the user is gaming, GPU work on hold.

- **WORKLOG: measured behaviour of each ring/plume threshold tried, and the next step (Fable, 2026-09-17, 23a8bb7).** Docs-only commit recording the diagnosis and the five renders compared (`gate.png`, `iso.png`, `thk.png`+`iso2`, `iso3.png`, `iso4.png` shipped) — see the "Ring / plume artefacts" entry above.
  Verification: renders pending — the user is gaming, GPU work on hold.

- **Never die on a refused swap chain: soft-fail + backoff retry + a log file (Fable, 2026-09-17, 221ea95).** The live wallpaper crashed with a fatal dialog (`CreateSwapChainForHwnd` hr=0x80070005 E_ACCESSDENIED) on the resume out of a fullscreen pause while the user was in a game on the same monitor: DXGI refuses a flip-model swap chain on an output another app still holds exclusively, and `resumeRenderer()` fires the instant the game loses foreground — before the game finishes releasing the output — so a routine, transient refusal went through `HR()` -> `Fail()` and was fatal. Fix: `CreateSwapChainSoft()`, shared by `CreateDevice`/`Reattach`, unwinds cleanly under `TryInit`/`TryReattach` instead of calling `Fail()` (a startup failure is still fatal outside them); `resumeRenderer()` returns bool and retries on its own clock (250 ms, 500, 1 s, 2, 4, 8, 16, then every 30 s), rebuilding the wallpaper window after 8 refusals in case Explorer restarted and took the WorkerW with it; the Explorer-reattach path uses `TryReattach` the same way; every attempt is logged (HRESULT + foreground window class/title) to a rolling 256 KB `%APPDATA%\FluidWallpaper\FluidWallpaper.log`. Also: `Shutdown()` now releases the droplet sim's two upload rings, which had stayed mapped and alive across every suspend/resume cycle.
  Verification: builds clean; a headless `--shot` of acid-rise-12 renders as before and writes no log file. The suspend/resume path itself cannot be exercised headless. Renders pending — the user is gaming, GPU work on hold.

- **Harden startup the same way, and log every Fail() before the dialog (Fable, 2026-09-17, 6ead6c7).** New evidence: a second live launch died too, this one at startup, with a fatal dialog whose text nobody captured. Startup now uses `TryInit`: a refused startup swap chain brings the process up SUSPENDED (the same state a fullscreen pause already produces) instead of a message box over the game, and the existing backoff loop takes it from there. The swap chain's colour-space calls (`CheckColorSpaceSupport`/`SetColorSpace1`) stop being fatal — `ReassertColorSpace()` already re-runs on every resume, reattach and HDR transition, so a refusal corrects itself in a second. `Fail()` now writes to the log BEFORE showing the dialog, so a dismissed or hidden box still leaves its HRESULT and the foreground window's class/title behind. Audited the droplet work for anything that fails only when `m_headless` is false — found nothing; the one real bug the audit turned up (Shutdown() not releasing the droplet rings) was already fixed in 221ea95.
  Verification: builds clean. Renders pending — the user is gaming, GPU work on hold.

- **Verification renders: droplet ring/smear fix (ee4c045) and swap-chain hardening (221ea95, 6ead6c7) (Fable, 2026-09-17).** Rote re-check, no code changes. `build2\FluidWallpaper.exe` (19:58:35) predates 6ead6c7's commit time (19:58:54) by 19 s; `build2.cmd FluidWallpaper` reported `ninja: no work to do` (vswhere.exe lookup also failed in this shell but did not block the no-op build) -- the binary already matches source, timestamp order is just build-then-commit. Ring/smear: one 960x540 t=90 acid-rise-12 render (`fixed-090.png`) against the prior `new-090.png`; crude hollow-ring count (dark center, 3+ of 4 radius-3 sides with R>150) dropped from 1538 before to 1035 after (`ring-before-after.png` crop at x590-790,y370-520, 4x). Strip regression (`--shot-series 3:0.5` off acid-rise-12, t=60/60.5/61): three files landed as sub-second names (`strip2-0600.png`, `strip2-0605.png`, `strip2-0610.png`), fixing the ee4c045 collision bug; pop-detector (>160 on any channel) counted 17282 and 17464 differing pixels across the two consecutive pairs, not 0 -- reported as measured, no further judgement made. md5 regression: we-look-live.ini 60 s 2560x1440 -> `10e36ebf1a74edfe609065d757300054`, matches the expected 10E36EBF1A74EDFE609065D757300054 (PASS). Resume-path proof: `--test-suspend` (src/main.cpp ~2088-2442) only wires into the windowed message loop entered from `wWinMain` when `ShotModeRequested()` is false; `RunShotMode()` (lines 1836-2049, the code path `--shot` takes) never reads `g_testSuspend`, so the hook cannot run headless -- skipped per the fallback instruction rather than forced against the exe.

- **Verification renders + presets: oil_drag/oil_dye_block/rise_parallax/rise_parallax_dim/oil_viscosity/mouse_oil_mode (Sonnet executor, 2026-09-17, 5abf21f).** Rote re-check, no code changes. `build2\FluidWallpaper.exe` (21:14:01) predates 5abf21f's commit time (21:15:16) by 75 s; `build2.cmd FluidWallpaper` reported `ninja: no work to do` (vswhere.exe lookup also failed in this shell but did not block the no-op build) -- the binary already matches source, timestamp order is just build-then-commit. New [liquid_acid] keys, all default to the prior behaviour when absent: `oil_drag` (0, drags the dye field toward the oil's own motion), `oil_dye_block` (0, blocks/attenuates dye advection under thick oil), `rise_parallax` (0) / `rise_parallax_dim` (0, small blobs rise slower and dim more than big ones), `oil_viscosity` (0, damps blob motion), `mouse_oil_mode` (0 off / 1 push / 2 comb) with `mouse_oil_radius` (0.12) and `mouse_oil_gain` (1.0). md5 regression (we-look-live.ini 60 s 2560x1440): **PASS**, `10e36ebf1a74edfe609065d757300054` matches the expected 10E36EBF1A74EDFE609065D757300054. Cost of `oil_drag`/`oil_dye_block` at 2560x1440 (t-base.ini vs t-drag-on.ini, 60 s = 8640 frames each): base wall 44.063 s, drag-on wall 49.081 s, delta 5.017 s -> **+0.581 ms/frame**. Sheets (960x540, seed 1234, --hdr on) in `build2/shots/oildrag/`: `sheet-drag.png` (1944x582, oil_drag 0 vs 0.9 at t=50), `sheet-parallax.png` (1944x1130, rise_parallax 0 vs 0.8 at t=20/25), `sheet-mouse.png` (2912x1130, no-mouse vs mouse_oil_mode=1 push vs mode=2 comb, each with `--shot-mouse 200,270,560,0,60,0.6` at t=61/64), `sheet-viscosity.png` (1944x1130, oil_viscosity 0 vs 0.6 at t=60/65). New presets: `oil_drag=0.9`, `oil_dye_block=0.7`, `rise_parallax=0.7`, `rise_parallax_dim=0`, `oil_viscosity=0.6`, `mouse_oil_mode=0` added to `acid-rise-12.ini`, `acid-rise-rotate.ini`, "Liquid Acid - rising colours.ini" and "Liquid Acid - rising hue rotation.ini" (drops emitter left OFF, no other value touched); two new preset files, "Liquid Acid - rising colours (mouse push).ini" (`mouse_oil_mode=1`) and "... (mouse comb).ini" (`mouse_oil_mode=2`, `mouse_oil_radius=0.30`, `mouse_oil_gain=1.6`). Executor's caveat: at the shipped ~90% oil coverage there is little open ink left for `oil_drag`/`oil_dye_block` to act on, so on these presets the two knobs mostly just erase the ink that is still visible rather than reading as "the oil is dragging wet ink" -- a lower-coverage preset would show the effect more clearly; this is a measurement, not a design call, and no coverage value was changed.

- **Verification renders: droplet ring cause fix (Sonnet executor, 2026-09-17, 7f0188d).** Rote re-check, no code changes. Ring cause per the commit message: `droplet_oil_weight` was a FIXED 1.2 against an 0.85 threshold, so a positive droplet only crossed the isoline where the ink was already close to it -- one that drifted into a deep hole raised the field a little, crossed nothing, and drew only its rim (the hollow-ring-with-oil-centre defect). It now carries the local deficit plus its authored weight, and the size floor is `max(1 px, 1.6x the dark rim's own half-width)`, so a droplet whose hole would otherwise be narrower than its own rim gets a real disc instead of all-rim-no-fill. The flicker diagnostic (`tools/popdetect.py` + millisecond `--shot-series` names) is new in this commit; the flicker FIX itself is PARKED pending the user's choice between the wide soft film edge and a crisp one (see the "Ring / plume artefacts" entry above) -- these numbers are the diagnostic, not a verdict on that choice.
  Built pre-fix (`git show 7f0188d^:src/fluid.cpp`/`shaders.h`, byte-verified against the parent commit's blob hashes) and post-fix (restored via `git checkout`, byte-verified against HEAD's blob hashes) into `build2\FluidWallpaper.exe`, one `build2.cmd FluidWallpaper` each side (exe timestamp moved 21:34:12 -> 21:38:28 pre-fix, then 21:38:28 -> 21:41:20 post-fix; both builds printed a harmless `'vswhere.exe' is not recognized` from the nested `cmd /c` quoting but still compiled+linked `fluid.cpp.obj`/`FluidWallpaper.exe` via ninja). `tools/shot.ps1 -Series "10:0.006944"` (one sim-frame apart at 144 fps) against `acid-rise-12.ini`, 60 s delay, both 960x540 and 2560x1440, before and after; `tools/popdetect.py` totals over the resulting 9 frame-to-frame transitions:
  | series | pops (appear+vanish) | jumpy band px |
  |---|---|---|
  | b (pre-fix, 960x540) | 3 (14 px) | 344 |
  | a (post-fix, 960x540) | 3 (14 px) | 320 |
  | b1440 (pre-fix, 2560x1440) | 334 (2195 px) | 3047 |
  | a1440 (post-fix, 2560x1440) | 331 (2169 px) | 2285 |

  At 960x540 the fix moves pop count not at all (3 -> 3) and band flicker down slightly (344 -> 320 jumpy px). At 2560x1440 pops drop marginally (334 -> 331, within run-to-run noise territory for a stochastic sim) but the jumpy band count drops more clearly (3047 -> 2285, -25%) -- consistent with the commit's own claim that this fix targets the ring/hole defect, not the band flicker, and that the flicker fix is a separate, parked change. md5 regression (`we-look-live.ini` 60 s 2560x1440): **PASS**, `10e36ebf1a74edfe609065d757300054` matches the expected 10E36EBF1A74EDFE609065D757300054. `git status` returned to clean after the pre-fix/post-fix file swap (`git checkout -- src/fluid.cpp src/shaders.h` verified byte-identical to HEAD before rebuilding). Standing line on model fit: this task was pure mechanical re-verification (build, render, count, report) with no design judgement called for, and none was made.

- **WORKLOG: popdetect hysteresis, and why the 334 pops were the instrument (Sonnet executor, 2026-09-17, 6bc9a11).** Docs-only recap, no code or render changes. Every one of the "appeared hole" components the strict oil/hole classifier (`R>150` oil, `max(R,G,B)<40` hole) counted between two consecutive 1440p frames turned out to be a single near-black pixel drifting ~3% across the hard cut at 40 -- not a droplet, not visible, sitting in the dim thin-edge band rather than on any particle, which also explains why the count scaled with resolution (more pixels near the boundary, not smaller droplets). `tools/popdetect.py` now only counts a component as appeared/vanished if it does not overlap the previous frame's LOOSE mask (hole < 70, oil > 120 instead of < 40 / > 150) -- i.e. those pixels were not even close to being a hole/oil before. On the existing 1440p 10-frame series this took the strict pop count of 334 down to 0 with hysteresis (a second series measured 1, 4 px) while jumpy band px held at 2285 (down 25% from 3047 pre the 7f0188d ring fix). No sim change: there was no popping to fix, only a measurement artefact to stop counting. The one real remaining instability, band jitter, is addressed next in 8b82ad5.

- **[ink] tonemap / tone_chroma / tint_hue_blend, and the 240 Hz cap (Fable executor, 2026-09-17, branch ink-tonemap).**
  Cause of "the colours are compressed": the parity-plus HDR gain keys on RAW DYE INTENSITY
  (`gain = lerp(1, peakGain, smoothstep(knee, capBright, m)^2)`), and the ink look feeds it
  `m = d * hdr_core * motion`. With the shipped `hdr_core` 0.3 and `[hdr] knee` 0.70, `m` maxes
  at 0.405 and the smoothstep is identically ZERO: the ink look used **none** of the panel's
  headroom, whatever `peak_nits` said. Measured, `ink-duo-pour-teal-vermillion.ini` 960x540
  t=60: max **228 nits**, 0.00% of the frame above SDR white (240), on a 1000-nit peak.
  New keys, all inert at their defaults (`tonemap` 0): `tonemap` 0|1, `white_nits` (0 = today's
  SDR white), `black_nits` 0, `tone_knee` 0..1, `tone_chroma` 1, `tint_hue_blend` 0. With
  tonemap=1 the ink composite's own 0..1 range is mapped onto [black, white] nits with an
  optional smoothstep S (mid slope 1.5x = "more steps of brightness"), hue-preserving, and the
  parity-plus gain is RETARGETED above the new white so hot moving cores still climb to
  `peak_nits`. `peak_nits` (ini or tray CMD_PEAK_*) is the ceiling and now also pulls `white_nits`
  down when it is set lower, so the tray peak menu works for ink.
  `tone_chroma` is `post_chroma`'s math, deliberately NOT clamped at 0 — with `gamut` > 0 the
  out-of-gamut components are negative on purpose and the QD-OLED shows them.
  Why vermillion read as pale peach, at source: the duotone pair is cross-faded per pixel by
  `tk` in RGB, and teal (0.04,0.63,0.65) -> vermillion (0.90,0.27,0.15) crosses (0.47,0.45,0.40),
  a dead beige, which is where most of a plume lives. Two fixes tried; the SHIPPED one is the
  existing `tint_mid_dip` (0.50 on the three duo presets) which routes the midpoint through the
  BACKGROUND instead of through grey. `tint_hue_blend` (restore the blend's S/V to the anchors',
  hue untouched) is implemented but ships at 0: it produced vivid green speckle wherever the
  RGB path is briefly green-dominant. Walking the HUE WHEEL instead was tried and rejected
  outright - teal -> vermillion the short way is a trip through green and yellow, which turns a
  duotone into a tricolour (`build2/shots/inktone/a400-hdr.png`).
  240 Hz: TWO causes, one ours. (a) `Present(1, 0)` is vsynced and the software FPS cap's wait
  has a ~2 ms floor, so a loop asking for 240 fps on a 240 Hz panel (4.17 ms vblank) sleeps
  straight past the vblank it aimed at and vsync rounds the miss up to a whole extra refresh -
  a hard 120 fps, exactly the analyzer's 121. FIXED: the software cap is skipped when the
  requested limit is at or above `MonitorRefreshHz()`, with a 1 ms floor left in so an occluded
  Present cannot become a busy spin. (b) the frame itself costs more than 4.17 ms at 1440p, so
  240 is out of reach anyway: headless 60 s at 2560x1440, ink **5.65 ms/frame** (tonemap off) /
  **5.61** (on, so the curve is free), WE fluid **6.01**. Achievable vsynced rates are 240/n, so
  the ink look lands on 120 until the frame drops under 4.17 ms. Not chased - the brief scoped
  this to caps in our code, not a perf pass.
  Nits at t=60, `ink-duo-pour-teal-vermillion.ini` 960x540 seed 1234 --hdr on (bins are % of
  frame, max channel):

  | | min | mean | max | <1 | 1-5 | 5-15 | 15-40 | 40-80 | 80-160 | 160-240 | 240-400 | 400+ |
  |---|---|---|---|---|---|---|---|---|---|---|---|---|
  | before (tonemap 0) | 1.5 | 26.7 | 228 | 0.00 | 5.86 | 16.95 | 20.72 | 8.31 | 20.77 | 27.38 | 0.00 | 0.00 |
  | SHIPPED (white 400 + mid_dip) | 1.3 | 35.2 | 400 | 0.00 | 10.01 | 34.32 | 4.77 | 4.50 | 5.79 | 6.35 | 28.61 | 5.63 |
  | white 600 (no mid_dip) | 2.0 | 70.6 | 600 | 0.00 | 2.84 | 12.20 | 10.99 | 7.09 | 6.03 | 5.03 | 12.45 | 43.36 |
  | white 1000 (no mid_dip) | 3.4 | 117.6 | 1000 | 0.00 | 0.57 | 7.62 | 13.61 | 5.79 | 7.65 | 3.31 | 5.63 | 55.82 |

  Fraction of the frame above SDR white: 34.2% at 400 (shipped), 55.8% at 600, 61.5% at 1000 --
  USER DECISION, and an ABL one: the panel does ~418 nits full-field, so 600 and 1000 mostly buy
  a dimmer whole frame. Acid look for comparison (`acid-rise-12.ini`, same shot, `peak_nits` 1000):
  max **293 nits**, mean 20.6, 11.1% above SDR white -- it does not reach peak either, but unlike
  ink it at least uses some headroom. NOT changed, per the brief.
  Also new: `--shot` logs a 10-bin nits histogram (fixed edges, comparable run to run) next to the
  existing mean/max line. Regression: `we-look-live.ini` 60 s 2560x1440 seed 1234 --hdr on md5
  **10E36EBF1A74EDFE609065D757300054** -- PASS, the fluid look is byte-identical.

- **Verification: oil_edge_mode ship + band-jitter cause fix (Sonnet executor, 2026-09-17, 8b82ad5).** Rote re-check, no code changes; another Opus executor was working in `src/` at the time, so the rebuild was only done after confirming `git status --short` showed src/ clean. `build2\FluidWallpaper.exe` timestamped 22:21:49 predates 8b82ad5's commit time (22:28:56) by ~7 min; `git status` was clean and `build2.cmd FluidWallpaper` reported `ninja: no work to do` (the nested `cmd /c` quoting also threw a harmless `'vswhere.exe' is not recognized` but did not block the no-op build) -- the binary already matched HEAD, build-then-commit ordering as usual, no source files newer than the exe.
  Fix recap: `lensR` (the film-thickness proxy driving the soft edge's fade width) now reads the blob-only gradient wherever that gradient is credible -- near a blob isoline, exactly where the band lives and where a droplet passing through was spiking `|grad|` and jumping the band width frame to frame -- and falls back to the full gradient deep inside a merged mass where the blob-only gradient goes to zero and would otherwise pin `lensR` to its ceiling and turn the oil black. `oil_edge_mode` (0 soft / 1 crisp) ships both film edges as a key: acid-rise-12/-rotate and both "Liquid Acid - rising" presets take mode 1 (crisp); "...(soft film edge).ini" carries mode 0, one tray click from the old look.
  md5 regression (`we-look-live.ini` 60 s 2560x1440): **PASS**, `10e36ebf1a74edfe609065d757300054` matches the expected 10E36EBF1A74EDFE609065D757300054.
  Fresh 1440p 10-frame series (144 fps, one sim-frame apart, `tools/popdetect.py`) against `acid-rise-12.ini` (shipped, oil_edge_mode=1 crisp) and a scratch copy with oil_edge_mode=0 (soft):

  | edge | pops (appear+vanish) | band px/frame | jumpy band px (9 transitions) | jumpy % |
  |---|---|---|---|---|
  | crisp (mode 1, shipped) | 0 | ~86.3k | 2529 | 2.9% |
  | soft (mode 0) | 0 | ~217.8k | 2347 | 1.08% |

  Both modes: 0 pops. The soft edge's absolute jumpy-pixel count sits close to the crisp edge's despite carrying ~2.5x the band area -- proportionally calmer per band pixel -- consistent with the commit's own claim that this fix targets mode 0's jitter at its cause. A/B sheet (960x540, seed 1234, --hdr on, t=20/60/100, `reference/configs/ab.py`): `build2/shots/edge/sheet-edge.png` (1944x1678) -- same palette, layout and droplets between the two columns, the big masses' wide fuzzy halo replaced by a hard isoline in the crisp column. Standing line on model fit: mechanical re-verification (build check, render, count, report); no design judgement was called for or made.

## 2026-09-18 -- CAM/SIM executor day: briefs N through Y, post pass + calibration (Fable rote pass, 4f10360..19d5f4a)

Rote docs pass, no code changes; the following is a same-day recap of the executors' own commits on `main`, grouped by subject rather than by branch (the `camera-dof` and `conservation` topic branches were each merged twice, so merge-commit order does not line up cleanly with feature order).

- **Camera / DOF / tilt-shift (briefs N, R, T; merges `a64c54b`, `af53f84`).** `0f9b098` perspective camera (view-angle asymmetry, curved focal plane) as the real fix for the depth-of-field ring artefact, merged with brief N's per-droplet depth (`6af0ded`, `b0ccfc8` post_blur_px 1.5->1.2). Brief R iterated tilt-shift/freelensing: `d4a96b3` perspective camera, `86ef4f1` idle-drift focus plane, `7e62f37` corrected to occasional readjustment instead of continuous drift. Brief T pushed the look toward the Blade Runner 2049 lab-instrument reference (Territory Studio refs, `a9744b4`, `dd324a1`): razor-sharp in-focus slice with steep falloff (`8ebd567`, `540d8e2`), no instrument/dish rim -- simulate a glass/plastic lid instead (`f7acb9a`), everything advected by the fluid velocity on the OLED (`168bc42`). `d488b35` landed the shared camera rig block; `8bfd600` fixed in-focus edges reading as pixel-stepped AA (softness 0->0.5, user photo); `7f7cebf` tightened dof_max_px 9->5 in the rising files.
- **Diffraction (brief W, `bfde4a7`).** Point-spread blur plus darkness that scales with droplet size; `cb6ff0a` fixed the PSF to blur + size-dependent contrast; `44d6d04` panel photo at the green phase -- user: "damn near looks real."
- **Droplet lens shading (brief X, `af53f84`).** Crisp at the droplet edge, gradual toward the interior (`7371e38`, `f781695`); `f781695` also tightened the DOF ratio by ini (dof 5, depth 0.35, glow 0.22 @ 10).
- **Bloom disc kernel (`74f1094`).** Replaced the two-ring bloom with a 4-radius Gaussian disc kernel plus per-pixel radius jitter -- cause was the user photographing concentric rings around bright droplets in the black.
- **Conservation of mass + clustering (briefs A+S, V; merge `89f4c7c` + the sim executor's population/motion commits).** `5cd7b53`/`6328bdb` conserve_mass in the acid sim (no oil spawning or vanishing in view), shipped to every rising preset in `ea25d37`. Brief V gave the rig one shared state driving lamp, lid ghosts, focus and motion (`dee52c7`), backed by the LAPD optics reference sheet (`447cf02`) and refs 11-14 (`cfc2a1b`); brief U scoped "everything moves" (`1c39472`); `5b51e06` landed the motion model itself (slow drift + occasional all-at-once readjustments). Population/clustering tuning: `101ba5b` (panel photo: medium droplets render as lumpy groups of ~5 -- the defect brief X and `2810fbb` end up fixing), `c0bb68d` step 1 (1500->1350, r_min +20%, r_max +10%, attract 0.40->0.50), `fe23cec` step 2 (1350->950, -30%, more large masses: disc_frac 0.22 / web 0.34 / bubble 0.20 / big_bias 0.62).
- **Coalescence + big hollow bubbles (merge `ef26209`).** `2810fbb` touching solids become one instead of a lumpy group; `b11521f` big hollow bubbles, sized so rings may outgrow the droplet grid cell; `af86a99` panel photo -- user wants more of the big hollow bubble.
- **Film scratches, shipped OFF (`85b3f74`).** Faint, soft-edged, broken into flickering segments -- user: "no film has a line like that."
- **Cellulose / film overlay / light-in-the-water (briefs O, P, Q; `7049d02`, `6009b1d`, `b62e447`).** Brief O: macro cellulose texture, fibrous strands mostly in the black masses (`cbf3a55`, `7049d02`). Brief P: film overlay artefacts -- hairs, dust, scratches, coloured light leak, resolution-independent grain (`05d53fc` supersedes the brief-O reading, `6009b1d` grain/film_noise, `e511711` light-leak + film_stock grade key, `b5986cd` reference film-overlay-4 photo). Brief Q: wide weak bloom with slow idle drift as volumetric "light in the water" (`07b0cc1`, `678de02` revised to bloom + volumetric fog against a backlit-glass reference, `b62e447` landed).
- **V0 output dither (`19d5f4a`).** Half an LSB of dither against 10-bit banding in the output pass -- the day's last commit.
- **Housekeeping.** `daaa3af`/`b7d1f19` kept each worktree's own build/lock helpers out of the branch (appears twice across the two topic-branch merges).
- **Queued next (`5bfe285`, plan only, no code).** Executor CAM: dither (shipped today), halation, lid, motion. Executor SIM: racers, weather. Brief Y (racing micro-bubbles, `1319ac6`) is the first of that queue to land.

**Regression:** every commit above kept `style=fluid` byte-identical; today's own re-check (`we-look-live.ini`, 60 s, 2560x1440, seed 1234, `--hdr on`) -- **PASS**, md5 `10e36ebf1a74edfe609065d757300054` matches the expected `10E36EBF1A74EDFE609065D757300054`.

## 2026-09-18 -- accent-by-size, halation V1, brief Z diagnosis, fullscreen pause fix (Sonnet rote docs pass, f384659..876c72a)

Rote docs pass, no code changes; recap of four more of today's commits on `main` (after the CAM/SIM
executor day above), plus this session's own md5 regression re-check.

- **Accent colour by size (brief J, executor C, `f384659`/`c6efa53`).** `[liquid_acid] accent_mode`:
  0 is today's behaviour, a blob's colour slot is drawn at seed time with no regard for size, so the
  4th shade -- the complement, in the 80-20 palettes -- can land on a big mass as readily as a
  bubble (the "random unintentional green blobs" the user disliked). Mode 1 restricts shade 4 to
  blobs under `accent_max_r` (a fraction of `disc_max`), and only `accent_frac` of those, chosen by
  a hash of the blob index (no `rand()` draw, so toggling the key never moves a blob's size or
  position). Reads `baseR` rather than the breathing radius so a blob near the threshold cannot flip
  shade twice a minute, with a hysteresis band and a 2 s colour crossfade so even a respawn is
  invisible. Shipped: `accent_mode` 1, `accent_max_r` 0.12, `accent_frac` 0.6 in
  `acid-rise-8020.ini` and both "(80-20 ...)" presets only -- the default 0.12 (down from a first
  pass at 0.35) cuts in under the biggest bubbles so only the droplets and small bubbles take the
  accent. `style=fluid` untouched.
- **V1 halation (executor A, `e925d2a`/`04c49ea`).** CineStill-style halation: ordinary film stock
  with the anti-halation backing removed lets light from a highlight cross the emulsion, bounce off
  the film base and return a few pixels out, reddened -- tight (~14 px), taken only from genuine
  brightness, landing in the dark AROUND a highlight rather than on it, unlike the existing wide
  `bloom`. Two jittered rings of taps (8 + 12) around a centre pulled toward the rig's lamp, so the
  glow's asymmetry turns as the lamp wanders. Finding worth keeping: the brightness gate must read
  the PEAK CHANNEL, not luminance -- written against luminance first, the effect evaluated to
  exactly zero on every frame, because this film is a saturated magenta (a 300-nit pink has R near
  4.0 scRGB but Y near 1.0), so a luminance threshold calls the brightest thing in the frame dark.
  Keys `halation` (0, ship 0.25), `halation_px` (14 @ 1440p), `halation_warmth` (0.75), shipped in
  every `acid-rise-*` config and every "rising colours" preset; the "(NO lens effects)" twin keeps
  them off.
- **Brief Z: real lateral CA needs a post-pass resample (`efbec41`, diagnosis only, assigned to
  executor A).** Doc-only commit (`reference/briefs/NEXT-rings-grain-aberration-refraction.md`), no
  code yet. The shipped chromatic-aberration keys (`aberration`/`aberration_px`/`aberration_field`)
  only ever gave a symmetric warm rim -- red-plus/blue-minus on every side of every droplet -- because
  the display pass computes `dR`/`dB` as a first-order derivative along the LUMINANCE gradient, which
  always points toward the brighter side; a real lateral split needs the sign to flip across the
  edge, which a derivative at a single pixel cannot do, and the display pass has nothing stored to
  resample anyway. `m_postTex` in the post pass does, so the fix is queued there: sample R/G/B at
  `uv +/- n_r * s` (`n_r` from the rig's lens centre, not the frame centre; `s` a radial ramp scaled
  by `aberration_px`/`aberration_field`), same key names, verified with a 4x crop showing red on the
  outer edge of a corner droplet and blue/cyan on the inner edge. `style=fluid` keys stay 0, so md5
  is unaffected either way.
- **Fullscreen pause fix (`876c72a`).** The pause-while-gaming detector keyed off window style
  (`WS_CAPTION` absent = fullscreen), which missed windowed-fullscreen titles that keep the caption
  style while their client rect still covers the whole monitor -- Deadlock in that mode was never
  pausing the sim. Now detects by comparing the foreground window's client rect to the monitor rect
  and ignores window style entirely. `src/main.cpp`, 12 lines changed.

**Regression (this session, Sonnet rote, worktree exe built from `876c72a`):** `we-look-live.ini`
60 s 2560x1440 seed 1234 `--hdr on` -- **PASS**, md5 `10E36EBF1A74EDFE609065D757300054` matches
expected.

## 2026-09-18 -- bubble weather, blob-count residue, brief AA depth (Sonnet rote docs pass, b243780..99f933c)

Rote docs pass, no code changes; recap of executor B's two commits on the `conservation` branch
(U9 bubble weather, brief S residue), the merge that landed them on `main`, and a diagnosis-only
brief AA commit, plus this session's own md5 regression re-check.

- **U9: bubble weather (`0a87ca2`).** The droplet population has exactly one steady state and
  settles into it -- after a couple of minutes the frame's density and its ring/solid mix never
  change again. `[liquid_acid] weather` (0 = today) and `weather_period_s` give it seasons instead:
  a spell of rings and big hollow bubbles, then of solids, a sparse spell, a crowded one. One smooth
  signal per channel walks between per-phase targets hashed from the phase index, holding each
  target for a hashed 25-75% of its length before easing across the rest, so similar consecutive
  targets read as one long season and the phase grid never shows; it is a pure function of
  wallpaper time, so nothing needs to be carried across a pause, a resume, or a `--shot` replay.
  The density channel scales the droplet target by +/-0.45*weather, gated on `conserve_mass` --
  without that gate a moving target reseeds the whole population every frame instead of growing and
  shrinking it; with it, the existing dissolve/grow machinery does the work. The ring channel scales
  `droplet_ring_frac` by +/-0.90*weather and `droplet_ring_big_frac` by +/-1.30*weather -- the share
  alone barely read (a ring must clear a band floor 3.5x the solid one to resolve), so the BIG hollow
  bubbles are what a spell of rings actually looks like. Shipped weather 0.5 / weather_period_s 300
  in all 24 rising configs and presets, NO-lens twin included. Verified (25 captures 15 s apart over
  6 min at 960x540 seed 1234, weather_period_s cut to 60 so seven phases fit in one render): visible
  droplets 705..859 (19% swing) wandering smoothly, largest step between two samples 11% of the
  mean; visible rings 1..10; big hollow bubbles 0..3.
- **Brief S residue (`7fb7058`).** Two sites the conservation pass left behind, both under
  `conserve_mass` and both exactly today's behaviour with the key off. (1) `blob_count`: moving the
  slider called `SeedAcidBlobs()`, replacing the whole population in one frame. Now the population
  WALKS -- `AcidBlob` gains `rTarget`, `baseR` relaxes toward it (spawn_grow_s up, dissolve_s down),
  and `AdjustAcidBlobCount` marks the surplus to shrink away (preferring off-frame blobs, then the
  smallest) and grows a shortfall in from under the bottom edge through the same door `rise_respawn`
  uses; a blob is erased only once it is under a fifth of a pixel of Wyvill reach. (2) The rise
  respawn used to redraw `baseR`, so a blob left the top at one size and came back under the bottom
  at another -- mass appearing/vanishing off-frame, the one thing this key forbids. It now keeps its
  radius; fresh x and curl phases are what stop the column reading as a loop. The real bug: a blob on
  its way out kept its way out, but restoring its target on a respawn handed a *different*, already-
  retiring blob its life back, so Adjust retired somebody else instead and the population churned --
  total blob area bled from 2.71 to 0.90 and kept falling instead of settling. Verified: `blob_count`
  96 -> 56 applied live at t=60 via `--shot-preset`, 80 captures 0.5 s apart at 960x540 seed 1234.
  Dark-pixel change at the swap frame vs. that run's own ordinary frame-to-frame motion: conserve_mass
  0 hit 138216 (median 59396, p99 111420, a 2.3x jump); conserve_mass 1 hit 36911 (median 27614,
  p99 36417, right at the p99 -- no jump). Total blob area walks 2.713 -> 0.889 over ~20 s and then
  holds. Sim-state parity at conserve_mass 0 after 60 s of `acid-rise-12`: md5
  `5A2C5901A43ADB7C6DD2CA5A8BCCBCDE`, unchanged.
- **Merge (`99f933c`).** `conservation` (bubble weather + S residue) merged onto `main`; both sides
  touched `fluid.cpp`/`fluid.h` in the same neighbourhood (the accent-by-size block from earlier
  today and the new `rTarget` block), so the merge kept both blocks rather than letting either side
  clobber the other.
- **Brief AA (`b243780`, doc-only).** Dye masses currently sit at the focus depth by construction --
  the depth accumulator's prior is 0.5, which is exactly `camera_focus`, so nothing has ever pushed a
  mass off the focal plane on its own. A slider test at curvature 0.9 / cap 14 softened the corner
  droplets but barely moved the mass edge, confirming the depth prior -- not the curvature -- is the
  lever that matters. Fix queued for later: a rig-driven `dye_depth` key assigned at the point the
  depth accumulator is seeded, rather than tuning curvature/cap further. No code yet.

**Regression (this session, Sonnet rote, worktree exe built from `99f933c`):** `we-look-live.ini`
60 s 2560x1440 seed 1234 `--hdr on` -- **PASS**, md5 `10E36EBF1A74EDFE609065D757300054` matches
expected.

## 2026-09-18 -- brief Z lateral aberration lands; merge onto camera-dof for item AA (Sonnet rote docs pass, c61ff10..b4173dd)

Rote docs pass, no code changes; recap of executor A's brief Z implementation and the merge that
brought `main`'s weather/residue work (`99f933c`) onto the `camera-dof` branch alongside it, plus
this session's own md5 regression re-check.

- **Z: lateral chromatic aberration, as a real resample in the post pass (`c61ff10`).** The
  diagnosis from `efbec41` is now code: the display pass's derivative trick only ever gave a
  symmetric warm rim -- the offset was taken along the luminance gradient, which always points at
  the brighter side, so red was added and blue subtracted on BOTH sides of every dark droplet, never
  a split. The fix moves into `kPostSrc`, which has the finished frame as a texture and can resample
  where the channels actually landed: red pushed OUT from the optical axis, blue pulled IN, green
  where it belongs. Applied as the DIFFERENCE the displacement makes rather than a raw resample,
  since `d` already carries the defocus and glare that `Src` lacks -- a sharp edge shows the full
  split, a defocused element's two samples are nearly equal and the fringe fades out with the blur.
  The optical axis is the rig's lens centre, which now has its own slow idle drift (in step with the
  lamp, out of phase with it), so the null point of the split never burns into one spot of the panel.
  Keys unchanged (`aberration`/`aberration_px`/`aberration_field`, same ranges); shipped 1.0/1.8/1.2
  in all `acid-rise-*.ini` and rising presets, NO-lens twin left off. The old derivative version is
  removed from the display pass entirely -- that pass has nothing stored to resample anyway. Rig
  block relabelled: `rg0` lamp+lens centre, `rg1` tilt/focus/phase, `rg2` chromatic split, `rg3`/`rg4`
  still free. `style=fluid` names none of these keys and the post pass is not entered there, so md5
  is unaffected.
- **Merge (`b4173dd`).** `main` (`99f933c`: bubble weather + brief S residue) merged into the
  `camera-dof` branch, bringing that work together with brief Z and brief AA's diagnosis ahead of
  item AA's fix.

**Regression (this session, Sonnet rote, worktree exe built from `b4173dd`):** `we-look-live.ini`
60 s 2560x1440 seed 1234 `--hdr on` -- **PASS**, md5 `10E36EBF1A74EDFE609065D757300054` matches
expected.

## 2026-09-18 -- item AA dye depth lands; film grain frame rate (Sonnet rote docs pass, 5c8deee..dbc55e0)

Rote docs pass, no code changes; recap of executor A's item AA fix (previously diagnosed only in
`b243780`), the merge that landed it on `main`, and executor Fable's film-grain frame-rate key.

- **AA: the dye masses get a depth of their own (`5c8deee`).** The display pass's per-pixel depth
  accumulator was primed with the constant 0.5 -- exactly `camera_focus` -- so every pixel with no
  droplet in it, i.e. every big dye mass, sat ON the focus plane by definition; only the focus
  surface's own curvature and tilt could ever defocus one, never the mass's own position, matching
  the `b243780` diagnosis. Three new `[liquid_acid]` keys give the dye a layer of its own:
  `dye_depth` (0.5 default and shipped, the old constant to the bit), `dye_depth_tilt` (0
  default, 0.28 shipped) -- a slope along the direction from the lens centre to the lamp, chosen so
  the dye slab is NOT parallel to the focus surface and the two cross on a travelling line rather
  than agreeing over a whole region -- and `dye_depth_w` (0.25), the weight the dye carries where
  droplets overlap it. `dof_max_px` goes 5 -> 9 in the rising configs alongside it, since the
  corners were already at the old clamp and had nowhere to show the extra depth. Measured on the
  corner mass edge: peak gradient after denoise 16.3 -> 13.5 (lower = softer) -- edge-energy metrics
  were dropped in favour of peak-gradient because grain was dominating them. One still is one rig
  pose; the number moves with wherever the rig happened to be pointed when the still was taken.
- **Merge (`56c98c5`).** `camera-dof` merged onto `main`, bringing item AA's fix together with
  brief Z and the bubble weather / brief S residue work already recapped above.
- **Film grain frame rate (`dbc55e0`).** At 240 Hz the old hardcoded 144 Hz grain/film-noise
  pattern changed every 1.67 refreshes, an uneven fizz. The new `film_grain_fps` key (default 24;
  both 24 and 30 divide 240 exactly) paces the pattern to a whole number of refreshes per change, as
  on real film stock -- slider range 1..240. `poP1.y` carries it in the display pass, `pp6.w` in
  the post pass. `style=fluid`: grain 0, branches not entered, md5 unaffected.

**Regression (this session, Sonnet rote, worktree exe built from `dc2febd` -- two brief-only
commits past `dbc55e0`, `git diff --stat dbc55e0 dc2febd -- src` empty):** `we-look-live.ini`
60 s 2560x1440 seed 1234 `--hdr on` -- **PASS**, md5 `10E36EBF1A74EDFE609065D757300054` matches
expected.

## 2026-09-18 -- V3 motion: the rig drifts always, re-aims all at once (Sonnet rote docs pass, 53217e9..d364e9e)

Rote docs pass, no code changes; recap of executor A's V3 motion keys and the merge that landed
them on `camera-dof` ahead of the transparent lid.

- **V3 motion, the rig drifts always and re-aims all at once (`53217e9`).** Five `[liquid_acid]`
  keys following the user's rule that nothing sits at a fixed OLED position and that motion is
  either a slight constant drift or an occasional all-at-once readjustment, never one effect
  moving alone: `shimmer`/`shimmer_px` (fine refractive wobble above the lamp, advected by the
  sim's own low-res velocity texture `t3` bound to the post pass, with the velocity term clamped
  so a burst shoves the heat with it instead of turning into flicker), `vignette_wander` (the
  vignette centre follows the lens via `poP1.xy` instead of sitting at a constant 0.5,0.5),
  `pixel_shift_px` (the whole finished frame orbits on two incommensurate ~7 and ~11 minute
  periods, applied to the coordinate the frame is READ from so defocus/aberration/halation/haze
  travel with the picture while grain and dither stay in screen space), and `rig_readjust` (how
  far the lamp and lens centre re-aim, riding the same damped spring as the tilt and focus, so
  the rig arrives together). `--shot` now prints a `[rig]` line (lamp, lens, tilt, focus, orbit);
  a rig table across t=30..120 on `acid-rise-12` showed the lamp/lens/orbit moving at every
  sample while the tilt/focus moved exactly once, with the two readjustments' worth of movement
  confirming the whole body moves together, not one effect alone. Ship: shimmer 0.35 /
  shimmer_px 1.2 / vignette_wander 0.6 / pixel_shift_px 3 / rig_readjust 0.5 in every rising
  config and preset; the "(NO lens effects)" twin keeps all five off. Lives in `rg3` of the rig
  block, leaving `rg4` free for the transparent lid on its own branch.
- **Merge (`d364e9e`).** `camera-dof` (V3 motion) merged; `poP1` repacked as vignette centre xy,
  grain fps in `.z`, vignette weight in `.w`.

## 2026-09-19 -- V2: the transparent lid lands; merge onto main (Sonnet rote docs pass, 8afca1d..01fa483)

Rote docs pass, no code changes; recap of executor B's transparent-lid keys (landed as a merge
commit on the `lid` branch) and the merge of `lid` onto `main`.

- **V2: the transparent lid (`8afca1d`, merge of `1269e93` + `d364e9e`).** Nine `[post]` keys,
  all default 0, simulating the transparent cover over the dish rather than any visible
  instrument or rim (negative constraint, brief T note 3 -- no mask, no border, full-bleed edge
  to edge): `lid`, `lid_ghost`, `lid_ghost_spread`, `lid_rings`, `lid_sheen`, `lid_sheen_px`,
  `lid_glint`, `lid_iris`, `lid_refract_px`. Ghosts, rings, sheen, iris and glint are all
  positioned from the rig -- the drifting lamp, the lens centre, and a lid wander of its own on
  four incommensurate 3-12 minute periods -- and are shoved to a new resting offset on the same
  damped focus spring at every readjustment, so nothing sits at a fixed screen position and
  nothing moves alone. All twelve numbers (position, rotation, five amplitudes, refract px,
  sheen px, ghost spread) are packed losslessly into the single `rg4` rig slot, since the
  graphics root signature was already at its 64-DWORD cap. Ship: subtle 0.45 / 0.24 / 1.0 / 0.25
  / 0.16 / 400 / 0.15 / 0.22 / 2.5 in all four `acid-rise-*.ini` and the base rising preset;
  presets "(lid subtle)" / "(lid medium)" / "(lid strong)"; every key explicitly 0 in the "(NO
  lens effects)" twin. The sheen is the term that lifts the OLED black -- a big dye mass's mean
  went 22 -> 34 of 255 at the subtle setting -- and `lid_sheen` is the single knob to reach for if
  the black needs to come back down (lowering the ghosts barely moved it).
- **Merge (`01fa483`, "Merge branch 'lid'").** `lid` merged onto `main`; needed a `kPostSrc`
  literal split, since MSVC caps a single string literal at 16380 bytes and the combined
  V3-motion + lid post-pass source landed at 16762. Verified at commit time: style=fluid parity
  md5 `10E36EBF1A74EDFE609065D757300054` on `we-look-live.ini`, seed 1234, 60 s, 2560x1440,
  `--hdr on` -- unaffected, so the post pass is still never entered for the fluid look.

**Regression (this session, Sonnet rote, exe `C:\Users\abg77\fw-wt\build2\FluidWallpaper.exe`,
confirmed built from `01fa483`):** `we-look-live.ini` 60 s 2560x1440 seed 1234 `--hdr on` --
**PASS**, md5 `10E36EBF1A74EDFE609065D757300054` matches expected.

## 2026-09-19 -- acid-rise-12 de-fuzz (Fable, b5c79bb)

The user: acid-rise-12 was "too fuzzy/fine, hazy". Config-only tune in `acid-rise-12.ini`,
no other rising config touched: `film_noise` 0, `film_grain_size` 2.5 px, `shimmer_px` 0.8 px,
`fog` 0.22, `lid_sheen` 0.04.

## 2026-09-19 -- AB: bubble crust on the masses; merge onto main (executor B, 7004632..7ee65e2)

Brief AB: the user's lava-lamp reference photos (`reference/shots/photos/lavalamp-ref-1..7`)
show the big wax masses carrying a dense crust of bubbles inside and on them, with the liquid
around them nearly clean -- the inverse of what we had (droplets on the open film, masses empty
black). The user's clarification settles the design as NOT a swap: the film keeps everything it
has today and a crust is added beside it.

- **Crust as a second population (`7004632`).** `[liquid_acid]` gains `droplet_mass_bias` (crust
  share of the film population), `droplet_crust_density` (crust density inside a mass vs. the
  film, 1 = none), `droplet_crust_r` (crust bubbles are small, `r_max` times this), and `mass_rim`
  (the display term below). The emitter now counts film and crust separately and tops each
  against its own spawn target, so neither can starve the other; with `droplet_mass_bias` 0 the
  crust target is 0 and this is the original single-population loop, unchanged.
- A crust bubble nucleates deep inside a mass (sdf < -0.010, in the black body rather than on the
  rim where the confinement term would eject it) with probability rising with the mass's
  thickness, which is what makes the crust patchy rather than an even sprinkle. It rides the mass:
  `AcidFieldAt` gained an optional soft-max blend over the negative blobs so a crust bubble takes
  the local dark mass's own velocity, the way a trapped droplet takes the oil's, instead of the
  water's flow (which would drag it out of the body within seconds); a quarter of the water's flow
  is kept so crust over open ink still drifts rather than freezing.
- Crust never coalesces, and getting that to look like foam took two corrections after the first
  render: excluded from the contact merge, it sat at exactly touching and two metaball fields
  simply blended into one capsule (a dash, not two bubbles), so crust now rests just outside
  contact (1.10x the sum of radii); that alone produced rigid rafts -- attraction plus a fixed
  rest distance locks half a dozen bubbles at one spacing, reading as a single lumpy polygon, the
  same failure the solid droplets had before `droplet_coalesce` -- so crust does not pull on
  itself at all, only nucleation placement plus the mass's own carry.
- `mass_rim` (display, `laP29`): a thin bright band just outside the isoline on the mass side
  only, scaled by the dot of the surface normal (`-grad(field)`) with the rig's lamp direction
  (`laP28.xy`, already carried for item X) -- the far side of the same mass gets nothing, which
  stops it reading as an outline and makes it read as a lit translucent body (refs 5, 7). Peak
  channel, never luminance.
- Ship 0.6 / 2.5 / 0.6 / rim 0.35 in all four `acid-rise-*.ini` and the base rising preset --
  "not as extreme" (the user): the reference photos are almost all crust, this lands at a third.
  Verified at seed 1234, t=60 (droplets counted in frame): in-mass (visible) / on-film (visible)
  / mass share -- off (today) 133 (75) / 1425 (794) / 8.5%; shipped 571 (265) / 1105 (610) /
  34.1%; high 0.9/3.5 623 (272) / 1410 (765) / 30.6%. The film count moves non-monotonically with
  the bias (1425 / 1105 / 1410), which is RNG-stream divergence and the weather phase, not the
  crust starving it -- the high config carries more crust AND more film than the shipped one.
  style=fluid parity md5 `10E36EBF1A74EDFE609065D757300054`, unchanged. A/B sheet
  `build2/shots/cons/crust/SHEET-AB-crust-1440p.png`.
- **Merge (`7ee65e2`, "Merge branch 'crust'").** `crust` merged onto `main`.

**Regression (this session, Sonnet rote, exe `C:\Users\abg77\fw-wt\build2\FluidWallpaper.exe`,
confirmed built from `7ee65e2`):** `we-look-live.ini` 60 s 2560x1440 seed 1234 `--hdr on` --
**PASS**, md5 `10E36EBF1A74EDFE609065D757300054` matches expected.

## 2026-09-19 — gpu.lock stale-reclaim helper (Fable, tooling only)

Added `tools/gpu-lock.ps1`, a dot-sourceable helper (`Wait-GpuLock -Owner ... [-TimeoutMinutes
30]` / `Release-GpuLock`, plus a `-Acquire`/`-Release` CLI wrapper for non-PowerShell callers) to
replace every render/build script's own ad hoc `Test-Path`/`Set-Content` wait loop on
`build2\shots\gpu.lock`. Motivation: a force-killed holder skips its `finally`, orphaning the lock
until a human deletes it. The lock now carries `pid=<holder>` and a waiter reclaims it once that
pid is dead (or, for the no-pid legacy format, once the file is >45 min old), logging what it
reclaimed and why to `build2\shots\gpu-lock.log`; acquisition uses `[IO.File]::Open(...,
FileMode::CreateNew)` so two waiters racing a freshly-freed lock can't both win, and release only
ever deletes a lock stamped with the caller's own PID. Verified without any GPU work: (1) a lock
with a dead pid was reclaimed and logged immediately; (2) a lock held by a real (sleeping)
process was NOT reclaimed and was only taken ~21s later, after that process exited; (3)
`Release-GpuLock` refused to delete a lock stamped with a foreign pid and logged the refusal.
Known gap: the CLI `-Acquire`/`-Release` pair only works correctly when invoked from the *same*
process, since each `powershell -File ...` call is a fresh process with its own PID — a caller
that acquires via CLI and releases via a second, separate CLI call will always get refused. Fine
for the dot-sourced (same-process) path that `tools/shot.ps1`-style scripts and the scratchpad
`build-wt-only.ps1` use; not yet fine for a genuinely separate-process external caller, which
would need the lock's pid to be settable rather than always `$PID`. No existing script was
switched over except the scratchpad `build-wt-only.ps1` (outside the repo); `tools/shot.ps1` and
`tools/ink3d-shot.ps1` still have their own inline wait loops and are candidates for a follow-up.

## 2026-09-19 -- AE: multicolour oil, a second dye hue in the film; merge onto main (Fable, 6c71d32..90f97b9)

Brief AE (ref `oil-colour-combo-ref-1.jpg`, the colour combo the user rated positive): a magenta
film carrying soft cyan patches that read like a light leak, black masses over both, and the
droplets inside the black lit in the second colour. The AE+AH decision block says why this is a
DYE and not a film-stock overlay: an additive leak lifts the black corners and this panel's black
has to stay black; and in the keeper the cyan sits UNDER the masses, which only a dye in the film
can do.

- **The field.** One scalar per cell, 0..1, on a 20x12 grid, stepped every frame: advected
  semi-Lagrangian off the same 64x36 velocity readback the blobs ride (plus the oil's own rise),
  so a patch stretches and folds WITH the flow instead of sliding over it; slow two-octave value
  noise injected at the patch scale; decay toward a base, without which advection plus injection
  walks the whole field to one value over minutes and the second hue stops being patches and
  becomes a flat tint. The noise phase jumps on the rig's readjustment, eased over ~1.6 s, so the
  patches re-lay at the moment the lamp, the lid and the focus move.
- **The cbuffer-slack field.** Rides the acid cbuffer's slack, four cells per float4: no new
  texture, no new descriptor, nothing added to a root signature already at its 64-DWORD limit. Per
  pixel it costs one fetch and a smoothed bilinear blend. 20x12 is not a compromise -- the
  reference's patches are a quarter to a half of the frame across.
- **The hue-rotation-not-blend fix.** A real bug on the way here, not a style choice: the first
  render had correct structure, correct patch scale and a byte-exact parity md5, and the patches
  came out pale lavender-white -- `lerp(magenta, cyan, 0.5)` in RGB is GREY, and at
  `film_hue2_amt` 0.5 the patch cores sat exactly on it. Fixed by ROTATING the hue with
  `AcidHueShift` instead of cross-fading toward the rotated colour; rotation also holds saturation
  and value, so the film is as vivid in the second colour as in the first. That changes what the
  amount MEANS: how far AROUND the wheel a patch travels, not a cross-fade fraction -- so shipped
  amount is 1.0 (half of -140 deg is blue, not half-cyan), not the 0.5 the brief assumed for a
  blend. `crust_hue_mix` keys off the BLOBS-ONLY field, not sdf: a crust bubble (brief AB) sits in
  a mass but punches the field above threshold at its own pixels, and sdf alone can't tell it from
  the open film.
- **Keys** (`[liquid_acid]`, sliders with help text, all defaulting to 0 amount): `film_hue2`,
  `film_hue2_amt`, `film_hue2_scale`, `film_hue2_drift`, `film_hue2_decay`, `film_hue3`,
  `film_hue3_amt`, `crust_hue_mix`. Shipped in `acid-rise-12.ini` ONLY: -140 / 1.0 / 0.35 / 1.0 /
  0.35 / 0 / 0 / 1.0 -- that preset sweeps eight hues, so `film_hue2` is a rotation off whatever
  the film currently is and holds the relationship at every step of the sweep.
- **The literal split.** Adding the block pushed a `kPostSrc` literal past MSVC's 16380-byte cap;
  split at the nearest top-level banner to its midpoint. Largest literal is now 14081.
- Verified: style=fluid parity md5 `10E36EBF1A74EDFE609065D757300054` on `we-look-live.ini`, 60 s,
  2560x1440, seed 1234, `--hdr on` -- held across every build here. Shot and crop sheet (film
  patch, mass edge over a patch, crust inside a mass) at `build2/shots/live/hue2-on.png` and
  `hue2-crops.png`; the patches read like the reference.
- **Merge (`90f97b9`, "Merge branch 'hue2'").** `hue2` merged onto `main`.
- **Panel keeper (`10f8bc4`, 13:43).** hue2 live on the OLED (acid-rise-12); user verdict "its
  beautiful". Keeper `reference/shots/panel/2026-09-19-hue2-cyan-in-magenta-beautiful.png`.

## 2026-09-19 -- Shimmer A/B, verdict deferred to the live feature tour (brief AI)

Shipped `shimmer`/`shimmer_px` 0.35 @ 0.8 px vs off: whole-frame MAD 0.11, invisible in a still;
1 @ 6 px MAD 1.84 with rim fringing -- verdict deferred to the live feature tour (brief AI); sheet
`build2/shots/live/shimmer-ab-sheet.png`; first attempt used the stale `build2/FluidWallpaper.exe`
and was redone.

## 2026-09-19 -- AE-b: the hue pair rotates for combos, the contrast hue wobbles; merge onto main (Fable, f840685..09fc257)

Brief AE-b (user, right after the hue2 panel keeper: "oh my", "its beautiful" -- "can't the global
hue just rotate to cause combos, while the contrast colour wiggles around like 170-190 degrees
opposite the primary?"), brief written to
`reference/briefs/NEXT-rings-grain-aberration-refraction.md`.

- **Preset parity (`f840685`).** `film_hue2`/`_amt`/`_scale`/`_drift`/`_decay`, `film_hue3`/`_amt`,
  `crust_hue_mix` had shipped in `acid-rise-12.ini` only (brief AE); added at their defaults (all
  0 amount) to the other 33 acid configs/presets (`reference/configs/acid-rise-{2hue,8020,rotate}.ini`,
  `reference/presets/Liquid Acid*.ini`) so every acid look carries the same keys, none turned on.
- **CONFIRMED, no change needed: `film_hue2` is already relative.** `hue_rotate_period` (existing
  key) rotates the whole oil family on the CPU (`effOil`/`UploadAcidConstants`) into
  `laOil`/`oilBase`; the shader shifts `oilC` by `film_hue2` afterwards, so the primary and its
  contrast rotate as a PAIR and the combo drifts round the wheel. Neither the rotation nor the
  hue2 shift touches the ink ramp, so the masses stay black through every hue; crust droplets are
  drawn from the oil colour, so they rotate with the pair.
- **`film_hue2_wobble` (deg, default 0) / `film_hue2_wobble_period` (s, default 300) (`fed09f9`).**
  The contrast hue is no longer nailed to one angle: two sines whose periods are in the golden
  ratio (never repeats exactly) plus the rig's readjustment phase, so it re-aims the same moment
  the lamp/lid/focus/hue2-patches do. Sliders with help text in `src/settings.cpp`, same
  `[liquid_acid]` section/key names.
- **acid-rise-12.ini: `film_hue2` 180 / `film_hue2_wobble` 10 / `film_hue2_wobble_period` 300 /
  `hue_rotate_period` 3600.** The pair lives in 170..190 as asked. `hue_rotate_period` is 3600,
  NOT the 10-20 min the brief suggested: `hue_sweep_period` 630 is already a global hue motion
  (eight curated families cross-faded in HSV, ~0.57 deg/s); a 900 s rotation adds another
  0.40 deg/s on top and the two together ran the frame through most of the wheel inside three
  minutes on the 60/240/420 strip -- it read as a rainbow, not a combo. At 3600 s the rotation
  adds about 17%: the eight sweep families never land on the same combo twice, nothing looks
  sped up. The sweep was NOT disabled in its favour: `sweep_ink_1..8` are all mono grey, while the
  base `ink_stop_*` fallback carries a teal and an orange, so turning the sweep off would have
  coloured the "black" masses and broken the one thing AE guarantees.
- **Patch band narrowed to `smoothstep(0.56, 0.68)`.** `k2` sweeps the hue continuously from 0 to
  `film_hue2`, so a 180 deg contrast passes through every intermediate hue on the way; at the old
  band (0.50..0.74) that put a full rainbow across a quarter of the frame. It didn't show at the
  old -140 (shorter arc); going to 180 as asked exposed it. Narrowed, the rainbow is the thin rim
  the reference actually shows at a patch edge.
- **hole_max trial reverted.** A `hole_max` 0.130 -> 0.150 step (bigger rare masses, same
  count/share) was tried and measured -- big masses 27.6% of frame before, 25.7% after, i.e. it
  went slightly DOWN -- but the sim is chaotic over 60 s and a single frame can't resolve a change
  that small. The user's latest word was "it's fine", so the mass keys stayed at baseline; a real
  answer needs a multi-frame area average, not a one-shot judgement call.
- Verified: style=fluid parity md5 `10E36EBF1A74EDFE609065D757300054` on `we-look-live.ini`, 60 s,
  2560x1440, seed 1234, `--hdr on` -- held on every build here. Strip
  `build2/shots/live/hue2b-strip.png` (t = 60/240/420, 640x360): magenta+green, blue+orange,
  green+magenta -- a clean complementary pair each time, thin rim, combo changed. The strip was
  rendered with the `hole_max` 0.150 trial still in the ini (mass size only, not the colour result
  it's evidence for); the shipped value is baseline 0.130.
- **Merge (`09fc257`, "Merge branch 'hue2b'").** `hue2b` merged onto `main`.

## 2026-09-19 -- Preset parity: film_hue2_wobble/_wobble_period at defaults into the other 33 acid files (Fable rote)

`09fc257` shipped `film_hue2_wobble`/`film_hue2_wobble_period` tuned (10/300) in `acid-rise-12.ini`
only. Added both keys at their DEFAULTS (0 / 300, confirmed against `src/fluid.h`) to the same 33
files that got the base hue2 keys in `f840685` (`reference/configs/acid-rise-{2hue,8020,rotate}.ini`,
`reference/presets/Liquid Acid*.ini`), right after `film_hue2_decay`, none of them turned on. No
comment lines were added: the actual `acid-rise-12.ini` diff (`fed09f9`) shipped the wobble keys
bare, with no new comment block of its own, so none was invented here for the other 33. Each file
gained exactly two lines, zero deletions; each file's own line endings (CRLF or LF -- the 33 files
are a mix) and no-BOM were preserved, verified with `git diff --check` plus a byte-level scan.
`src/settings.cpp` already carries both sliders with help text under the same `[liquid_acid]`
section/key names -- confirmed, no change needed there.

## 2026-09-22 02:xx overnight: user notes filed as AK-.., hue2c in progress

Brief items AK-BA (the user's 16 notes, 2026-09-20) filed verbatim to
`reference/briefs/NEXT-rings-grain-aberration-refraction.md`, plus panel photos and a colour-wheel
reference (`a074c19`).

**AE-c: hue2 patches are generated off screen and only rise (`6336c36`, merged `b09e0bc`).** User's
standing rule, repeated again tonight: colours must not appear out of thin air, everything has to
be generated off screen and only move up once inside. The mix grid is now taller than the screen --
rows 0..kMixH-1 are the visible frame, `film_hue2_seed_rows` hidden rows sit under the bottom edge.
New material is made ONLY in those hidden rows; a visible cell is pure advection, taking only what
was below it, so nothing can form, brighten or re-lay inside the frame. Vertical velocity is
clamped at or below zero (uv y is down) so a patch can be carried up fast or slow, sideways, or
stretched, but never down. The readjustment no longer re-lays the field -- it changes the phase and
scale of what is being seeded below the edge instead, so the composition still turns over minutes
while everything still walks in from underneath. One deliberate deviation from the brief, which
said decay unchanged: decay now applies only in the seed rows, because with nothing to inject in
the visible rows the only thing left to relax toward there is the base, and at the shipped 0.35
that is a time constant under three seconds -- a patch entering at the bottom would be gone before
it climbed a tenth of the screen and the feature would not exist at all. Decay now shapes material
while it is still being made, below the edge; the key and its range are unchanged. New keys
`film_hue2_seed_rows` (default 2; 0 restores the old in-frame injection, so the two can be A/B'd
from one binary) and `film_hue2_rise` (screen heights per minute, default 0.25); sliders, help
text, getF/putF. Both only matter when `film_hue2_amt` > 0, so every existing preset besides
acid-rise-12 is untouched. `acid-rise-12.ini`: seed_rows 2, rise 0.3, with comment lines.
Provenance check on the mix field itself (the `FW_ACID_DUMP` csv, visible and hidden rows), 2 s
apart, local window (rows [y,y+2], cols [x-3,x+3]) sized to what the flow can actually move in the
interval: old in-frame injection 44 violations of 1680 (2.6%, worst excess 0.0358); off-screen
generation 0 violations of 1680 (0.0%, worst excess 0.0000) -- no visible cell ever exceeds what
was locally at or below it. (A coarser 60 s-apart check was inconclusive -- 44.3% before / 5.5%
after -- because material legitimately arrives from the seed rows over that gap, and an at-or-below
test across it flags those legitimate arrivals too.) style=fluid parity md5
`10E36EBF1A74EDFE609065D757300054` held on `we-look-live.ini`, 60 s, 2560x1440, seed 1234,
`--hdr on`. Strip `build2/shots/live/hue2c-strip.png` (t = 60/120/180/240/300, 640x360): the combos
turn and the patches climb. Headless only this session -- no panel check yet.

## 2026-09-22 -- Preset parity: film_hue2_seed_rows/film_hue2_rise at defaults into the other 33 acid files (Fable rote)

`6336c36` shipped `film_hue2_seed_rows`/`film_hue2_rise` tuned (2/0.3) in `acid-rise-12.ini` only.
Added both keys at their DEFAULTS (2 / 0.25, confirmed against `src/fluid.h`) to the same 33 files
that got the base hue2 keys in `f840685` (`reference/configs/acid-rise-{2hue,8020,rotate}.ini`,
`reference/presets/Liquid Acid*.ini`), right after `film_hue2_wobble_period`, none of them turned
up. No comment lines were added: the actual `acid-rise-12.ini` diff (`6336c36`) shipped these two
keys alongside comment lines of its own, but the wobble-key precedent (`6e4a81c`) is to add the
bare keys only to the other 33, so none was invented here either. Each file gained exactly two
lines, zero deletions; each file's own line endings (CRLF or LF -- the 33 files are a mix) and
no-BOM were preserved, verified with `git diff --numstat` plus a byte-level scan. `film_hue2_amt`
is still 0 in all 33, so the new keys are inert there too. `src/settings.cpp` already carries both
sliders with help text under the same `[liquid_acid]` section/key names -- confirmed, no change
needed there.

## 2026-09-22 02:45 overnight (Fable)
AG dye keys merged (7e0050b, e916547); dye_hue defeated by ink_hue_vary + complement lock in the display pass; acid-rise-12 dye_lum back to 0 (9420b10); dye3 fix attempted before the 03:00 stop; AE-c off-screen hue2 seeding merged b09e0bc; user notes filed AK-BA.

## 2026-09-22 22:45 -- Brief BD: the high-ISO speckle in the dark masses (Opus executor, branch noise)

User photo (reference/shots/photos/high-iso-noise-in-mass-phone.jpg): "it doesn't read like film, it looks
more like high ISO artifacting". Phase 1 diagnosed it with a baseline + four ablations (aberration off, both
grains off, cellulose off, all post lifts off) measured inside four flat dark-mass patches:
- lateral aberration = 72% of the mass chroma noise. `d.r += Src(uv+duv).r - Src(uv).r` is a first
  difference of the SHARP display output on R and B only (never G), so it printed every per-pixel detail --
  above all the grain -- as colour, and painted crisp red/blue outlines onto an already-defocused picture.
- two grains = 69% of the luma noise: `[post] film_grain` 0.11 and `[liquid_acid] grain` 0.057 (never
  deferred, re-rolled every refresh, living in the texture the aberration resamples). Both were an equal
  ENCODED step on R, G and B (not luminance-only), with a 0.15 amplitude floor on absolute black and full
  amplitude from ~0.4 nits up: 64-79 PQ10 codes of pixel-to-pixel spread in a mass vs 22 on the open film.
- post lifts (sheen with no dark fade at all, glint halo, fog, overlay floor 0.10) and per-pixel jitter in
  the halation and bloom gathers (27% of the luma noise, warm-tinted).

Phase 2 (decisions by Fable; user away):
- `[post] film_grain_chroma` (default 1 = old additive; 0 = the grain SCALES the encoded pixel, so channel
  ratios survive -- hue-preserving, cannot lift black, cannot clip; gain 0.6667 = 0.5/0.75 so an encoded-0.75
  pixel, the open film, gets exactly the old swing). `[post] film_grain_density` (default 0 = old curve;
  1 = smoothstep(0.10,0.32) x (1-smoothstep(0.55,1)), zero floor). Both in Emulsion() and in the display
  pass's copy of the grain. At the defaults the old arithmetic runs unchanged behind [branch].
- Aberration: both taps from a ~1.5 px average (AvgTap, 4 bilinear taps) -- unkeyed, the grain-as-colour
  fix. `[post] aberration_coc` (default 0 = the uniform split the user approved 2026-09-19; 1 = scaled by
  the pixel's own CoC against the psf floor). The comment that claimed the old code already faded with the
  blur is corrected.
- One shared spatial test `massDeep` (4 taps 40 px out, smoothstep(0.02,0.18) on their encoded luma):
  gates the lid sheen and the glint's wide halo (unkeyed; the glint core untouched, brief AF) and, through
  `[post] fog_mass_gate` (default 0), the haze. A level-based gate was tried first and dropped: the mass
  interior (0.73 nits) and the lighter edge band (1.67 nits) are only 2.4x apart, so any level ramp that
  clears the interior also darkens the band the user wants kept ("that low-key should stay").
- Film overlay dark floor 0.10 -> 0. Halation and bloom angular jitter cut to half a tap spacing; the
  bloom's per-pixel RADIUS jitter removed (the per-frame breathing and creep stay).
- `[liquid_acid] grain`: dropped where the post pass has a film_grain of its own (not merely where the
  post pass runs -- the "(80-20, NO lens effects)" twin runs it for dither alone with film_grain 0, and this
  grain is its only one); paced to film_grain_fps instead of every refresh.
- The four keys ride rg2.w as four 6-bit fields (b0 is full, root signature at 64 DWORDs); defaults
  1/0/0/0 -> 63/0/0/0 -> exactly 1.0/0.0/0.0/0.0. The display pass's copy uses mirror-block slots 10-11,
  which were literal zeros.
- acid-rise-12: grain 0, film_grain 0.10, film_grain_chroma 0, film_grain_density 1, aberration_coc 1,
  fog_mass_gate 0.7.

Measured, before = main 5e78022, after = noise, same preset, seed 1234, t=60, --hdr on, same pixels:
- pixel-scale speckle (3x3 high-pass, flat shadow): luma 9.64 -> 1.26 (-87%), hue 27.0 -> 5.33 (-80%).
  With aberration_coc 0: 1.47 / 10.3 (-85% / -62%).
- edge band 2-14 px inside a mass: luma sd 0.0159 -> 0.0015, PQ p5-p95 59 -> 25; level 1.944 -> 1.880 nits
  (-3.3%), rim/core 1.43 -> 1.40, half-level width ~5 px both. The -3% is the old grain's own lift going
  away: symmetric noise in the encoded domain raises the LINEAR mean by convexity (predicted +3.7% at the
  band's level; Phase 1's grain-off ablation measured -2.3..-3.8%).
- open film flat patch: luma sd 0.0115 -> 0.0102 (-11%; -9% of it is film_grain 0.11 -> 0.10).
- aberration_coc 1: the split is active (>2/255) on 40.7% of the frame; on 19.2% of the frame it drops by
  more than half, on ~7% by more than 90%. IT IS NOT A SUBTLE CHANGE: the out-of-focus droplets' crisp
  outlines were drawn by the aberration (sheet bd-aber-4x: aberration 0 shows them soft), so coc 1 turns
  them into soft discs. Three-way live choice for the user: bd-after-coc0 (uniform 1.8), bd-after =
  bd-aber-18 (coc 1, 1.8), bd-aber-09 (coc 1, 0.9).
- ranges: film_grain_density 0/0.5/1 nearly identical once chroma is 0 (it matters with chroma 1);
  film_grain_chroma 0/0.5/1 differs only in the mid-tones (hue noise 3.68 / 4.58 / 6.58).
- the masses are no longer near-black on main (brief AG dye, dye_lum 0.44): the frame's near-black area is
  0% before AND after, so the "black lift" in this preset is now the dye's by design.
style=fluid parity md5 `10E36EBF1A74EDFE609065D757300054` held. Every acid preset changes (the averaged
aberration source, the lid gate, the overlay floor and the gather jitter are unkeyed fixes); 25 of them also
lose their `[liquid_acid] grain` second stock (every rising config/preset with film_grain 0.11).
## 2026-09-22 -- Photos: INDEX.md for every reference/panel photo (Sonnet, afdeed1)

Every file under `reference/shots/photos/` and `reference/shots/panel/` gets one line in a new
`reference/shots/photos/INDEX.md`: what it shows and why it was kept (a user verdict, a markup, a
measurement target). Docs only, no renders, no code.

## 2026-09-22 -- FEATURES.md: the complete key manual (Sonnet, 1b7901b)

Generated, not hand-written: parsed `src/settings.cpp`'s slider table (label, range, step, section,
key, help text) grouped by section, cross-checked against `src/fluid.h` defaults and the live
`acid-rise-12.ini` values. First complete manual of every exposed key in one place instead of
scattered across briefs; this is the doc that step 4 of the 2026-09-23 booking session regenerates
with the day's new keys folded in (dye, shadow, boundary reflect, meniscus_film_mix, the grain/
aberration/fog keys) and the inert ones flagged from the audit.

## 2026-09-22 -- Audit 2026-09-22: acid cbuffer slot table, literal budget, ring-amplitude ordering, card defects (Opus auditor, 5d271d4)

Auditor pass on `main @ 1ea3611`, no feature code touched. Findings, in order of severity:
- **Cbuffer packing is sound** -- all 128 slots of `p0`-`p31` cross-checked between
  `UploadAcidConstants` and every `laP*.xyzw` read in `src/shaders.h`: no slot misread, unwritten,
  or double-meaning. Every defect found was in the comment table 1000 lines away from the code it
  documents: `laP31.y/.z` stale (the auditor's own AJ commit), `laP19.w` stale, `laP11.w` documents
  a `sweepDeg` that is hard-written 0 and read nowhere, `laP29.y`/`laP26.y` advertise keys that are
  actually hardcoded magic constants. 7 slots genuinely free (`laP11.w`, `laP16.z/.w`, `laP17.w`,
  `laP29.z/.w`, `laP31.w`).
- **Literal budget**: 24 HLSL string literals, none over MSVC's 16380-byte cap, but the top three
  have only 2299 / 2771 / 2798 bytes of headroom -- `kDisplaySrc` is tightest, and it is the chunk
  nobody was currently editing, so the next person to touch it gets a surprise build break.
- **The ring competition**: nine terms paint the same droplet-boundary pixels with independent
  amplitudes. The meniscus halo replaces up to 0.85 of the pixel and is ink-derived
  (`meniscus_from_ink` 1.0), so it sits on top of and hides every film-hue feature; the two terms
  that actually carry the film's colour outward (`mass_rim`, the bright-field halo) peak at 0.098
  and 0.072. Proposal: AF ("weak lid"), AW ("weak lid effect") and AJ (boundary reflect radius) are
  three requests for the same ~0.17 of ring budget and should be judged as one stack after a
  solo-term sheet, not tuned one key at a time -- this became `meniscus_film_mix` (branch `menis`,
  see below).
- **EXECUTOR-CARD.md, 7 issues found**, the critical one (finding 4.1) a live concurrency bug: the
  card's own GPU-lock recipe dot-sources `tools\gpu-lock.ps1`, which at the time resolved the lock
  path from **its own repo root** (`Split-Path -Parent $PSScriptRoot`) -- so a worktree checkout
  locked `<worktree>\build2\shots\gpu.lock`, a gitignored, worktree-private path that excluded
  nothing. With four branches in flight that day, every "lock" any executor took was guarding
  against itself alone. Also: the 2026-09-15 OLED-grey incident line had been deleted in the same
  edit that relaxed the wait policy to "2 min then render anyway", `SendUserFile` is not in the
  executor toolset (the rule was unactionable and silently skipped), and `build-wt.cmd` was
  `.git\info\exclude`d so every fresh worktree had to reinvent it. **Lesson for the stuck-lock
  class of bug**: a shared resource guarded by a path derived from "where am I" instead of a single
  hardcoded shared root is not actually shared the moment more than one checkout of the repo
  exists -- fixed the same day in `ddf8500` (`tools/gpu-lock.ps1` now hardcodes the main repo's
  absolute path, `$env:FW_GPU_LOCK` only for tests) and in `5c07720` (incident line restored,
  `SendUserFile` instruction removed, `tools/build-wt.cmd` tracked).

## 2026-09-22 -- AJ lands: boundary_reflect_r, a hue2 seam reaches the droplet rims around it (branch refl, 1ea3611)

The user, watching the hue2 seam live: "whatever algo is mixing the oil boundary is insanely good...
and the way the bubbles reflect it, accurately... chef's kiss" -- then asked to widen the reach,
and on a marked-up panel photo (`reference/shots/photos/aj-reflection-radius-markup.jpg`) drew two
lines: the seam's colour should reach droplets up to about a THIRD of the screen height above it,
fading with distance. Before this key, a droplet's lit rim, lens highlight and glow were tinted
with the film colour AT THE DROPLET'S OWN PIXEL (`oilC`), so a droplet only carried the seam's
colour while it stood inside the hue2 field's 0.56..0.68 transition band -- about one droplet
across. New `boundary_reflect_r` (reach, fraction of screen HEIGHT, 0 = today) and
`boundary_reflect_amt`; the rim terms (`mass_rim`, the droplet lens's dome/meniscus/specular, the
bright-field halo, `oil_glow`, the swarm caustics) now tint with `oilR`, the same colour sampled and
rotated toward the seam's own hue (the transition band's midpoint) by a smooth falloff over the
reach, leaving the film and every blur untouched. Probe: 8 cbuffer taps of the 20x12 hue2 field (4
central differences for direction, 4 marching along it). acid-rise-12 ships `boundary_reflect_r`
0.33 / `boundary_reflect_amt` 1.0. Sheet `build2/shots/live/refl-sheet.png`; subtle in an SDR
still (the rim terms are HDR highlights and the 0.85 ink-derived meniscus dominates the ring in a
tone-mapped capture) -- judged live on the panel, not from the sheet. style=fluid parity md5
`10E36EBF1A74EDFE609065D757300054` held.

## 2026-09-22 -- BG lands: --shot also writes a lossless scRGB-half .jxr and a 16-bit PQ png (branch jxr, 4b291c5)

User: "can you render in 10 bit at all? It's low-key important." Both `--shot` PNGs were 8-bit,
SDR-mapped and tone-mapped, so an HDR-only effect (a lid ghost, a highlight rolloff) was being
judged through a window that could not show it. `WriteShotPair` now writes four files per shot:
the original 8-bit `.png` (still the md5 file for parity/identity), `-hdr.png`, a lossless
JPEG XR (`64bpp` half-float scRGB via WIC -- the same format the Windows Game Bar HDR screenshot
uses, opens in Windows Photos in true HDR on the OLED) and a 16-bit Rec.2020/ST 2084 `-pq.png`
with a cICP chunk. `tools\jxr-check.ps1` verifies the shipped file. Cost: ~35 MB extra per shot
(measured breakdown in audit (2): `.jxr` 12.4 MB + `-pq.png` 21.2 MB + `.png` 5.1 MB + `-hdr.png`
4.7 MB = 43 MB for one 2560x1440 shot) -- `build2\shots` needs periodic sweeping, and since the
main repo's `build2\` lives inside OneDrive (unlike a worktree's), OneDrive syncs every one of
these gitignored bytes.

Audit (2) (`5da0e87`) verified the colour maths independently rather than assuming it: the 709->
2020 matrix sums each row to 1.000000 against BT.2087, the PQ curve constants match ST 2084
exactly, the cICP chunk reads back `[9,16,0,1]` with a valid CRC and no leftover `sRGB`/`gAMA`,
`FloatToHalf` is correct to <=1 ULP, no COM leak (every interface a `ComPtr`), and the shipped
`.jxr` genuinely carries negative scRGB values (3.76% of pixels, min -0.0607) instead of silently
clamping them away. Two failure modes flagged for a follow-up, not yet fixed as of this session:
partial files are left on disk if a later WIC step fails (the file is created/truncated before
anything can fail), and `PngSetCicpPq` can report FAILED while leaving a valid but unmarked PQ png
on disk -- an unmarked PQ png is read as sRGB, exactly the "looks like fog" failure the function
exists to prevent.

## 2026-09-22 -- BC lands: the lamp casts shadows (branch shadow, c68c0bf)

User: "the light should cast shadows essentially... it can be from behind, or the bottom, or the
top or side." Until now the lamp drove specular/mass_rim/penumbra/haze/bloom but nothing in the
frame blocked its own light. Every mass and droplet now darkens the film on the side AWAY from the
lamp, tightest and darkest against the caster and softening with distance; a droplet's shadow is as
short as the droplet is tall. New `[liquid_acid]` keys `shadow_amt` (0..1), `shadow_len` (fraction
of screen height, 0..0.30) and `shadow_soft` (0..1), plus `[post] light_z` (-1..1) for how far the
lamp stands off the plane of the dish -- negative puts the lamp BEHIND the dish for a backlit mode
(tight even dark band around every edge instead of a directional cast shadow). acid-rise-12 ships
0.40 / 0.12 / 0.55 / 0.35. New float4 `laP32` in the packed acid cbuffer (layout now 1632 bytes).
Sheets `build2/shots/live/shadow-sheet.png` + `shadow-full-*.png`. Rote follow-ups logged: measure
the ms cost at 1440p (amt 0 vs 0.4), sheets for `shadow_len`/`shadow_soft`/`light_z` to tighten the
ranges, the keys into the other rising presets at 0.3-0.4, `laP32` into the slot table, a fresh
preset-identity baseline. Still needs the user: the backlit mode judged live.

## 2026-09-22 -- AG/AM lands for real: the dark masses are dyed, in the display pass on inkC (branch dye4, c5e78d3)

User: "add ability to dye the black ink" / note 11, "make the currently black oil mostly purple".
Three attempts before this one landed, traced end to end rather than guessed at each time:
1. The first attempt (`7e0050b`/`e916547`, keys `dye_hue`/`dye_sat`/`dye_lum`/`dye_hue_follow`
   merged) dyed the ink RAMP. `dye_hue` never reached the frame -- only the dye's VALUE survived,
   reading as brown -- because the display pass rewrites the ink ramp's hue twice downstream
   (`ink_hue_vary` rotates it by the sim's own chroma direction, then a complement-lock clamps it
   to a window round the oil hue).
2. The second attempt (branch `dye3`, `993747f`, NOT merged) restored the ramp hue after both of
   those steps. Delta: **0**. Hypothesis logged for the next session rather than guessed further:
   acid-rise-12 runs `ink_mode = water`, whose dark-mass shading might be a different code path
   from the banded ink ramp entirely.
3. Reading the water path end to end (not guessing) found the real cause: inside a mass the oil
   field is below threshold so `alpha -> 0` and the pixel IS `inkC`; `ink_mode = water` routes
   `inkC` through `InkWater()`, which for a mass pixel (sim dye density ~0) ends at
   `ikPaper.rgb = [ink] paper_color = 0 0 0` -- the black the user sees. The ink ramp the first two
   attempts patched (`laInk[]`/`effInk`) is read in exactly two places in the whole shader, and
   BOTH are dead for this preset (the bands branch is unreachable under `ink_mode=water`; the other
   reader, `toe_tint`, is 0 by default) -- so no edit to the ramp, in any order, could ever have
   changed one bit of acid-rise-12's output. This is also what the "byte-identical four-hue sheet"
   from the first attempt was actually measuring.

Fix: the dye is applied in the display pass directly on `inkC`, right after the lamp ramp, as a
translucent wax -- thickness = depth into the mass (`-sdf`), transmitted light =
`exp(-depth/0.055)` with a 0.42 floor, gated by `(1 - cov)` so the film is untouched. Rides
`laP29.z/.w` (amount, hue) and `laP31.w` (saturation) -- no new root params, no new keys beyond the
four already merged. Proof: acid-rise-12, seed 1234, t=60s, `dye_sat 0.8 dye_lum 0.30
dye_hue_follow 0`, `dye_hue 285` vs `0` -> max|delta| 143/255, 43% of pixels differ by more than 2
(it was 0 on `dye3`); `dye_sat 0` renders byte-identical to the pre-change build
(md5 `CA1B5B2977964C112CEAD7D414815643` both), style=fluid parity md5
`10E36EBF1A74EDFE609065D757300054` held. Sheet `build2/shots/live/dye4-sheet.png` (3 hues x 3
lums). Shipped in acid-rise-12: `dye_hue` 285, `dye_sat` 0.8, `dye_lum` 0.30, `dye_hue_follow` 0;
`dye_lum`'s slider re-ranged 0..0.50 step 0.02 from the sheet (under ~0.10 still black, past ~0.45
the mass stops reading as dark). Open follow-up logged: with `dye_hue_follow 0` the wax stays
purple while the 8-pair sweep turns the film, so the two sit close when the sweep reaches its own
violet pair (`sweep_oil_3`) -- wants its own A/B against `dye_hue_follow 1`.

## 2026-09-22 -- BD follow-up: the edge band measured separately, aberration_coc back to 0 (53c9252, b7dc031)

Two user constraints landed after the `noise` merge (42ae2eb) that the phase-1/phase-2 numbers in
the prior entry didn't yet cover, because the user flagged the mass-edge lighter band
(`reference/shots/photos/mass-edge-light-band-keep-phone.jpg`) as a keeper, not part of the noise
problem: "It has a cool effect, the ISO issue may be doing some of the work, but it's a separate
thing and needs to stay" -- and "the edge band is where the noise is MOST apparent; fixing the
noise there must not change the band's level or width" (a "sensitive job"). Measured separately
from the interior and the open film, before/after the noise fixes: edge band level 1.944 -> 1.880
nits (-3.3%, the old grain's own Jensen lift going away, not a deliberate darkening),
half-level width ~5 px unchanged both sides, rim/core ratio 1.43 -> 1.40. Edge-band noise itself
dropped ~90% alongside the interior's -87% luma / -80% chroma.

Separately, the CoC-scaled aberration (`aberration_coc 1`, physically correct: no lateral CA on a
bokeh disc) read quieter than the uniform 1.8 px split the user had approved on 2026-09-19: turning
it on had been drawing the out-of-focus droplets' crisp outlines (verified with a frame showing
them go soft when aberration was disabled entirely), an artefact the noise fix was not supposed to
remove. `acid-rise-12` shipped `aberration_coc 1` when `noise` first merged; `53c9252` set it back
to **0** (the uniform split) so the approved crisp droplet outlines are unaffected -- the fluid.h
default is also 0, so this is "today's behaviour" for every other preset. Three-way live choice
left open for the user (note 9 / item AT): `bd-after-coc0.png` (uniform 1.8), `bd-aber-18.png`
(coc 1, 1.8 px), `bd-aber-09.png` (coc 1, 0.9 px), all full-res in `build2/shots/live/`.

## 2026-09-22 -- meniscus_film_mix A/B: the meniscus and rim_dark are inert in acid-rise-12 (branch menis, 86d1fd3)

Follow-up to the auditor's ring-competition proposal (5d271d4): rather than adding a tenth
boundary-amplitude key, `meniscus_film_mix` (default 0, `laP16.z`) answers whether the meniscus
halo should take a share of the FILM's colour instead of the ink's. Executor G's A/B turned up a
correction to the audit's own ring table: in acid-rise-12 the meniscus and `rim_dark` paint
**nothing today**. Both are gated by `meniscus_from_ink`'s ink-brightness read just outside the
oil, and the ink outside the oil is black everywhere in this preset, so the gate evaluates to 0 at
every pixel -- `meniscus = 0` already renders byte-identical to `meniscus = 0.85`, and so does
`rim_dark = 0`. The premise that "the 0.85 meniscus dominates the ring" was wrong; the film-derived
rim terms (mass_rim, the bright-field halo, oil_glow, the droplet lens rim) are what actually
shows. `meniscus_film_mix` both recolours the halo from the film (the same `oilR` its lit rim
already uses, so a hue2 seam reaches the halos too) AND opens the ink gate for the meniscus only --
at the shipped default 0 this changes nothing (accepted going in: the A/B is the judge, not the
default). A/B/C = 0 / 0.5 / 1.0, `build2/shots/live/menis-*.{png,jxr}` + `menis-sheet.png`; MAD
A-B 0.92, B-C 3.68 -- G's read: B is subtle, C is strong but washes out the purple dye in seam
droplets; would pick 0.3-0.5, left for the user to judge live. Rote follow-ups: ship
`meniscus_film_mix 0` into the tray presets (folded into this session's step-1 preset-parity
commit), fix the audit's ring table (done in audit (3), 1bdf5ad), re-point `preset-identity.ps1`
at yield 8 + the main-repo lock (done in this session's step 2).

## 2026-09-22 -- Diagrams: camera geometry to scale from the live preset (Sonnet, d921886)

A script + SVG + PNG laying out the perspective-camera/DOF/tilt rig (`camera_fov`, `camera_focus`,
`focus_tilt`, `focus_band_px`, `dof_max_px` etc.) to scale from acid-rise-12's own live values, so
the geometry briefs (N, R) have a picture to check claims against instead of prose alone. Docs
only, no code or render changes.

## 2026-09-22 -- Audit 2026-09-22 (3): slot table clean, one literal near the cap, ring table corrected (Opus auditor, 1bdf5ad)

Re-run after `shadow`, `dye4`, `noise` and `menis` all merged. Slot table: **0 mismatches** across
every `laP0`-`laP32` component; C++ (`AcidParamsGPU`) and HLSL (`AcidCB`) now agree on order as
well as size (`static_assert == 1632` bytes on both sides, which is the check that matters when a
float4 is inserted before `laMix` -- a mismatch there would shift all 240 hue2 cells silently). 3
free scalars left (`laP11.w`, `laP16.w`, `laP17.w`) -- not a crisis, since `AcidCB` is a CBV and can
grow by a float4 at no cost to the 64-DWORD root signature (root *constants* are the scarce
resource, not the cbuffer). `rg2.w`'s packed 6/6/6/6-bit fields verified exact over all 625 edge
combinations of {0,1,31,62,63} in float32: 0 round-trip failures.

Literal budget: the CAST SHADOWS chunk of `kDisplaySrc` (lines 2450-2703) is down to **858 bytes**
of headroom under the MSVC 16380-byte cap -- about 11 lines of this project's commented HLSL. Flagged
to split before the next shadow-adjacent edit.

Ring table corrected: executor G's finding (above) was right and the audit's first table (5d271d4)
ranked nominal amplitudes instead of effective ones. With the `haloInk` gate at 0 everywhere in
acid-rise-12, the meniscus and `rim_dark` are dead, and by code `swarm_lens` is dead too (swarms
are forced off wherever the droplet particle sim is running, which acid-rise-12 does at 950
droplets). The live boundary is dominated by darkening (thin edge 0.70, shadow 0.40, penumbra
0.30, roughly 1.4 of budget) against ~0.3 of lift, and every live lifting term already takes its
colour from `oilR` -- so `boundary_reflect_r`'s effect being small is a budget problem, not a
hiding problem. Also corrected: the lid is a post-pass layer, not a ring term, so the audit's earlier
explanation of "weak lid" (AF/AW) via ring competition was wrong; what actually bears on it is the
`noise` merge's unkeyed `massDeep` gate on the lid sheen and iridescence halo, which lifted the
lid's warm floor away from every mass interior -- the opposite direction from "raise the lid",
landed deliberately for the noise fix and not yet re-judged against AF/AW.

Hygiene items filed for this booking session to fix (see the 2026-09-23 hygiene entry below):
`gravity`/`gravity_pow`/`gravity_blur` read but never written back by `WriteConfigToIni`, so every
mood/preset save silently dropped them; `dye_hue` 285, `hue_rotate_period` 3600 and `ink_soft` 0.6
all sat outside their Settings sliders' ranges (the first nudge of `dye_hue` would have jumped it
105 degrees toward its clamped -180..180 range).

## 2026-09-22 -- tools/preset-identity.ps1: acid-side equivalent of the fluid parity check (Sonnet, 94e1f2b)

No acid-side byte-identity check existed before this; every executor either rebuilt a clean `main`
by hand to prove existing presets were untouched, or skipped the step. Renders a preset list
headless with a given exe, md5's the PNGs, and either saves a baseline or compares an exe's output
against one (`-Save`/`-Baseline`). The fluid look (`we-look-live.ini`) is always rendered first and
reported separately, because that check is sacred and independent of whatever acid presets are
under test. Tested against a clean worktree build (`fd81f49`): fluid line matched the parity md5
`10E36EBF1A74EDFE609065D757300054`; baseline saved to
`build2/shots/preset-identity-baseline-48f6e95.txt` (gitignored). Lesson from the GUI-app side of
this tool: `FluidWallpaper.exe` is a windowed-subsystem app whose `--shot` mode writes its PNG
asynchronously with no signal back to the launching process (and `--console`'s stdout re-attachment
means shell redirection goes silent per AGENTS.md), so the script cannot simply wait on the process
exiting -- it has to poll the output file for a stable size instead, and that poll needs its own
bounded timeout with a clear failure line rather than either hanging forever or throwing an
unhandled exception out of the GPU-lock `try`/`finally` (fixed in this booking session, see below).

## 2026-09-23 -- Booking: preset parity + audit hygiene fixes (Sonnet, b44991e, 43dfbf8)

Rote/hygiene pass called for by EXECUTOR-CARD.md and the two 2026-09-22 audits, with the GPU lock
held by another agent's lid render until ~00:10, so this ran config/docs-only steps first and left
the build+render for last (see the build/verify entry below).

**Preset parity (b44991e).** The ten `[liquid_acid]` keys and five `[post]` keys that landed
2026-09-22 (`meniscus_film_mix`, `boundary_reflect_r/amt`, `dye_hue/sat/lum/hue_follow`,
`shadow_amt/len/soft`; `light_z`, `film_grain_chroma`, `film_grain_density`, `aberration_coc`,
`fog_mass_gate`) were only in `acid-rise-12.ini`. Added at their `src/fluid.h` DEFAULTS (confirmed
against `src/main.cpp` getF/putF too) to the same 33 files `f840685`/`6e4a81c`/`2011d8a` used,
with the acid-rise-12 comment blocks copied verbatim, each key placed after its nearest existing
neighbour in acid-rise-12's own order (`meniscus_from_ink`, `mass_rim`, `crust_hue_mix`,
`film_grain_color`, `aberration_field`, `fog_px`, `light_drift`). Two of those neighbours
(`meniscus_from_ink`, `mass_rim`) are missing from 11 of the sparser "Liquid Acid A"/"oil on ink
water" presets; for those the block goes immediately before the MULTICOLOUR OIL comment instead,
preserving the same relative order. 7 of the 33 have no `[post]` section at all (by design, a
partial overlay), so the 5 `[post]` keys are not added there. 3235 insertions, 0 deletions across
33 files; each file's own CRLF and no-BOM verified byte-for-byte before and after.

**Audit hygiene (43dfbf8).** From AUDIT-2026-09-22.md section 13/14 and audit (3)'s hygiene list:
`src/settings.cpp` sliders widened to contain their own live values (`dye_hue` -180..180 ->
0..360, live 285; `hue_rotate_period` 0..1800 -> 0..3600, live 3600; `ink_soft` 0.01..0.5 ->
0.01..1.0, live 0.6 -- the old `dye_hue` range meant the first Settings-window nudge would have
silently jumped it 105 degrees on clamp). `src/main.cpp`'s `WriteConfigToIni` now writes back
`[sim] gravity`/`gravity_pow`/`gravity_blur` next to `shadow_knee` (mirroring their read order),
since they were read but never written -- every mood save and tray save that went through it was
silently dropping them. `AGENTS.md`'s "Bloom: never" corrected to describe the keyed post bloom
(default 0, acid-rise-12 ships 0.30) vs. the still-banned old full-screen bloom, and "Builds need
no lock" corrected to "Builds AND renders take build2\shots\gpu.lock ... one render at a
time with --shot-yield 8" (EXECUTOR-CARD.md already carried the newer "renders never wait"
policy; AGENTS.md had not been updated to match either policy and needed a real value, not a
stale one). `tools/preset-identity.ps1`: `--shot-yield` 2 -> 8 (menis executor G's follow-up);
`Wait-StablePng` bounded at 20 minutes (was 180 s) and no longer throws past the GPU-lock
try/finally on timeout -- it returns false, `Get-PresetMd5` prints a "<name> TIMEOUT" line and
the caller moves on instead of hanging or crashing with a bare stack trace. Confirmed (no code
change needed): `tools/gpu-lock.ps1` has resolved to the main repo's hardcoded absolute path
since `ddf8500`, so `preset-identity.ps1`'s dot-source already gets the shared lock regardless of
which worktree it runs from.

## 2026-09-23 -- BE lands: the packed acid cbuffer has named fields + tools/slot-check.ps1 (Opus executor H, branch slots)

Pure refactor, zero visual change. Every packed acid scalar (129 of them, laP0..laP32) now has a
NAME in one table, `src/acid_slots.h` (an X-macro: `X(NAME, vec, comp, "ini key / source")`).
Everything else is derived from it:

- C++: `enum AcidSlot { LA_<NAME> = vec*4 + comp }`. `UploadAcidConstants` no longer builds
  `float pN[4]` arrays and memcpy's them: it writes `slot(LA_<NAME>, value)`, once per scalar,
  through a `V[kAcidSlotVecs]` pointer table onto the unchanged `AcidParamsGPU` (layout still
  1632 bytes, static_assert kept).
- HLSL: `kAcidSlotMacros` (fluid.cpp) is the LIQUID_ACID PSO's D3D_SHADER_MACRO list:
  `LIQUID_ACID=1` plus `LA_<NAME>=laP<vec>.<c>` for every row. All 259 laP reads in the acid
  literals became `LA_<NAME>` (plus 10 comment mentions). A D3DCompile define rather than a text
  prelude, so it costs the literals nothing and the fluid/ink PSOs never see it.
- Docs: the `cbuffer AcidCB` declaration + comment table in shaders.h sits between GENERATED
  markers and is written by `tools/slot-check.ps1 -Fix` from the table and the C++ struct.

`tools/slot-check.ps1` (no GPU, <1 s) fails on: a row written 0 or 2+ times, written but never
read, read but not written, an unknown name, two names on one component, any raw `laP<n>.<c>`
left in shaders.h (comments included), a raw `p.p<n>` write in the upload, C++/HLSL member order
mismatch, a static_assert that disagrees, a stale generated table, `LA_` in a literal other than
kDisplaySrc, or any literal piece over 16000 bytes. Negative-tested on a scratch copy (raw laP,
duplicate write, two names on one slot, reordered cbuffer, orphan row): each fails with a clear
line. The card's section 3.5 now says to run it before merging.

Dead slot `laP11.w` (written 0, read 0x, audit section 1) is no longer written; free components:
laP11.w, laP16.w, laP17.w. The hardcoded `laP29.y` (3/1440) and `laP26.y` (1/0.12) are named
MASS_RIM_W / COC_SPAN_INV and marked HARDCODED in the table. The CAST SHADOWS chunk of kDisplaySrc
(15522 bytes, ~858 headroom, audit 3 section 11) is split at "the droplets, analytically";
largest piece is now 14211 (kDisplaySrc @ 1092).

Verification, strongest first: (1) the acid, fluid and ink PSOs were compiled old-vs-new with
d3dcompiler_47 (python ctypes, VS+PS): DXBC byte-identical for all six blobs; (2) every old
`pN[c]` expression compared textually against the new `slot()` expression for the same slot:
129/129 identical, the 3 free slots were literal 0.0f and are now the `= {}` zero; (3) renders, one at a time,
yield 8: fluid parity `10E36EBF1A74EDFE609065D757300054` MATCH; `tools/preset-identity.ps1`
against `preset-identity-baseline-941cd4d.txt`: we-look-live, acid-rise-12, -2hue, -8020, -rotate
all MATCH (495 s).

Gotchas found on the way: `preset-identity.ps1 -Presets a,b` does NOT survive `powershell -File`
(the array arrives as one string, no preset resolves, and the script silently renders only the
fluid row and exits 0) -- call it in-process with `& tools\preset-identity.ps1 -Presets $arr`.
The 941cd4d baseline has no `Liquid Acid - rising` row, so a default-list run prints SKIPPED for it.

## 2026-09-23 -- Session summary: the 2026-09-22 evening landings, BJ's inert-key sweep, and the day's user verdicts (Sonnet docs pass, 5010122..1553a7f)

Rollup of everything since the 2026-09-22 18:00 cutoff (git log + `reference/briefs/NEXT-rings-grain-aberration-refraction.md`), tying together items that mostly already have their own detailed entries above, plus two batches that didn't: BJ, and the evening's user verdicts and new briefs.

**Landed, already logged in detail above:** AE-c off-screen hue2 seeding (`6336c36`/`b09e0bc` -- merged 02:27 the night before the 18:00 cutoff, but it is the seed field everything else this session touches reads from); AJ `boundary_reflect_r` reaching droplet rims from a hue2 seam (`333bcc0`/`1ea3611`); BG `.jxr`/`-pq.png` HDR shot output (`fcc2da0`/`4b291c5`); BC the lamp casts shadows (`53ae1c0`/`c68c0bf`); AG/AM the dark masses dyed for real in the display pass on `inkC` (`3ca35fd`/`c5e78d3`); BD's high-ISO noise fix -- density-weighted grain, averaged aberration source, `aberration_coc` back to 0 (`16cdfb3`/`42ae2eb`/`53c9252`/`b7dc031`); `meniscus_film_mix` A/B, which also proved `meniscus`/`rim_dark` are inert on this preset (`f1a656b`/`c77767d`/`4efcf92`/`86d1fd3`); BE named cbuffer fields + `tools/slot-check.ps1` (`7772b5f`/`1d2786a`/`f2cfd87`); `FEATURES.md`'s complete key manual (`1b7901b`); the reference-photo `INDEX.md` (`afdeed1`); the to-scale camera-geometry diagram (`d921886`); and the three 2026-09-22 audits (cbuffer/literal/ring-amplitude `4d30545`/`5d271d4`, jxr colour maths `5da0e87`, slot table/ring table `1bdf5ad`).

**Landed, no entry until now:**
- BJ key-effect harness (`3968d5e`, `3157ec1`) plus its report (`f0ec839`, `bb6a99c`): `tools/key-effect.ps1` renders a preset at each key's live value against its `fluid.h`/`settings.cpp` default and diffs. 248 keys in scope on `acid-rise-12`, 146 tested, 30 byte-identical (every `ink_*`/`seam_strength`/`seam_hi` -- dead under this preset's `ink_mode=water`; every `swarm_*` plus `swarm_lens` -- dead once `droplets>0`; `film_hairs`, `film_leak`, `lid_sheen_px`, `rim_vary`, `rim_inset`, `rim_ink_follow`, `oil_dye_block`, `oil_ink_blur`, `grain`, `rig_readjust`, `focus_tilt_period`), plus `meniscus`/`rim_dark` confirmed inert by hand, plus 10 more keys under MAD 0.05. Folded into `FEATURES.md` this session (`ad00722`): an inline "INERT on acid-rise-12 (why: ...)" note on each of the 30, tracing the `ink_mode==bands` branch and the shared `haloInk` ink-brightness gate in `src/shaders.h` and the `droplets>0` swarm zeroing in `src/fluid.cpp` to confirm 25 of the 30; the other 5 (`oil_ink_blur`, `oil_dye_block`, `film_hairs`, `film_leak`, `lid_sheen_px`) are marked "gate not traced" since the report gives no reason for them.
- User verdicts, live on the panel: BD's noise fix is "fire" (`5010122`, 18:07); BC's cast shadows are "fire" (`6940432`, 18:10 -- the same commit also opens BK below).
- BK, "lost the sense of darkness": flagged at 18:10 (`6940432`, "it lost the sense of darkness in a lot of the references... don't do much yet, I'll send reference photos"); two vibe references followed at 18:20 (`ee2c7cd`: `bk-vibe-ref-1-cyan-black-masses.jpg`, `bk-vibe-ref-2-magenta-green-black-masses.jpg`, true-black masses/droplets, no lift); a live "too bubbly" phone photo followed at 18:30 (`d01d15f`: `bk-too-bubbly-live-0923.jpg`) with the verdict "too bubbly... the black ink needs to be very dim or almost maroon. Smoky. Or maybe the background darker?"
- BL, a new lighting-model idea (not a value tweak): fluorescent/emissive oil under the lamp, dim ambient light on the fluid underneath (`cc4d40d`, 18:12, "what if the lamp lights up the oil, and it's like fluorescent? And the ambient light brightens the fluid?"), with bioluminescent-plankton references at 18:35 (`e18da72`: `bl-biolum-ref-1.jpg`, `bl-biolum-ref-2.jpg`).
- BM, the scratched-acrylic lid effect the user meant all along by "the plastic effect" (`bba8f00`, 18:21): four references (`lid-scratch-ref-1.jpg`, a real scratched acrylic PC side panel; `lid-scratch-ref-2/3/4.jpg`, stock scratch close-ups), with a spec sketched for `lid_scratch`/`lid_scratch_density`/`lid_scratch_len`/`lid_scratch_corner`/`lid_scratch_soft`.
- `acid-rise-12.ini`: `dye_lum` reverted 0.44 -> 0, "black masses back for now" (`1553a7f`, 18:23) -- the user's immediate response to the BK bubbliness/darkness feedback, ahead of the fuller BK/BL rework. `FEATURES.md`'s `dye_lum` Live cell (last generated at `1b7901b`/`9999eba`, still shows 0.30) has not caught up with either the 0.44 value it carried during the day or tonight's revert to 0; not corrected in this pass.

**In flight, no code landed yet:** two worktrees were branched off tonight and still sit at the same commit as `main`, ready for an executor: `fw-scratch` (branch `scratch`, at `bba8f00`, for BM's scratched-acrylic lid) and `fw-bk` (branch `bk`, at `1553a7f`, for BK's darkness levers -- candidate keys `dye_droplets`/`dye_smoke` so droplets stay dark while masses can still carry colour, per the brief's "too bubbly" plan). Also queued but not yet run: a dye colour-variation sheet (hue/sat/lum grid plus `dye_hue_follow` plus a dark variant) that the brief asks be done FIRST, before either BK's value tweaks or BL's structural rework, "because it's hard to say what's wrong with it."
