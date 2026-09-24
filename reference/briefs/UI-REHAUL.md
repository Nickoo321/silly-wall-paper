# UI-REHAUL: rebuild the Settings window (planner brief, 2026-09-24)

User: "please completely rehaul the ui. You can build from scratch, its actually the worst ui on earth" /
"I want the UI to actually be user usable. Why does it still say I'm in neon mode." /
"the UI needs to have accurate, not show unusable settings, and have methods to switch/ line up modes of
operation" / "something similar to how keyboard or rgb panels work".
Read AGENTS.md + EXECUTOR-CARD.md first. Scope: src/settings.cpp (replaced), tray menu grouping, preset
tracking. Display-side only: the render path is not touched (section 5).

## 0. Why it is unusable (evidence)
1. The "Neon" lie. InitMoods defaults base_mood to "Neon" (src/moods.cpp:469); the mood bar and status
   line print MoodsCurrentName() whatever is running (settings.cpp:718-727, :1087). User's settings.ini:
   [look] style=liquid_acid, [moods] enabled=0, no base_mood -> "Mood: Neon" over Liquid Acid. Changing
   the default alone fails: UpdateMoods turns s_current -1 into 0 (moods.cpp:500) -> "Abyss".
2. Markers compare against the wrong thing: ●/○/* (legend :1419) diff vs the mood-file cache through
   kFieldMap (:586-651), ~60 fluid keys; the 166 liquid_acid / 79 post / 17 ink sliders never show "*".
3. One flat wall: 341 sliders + 25 checkboxes (:164-546), all identical label-over-trackbar rows of
   48 px (kRowH :108). "Liquid Acid" page = 166 sliders (:223-390, ~8,000 px of scroll), Post = 79
   (:433-511). No search, no front page, no grouping by what a key does. The 57 keys the user actually
   moved (reference/configs/lapd-look-user-0924.diff.txt) are scattered across it.
4. No look/preset control in the primary window: the look is two checkboxes on the System page (:538,
   :541); presets only via "Scenes…" (second window, scenes.cpp:39); applying any preset CLOSES Settings
   (main.cpp:1987). With the hidden taskbar the tray is not a real alternative.
5. Tray preset list capped at 50 (main.cpp:1054); moods\ holds 67, so everything after "Mirror - off"
   (incl. "WE parity (fluid)", "WE Original") is unreachable.
6. Values lie: SliderPos clamps to the slider range (:572-577), so dye_droplet_lum default -1 ("inherit",
   FEATURES.md:234) shows -0.02 and a nudge writes it; peak_nits auto (-1) is labelled "off" (:685).
   Enums are sliders with the legend in the label (mirror mode :424, oil_edge_mode :283, ink_water :390).
7. Dead controls at full weight: 39 keys inert/unverified on the live preset (FEATURES.md:577); gated
   keys (lid_* at lid 0, ink_* under ink_mode=water, swarm_* with droplets>0, dye_*_vary at dye_lum 0)
   look live. Mood cycling is offered under acid/ink, but the conductor only lerps fluid fields
   (moods.cpp:43-115) and FlipDiscrete (:92) never switches the look.
8. Edits revert on restart: applying a preset writes base_mood (moods.cpp:593); every start re-applies
   that file over settings.ini (main.cpp:2829 -> moods.cpp:426) -> later edits to its keys are lost.
9. Three names, one folder: tray "Presets" lists moods\ (main.cpp:1041), "Save current as new preset"
   writes moods\ (:2024), Scenes lists moods\, and a separate presets\ exists (:1347). A mood change
   destroys and recreates the whole window (settings.cpp:1112-1120).

## 1. Hard requirements (user MUSTs)
R1 ACCURATE - every control shows the true live state.
- Immediate-mode UI reads every value through its pointer every frame: no cached label strings, no
  rebuild-on-mood hack. Header computed each frame: look = Config().ink.enabled / acid.enabled; preset =
  [ui] active_preset (set only by Apply/Save); overlays = applied files with no [look] section; mood =
  conductor name ONLY when [moods] enabled=1; HDR = g_hdrActive (export an accessor) + peak + gamut;
  dirty = number of keys whose live value != reset target.
- Labels/ranges from SliderDef. Code default = FluidConfig{} at the same offset; preset value =
  FluidConfig{} + LoadConfigFromFile(active preset) = the "reset target". Offset trick:
  (char*)d.fval - (char*)&Config() indexes any FluidConfig copy -> replaces kFieldMap for all rows;
  globals (peak_nits, moods) handled explicitly.
- Out-of-range values shown as text with an "outside slider range" badge, never rewritten until dragged;
  sentinels named from keymeta (-1 "inherit", 0 "off", peak -1 "auto").
- PROOF: --ui-dump for WE parity, acid-rise-12, Ink - inverted, Liquid Acid - oil on ink water, and
  Mirror quad applied over acid: header look == file's style, preset == file name, "Neon" absent unless
  Neon applied; dirty 0 after apply, 1 after one scripted set, 0 after undo; dye_droplet_lum -1 -> "inherit".
R2 NO UNUSABLE SETTINGS - inert / wrong-look / gated keys hidden or disabled with a reason.
- src/ui/keymeta.inc: one row per key {sec, key, group, looks F|A|I, gate, flags, special}. looks =
  which look's render reads it. gate = tiny expression on other keys resolved through SliderDef at
  startup: "post.lid>0", "liquid_acid.droplets==0", "liquid_acid.ink_water==0", "liquid_acid.dye_lum>0",
  "post.film_grain==0" (for grain). flags: MOTION (only visible over time: focus_tilt_period,
  rig_readjust, shimmer; clock badge, never hidden), UNVERIFIED (gate not traced: oil_ink_blur,
  oil_dye_block, film_hairs, film_leak, lid_sheen_px; "?" badge), SUPERSEDED (dye_masses, dye_droplets
  per the KEY PASS), SHELL (sim_res/dye_res: read-only, AGENTS rule).
- Behaviour: wrong look -> hidden; gate false -> disabled, greyed, one-line reason ("needs Lid > 0",
  click jumps to Lid); "Show everything" chip reveals all, so nothing is unreachable.
- Generated, not hand-written: tools/gen-keymeta.py seeds rows from FEATURES.md (section per key +
  its INERT/why notes); after that the .inc is the source of truth. tools/keymeta-check.ps1 (no GPU)
  fails on a SliderDef key with no meta row or a gate naming an unknown key.
- PROOF: --ui-dump per look = visible / disabled / hidden counts + every disabled row's reason;
  acid-rise-12: none of BJ's 30 byte-identical keys is enabled; WE parity: 0 liquid_acid/ink rows
  visible; keymeta-check green.
R3 MODES OF OPERATION - switch and line up modes; the Modes page shows the operating state as a whole.
- Section 3D. Every cell of the validity matrix is verified with tools/key-effect.ps1, not guessed
  (e.g. the user turned hueshift_enabled on in an acid look, so "fluid only" is unproven).
- PROOF: --ui-shot of the Modes page per look + matrix dump; scripted acid -> ink -> fluid -> acid
  round trip returns identical header/state, and the ini diff touches only [look] keys.

## 2. Organising metaphor: an RGB lighting panel (iCUE / Synapse / SignalRGB / G Hub)
Effect = look (Fluid WE / Liquid Acid / Ink). Profile = preset. Animation = fluid hue cycler + hue
bursts, acid hue rotation / palette sweep, ink duotone rotation, mood conductor. Zones = the three
roles film / masses / droplets. "What's running" = the header strip.
Taken: effect cards, a few big knobs per effect with the rest under Advanced, profiles you
save/duplicate/rename/assign, colour by swatch + wheel, a status strip.
Does NOT fit, and why:
- Big live device preview: the wallpaper IS the preview and sits behind the window; a thumbnail needs a
  second render/readback in the D3D12 path. Instead: "Peek" (hold: window alpha 10%, layered window),
  and static per-preset thumbnails from headless --shot (phase 3).
- Per-zone/per-key painting: a continuous sim has no addressable zones; the roles are the only zones.
- One global brightness knob: on the ABL OLED a global gain lifts the full frame and trips ABL.
  Brightness = peak_nits (hot spots) + film_level (film role).
- Per-app profile switching: it already pauses for fullscreen/maximised; auto-switching looks restarts
  sim state and compiles a PSO. Not offered.
- Instant effect switch: the first switch to a look compiles its PSO and restarts that look's sim; the
  card shows "compiling…".
- Exact colour from a picker: the swatch is an SDR approximation (lamp, film and HDR change it on the
  panel); label it "base colour".
- Speed knobs: the keys are periods (s per turn, 0 = off). The UI shows speed and writes the same key.

## 3. Information architecture
One top-level window, min 900x640, per-monitor DPI, dark, remembers rect; opened from the tray and by
second launch (main.cpp:2765-2770, required with the hidden taskbar); never closes on preset apply.
A. Header (always): "Liquid Acid · acid-rise-12 · 57 changes · HDR on 1055 nt BT.2020 · Mirror off ·
   Cycling off · 144 fps" | Undo Redo | Save | Save as… | Revert | Peek | Pause.
B. Effects (front page)
 - Effect cards: Fluid (WE) | Liquid Acid | Ink. Clicking loads that look's start preset (line-up),
   not a bare look.
 - Profile strip for the active look: presets whose [look] style matches; overlays as chips (Mirror
   quad / kaleido / off). Apply, Save, Save as, Duplicate, Rename, Delete (Recycle Bin via
   SHFileOperation FOF_ALLOWUNDO, never a hard delete), Compare (phase 2).
 - Big knobs by role (proposal, Q2), each = wide slider + typed field + right-click reset (to preset /
   to default) + a dot when it differs from the preset:
   Liquid Acid - FILM film_level, colour speed (hue_rotate_period); MASSES colour (dye_hue/sat/lum);
     DROPLETS amount (droplets), colour (dye_droplet_hue/sat/lum, "inherit"); MOTION rise_speed;
     OPTICS bloom, fog, lid, film_grain; GLOBAL peak_nits, mirror chips.
   Fluid (WE) - peak_nits, hue band (arc on a wheel: hue_center ± hue_range), colour speed
     (color_cycle_period), hue bursts (hueshift_enabled), splat_radius, wanderer_count, mouse stirs
     (show_mouse), post_saturation, mirror. Banner "differs from WE parity" when it does.
   Ink - paper/inverted chips, chroma, density, drops on/off, drop interval, gravity, duotone rotation
     (pair_sweep_period), hdr_core, peak_nits, mirror.
C. All settings (advanced tier, generated from SliderDef/CheckDef + keymeta; no hand-written widgets)
 - Search (label, key, help text; Ctrl+F); chips [This look] [Changed] [Show everything].
 - Groups by what the key does to the picture: Film · Masses · Droplets · Oil shape & sim · Colour &
   animation · Lamp & lens optics · Lid · Post & film stock · Motion & rig · Ink · Fluid sim ·
   Output/HDR · Behaviour & system. Film / Masses / Droplets stay first and named as the three roles.
 - Row: label, typed value, slider (SliderDef range), preset-value tick on the track, reset,
   badges, tooltip = SliderDef tip + "[post] lid". Enums (mirror mode, oil_edge_mode, ink_water,
   dye_hue_follow) become chips via keymeta `special`.
D. Modes & animation
 - State list, one line per switch: current value + what it changes. Look (radio). Sub-modes: acid ink
   under oil bands|water (ink_water), acid edge soft|crisp (oil_edge_mode), ink paper|inverted.
   Animation clocks per look (only the active look's enabled). Mood conductor: on/off, dwell,
   transition, jitter, rotation list with In-cycle checks; non-fluid presets disabled with reason.
   Overlays: Mirror, Drops. Output: HDR detected on/off (read-only), peak, gamut; pause rules.
 - Validity matrix (cells to be verified, R3):
   | switch            | Fluid | Acid | Ink | note                                          |
   | mood cycling      | yes   | no   | no  | conductor lerps fluid fields only (moods:43)  |
   | hue bursts        | yes   | ?    | ?   | post CssHueRotate; verify with key-effect     |
   | ink under oil     | -     | yes  | -   | bands vs water; many ink_* only in bands      |
   | paper/inverted    | -     | -    | yes |                                               |
   | mirror, drops     | yes   | yes  | yes | display fold / sim emitter                    |
   | HDR on/off        | yes   | yes  | yes | must look right in both                       |
 - Line-up (phase 2): per look "start preset"; mood rotation order; stored in [ui].
   Cross-look sequencing (acid 20 min -> ink) is NOT supported by the conductor; phase 3, render-path
   brief of its own.
E. System: autostart, pause rules, fps limit, second monitor, analyzer, exit; sim_res/dye_res read-only.
Undo/redo: ring of {sec,key,old,new}; one drag = one entry (activate -> deactivate); a preset apply =
one whole-config entry; Ctrl+Z / Ctrl+Y; each step writes the ini like a live edit.
Words: "Preset" everywhere in the UI; moods = "presets in the cycle".

## 4. Technology
(a) Dear ImGui, vendored at src/third_party/imgui (MIT, ~6 .cpp), win32 + DX11 backends, its OWN
    D3D11 device + flip swapchain on the settings HWND, created on open and released on close.
    DX11 rather than DX12: zero shared state with the renderer (device, queue, descriptor heaps, root
    signature at the 64-DWORD cap) and a much smaller backend; d3d11.lib is in the SDK. Present(0,0),
    driven by input + a 30 Hz WM_TIMER; never Present(1) (main-loop vsync pacing, main.cpp:3194).
    Built in: text filter, collapsing headers, tables, ctrl-click typed entry, hue-wheel picker,
    tooltips, context menus. Headless: WARP into an offscreen texture -> --ui-shot, no GPU, no screen.
    Effort M (~1.5-2k lines UI). Risk low-medium: needs the user's OK (AGENTS "no deps"); the stock look
    is "dev tool", so a style pass is required (Segoe UI Variable from C:\Windows\Fonts, 6 px rounding,
    accent, spacing).
(b) Pure Win32 + Direct2D/DirectWrite custom controls: no dependency; hand-write the virtualised list,
    slider, text entry, focus/keyboard, scrolling, tooltips, colour wheel. Effort L (3.5-4.5k lines),
    risk medium (time, input edge cases), look: best possible if done well. Headless via a WIC target.
(c) WebView2 HTML: nicest look, but runtime + NuGet loader dependency, IPC plumbing, heavier. No.
    Regrouping the existing common controls (old NEXT-settings-advanced-tab brief) keeps the cached-label
    architecture that produced the Neon lie. No.
RECOMMENDATION: (a). The toolkit (search, groups, typed entry, colour wheel, reset menus) is free, so
the effort goes into accuracy and the look/mode model, and a DX11 island cannot touch the render path.
Fallback (b) if the user refuses the dependency.

## 5. Constraints that must hold
- Render path untouched. Phase 1 must not change src/fluid.cpp, fluid.h (include only), shaders.h,
  acid_slots.h, journey.*. The UI writes the same Config fields through the same pointers and calls the
  same hooks as today (ReinitWanderers, EnsureLookResources). Per merge: `git diff --stat main --
  src/fluid.* src/shaders.h src/acid_slots.h` empty; parity md5 10E36EBF1A74EDFE609065D757300054
  (we-look-live.ini, 60 s, 2560x1440, seed 1234, --hdr on); preset-identity -Baseline all MATCH;
  dxbc-cmp IDENTICAL.
- Live apply on every drag tick; per-key ini write to g_iniPath as today; still no disk hot-reload.
- Ini format unchanged: same sections/keys/meaning; presets and moods stay partial overlays; the look is
  written with the same int keys the checkboxes write today. Only addition: a [ui] section in
  settings.ini (active_preset, overlays, start_preset_<look>, window rect). It is never written into a
  preset/mood (WriteConfigToIni includeShell=false skips it).
- Tray kept; presets grouped into submenus by [look] style (Fluid / Liquid Acid / Ink / Overlays),
  no 50 cap.
- Hidden taskbar: WS_EX_APPWINDOW; second launch restores and focuses the window; hotkey = phase 2.
- HDR on/off: the UI swapchain is SDR; the UI never changes g_hdr* on its own; the user checks both.
- Build: build2.cmd FluidWallpaper (MSVC/Ninja, takes the GPU lock); third_party at /W0. The --shot
  path is untouched (RunShotMode returns before any UI).
- New flag: --ui-shot <png> --ini <ini> [--ui-page effects|all|modes|system] [--ui-search s]
  [--ui-script "set sec.key v; undo; apply <preset>"] [--ui-dump <json>]. WARP, no WorkerW, tray or
  renderer, g_configReadOnly. So the UI takes a model (FluidConfig& + hooks), not g_renderer.

## 6. Executor plan
PHASE 1 - usable window (branch ui1; the only phase that must land)
 1. Vendor ImGui (if Q1 = yes), CMake sources, DX11 island window, style pass, --ui-shot / --ui-dump.
 2. Move SliderDef/CheckDef tables to src/ui/keys.inc unchanged; write keymeta.inc (gen-keymeta.py,
    then hand-fix) + keymeta-check.ps1.
 3. Header, Effects page (cards, profile strip, big knobs), All settings (search/groups/reset/gates),
    Modes page (state + switches + matrix), System page; undo/redo; Save / Save as / Revert.
 4. Fixes: Neon (header never shows a mood unless cycling is on); peak "auto" label; clamped
    display; ApplyPreset keeps the window open; tray grouping without the cap; restart behaviour per Q3.
 5. Delete src/settings.cpp and the Scenes button (scenes.cpp stays reachable from the tray until
    phase 2).
 PROOF (EXECUTOR-CARD §6 adapted: window shots, not renders; all WARP, no screen):
 - WORKS: --ui-shot PNGs in build2\shots\live\ui1\: Effects page per look (3), All settings searched for
   "lid" with lid=0 (disabled rows + reasons), Modes page per look, header after a scripted edit (dirty
   1) and after undo (dirty 0); --ui-dump JSONs meeting R1/R2/R3; keymeta-check green; parity md5 +
   preset-identity MATCH + empty render-path diff.
 - DIFFERENCE: one contact sheet (new pages) + one sentence per page on what the user can now do that
   they could not (e.g. "switch look and preset without leaving the window").
 - WHAT ADDS: per look, counts of front / visible / disabled / hidden rows, and the list of hidden keys
   with reasons. Any key that should be on the front page but is not gets flagged.
 - Live check (parent asks the user, then swaps): open via second launch with the taskbar hidden; drag
   a knob (live); undo; apply a preset (window stays); header correct; HDR off then on.
PHASE 2 - compare and line-up: hold-to-see-preset A/B (snapshot/restore Config), Changed-vs-any-preset
 diff list, colour wheels for film/masses/droplets, per-look start preset + mood rotation editor, the 72
 ini-only keys listed read-only, optional hotkey. Proof: shots + dumps as above.
PHASE 3 - optional: preset thumbnails (headless --shot per preset; rote, cheaper model); cross-look
 sequences (render-path fade -> separate brief with parity); feature tour hook (item AI).
Rote (hand to a cheaper model): keymeta seeding review, tray grouping, thumbnail renders, FEATURES.md
 cross-links.

## QUESTIONS FOR THE USER
1. OK to vendor Dear ImGui (MIT licence, source in src\third_party, no DLL) for the Settings window?
   It would be the one exception to "no dependencies beyond the Windows SDK". If not, we build custom
   Win32/Direct2D controls: about twice the work, same result.
2. For Liquid Acid, are these the right main knobs: film brightness, colour speed, mass colour,
   droplet colour, droplet amount, rise speed, glow, haze, lid, grain, peak nits, mirror? Is anything
   missing that you reach for every time?
3. On restart, should the app keep your latest edits, or reload the last preset you applied? Today it
   reloads the preset, which silently drops edits made after applying it.

## Review questions (EXECUTOR-CARD §6), answered from the planner's side
- WORKS: nothing here counts as done on a build alone. Each requirement has a machine check (ui-dump,
  keymeta-check, parity md5, preset-identity) plus a window shot. The one thing only the user can
  confirm is the live panel check (HDR on/off, hidden taskbar).
- DIFFERENCE: today, reaching dye_lum means scrolling ~6,000 px of one page under a header that
  names the wrong mode. After phase 1 it is a named knob under MASSES on the front page, and the header
  states the real look, preset and change count.
- WHAT ADDS: the gain is concentrated. The accurate header, the in-window look/preset switch, search
  and the gates remove the "unusable" complaints; undo/compare/colour wheels are comfort (phase 2);
  thumbnails and sequences are optional. If the user refuses ImGui, the phase 1 feature list stays and
  only the cost doubles.
