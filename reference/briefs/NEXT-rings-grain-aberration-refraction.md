# Queued Opus brief: hollow "lens" droplets, film grain, chromatic-aberration vignette, local refraction doubles
(launch after the oil_edge_mode executor commits; one Opus at a time)

Repo: "C:\Users\abg77\OneDrive\Desktop\wall paper engine\claude code". Read AGENTS.md, PROGRESS.md, WORKLOG.md tail, `git log -12 --stat` (droplet sim 210ff10/23d23d7/ee4c045/7f0188d, oil drag & co 5abf21f, edge mode = the commit after 6f40296). HARD RULES as every executor: never touch the live wallpaper (build2\live\FluidWallpaper.exe), build\, Wallpaper Engine, tools\away-pause.ps1, %APPDATA%; build ONLY build2 from PowerShell via build2.cmd; gpu.lock around builds AND renders; headless --shot only; fluid look byte-identical with every new key 0 (we-look-live.ini 60 s 2560x1440 seed 1234 --hdr on md5 10E36EBF1A74EDFE609065D757300054). Process: engineering + ONE verifying 960x540 shot per feature, commit (Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>), STOP with a report < 300 words + a rote list (sheets, md5, 1440p ms delta, presets, docs) for a Sonnet worker. Standing line: say what is delegable / needs the user.

User (2026-09-17 22:30, two panel photos of the live rising look, reference/shots/panel/2026-09-17-rings-liked.jpg): "I like these empty doubles, you think you can add more of them. Also there is some noise, it's fine, but do you think you could add some film noise? And possibly, sorta like chromatic aberration, maybe a tiny bit, but more like a double image / refraction, but not as a vignette, just like around spots, randomly generated. I mean chromatic aberration separately probably as a vignette."

AESTHETIC TARGET for the camera family B/C/D/G (user, 23:05): "it should look like a MICROSCOPE." I.e. the frame reads as oil under a macro/microscope lens with bright-field illumination: shallow depth of field (only one plane crisp, far/small elements soft, bokeh on highlights), a gentle field-edge falloff (softness + slight vignette toward the corners, never a hard circle), chromatic-aberration fringes that grow toward the field edge and along hard edges, fine sensor grain, and a hint of slow focus breathing (`dof_breathe`, tiny periodic focus-plane drift, default 0). Tune the preset values of B/C/D/G together so they read as ONE lens, not four filters. Not a vintage-film look: no scratches, no flicker of exposure, no sepia.

IMPLEMENT (all `[liquid_acid]` unless noted; every key default 0 = off):
A. HOLLOW "LENS" DROPLETS — the "empty doubles": a droplet drawn as a thin dark ring with the oil colour showing through its centre (what 7f0188d removed as an accident: a rim without a fill). Make it deliberate: `droplet_ring_frac` (0..1, fraction of NEW water-in-oil droplets that are rings; the choice is per-droplet, seeded, fixed for its life), `droplet_ring_width` (rim width as a fraction of radius, default ~0.18), `droplet_ring_lift` (0..1, how much the interior is lightened / slightly refracted like a lens, default 0.15). Rings keep the full sim (drift, coalescence: a ring merging with a solid droplet becomes solid; ring+ring = bigger ring). Render inside the same field pass (the ring is the isoline band of a droplet whose interior is pushed back above the threshold), so edges/halo stay consistent. CLUMPING (user, 22:40, refs reference/shots/photos/bubbles-ref-1.jpg / -2.jpg = boiling oil bubbles, soap bubbles, a pile of glass beads: "it kinda looks like this, should probably clump"): rings behave like BUBBLES, not lone lenses — they attract each other over a short range and pack into clumps / rafts, touching with a shared thin wall (two rings in contact do NOT merge into one bigger ring; their overlap becomes a single dark wall between two interiors, like foam), sizes varied, occasional lone ring. `droplet_ring_clump` (0..1, attraction strength, default 0.6 in the rising presets), a max raft size so a raft never becomes a solid mass. Rafts drift with the oil as a group. A ring meeting a SOLID droplet: the solid absorbs it (ring disappears into the solid, area-conserving). Refs are ideas, never parity targets. Rising presets: droplet_ring_frac 0.35.
B. FILM GRAIN — `[post] film_grain` (0..1 amount), `film_grain_size` (px, default 1.5), `film_grain_speed` (default 1, new pattern every frame at 1; lower = slower flicker), luminance-weighted (more in mids/darks, less on peaks, never lifting true black on the OLED beyond a floor), applied in the final composite AFTER post_chroma/post_lift, monochrome by default with `film_grain_color` (0..1) for RGB grain. Cheap hash noise, no textures. Rising presets: 0.08.
C. CHROMATIC ABERRATION VIGNETTE — `[post] aberration` (0..1) radial RGB split growing from the centre outward (r/b sampled at ±k*(uv-0.5)*|uv-0.5|), `aberration_max_px` (default 3). Tiny by default (rising presets 0.25 → ~0.75 px at the corners). Separate from D.
D. LOCAL REFRACTION DOUBLES — around SPOTS (droplets and holes), randomly chosen: a faint displaced second image of the surroundings, like looking through a lens / a doubled edge. `spot_double_frac` (0..1 of droplets+blobs that get it, per-element seeded), `spot_double_offset` (px, default 2.5), `spot_double_strength` (0..1 blend, default 0.35), `spot_double_radius` (multiple of the spot radius over which it fades out, default 1.6). Implement as a per-pixel offset sample driven by the nearest selected spot's gradient (refraction direction = away from the spot centre), blended with the original; no full-screen cost when the fraction is 0. Must not fight the film-edge band or the meniscus halo.
E. Plumbing as usual: fluid.h/fluid.cpp/main.cpp ini parse+write, settings.cpp sliders. Presets: set the values above in reference/configs/acid-rise-12.ini, acid-rise-rotate.ini and the two "Liquid Acid - rising" presets (and the "(soft film edge)" variant if it exists); leave the WE fluid presets untouched.

RENDER for your own check (960x540, seed 1234, --hdr on, t=60): base vs A / B / C / D individually, then all together. The rote worker will make the sheets.

F. (added 22:45, user ref reference/shots/photos/liquid-acid-ref-lensshade.jpg: "the oil still has some brightness gradient in itself") LENS SHADING inside each oil mass: `[liquid_acid] oil_lens_shade` (0..1, default 0): darken the oil colour smoothly with distance INSIDE the isoline — brightest/flat in the middle, falling off toward the rim, plus a thin slightly darker band just inside the meniscus; falloff distance relative to the local lens radius (small droplets get a proportionally small shade), S-curved (reuse the oil_edge_curve profile), never touching the ink/holes, never crushing the colour below ~70% (`oil_lens_shade_floor`). Rising presets 0.35. Verify with one crop of a big mass and a droplet cluster.

G. (user 22:50: "maybe like bokeh? depth of field? I suppose that's not film, just camera artefacts") DEPTH OF FIELD tied to the parallax depth: `[post] dof` (0..1, default 0): blobs/droplets that rise_parallax treats as further away (smaller size factor s) get a soft blur that grows with (1 - s), and tiny bright highlights (specular/glow points) on them bloom into small bokeh discs (`dof_bokeh_px`, default 6); the near/large masses stay sharp. Cheap: a 2-tap or 4-tap radial blur keyed on a per-pixel "depth" derived from the nearest blob's s. Rising presets 0.3. Camera-artefact family together with B/C/D.

H. (user 23:00: "add some colour splashes in the oil occasionally, just want to see what it looks like") COLOUR SPLASHES: `[liquid_acid] oil_splash_rate` (per minute, default 0), `oil_splash_size` (fraction of disc_max, default 0.5), `oil_splash_life` (s, default 25), `oil_splash_hue` (0 = a contrasting colour from the active sweep palette (the slot 3-4 steps away), 1 = complementary hue of the current oil colour, 2 = random tile9 palette colour). Behaviour: occasionally a splash of a different colour appears INSIDE an existing oil mass (spawn point inside a blob, not in the ink), starts small and vivid, grows over ~3 s like a dye drop hitting oil, then its colour DIFFUSES into the surrounding oil (per-blob colour relaxes toward neighbours over oil_splash_life; the metaball colour weighting already blends merged blobs, so a splash blob's colour bleeding into its neighbours is the mechanism) and fades back into the mass. It must never punch a hole and never affect the ink. RARE (user): rising presets rate ~0.3/min (one every 3-4 minutes, randomised so it never feels scheduled); for the verifying shot use a scratch ini with a high rate.

REVISION of C and D (user, 23:00, microscope refs reference/shots/photos/microscope-ref-1-edge-ca.jpg / -2-field-edge.jpg / -3-halo-double.jpg — ideas, not targets):
C' CHROMATIC ABERRATION = LATERAL, PER-EDGE (ref 1): along every hard edge (droplet rims, hole isolines, ring walls) a thin MAGENTA fringe on one side and a GREEN/CYAN fringe on the other, i.e. the R and B channels sampled slightly displaced ALONG THE LOCAL EDGE NORMAL (use the field gradient direction), amplitude `aberration_px` (SUBTLE — user 23:05: "dial the chromatic aberration down": default ~0.7 px at 1440p, visible only when you look for it; the HALO is the main effect) growing modestly with distance from the field centre (`aberration_field` 0..1) — the radial corner-only split from the original C is the minor component, the edge fringe is the look. Keys: `[post] aberration` (0 off), `aberration_px`, `aberration_field`.
D' THE "DOUBLE" = BRIGHT-FIELD HALO (refs 1 and 3): a thin BRIGHT rim hugging the OUTSIDE of every dark edge (and a faint dark rim outside bright edges) — the defocused diffraction ring of a microscope, so a hairline object reads as a dark line with a bright line beside it and every dark droplet has a light halo ring. Implement as a signed-distance band just outside the isoline (width `halo_px` ~2 px at 1440p, strength `halo` 0..1, slight colour lift toward the oil colour / white), NOT a displaced copy of the surroundings; the existing meniscus_from_ink halo is the natural place to extend. Applies to droplets, rings and holes alike; on far/blurred (dof) elements the halo widens and fades with the blur.
Field edge (ref 2): the gentle darkening + softness toward the corners is the vignette, with a slight warm/yellow fringe at the very edge if `aberration_field` is high; never a hard circle.
CALIBRATION (user 23:10, ref reference/shots/photos/microscope-ref-4-subtle-halo.jpg — "those [refs 1-3] are extreme; this is closer to the idea, just a subtle thing"): the halo is a SOFT bright glow hugging the outside of dark objects (phase-contrast look), a few px wide with a smooth falloff rather than a crisp thin line, and mild — noticeable, not graphic. Colour fringes barely there. Overall slightly soft focus + fine grain. Everything in this family errs on the subtle side; the user can turn knobs up from the tray later.
NOTE (user 23:12): the microscope target does NOT disqualify standard FILM GRAIN — item B stays as a real film-grain look (visible, organic, luminance-weighted), not just faint sensor noise. Ship B at a clearly visible but tasteful level (~0.10-0.12) and keep the knob.
THE TARGET (user 23:15, "omg I found it — this": reference/shots/photos/microscope-ref-5-THE-ONE.jpg, plant cells under a light microscope). Supersedes the amplitude notes above for C'/D': (1) lateral aberration on a WARM/COOL axis — every edge gets an ORANGE/yellow fringe on one side and a CYAN/blue fringe on the other (= R and B displaced in opposite directions along the edge normal; G stays), amplitude at this ref's level relative to feature size (~1.5 px at 1440p on droplet rims, a touch more on big hole isolines), present on every edge in the frame, a little stronger toward the field edge; (2) the DOUBLE CONTOUR — edges read doubled: a bright core line with soft darker lines either side (defocused), i.e. the halo is a soft bright band just outside the dark edge plus a faint dark echo beyond it; (3) overall slightly soft, dreamy, fine grain; the interiors of the pale cells are smooth. Still ONE lens: tune B/C'/D'/G together to land near this ref's feel on the pink rising look; refs are ideas, never parity targets.
FINAL CALIBRATION (user 23:20, more general refs -6/-7/-8 in reference/shots/photos/: wood section, grainy micrograph, SEM triangles): "super super slightly doubling plus noise"; the orange/cyan walls in ref 5 may be the specimen's pigment, not the lens — DON'T chase them. Ship: (a) doubling very slight (a faint second contour / soft bright rim on shapes, like the SEM edge glow in ref 8), (b) real film grain (item B), (c) overall gentle softness, (d) warm/cool edge fringe present but minor (~0.7-1 px), (e) subtle vignette. Common thread over all refs = subtle doubling + grain + edge glow + softness. Knobs stay so the user can push any of them from the tray.
HALO SHAPE (user 23:22): "very weak, but quite wide" — low amplitude (a few % lift) over a WIDE band (~8-15 px at 1440p, scaling a little with the shape's size), smooth falloff; not a thin bright line.
EDGE SOFTNESS (user 23:25, live photo reference/shots/panel/2026-09-17-edges-too-sharp.jpg: "these edges are way too accurate and focused"): a `[post] softness` (or `lens_blur_px`) key: every droplet/hole isoline gets a slight DEFOCUS — ~1.5 px at 1440p on the transition (an isoline-aware blur or a widened, S-curved anti-aliasing band, not a full-frame blur that mushes the grain), more on far/small elements via dof, so no edge in the frame is razor-sharp. Rising presets: on. This is the most important item of the camera family for the user.

I. BACKLIGHT PENUMBRA (user 23:30, diagram reference/shots/panel/2026-09-17-penumbra-diagram.jpg, view rotated 90 deg CCW: the light sits UNDER the centre of the frame; "pigments lit less look different, like the sky and sun angle"): around every black mass (holes, ink) the oil gets less light → over a band a few px to ~10 px wide at 1440p ("there are layers, up and down, causing the gradual shift" → smooth, S-curved gradient, not a step) the oil colour (a) DARKENS a little and (b) HUE-SHIFTS in an arbitrary but consistent direction per look. Keys `[liquid_acid] oil_penumbra` (0..1 strength, default 0), `oil_penumbra_px` (band width at 1440p, default 8), `oil_penumbra_hue` (degrees, default +25, sign = direction; per-look value), `oil_penumbra_dark` (0..1, default 0.25 of the strength). Applies to the dye/oil side of every isoline (droplets, rings, big holes, ink masses); never touches the black. Rising presets: on at a modest value; the user wants to TRY it and judge. Optional: a very faint mirrored version on the far side of small droplets (light wrapping) if cheap.
Magnitude (user 23:32): realistically very subtle — e.g. an orange oil goes a slightly yellower, slightly darker orange in the band, "almost impossible to see" on its own; it should register as depth, not as a coloured outline. Presets: oil_penumbra ~0.3, hue ~+12 deg, dark ~0.15.

J. ACCENT COLOUR BY SIZE (user 23:25, photo reference/shots/panel/2026-09-17-accent-crop-liked.jpg: "if the entire frame looked like the cropped one it would be better" — one colour with small accents of its complement, "like 80/20, one colour + opposite"): today a blob's colour index is random, so a complement shade lands on big masses too. Add `[liquid_acid] accent_mode` (0 = current random; 1 = BY SIZE: blobs below `accent_max_r` (fraction of disc_max, default 0.35) and the oil-on-ink droplets (kind 1) take the slot's ACCENT colour = the 4th shade (`sweep_oil_N` floats 10-12; the 80/20 configs put the complement there), everything larger uses shades 1-3), plus `accent_frac` (0..1, default 0.6: the share of small elements that get the accent, hashed per blob so it is stable) so it stays an accent, not a rule. Rings can be accent-coloured too. Presets: acid-rise-8020.ini + "(80-20 …)" presets → accent_mode 1.

K. (user 23:27, photo reference/shots/panel/2026-09-17-rings-too-perfect.jpg: "these are pixel perfect circles") The deliberate rings from 20154bb read as stamped "O" glyphs: perfectly round, thick uniform wall, hard edges. The rings the user LIKED (reference/shots/panel/2026-09-17-rings-liked.jpg) were thin, soft-edged and slightly lopsided. FIX FIRST: (1) wall thin by default (droplet_ring_width ~0.08) and its edges get the same isoline defocus/S-curve/halo as every other edge; (2) rings are never perfect circles: per-ring seeded anisotropy (ellipse 1.0-1.25 with a slowly rotating axis) + a low-order wobble (2-3 lobes, few %) that breathes; (3) a ring next to any other element deforms toward it exactly as solid droplets do (they share the field), and a lone ring still shows the wobble; (4) ring frac ~0.15 by default, more only if the user asks; (5) interior lift subtle. Verify with a 4x crop of three lone rings.

L. (user 23:35, photo reference/shots/panel/2026-09-17-shading-wanted-everywhere.jpg: "what happened that the bubbles got a solid outline — whatever shading is in this photo needs to be everywhere") The soft wide rim + glow on the BIG mass is right; on the medium solid droplet the same effect collapsed into a crisp 1-px bright OUTLINE. Cause: halo / penumbra / softness / thin-edge bands scale with the element's lens radius, so small elements get sub-pixel bands = hard lines. Fix: every band gets a MINIMUM width in output pixels (halo ≥ ~6 px, softness ≥ 1.5 px, penumbra ≥ 4 px at 1440p, scaled with resolution) and a maximum, so small/medium droplets show the same soft glow as masses and no element ever shows a solid outline. Also the pink droplets sitting in the black (kind 1) have exactly the wanted glow — keep that and give the black droplets in the pink the mirrored version. Verify with a 4x crop of one big mass edge, one medium droplet and one small one side by side.

DIRECTION CHANGE for softness (user 23:45, going to sleep: "maybe move the blur largely to the sim as a whole + post processing"): the defocus is ONE GLOBAL optical blur of the whole composited frame (oil, ink, droplets, rings, holes alike) applied in the [post] pass BEFORE grain (grain stays crisp on top, like a sensor), with `softness` = blur radius in px at 1440p (~1.5-2.5), plus the dof term adding more blur on far/small elements; per-element bands remain only for halo and penumbra (with the minimum px widths of item L). This replaces the isoline-aware band approach for softness: it guarantees every edge in the frame gets exactly the same optical treatment and no element can show a hard outline. Cheap separable blur (or a 2-pass 5-tap) on the final colour buffer; skipped entirely when softness = 0 (fluid md5).

M. (user 23:50, ideas list, "just ideas", not orders) 1. Simulated OFF-VIEW light: a lamp outside the frame,
   so shading/highlights on droplets and rings are directional (one side lit, the other in shadow), and the
   oblique light makes round things APPEAR oval (foreshortened highlight/shadow), which also helps K.
   2. Simulated DEPTH ("death" in the message = depth): elements at different heights in the film, slightly
   different focus/scale/parallax (ties to G dof/bokeh and rise_parallax). 3. Direction/force-guided
   OVALNESS: when a grouping is pushed (mouse push, rise, drag) it stretches slightly along the push
   direction; velocity -> anisotropy of the droplet/ring band.

N. (user 2026-09-18 17:35, on the live post-pass build) DEPTH per droplet: give every droplet/ring a depth
   value; the back layer rises faster (their words: "the back particles going up faster"); use depth for
   a real DEPTH OF FIELD in the post pass (blur radius per element by |depth - focus|, i.e. the post
   pass needs a depth/CoC channel written by the display pass -- the FP16 target's alpha is free for
   it) instead of one global defocus. Ties to G (dof/bokeh) and M2 (depth). Also: "slightly slightly
   too much blur" on post_blur_px 1.5 -> 1.2 applied live.

   Refs for N/G (user 17:40, Requiem-for-a-Dream microscope frame): reference/shots/photos/microscope-ref-9-requiem-strip.jpg, -10-requiem-cell.jpg -- ONE cell in focus (grainy body, thin pale wall with a warm/cool fringe, soft halo) while the background rings are out-of-focus ghosts: that is the depth-of-field target, plus the film grain sitting on everything.

O. (user 2026-09-18 17:50, "macro-ish cellulose noise", ref microscope-ref-10-requiem-cell.jpg) The cell body
   in the ref is not smooth: a fibrous, mottled, slightly streaky texture like paper fibres / cellulose
   strands under a microscope. Wanted MOSTLY IN THE BLACK masses ("the black oil more than the oil"),
   fainter on the coloured film. It belongs to the SURFACE: low contrast, features ~20-60 px at 1440p,
   anisotropic (fBm stretched along a slowly turning direction, so it reads as strands not blobs),
   advected with the rise motion so it drifts with the masses, and it sits UNDER the film grain (i.e.
   in the display pass, not the post pass). On the black side it must stay dark: lift the black by a
   few nits at most, never a grey wash -- the OLED's true black is the best thing on the panel, so the
   texture there should read as faint dark-grey strands in the black, strongest near edges and
   fading to pure black deep inside a big mass (a plausible thickness falloff), not a uniform fill.
   Keys: [liquid_acid] cellulose (0..1 master, default 0), cellulose_ink (weight on the black side,
   default 1), cellulose_oil (weight on the film, default 0.35), cellulose_scale (px), cellulose_drift
   (0..1 with the rise motion). Ship values in the rising configs/presets that read as subtle on the
   panel; the NO-lens twin stays 0. Verify with a 4x crop of a black mass edge + interior and a film
   patch, before/after.

P. (user 2026-09-18 17:58, SUPERSEDES the reading of O; refs reference/shots/photos/film-overlay-ref-1..3.jpg)
   What "macro-ish cellulose noise" meant: the artefacts of a projected/scanned FILM overlay -- hairs and
   fibres caught in the gate (thin, curly, bright strands, a few px wide, 20-150 px long), dust specks
   (sparse bright points, 1-3 px), fine vertical scratches (faint, full-height or partial, drifting
   sideways), and optionally a very faint light leak. Visible mostly over the DARK areas ("the black
   oil more than the oil"): additive, weighted toward dark pixels, near-invisible on the bright film.
   It has to CHANGE: "once in a while, or continuously" -- real film: specks flicker frame to frame,
   a hair sticks in the gate for a few seconds then is gone, a scratch persists for a stretch then
   drifts/disappears; nothing sits still. This is an IMAGE-SPACE overlay -> kPostSrc (after the blur,
   with/after the grain), procedural (no textures): per-artefact seeds + lifetimes from a hash of
   floor(time/period), so the population changes every few seconds. Keys [post]: film_dust (0..1,
   master), film_hairs (count/intensity), film_scratches, film_leak (default 0), film_artefact_rate
   (how often the population changes, s). Ship subtle (a few specks, one hair now and then, scratches
   faint); NO-lens twin 0. Must stay off unless named (fluid parity by construction).

Q. (user 2026-09-18 18:10, "do you see that light, spreads?" on film-overlay-ref-1/-4 and the Requiem cell)
   BLOOM: the brightest areas bleed a soft, very wide wash into the dark (the corner light leak covers a
   third of the frame; the pale cell wall glows outward into the black). Post pass (kPostSrc): a large-
   radius (>= 100 px at 1440p, scaled with the frame), very weak (a few percent) bloom taken from the
   bright film into the black; cheap = downsample chain or a few wide taps at low res. It must MOVE
   by itself, a tiny bit, like an idle animation: the bloom's centre/weight/radius drift slowly and
   continuously (slow sine sum, seconds to a minute), never static, never jumping. Keys [post]:
   bloom (0..1, default 0), bloom_px (default ~120), bloom_drift (0..1, default 1). Subtle.

Q (REVISED, user 2026-09-18 18:20): the bloom and the VOLUMETRIC FOG are ONE task. Ref
   reference/shots/photos/backlit-glass-ref-volumetric.jpg (a glass of cloudy water with a blob, lit by
   a lamp from below): the water itself glows -- brightest near the lamp, fading with distance -- so the
   dark side is never pure black near the light but a soft milky murk; the bright film bleeds a wide,
   weak glow into the dark (the bloom); blobs are lit from the lamp side (brighter toward it, darker
   away) with a thin refractive rim on the lamp side. Build it as one "light in the water" post effect:
   an off-view light position (drifts slowly, tiny idle motion, never static -- the user's "idle
   animation") that drives (a) a distance-falloff haze added into the dark areas, (b) the wide weak
   bloom from the bright film, (c) a gentle directional shading term on the masses if it can be read
   from the display pass (optional; the display pass has the isoline normal). Keys [post]: fog (0..1
   master, default 0), fog_px / fog_reach, bloom (0..1), bloom_px, light_x / light_y (default: below
   centre, off-frame), light_drift (0..1, default 1). The user: "idk how it would look" -- render
   three strengths (subtle / medium / strong) for them to pick from; keep OLED true black deep in the
   dark far from the light (haze must fall to zero, not lift the whole frame).

P addendum (user 2026-09-18 18:30, ref film-overlay-ref-5-colour-leak-stock.jpg): the light leak is COLOURED
   -- warm core (red -> orange -> yellow) fading to a cool green/teal fringe, entering from one edge with
   soft vertical bands (gate/sprocket shadows), drifting and coming/going like the other artefacts.
   Plus a separate FILM STOCK grade key [post] film_stock (0..1, default 0): lifted teal shadows, warm
   highlights, slightly different per-channel curves (cross-process feel). Keep every key; the user
   will grade everything by hand on the panel afterwards, so expose all of it in Settings.

R. (user 2026-09-18 18:50, sketch reference/shots/photos/sketch-perspective-camera.jpg: a lens at the tip of a
   view cone looking at the flat dish) The camera is PERSPECTIVE, not orthographic, and that is the
   real fix for "perfect circles": (1) a hollow ring/bubble is a body with height, and the focal plane
   is a plane, so no ring is entirely in focus "unless very lucky" -- with per-droplet depth (item N)
   the blur is |depth - focus| per element, and the focal plane is effectively CURVED relative to the
   flat dish (field curvature), so centre and edge of the frame cannot both be sharp; (2) every element
   is seen at the angle between the optical axis and the ray to it: off-axis rings foreshorten
   radially (minor axis pointing at the frame centre, growing with distance from it), the wall reads
   thicker on the far side and thinner on the near side, and the rim/highlight favours the side facing
   the axis -- INTRINSICALLY asymmetric, never a stamped O; (3) the off-view lamp (Q) adds the second
   asymmetry (lit side / shadow side). Keys: [post]/[liquid_acid] camera_fov (0 = orthographic =
   today), camera_focus (depth of the focal plane), camera_field_curve, camera_axis_x/y (where the
   optical axis meets the dish; default centre, may drift slowly with the lamp). Implement together
   with N (depth) as ONE task: "perspective camera + depth of field".
   R addendum (user 18:55): TILT-SHIFT / freelensing (Lensbaby): the lens tilted off the sensor plane
   so the plane of focus cuts the dish at an angle -- a band of sharpness (a "sweet spot" or a slanted
   strip) with focus falling off smoothly to either side, and the axis of that band slowly turning and
   drifting (the same idle motion as the lamp). Keys: focus_tilt (0..1 amount), focus_tilt_angle
   (deg, drifts when focus_tilt_drift > 0), focus_band_px (width of the sharp strip). This is the
   same per-pixel circle-of-confusion machinery as the perspective camera: CoC = f(element depth,
   field curvature, tilt-plane distance); implement all three in the one N+R task.
   R correction (user 18:58): the tilt does NOT drift continuously. It is an OCCASIONAL READJUSTMENT: the
   focus plane holds still for a while (tens of seconds to minutes), then someone "re-tilts" the
   lens -- a short eased move (a second or two, maybe a slight overshoot/settle like a hand on a
   lens) to a new angle/offset, then still again. Keys: focus_tilt_period (mean seconds between
   readjustments, randomised), focus_tilt_move_s (duration of a move). Same for the focus distance:
   an occasional refocus, not a drift. The LAMP (Q) keeps its slow continuous idle drift.

S. (user 2026-09-18 19:00) CONSERVATION: "matter cannot be created or destroyed" -- dark oil that just
   spawns in from nothing looks wrong (rare, but it happens on the panel). Audit every place the sim
   creates or removes mass: blob (re)seeding, blob respawn when one drifts off-frame or shrinks, the
   droplet emitter (kind 0 holes and kind 1 oil), ring births, merges/absorption (area-conserving
   already?), and the palette/hue sweep if it ever pops a blob. Rule: nothing appears at a visible
   size in the open. New material must either (a) enter from OFF-FRAME (drift in across an edge),
   (b) grow from a tiny seed that is below the visible threshold and swells over seconds (a droplet
   budding off / rising out of a mass), or (c) split off an existing mass (pinch-off) so the total
   visible area is conserved; removal likewise: shrink, merge into a neighbour, or leave across an
   edge -- never vanish. Log each spawn site in the report with what it now does. Also check the
   fullscreen-pause resume and the dt clamp: a big time jump must not teleport or respawn blobs.

T. END GOAL (user 2026-09-18 18:40, refs reference/shots/photos/endgoal-ref-1..5.jpg): "very very dreamy,
   aesthetic, hyper-realistic colours, as if a very very nice film camera is taking a photo of this oil
   thing; clearly restricted, like you're in a scope; maybe internal reflections; still the oil; depth,
   looks real." Named ingredients (Fable's reading, for the user to confirm on their phone):
   1. OIL-AND-WATER MACRO photography (search "oil and water macro photography abstract"): droplets
      that REFRACT a coloured background, razor-thin focus, big soft bokeh discs, saturated but
      physically plausible colour -> our droplets/rings should refract the film behind them (item N+R
      depth + a refraction term inside each lens), not just be flat holes.
   2. CINESTILL 800T HALATION (search "cinestill 800t halation"): a warm red-orange glow bleeding around
      every hot highlight (the film's missing anti-halation layer), teal shadows, amber lights ->
      a halation key in the post pass: bloom taken ONLY from the brightest pixels, tinted warm, tighter
      than the fog bloom (item Q is the wide one).
   3. SCOPE / EYEPIECE (search "microscope eyepiece view phone photo", "rifle scope view"): the image
      lives inside a CIRCULAR field stop with a soft dark edge, slight barrel curvature at the rim,
      the outside is black or near-black; faint INTERNAL REFLECTIONS on the eyepiece glass (a ghost of
      the bright areas, offset and dim, drifting with the lamp) and a glint on the rim -> keys [post]
      scope (0..1), scope_radius, scope_edge_px, scope_ghost (internal reflection), scope_rim.
   4. DEPTH: N+R (perspective, per-element focus, tilt) + Q (lamp/fog) already queued.
   Everything additive to the current look; the oil stays the subject.

T (REVISED, user 2026-09-18 19:00, refs endgoal-ref-6..10 = Territory Studio's Blade Runner 2049 screen
   graphics, the "Triboro - Bioluminescence" mood board): "more dreamy than oil-water macro; not exactly
   film, but obviously artifacted BY THE CAMERA." The look is a LAB INSTRUMENT'S CAMERA looking at a
   petri dish on a lightbox: (1) the DISH itself is in frame -- a circular rim with a glint, specimen
   lit from below, optionally a faint counting grid under it; (2) instrument optics, not film stock:
   heavy soft bloom of every highlight, low-contrast haze, slight barrel distortion, chroma bleed
   toward the edges, soft vignette, the picture sometimes seen THROUGH curved glass or a monitor tube
   (rounded-rectangle mask, faint scanlines, edge smear -- the Denabase shot); (3) colour: a duotone
   grade (teal/cyan/violet or red-lamp) on the surround while the specimen stays hyper-saturated
   pastel (bioluminescent colonies, cream discs on dark agar); (4) optional instrument UI marks
   (frame lines, tiny markers, no text) -- only as a separate preset, "it should still be the oil".
   Replaces the CineStill/eyepiece reading above where they differ; halation stays as bloom.
   T note (user 19:05): "what's in focus is EXTREMELY in focus" -- the dreaminess is bloom, haze and
   out-of-focus depth, NOT global softness. The focused slice must be razor sharp (crisp edges, visible
   texture/grain detail), so the global post_blur_px should drop to ~0 once N+R's per-element depth of
   field lands, and all blur comes from depth (out-of-focus elements very soft, in-focus ones sharper
   than today). Sharp + bloom, not soft + bloom.
   T note 2 (user 19:10): the Denabase tube shot (endgoal-ref-8) is NOT an aesthetic target -- it is the
   FOCUS reference only: "extreme contrast in focus level": the focused plane is tack sharp with fine
   texture resolved, everything off it falls away fast into a very soft blur (a shallow, macro-lens
   depth of field with a steep falloff, not a gentle one). Aesthetic targets = the bioluminescence and
   red-dish boards equally (endgoal-ref-6/-7/-10).
   T note 3 (user 19:15): NO visible instrument, NO petri-dish rim, NO grid, NO UI marks, and NO dark
   corners / black areas at the left and right of the screen (the frame stays full-bleed oil edge to
   edge). What may be simulated instead: a plastic or glass CAP/LID over the dish -- i.e. the picture
   seen THROUGH a transparent cover: faint reflections/ghosts of the bright film, a soft sheen or
   smear that drifts, slight refraction, a subtle highlight from the lamp, maybe a faint fingerprint
   or condensation haze -- all full-frame, never a mask or a border. The vignette stays as it is
   (gentle), nothing that reads as a hole in the display.
   T note 4 (user 19:20, OLED burn-in): EVERYTHING must move -- nothing (lid sheen, ghost, haze, glint,
   leak, lamp) may sit at a fixed screen position, ever. Driving idea: the haze/sheen pattern moves
   WITH the fluid -- the sim's own velocity field (the 64x36 low-res velocity readback already exists)
   advects the haze/condensation/lid-smear pattern, so a burst on the right that moves the oil also
   pushes the haze ("heat" moving it). Rule for every post effect: position = slow idle drift + an
   advection/warp term from the fluid velocity, and long-period wander so no pixel is bright for
   minutes. Applies to the lamp (Q), the lid (T note 3), the leak (P), the bloom centre.

U. IDEAS (Fable, user asked 19:25; sole constraint: nothing exists on screen permanently). Each with its
   motion source:
   1. Convection haze: the fog is a field advected by the sim velocity + a slow buoyant drift upward
      from the lamp side, thinning as it rises; a burst pushes a plume of haze ahead of it.
   2. Breathing focus: the focal plane drifts slowly through the depth range between the occasional
      hand readjustments, so which droplets are sharp changes continuously (nothing stays sharp).
   3. Lid condensation: droplets of condensation on the lid that grow, run, merge and clear in cycles
      (minutes), refracting the film; the clear patch wanders.
   4. Lamp on a gimbal: the light position does a slow Lissajous wander plus a rare re-aim; the bloom,
      fog and lamp-side shading all follow it (already in Q, formalised).
   5. Ghost of the frame: a dim, mirrored/offset internal reflection of the bright film in the lid
      glass whose offset vector rotates slowly (lid tilt), so it never overlays the same spot.
   6. Lens breathing: the whole picture's scale pulses a fraction of a percent with the focus moves
      (real lenses breathe), which also shifts every static pixel.
   7. Thermal shimmer: a very fine, slow refractive wobble (heat above the lamp) warping the picture
      by a pixel or two, strongest near the lamp, moving with the fluid.
   8. Grain that walks: the grain/noise pattern's cell grid slides slowly (sub-pixel per second) as
      well as re-rolling, so film grain never re-lands on one pixel lattice.
   9. Bubble weather: the droplet population has slow "seasons" -- more rings, then more solids,
      then sparse -- driven by a minutes-long clock, so the frame's density never settles.
  10. Hue tide: the palette sweep gets a spatial component (hue varies slightly across the frame and
      that gradient rotates), so no region keeps one colour.
  11. Wandering vignette: the vignette centre drifts with the lamp so the darkest corner rotates.
  12. Pixel-shift safety net: the entire final image translates by up to a few px on a many-minute
      orbit (classic OLED pixel shift) as the last guarantee, sub-pixel steps so it is invisible.
   U decision (user 19:35): DO ideas 4 (lamp on a gimbal), 5 (ghost of the frame in the lid), 7 (thermal
   shimmer), 9 (bubble weather), 11 (wandering vignette), 12 (pixel-shift safety net). NOT 1, 2, 3, 6,
   8, 10. Together with T note 3 (transparent lid: sheen/ghost/refraction/lamp highlight, full-frame,
   no mask) these form the next post/sim task "V: lid + motion". More refs: endgoal-ref-11..14
   (BR2049 scope cell, bone scan, LAPD optics sheet with coloured lens flares/ring ghosts and
   rainbow fringes on a warm field, brain) -- note the LAPD sheet's coloured ring ghosts and soft
   rainbow flare are exactly idea 5's ghosts + the halation bloom; full-frame versions only.
   V KEY REF (user 19:40): endgoal-ref-13-br2049-lapd-optics.jpg (the six-panel LAPD sheet) is EXACTLY
   the internal artifacting wanted: concentric coloured ring ghosts (lens-element reflections of the
   bright field: red/amber/green rings, offset from centre), soft rainbow fringes, a warm veiling
   flare across the field, bright curved streaks (the cover glass edge catching the lamp), a hot
   spot with a wide amber glow, faint iridescent (oil-film) colour sweeps on the surface. All of it
   drifts and reorients (idea 4/5: the ghosts' offset vector follows the lamp), full-frame, no
   circular mask. This sheet defines task V's look; the lid (T note 3) is the physical excuse for it.
   V ARCHITECTURE (user 19:45): the light source, the lid ghosts/flare, the idle motion, the depth-of-
   field readjustments and (maybe) some fluid mixing are ONE MOVEMENT: a single shared "rig" state
   (lamp position, camera axis/tilt, focus distance, lid tilt) with one slow idle drift plus the
   occasional eased readjustment; EVERY effect reads from it -- ghost offset vector, flare position,
   vignette centre, shimmer origin, focus plane, lamp-side haze -- so when the rig moves, everything
   moves together coherently, as one physical thing. Optional: a readjustment also gives the fluid a
   tiny impulse (a bump of the dish -> slight mixing). Implement the rig state once (the camera
   executor's readjust state machine is the natural home) and expose it to the post pass as a block
   of constants that later tasks (V) consume.
   V MOTION MODEL (user 19:55, confirmed): the rig moves SLIGHTLY and SLOWLY all the time (barely
   perceptible idle drift, enough for OLED safety), plus OCCASIONAL readjustments where ALL the
   things move at once (lamp, ghosts, flare, focus, vignette centre) as one eased move with a settle.
   Never constant visible motion, never a single effect moving alone. Task V is LARGE -- schedule it
   after the camera executor (N+R) lands, as its own executor.

W. DIFFRACTION (user 2026-09-18 19:10, photo reference/shots/panel/2026-09-18-infocus-pixelated-ring.jpg:
   "a thin hair on a film will never cause pure darkness because the light bends around it; it's not
   out of focus per se"). Light diffracts around small features: nothing small can be both fully
   black and hard-edged, whatever the focus. Two rules: (1) a FIXED point-spread blur on everything,
   ~1 px at 1440p ([post] psf_px, applied as the floor of the per-pixel defocus radius regardless of
   focus -- "razor sharp" = diffraction-limited, not pixel-limited); (2) SIZE-DEPENDENT CONTRAST in
   the display pass: a droplet's/ring wall's peak darkness scales with its size relative to the
   point spread (e.g. darkness *= 1 - exp(-(r / (k*psf))^2), ring wall by its wall width), so tiny
   droplets are soft grey dots, medium ones dark with soft edges, only big masses fully black;
   [liquid_acid] diffraction (0..1 master, default 0), diffraction_px. Also the "straight pixeled O":
   in-focus edges at softness 0 are raw coverage AA -> with psf_px >= 1 that never happens.

X. DROPLET LENS SHADING (user 2026-09-18 19:30, ref reference/shots/photos/oilwater-ref-3-green-lens-shading.jpg,
   panel photo 2026-09-18-medium-clumps-blurry.jpg, screen capture build2/shots/live/2026-09-18-1930.png):
   "I was thinking of the oil boundary layer -- some mechanics of how it should be gradual -- and it
   translated to it just being blurry. The ratio of blurry to focused is off." In the ref EVERY droplet,
   down to 4 px, is CRISP: a thin bright refractive rim (meniscus) just outside the edge, a darker
   band just inside it, a lighter centre (the droplet is a lens focusing the backlight), a small
   specular from the lamp, and a gradual radial shading -- gradual INSIDE the droplet, sharp AT the
   edge. Ours are soft-edged flat fills. Implement per-droplet (and per-mass-edge) lens shading in
   the display pass: interior radial gradient (centre lift toward the film colour for holes / toward
   the backlight for oil drops), dark inner band, bright outer meniscus (the existing meniscus keys
   scaled to droplets with the band floors), a specular offset toward the rig lamp (CameraRig) that
   moves with it; keys [liquid_acid] droplet_lens (0..1 master, default 0), droplet_lens_centre,
   droplet_lens_band, droplet_spec. Ship subtle-medium. Do NOT add blur; the crispness comes from
   the edge, the gradualness from the interior. Separately Fable tightened the DOF ratio by ini
   (dof_max_px 9->5, droplet_depth 0.65->0.35, post_glow 0.35->0.22 @ 10 px).

Y. RACING MICRO-BUBBLES (user 2026-09-18 19:50): "I just saw racing small bubbles -- add more of those,
   like in water the uber small bubbles that go up: still slowish but considerably faster than the
   others." A dedicated class: a fraction of the SMALLEST kind-0 droplets are "racers" -- they rise
   noticeably faster (2-3x the others, still slow in absolute terms), with a tiny lateral zigzag /
   spiral the way real micro-bubbles wobble, born at the bottom / off-frame and wrapping (conserve
   rules), never popping in view. Keys [liquid_acid] droplet_racer_frac (default 0; ship ~0.12),
   droplet_racer_speed (x, ship ~2.5), droplet_racer_wobble (ship ~0.5), droplet_racer_r_max (only
   droplets below this radius qualify). The existing depth_rise back-layer bonus stays as is.

PLAN (Fable, 2026-09-18 19:55, user: "think about all of them first, then 2 executors, maybe 3"):
  Executor CAM (post pass + rig, worktree fw-cam), in this order, one commit each:
    V0 dither: blue-noise ~0.5 LSB on the final output (10-bit banding on saturated flats), key
       [post] dither (default 0, ship 1).
    V1 halation: tight warm bloom from only the brightest pixels ([post] halation, halation_px),
       drifting with the rig lamp.
    V2 lid + internal artefacts (LAPD sheet, T note 3, V key ref): coloured ring ghosts offset by
       the rig, soft rainbow fringe, veiling flare, curved glass-edge streaks, faint iridescent
       sweeps -- full-frame, no mask, no dark corners; presets subtle/medium/strong.
    V3 motion: thermal shimmer (U7), wandering vignette (U11), pixel-shift orbit (U12), all off the
       rig; and the rig's own model = slight slow drift + occasional all-at-once readjustments.
  Executor SIM (worktree fw-cons): Y racers (running) -> U9 bubble weather (slow seasons in the
    droplet population) -> S residue (blob_count reseed, y-respawn mass).
  Then J accent-by-size (display palette) as a third executor when a slot is free; Sonnet rote
    (md5, sheets, docs) at the end. Fable asks before every swap.

## Z. Real lateral chromatic aberration in the POST pass (2026-09-18, Fable diagnosis)

The user cannot see the aberration on the panel, and raising the keys does not help:
`aberration 0.6 / aberration_px 3.0 / aberration_field 1.2` only gave a thicker WARM rim on
every side of every droplet, no cool side anywhere (build2/shots/live/2026-09-18-aberr-0-*4x.png).

Why (src/shaders.h, the `[post]` block in the display pass, ~line 2213): the current code is a
first-order expansion, `dR = dot(grad R, +off)`, `dB = dot(grad B, -off)` with `off` along the
LUMINANCE gradient. The luminance gradient always points toward the brighter side, so `dR` is
positive on BOTH sides of a dark droplet and `dB` negative on both -- a symmetric red-plus,
blue-minus outline, not a split. A lateral split needs the sign to flip across the edge, which
a derivative about the pixel cannot do, and a 3 px shift cannot be expressed by one derivative
at all. The display pass could not resample (the discs are computed, not stored). The post pass
CAN: m_postTex is stored.

Do (executor, in kPostSrc, AFTER the psf/glow taps, BEFORE grain/dither):
- Transverse CA: sample R at uv + n_r * s, G at uv, B at uv - n_r * s, where n_r is the
  direction from frame centre and s = aberration_px (px at 1440p, scaled with the frame) *
  (aberration_field-shaped radial ramp: ~0 at centre, full at the corners; keep a small
  floor so it exists mid-frame too). This is the classic per-frame radial split the LAPD sheet
  shows; keep the keys `aberration` (master), `aberration_px`, `aberration_field` (same
  names, same slider ranges 0..1 / 0..6 / 0..2).
- Remove the derivative version from the display pass (or leave it behind `aberration_mode 0`
  if it is cheaper than arguing; default to the new one for every ini that sets aberration).
- The radial offset must MOVE with the rig (CameraRig on b3): the optical centre = the rig's
  current lens centre (slight drift + the occasional readjustment), never the exact frame centre.
- style=fluid: keys are 0 there -> post pass not entered -> md5 must hold.
- Verify: 4x nearest-neighbour crop of a droplet at the frame edge shows red on the outer side,
  blue/cyan on the inner side; centre droplets near-neutral. One --shot at 1440p, seed 1234.

## AA. The dye masses need a depth of their own (2026-09-18, user on the halation shot)

User: "the bottom right blob isn't getting more out of focus even as it approaches the edge."
Correct observation, and by construction: in the display pass the depth accumulator starts at
`depSum = 0.5*0.25, depW = 0.25` (src/shaders.h ~line 1047), i.e. any pixel with no droplet in
it -- every big black dye mass -- sits at depth 0.5, which is exactly camera_focus. A mass can
therefore only defocus through the curvature/tilt of the focus SURFACE, never through its own
position, and with camera_field_curve 0.12 and dof_max_px 5 that is invisible.
Slider test (build2/shots/live/2026-09-18-fieldcurve-corner-ab.png): curvature 0.9, band 220,
cap 14 softens the corner droplets clearly but the mass edge only a little. Sliders are not enough.

Do (executor A, camera rig, after Z and the lid):
- Give the dye field a depth: `dye_depth` (0..1, default 0.5 = today's behaviour) plus
  `dye_depth_tilt` (a gentle slope across the frame, direction from the rig) and let the rig's
  slow drift + occasional readjustment move it like everything else -- the black body is a
  layer floating at its own distance, not glued to the focus plane.
- Where droplets and dye overlap, blend the depths by weight as now (dye weight = today's 0.25
  prior, or expose `dye_depth_w`).
- Keep the steep shoulder; raise the default dof_max_px only in the acid-rise inis if the
  executor's own crop shows the corner mass edge still too sharp at the shipped keys.
- style=fluid untouched (post pass not entered, md5 must hold).
- Verify: corner crop of a mass edge, shipped keys vs new, seed 1234, t=60.

## AB. Bubble crust ON the masses (2026-09-18, user's lava-lamp photos)

Refs: reference/shots/photos/lavalamp-ref-1..7.jpg (a real lava lamp, warm-lit from below).
User: "Can we get more of this going on. Real life model. Not as extreme, but along those lines."

What the photos show, and what we do not:
- The BIG wax mass carries a dense crust of small bubbles, inside it and on its surface; the
  open liquid around it is nearly clean. Ours is the inverse: droplets live on the open film
  and the dye masses are empty black.
- Every bubble is a tiny lens: bright lamp rim on the lamp side, darker core, a refracted
  copy of the glow behind it; sizes span two decades and the tiny ones cluster into patches
  and chains (refs 1-3), the big ones sit alone (refs 4-5, 7).
- The mass is TRANSLUCENT: the lamp glows through it darker and warmer, its edge is a
  refracting boundary with a bright rim (ref 5 top edge, ref 7), never a flat black cut-out.
  We already have oil_transparency (held chroma) and the droplet lens shading (brief X): the
  missing part is WHERE the droplets are and the crust density.

User clarification: "it can be inside and outside" -- this is NOT a swap. Keep today's film
population and ADD the crust on the masses; the bias only shifts the balance, it never empties
the film.

Do (SIM executor, brief S/Y/U9 lineage, [liquid_acid] keys, all with sliders, all default =
today):
- `droplet_mass_bias` (0..1, ship ~0.6): the droplet population prefers the dye masses. On
  spawn, sample the dye field: with probability bias the droplet is placed where dye > 0.5
  (the mass) and it is ADVECTED WITH the mass (it rides the blob, it does not race across it);
  off-mass droplets keep today's behaviour. Under conserve_mass, respawn follows the same rule.
- `droplet_crust_density` (1..4, ship ~2.5): density multiplier inside masses relative to the
  film, so a mass reads as crusted while the film thins out ("not as extreme": ship well under
  the photos).
- `droplet_crust_r` (ship ~0.6): crust droplets are small (r_max scaled by this) and clump
  (reuse the coalescence attract with a higher merge threshold so they touch but do not merge).
- Rendering: crust droplets over a mass use the lens shading with the LAMP side bright (rig
  lamp direction, rg0) and the refracted glow of the mass behind (today's "pink dots in the
  black mass" but with a rim, not a flat dot); the mass edge gets a thin bright refracted rim
  from the lamp side (key `mass_rim`, ship subtle). Peak-channel thresholds.
- Everything moves with the rig as usual; style=fluid untouched (md5 must hold).
- Verify: one 1440p A/B at seed 1234 t=60 with a crop of a mass, plus a droplet count
  in-mass vs on-film.

## AC-AG. User review list (2026-09-19 13:10), to refine one at a time after the rote items

AC. **Acid/oil presets do not switch the look.** User: "the presets, acid oil don't do oil, just more
fluid sim." Diagnose: do the tray presets (moods folder, installed by panel-check -InstallPresets)
carry [look] style=liquid_acid, and does the tray preset load apply [look] at all, or only the colour
sections? If the loader keeps the running style, the fix is to apply [look] on preset load (with a
renderer rebuild if the style changes). Verify headless by loading a preset through the same code path.

AD. **Menus are confusing.** Deferred by the user ("sort that out later"). Collect the complaints
during the review before touching settings.cpp.

AE. **Multicolour oil.** User will re-send the references (candidates already in
reference/shots/photos: liquid-acid-ref-1..7, oilwater-ref-1..3, we-pour-1..7). Wait for the photos.

AF. **Not enough internal reflections.** The lid at subtle is invisible ("I wouldn't say I notice
the lid"), and the sheen was the only term that showed (now 0.04). Raise the reflection terms, not
the veil: lid_ghost, lid_rings, lid_glint, lid_iris up (try the medium preset's 0.50 / 0.40 / 0.32 /
0.35 with lid_sheen kept at 0.04), and consider a ghost of the DROPLETS' rims (the lid reflecting the
oil itself, offset copies of the bright rims), which the LAPD sheet shows and the current ghosts
(copies of the bright film only) do not.

AG. **Dye that is not just black.** The dark masses are always near-black. Add a dye colour: the
negative blobs take a deep translucent colour (a 5th palette shade or a hue offset from the film,
key dye_hue / dye_sat / dye_lum, defaults = today's black), lit through by the lamp like the
lava-lamp wax (refs lavalamp-ref-4/5: amber wax over orange), with the crust and mass_rim reading
against it. Palette per preset, gradable by sliders. style=fluid untouched.

AH. **Film light leaks / stock artefacts, driven by the oil.** Ref film-lightleak-ref-1.jpg
(35 mm scan: warm orange/red leak bleeding in from one edge, a vertical soft streak, cyan/green
fringe on the opposite edge, fine white dust specks and hairs). The user: "this effect doesn't seem
like it's in the sim yet. maybe the oil movement should have it happen so it's not just sitting
there." Read as: NOT a static overlay. Tie the leak to the sim so it is an event, e.g. a leak that
swells when a large mass or a coalescence passes near an edge, a streak that follows a racer,
dust that appears for a few frames then goes. Slow drift plus occasional all-at-once readjustment
(OLED rule). Keys: leak amount / hue / edge, streak, dust density, all default subtle, gradable.
style=fluid untouched. Waiting on one more user message before scoping.

DECISION (AE + AH, 2026-09-19): the light-leak LOOK comes from the OIL'S COLOUR, not from a film
overlay. Ref oil-colour-combo-ref-1.jpg (an earlier colour-combo result the user rated positive:
magenta film with soft cyan patches that read like a light leak, black masses over it, cyan-lit
droplets inside the black). Why: in the film ref the leak is additive and lifts the black corners
to orange; on the OLED the black must stay black, and the cyan patches in the keeper sit UNDER the
masses, which only a dye in the film does. So:
  AE = a second (and third) dye hue living in the thin film as a slow field advected by the sim
       velocity (t3), soft-edged, patch-scale 1/4 to 1/2 screen, keys film_hue2 / film_hue2_amt /
       film_hue2_scale / film_hue2_drift, drifting slowly with occasional readjustment. The masses
       stay black over it (AG handles a coloured dye separately). Droplets inside the masses lit in
       the second hue = the crust population taking film_hue2 (crust_hue_mix).
  AH = kept small: a rare additive edge leak plus dust as EVENTS tied to the sim, default amount
       low enough that the oil colour does the work. Never lifts black more than leak_black_lift.
Order: AE first (one Opus executor), then AF/AG, AH last. style=fluid untouched, parity md5 holds.

CORRECTION (user, same day): "it can be in both. oil and film." So AE and AH are BOTH full
features, not oil-first with a token leak. The oil colour field (AE) and the film leak/dust layer
(AH) each get their own keys and each default to a visible amount; the two are tuned together so
they read as one thing (leak hue follows film_hue2 by default, key leak_hue_follow). The only rule
kept from the decision above: the leak's black lift stays capped (leak_black_lift) so OLED black
holds. Order unchanged: AE, then AF/AG, then AH, unless the user picks otherwise.

CORRECTION 2 (user): the oil colour (AE) and the film leak (AH) "can read as different things",
they might just look similar. Do NOT tie the leak hue to the oil hue by default. The leak colour
can clash with the palette (red + teal oil with a yellowish leak looks bad; a MONO, neutral
reflection probably looks good; some tricolour combos may work). So AH gets leak_hue_mode =
mono | film | fixed (default mono: a neutral, near-white leak that takes the film's colour only
through the OLED's own blend), plus leak_hue for the fixed mode, chosen per preset. Same for the
lid reflections in AF: lid ghosts stay mono by default (they copy the film, which is already the
right colour); any tinted ghost is a key, off by default. The presets decide the combo; nothing
forces it.

AI. **GOAL after AC-AH land: an on-screen feature tour, every feature on/off, on the real panel.**
User: "after everything for now is implemented, do an on screen display of every feature on off.
idk how you want to do it. but irl rendering is better." So NOT a headless crop sheet: the live
exe on the OLED shows each feature toggled off then on, one at a time, so the user judges each
in HDR at 240 Hz. Proposed shape (decide when we get there): a tray command / CLI flag
--tour that steps through a list of (key, off value, on value) pairs from the loaded ini, holding
each state for N seconds, with a small on-screen caption ("halation: OFF" / "halation: ON") drawn
by the app; sim keeps running between steps so the oil is alive. Order = sim -> per-droplet optics
-> camera -> film (racers, coalescence, big rings, weather, residue, crust, dye depth, refraction,
lid ghosts/rings/glint/iris/sheen, DOF, aberration, halation, fog/bloom, shimmer, grain, film
noise, vignette, hue2, leak). Needs: runtime key set without restart (no ini hot-reload today; the
settings panel path already applies values live, reuse it), caption rendering, and a pause/step
hotkey so the user can hold on one. Ask before the swap that starts the tour; the user may be
gaming. This replaces the queued "review checklist document" as the walk-through vehicle; the
document stays as the tour's script.
