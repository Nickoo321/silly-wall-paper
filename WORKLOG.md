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
