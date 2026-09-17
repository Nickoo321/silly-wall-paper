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
