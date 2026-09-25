#pragma once
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <vector>

// ---------------------------------------------------------------------------
// "Liquid Acid" render look (ini section [liquid_acid], enabled by
// [look] style=liquid_acid).
//
// Macro footage of oil floating on inked water, per reference/shots/photos/
// liquid-acid-ref-*.jpg. Two layers:
//   INK  â€” the fluid sim's dye, re-styled: luminance posterised into soft flat
//          bands, remapped through a 4-stop duotone ramp, dark seams painted
//          where the dye gradient is steep. The marbling/filaments are the
//          sim's, unchanged; only the colour mapping is restyled.
//   OIL  â€” a CPU metaball field (discs, webs, bubbles, plus NEGATIVE blobs that
//          eat round holes/bubbles out of the oil) advected by the fluid's own
//          velocity field, rendered as flat saturated fills with a thin dark
//          rim just inside the field==1 isoline.
// Every constant below is a named field so the look is tunable from the ini
// and the settings window. All of it is inert when `enabled` is false â€” the
// display shader is then compiled without the LIQUID_ACID macro at all, so the
// normal fluid look is bit-identical.
// ---------------------------------------------------------------------------
struct LiquidAcidConfig {
    bool  enabled = false;          // [look] style = fluid | liquid_acid

    // Which ink the oil floats on:
    //   0 "bands" â€” the posterised duotone ramp below (the shipped look)
    //   1 "water" â€” the SHARED ink-in-water block (Beer-Lambert translucency,
    //               edge darkening, paper/inverted), i.e. exactly what
    //               [look] style=ink renders, with the oil composited on top.
    //               The acid refs literally are oil floating on inked water.
    // [liquid_acid] ink_mode = bands | water   (int form: ink_water = 0|1)
    int   inkMode = 0;

    // --- oil population (counts are fractions of blobCount) ---
    // The references are mostly OIL with ink showing through as channels, so
    // the discs and webs are huge (a third of the frame tall) and the small
    // stuff is handled by the procedural swarm layer below, not by blobs.
    int   blobCount   = 96;         // blobs stepped on the CPU and looped per pixel (max 128)
    float discFrac    = 0.22f;      // huge flat discs  (ref 1)
    float webFrac     = 0.42f;      // big blobs seeded in chains -> oil sheets/webs (ref 2)
    float bubbleFrac  = 0.22f;      // free-floating oil droplets
                                    // remainder = negative "hole" blobs
    float discMin     = 0.250f, discMax   = 0.460f;   // radius, uv-y units
    float webMin      = 0.110f, webMax    = 0.260f;
    float bubbleMin   = 0.015f, bubbleMax = 0.075f;
    float holeMin     = 0.030f, holeMax   = 0.130f;
    float sizeBias    = 2.0f;       // >1 skews bubble/hole radii toward the small end
    float bigBias     = 0.55f;      // <1 skews disc/web radii toward the LARGE end
    float holeWeight  = 0.75f;      // negative-blob field weight (how hard holes bite)

    // --- metaball field (compact Wyvill kernel (1-t^2)^3) ---
    float threshold   = 0.50f;      // field level of the oil surface (the isoline)
    float supportScale= 2.20f;      // blob support radius / visible radius. With
                                    // threshold 0.5 the visible radius is ~= baseR;
                                    // bigger = stickier, blobs bridge further apart
    float aaScale     = 1.3f;       // fwidth multiplier for the coverage smoothstep

    // --- oil motion ---
    float flowGain    = 1.15f;      // fluid velocity -> blob advection
    float curlDrift   = 0.0016f;    // analytic divergence-free drift on top
    float repulsion   = 0.55f;      // soft separation between same-sign blobs
    float buoyancy    = 0.0020f;    // radius-proportional rise (uv/s at r=0.12)
    float damping     = 2.2f;       // velocity relaxation rate (1/s), fps-normalised
    float breathAmt   = 0.10f;      // radius breathing amplitude
    float wrapMargin  = 0.20f;      // uv margin before a blob wraps to the far side

    // --- LAVA LAMP "rise" mode. Every key defaults to 0 = the shipped drift,
    // so no existing ini moves by a pixel. ------------------------------
    // A constant upward drift on top of everything else (uv y is DOWN, so it
    // is SUBTRACTED from the target velocity). 0.015 uv/s = one screen height
    // in ~65 s, which is the slow climb of a real lamp.          rise_speed
    float riseSpeed   = 0.0f;
    // A lazy per-blob sinusoidal x wobble on the way up, amplitude relative
    // to the rise speed, phased by the blob's own s1/s2 so no two blobs sway
    // together. 0 = dead-straight columns.                       rise_wobble
    float riseWobble  = 0.0f;
    // With rise_speed on, a blob that leaves the TOP is re-entered BELOW the
    // bottom edge at a fresh x and a fresh radius from its own kind's range,
    // instead of wrapping in place. Population is constant either way; this
    // just stops the frame from looking like a loop.            rise_respawn
    bool  riseRespawn = false;
    // Rising blobs elongate along their velocity: an anisotropic kernel
    // (q.y scaled by 1/(1+stretch) before the distance), strongest on the
    // small fast ones, relaxing round as a blob slows.          rise_stretch
    float riseStretch = 0.0f;
    // A gentle vertical brightness gradient on the oil -- hotter near the
    // bottom edge, cooling as a blob climbs, like the lamp's base underneath.
    // Applied to the oil colour AND to its HDR lift.        rise_bottom_light
    float riseBottomLight = 0.0f;
    // LAVA-LAMP PARALLAX. The user, watching the rise live: "for the lava lamp
    // effect make the smaller / further particles move slower, aka parallax."
    // A blob's whole motion budget -- rise speed, wobble and how much of the
    // water's flow it picks up -- is scaled by
    //     s = lerp(1, clamp(r / disc_max, 0.25, 1), rise_parallax)
    // so a small blob reads as a FAR one: it crosses the frame slowly and is
    // barely pushed around by the near currents.                rise_parallax
    float riseParallax = 0.0f;
    // Optional depth cue on top: a far (small) blob is slightly dimmer, its
    // colour pulled toward the background by (1 - s) * dim. Subtle by design;
    // past ~0.4 the small blobs read as a different palette, not as distance.
    //                                                       rise_parallax_dim
    float riseParallaxDim = 0.0f;

    // --- oil as an OBSTACLE to the ink underneath (user: "make it impossible
    // for the fluid sim underneath, the mono ink, to get under the oil, or
    // rather an intense friction that makes it hard for it to get under") ---
    // Both default 0, and with both 0 the three extra compute passes are not
    // dispatched at all, so style=fluid (and every existing acid ini) is
    // untouched. Coverage is evaluated once per frame into a sim-res "oil
    // mask" (the same Wyvill field the display shader thresholds, so holes --
    // negative blobs -- are naturally NOT oil and do not drag).
    // Velocity under the mask decays toward rest at this per-second rate
    // (fps-normalised, k = 1 - exp(-rate*dt)), and the mask's gradient adds a
    // gentle OUTWARD push at the rim so ink piles up along the edge of an
    // island instead of streaming in under it.                     oil_drag
    float oilDrag = 0.0f;
    // Dye that ends up under the oil fades out over ~1-2 s, so the oil reads
    // as sitting ON the water rather than as a colour filter over trapped
    // ink. Ink outside the mask is untouched.                  oil_dye_block
    float oilDyeBlock = 0.0f;

    // --- mouse interaction, as a MODE (user: "maybe make the mouse comb-like?
    // it's all experiments, maybe oil mode just doesn't let you interact") ---
    // 0 = none (DEFAULT, and what the shipped rise presets use: the cursor
    //     does nothing to this look at all)
    // 1 = push  -- blobs inside the radius take the pointer's own velocity
    //     plus a weak radial shove, so the cursor parts the oil
    // 2 = comb  -- the pointer path is a marbling comb: blobs in a narrow
    //     band along it stretch ALONG the drag direction, ramping with
    //     pointer speed and relaxing round again over ~3 s
    // The mouse never touches the INK in this look and never deletes or
    // shrinks oil.                                           mouse_oil_mode
    int   mouseOilMode = 0;
    float mouseOilRadius = 0.12f;   // uv (y units)          mouse_oil_radius
    float mouseOilGain   = 1.0f;    // strength multiplier     mouse_oil_gain

    // --- oil_viscosity: ONE key that makes the oil move like a THICK liquid
    // (user: "and more viscosity?"). It lowers the blob velocity relaxation
    // rate (blobs lag the water, accelerate slowly and coast), cuts the flow
    // and curl-drift response, slows the breathing and the rise wobble, adds
    // a short-range viscous attraction with a softer contact so two blobs
    // NECK together over seconds instead of snapping, smooths the droplet
    // particles' velocities (and damps their Brownian jitter), and -- only
    // when oil_drag is also on -- raises the SIM's velocity diffusion under
    // the mask.                                               oil_viscosity
    float oilViscosity = 0.0f;

    // --- oil shading ---
    float rimWidth    = 0.0013f;    // dark rim half-width, sdf units (~2 px at 1080p)
    float rimInset    = 0.0013f;    // rim band centre, INSIDE the isoline
    float rimDark     = 0.80f;      // 0..1 darkening at the rim core
    // meniscus: the THIN BRIGHT ink-coloured halo just outside the dark rim
    // (cyan on ref 1, pale violet on ref 2) â€” the ink refracted by the edge of
    // the oil lens. Painted with the ink ramp's bright stop so it reads even
    // where the ink behind is black.
    float meniscus    = 0.85f;      // 0..1 strength
    float meniscusW   = 0.0020f;    // half-width, sdf units
    float meniscusOff = 0.0022f;    // band centre, OUTSIDE the isoline
    float meniscusCol[3] = { 0.353f, 0.918f, 0.894f };   // bright cyan (ref 1)
    // Rim variation. The reference rim is not a uniform stroke: it thickens
    // and brightens where the ink under it is bright and fades out along
    // other stretches. BOTH DEFAULT TO 0 = the shipped, perfectly even rim.
    float rimVary     = 0.0f;       // 0..1 low-frequency noise (position +
                                    // slow drift) on rim/halo width and on
                                    // halo intensity, incl. stretches at ~0
    float rimInkFollow= 0.0f;       // 0..1 scale the halo by the ink
                                    // brightness just OUTSIDE the isoline
                                    // (the halo is refracted ink)
    // Paired-annulus ordering (Micromachines 13(7):1021 â€” the meniscus makes a
    // DARK ring hugging the drop and a BRIGHT caustic piled up just outside
    // it). 0 = the shipped placement (rim at rim_inset, halo at
    // meniscus_offset, which may overlap or sit apart); 1 = force them
    // adjacent and ordered: dark band centred one half-width INSIDE the
    // isoline, bright halo centred one half-width OUTSIDE it.   rim_order
    bool  rimOrder    = false;
    float refraction  = 0.050f;     // ink uv offset along the field gradient near rims
    float translucency= 0.16f;      // how much the ink under the oil modulates it
    float oilTexture  = 0.07f;      // faint in-blob mottle (interiors stay flat)
    float inkShading  = 0.00f;      // how much of the fluid look's pseudo-3D emboss
                                    // survives on the ink (refs are flat; 1 = normal)
    float oilHdr      = 0.0f;       // >0: drive HDR highlight gain for oil pixels
    float rimHdr      = 0.0f;       // >0: extra HDR level on the rim band only
    // --- "real oil" block (refs 4-7). EVERY key defaults to 0 = the shipped
    // cut-out look, so no existing ini changes by a single pixel. ----------
    // The film thins to nothing at its edge, so there the light has crossed
    // the ink AND a sliver of oil: the colour slides toward the product of
    // the two (orange over red ink goes red) and the disc fades out over a
    // band a few percent of its own radius instead of over 1-2 px of AA.
    // Also scales the dark hairline down.                     oil_thin_edge
    float oilThinEdge = 0.0f;
    float oilEdgeFrac = 0.14f;      // that band as a fraction of the LOCAL
                                    // lens radius (R ~= 0.78/|grad|)  oil_edge_frac
    float oilSpecular = 0.0f;       // broad Blinn lobe + Fresnel off a
                                    // gradient+fbm normal (refs are backlit,
                                    // so keep it weak)             oil_specular
    float oilIrid     = 0.0f;       // thin-film hue ramp indexed by the fbm
                                    // thickness, strongest where thin  oil_iridescence
    float swarmLens   = 0.0f;       // trapped droplets as holes in the film:
                                    // soft edge, thin-oil fringe, offset
                                    // highlight, softened ring        swarm_lens
    float meniscusFromInk = 0.0f;   // the halo becomes EMERGENT: its colour is
                                    // the ink beneath (lifted), its weight the
                                    // ink's own brightness, its width a few %
                                    // of the lens radius. On dark ink the halo
                                    // AND the hairline vanish.   meniscus_from_ink
    // The meniscus replaces up to `meniscus` (0.85 live) of the boundary
    // pixel and, at meniscus_from_ink 1, its colour is derived from the INK --
    // so it is structurally immune to every FILM-hue feature (hue2, the seam
    // reflect, the sweep), while the two terms that do carry the film's colour
    // outward peak at 0.098 and 0.072 (AUDIT-2026-09-22 section 3). This mixes
    // the halo's colour toward the film colour the rim terms already use
    // (oilR, so it carries a hue2 seam where there is one), lifted exactly the
    // way the ink path lifts: only the colour SOURCE moves -- the band's
    // shape, width and centre are untouched. It also opens, by the same
    // amount, the ink-brightness gate meniscus_from_ink puts on the halo:
    // that gate exists so a foreign INK colour is never stroked onto black
    // ink, and once the colour is the film's it is guarding nothing. It has
    // to open, or the key does nothing here: in acid-rise-12 the ink outside
    // the oil is black in every pixel, so the gate is 0 everywhere and the
    // meniscus -- the 0.85 at the top of the ring stack -- paints NOTHING
    // today (measured 2026-09-22: meniscus 0 is byte-identical to meniscus
    // 0.85; so is rim_dark 0, which the same gate kills). The dark hairline
    // keeps the original gate. 0 = today, byte-identical.
    float meniscusFilmMix = 0.0f;   // 0..1                  meniscus_film_mix
    float oilGlow     = 0.0f;       // diffuse spill of the oil's colour into
                                    // the ink outside it, never a line  oil_glow
    float refractionWidth = 0.0f;   // rim_width multiplier for the refraction
                                    // band; 0 = the shipped 7      refraction_width
    // --- "transparent coloured oil" block. The oil stops being a fill and
    // becomes an ABSORBING FILM over the refracted ink, so the marbling reads
    // through it. Again: 0 = the shipped look, exactly. ------------------
    float oilTransparency = 0.0f;   // 0 = opaque fill, 1 = pure Beer-Lambert
                                    // film over the ink       oil_transparency
    float oilAbsorb       = 2.6f;   // absorption coefficient k in
                                    // T = exp(-k*thickness*(1-oilHue)). Higher
                                    // = a deeper, more saturated film  oil_absorb
    float oilFilmBump     = 0.35f;  // slow fbm on the thickness, so the film
                                    // has islands of thick and thin  oil_film_bump
    float oilRefractBody  = 0.0f;   // uv units: refraction across the WHOLE
                                    // film body (gradient of field + fbm), not
                                    // just the edge band        oil_refract_body
    float oilInkBlur      = 0.0f;   // the ink under the film is slightly out
                                    // of focus, by thickness      oil_ink_blur
    // oil palette: up to 4 colours, dominant-blob pick (no colour-bleed averaging)
    float oilColors[12] = { 0.902f, 0.278f, 0.157f,     // vermillion disc
                            0.976f, 0.400f, 0.078f,     // hot orange web
                            0.859f, 0.204f, 0.098f,     // deep red-orange
                            0.988f, 0.541f, 0.114f };   // amber bubble

    // --- ink (the fluid, re-styled) ---
    float inkLevels   = 5.0f;       // posterise bands (few + soft = broad flat plateaus)
    float inkSoft     = 0.42f;      // band-edge softness (0 = hard steps)
    float inkMix      = 0.88f;      // 0 = keep the parity colour, 1 = full ramp
    float inkHueVary  = 14.0f;      // degrees of ramp hue-rotate driven by the dye's own hue
    // Complement lock: hold the ink's hue OPPOSITE the oil's on the wheel.
    // The regional variation above still moves the ink around, but only inside
    // a window centred on (mean oil hue + 180), so the ink can never drift
    // toward the oil hue and the pair stays complementary all the time.
    bool  inkComplementLock = true;
    float inkComplementSpan = 40.0f;   // width of that window, degrees
    // Sweep the palette through a curated list of VIVID complementary pairs
    // (orange/teal -> red/cyan -> magenta/green -> gold/violet -> lime/purple),
    // cross-fading oil family and ink ramp together. A continuous hue rotation
    // was tried first and rejected: no rotation keeps a palette vivid at every
    // hue, because the saturation and lightness a colour needs to read as
    // "vivid" depend on the hue (a mid-value teal is a good teal; the same
    // value at yellow is olive). Curated anchors sidestep that entirely, and
    // match the reference pack, where each loop is one vivid pair.
    // Seconds for a full trip through the list; 0 = off (use the fixed palette).
    float hueSweepPeriod = 0.0f;
    static const int kSweepPairs = 5;    // the shipped list length (= default)
    static const int kSweepMax   = 12;   // how many entries an ini may carry
    // How many entries of the list below are actually used. Default = the
    // shipped 5, so every existing ini sweeps exactly as before.  sweep_count
    int   sweepCount = kSweepPairs;
    // Continuous rotation of the WHOLE oil palette's hue (HSV, saturation and
    // value preserved), seconds per full turn; 0 = off. This is the "the hue
    // of the entire screen shifts" option, and it is deliberately GLOBAL: no
    // per-blob colour, so a disc never changes hue relative to its neighbour.
    // With mono ink + meniscus_from_ink the ink stays grey and only the oil
    // turns. Not every hue is equally vivid at one S/V (yellow goes olive) --
    // that is the price of a continuous rotation, and why the curated sweep
    // above exists as the alternative.                    hue_rotate_period
    float hueRotatePeriod = 0.0f;
    // HSV saturation multiplier on the effective oil palette (CPU-side, after
    // the sweep and the rotation), so vividness is one panel knob whichever
    // colour source is in use. 1 = the authored colours, untouched.
    float oilSaturation = 1.0f;     //                        oil_saturation
    // Each pair is two vivid anchors: the oil colour and the ink's mid tone.
    // The rest of the palette (the other three oil shades, the ink's near-black
    // and its two oil-hue bands, the meniscus) is derived from them using the
    // saturation/value ratios of the authored palette above, so every swept
    // pair has the same internal structure as the hand-tuned one.
    float sweepOil[kSweepMax * 3] = {
        0.898f, 0.271f, 0.145f,   // vermillion
        0.930f, 0.120f, 0.160f,   // red
        0.880f, 0.120f, 0.620f,   // magenta
        0.970f, 0.780f, 0.100f,   // gold
        0.550f, 0.850f, 0.120f,   // lime
    };
    float sweepInk[kSweepMax * 3] = {
        0.086f, 0.478f, 0.494f,   // teal
        0.100f, 0.620f, 0.500f,   // cyan-green
        0.220f, 0.700f, 0.240f,   // green
        0.340f, 0.160f, 0.720f,   // violet
        0.480f, 0.120f, 0.660f,   // purple
    };
    // An entry may ALSO carry its own four oil shades verbatim (the ini gives
    // sweep_oil_N twelve floats instead of three). The derivation above is a
    // good generic family, but the user's favourite palettes -- the tile9
    // batch -- are hand-picked four-shade sets and must reach the screen
    // exactly as they were rendered, not re-derived from one anchor. When no
    // entry carries a full set the old anchor path runs untouched.
    bool  sweepOilFullSet[kSweepMax] = {};
    float sweepOilFull[kSweepMax * 12] = {};
    // --- ACCENT COLOUR BY SIZE (accent_mode, brief J) ---------------------
    // The user, on a crop of the live look: "if the entire frame looked like
    // the cropped one it would be better" -- ONE colour with small scattered
    // accents of its complement, "like 80/20". The failure it replaces is the
    // photo of "random unintentional green blobs": a blob's colour slot is
    // drawn at seed time with no regard for its size, so the 4th shade (which
    // the 80-20 palettes make the complement) lands on big masses as often as
    // on small ones, and a complement mass is not an accent, it is a second
    // colour.
    //   0 = the shipped behaviour, untouched.
    //   1 = BY SIZE. Shade 4 is the ACCENT and belongs to the SMALL elements
    //       alone: a blob under accent_max_r (a fraction of disc_max) may take
    //       it, anything larger is folded back into shades 1-3, so no big mass
    //       can be the complement. The oil-on-ink droplets and the hollow
    //       rings already draw shade 4 and keep it -- they are the smallest
    //       things in the frame and are exactly the scattered dots the photo
    //       is made of.
    // accent_frac keeps it an ACCENT rather than a rule: only that share of
    // the small blobs take it, chosen by a HASH of the blob's index, so it is
    // stable frame to frame and switching the key on moves nobody.
    int   accentMode = 0;           //                          accent_mode
    float accentMaxR = 0.35f;       // fraction of disc_max      accent_max_r
    float accentFrac = 0.60f;       // share of the small ones   accent_frac
    float inkGain     = 2.30f;      // luminance -> ramp position
    float inkBias     = 0.05f;
    float seamStrength= 0.70f;      // dark seams along |grad dye|
    float seamLo      = 0.06f, seamHi = 0.45f;
    float seamScale   = 2.6f;       // gradient tap spacing, screen texels
    // 4-stop ink ramp, dark -> bright. Ref 1's ink is teal AND orange, so the
    // ramp crosses the complement: near-black teal -> teal -> burnt -> hot.
    float inkRamp[12] = { 0.006f, 0.034f, 0.038f,       // near-black teal
                          0.043f, 0.353f, 0.376f,       // teal
                          0.478f, 0.173f, 0.020f,       // burnt orange
                          0.969f, 0.510f, 0.055f };     // hot orange

    // --- bubble swarms (procedural, NOT metaballs) ---------------------
    // The references carry hundreds of round droplets with a wide size range:
    // dark water droplets trapped INSIDE the oil, and oil droplets sitting on
    // the open ink. A jittered cellular layer gives all of them for ~20 hashes
    // per pixel; a hundred more metaballs would cost far more.
    float swarmHoles  = 0.90f;      // strength of the hole swarm inside the oil
    float swarmDrops  = 0.55f;      // strength of the oil-droplet swarm on the ink
    float swarmDensity= 0.55f;      // fraction of cells that carry a droplet
    float swarmScaleA = 26.0f;      // cells per p-unit, hole swarm (bigger = smaller holes)
    float swarmScaleB = 34.0f;      // cells per p-unit, droplet swarm
    float swarmRMin   = 0.045f;     // droplet radius in CELL units (wide range = every size)
    float swarmRMax   = 0.430f;
    float swarmRimDark= 0.55f;      // thin dark edge on every droplet
    float swarmDrift  = 1.0f;       // how fast the swarm layers creep / warp
    float swarmClump  = 0.70f;      // 0 = even blanket, 1 = droplets only in patches
    float swarmDark   = 0.80f;      // how dark a trapped water droplet reads

    // --- DROPLET PARTICLE SIM (the swarms' replacement) -----------------
    // The user, on the swarm layers: "it still looks like 2 things layered.
    // The dots look png'd on. They need to be simulated and attached to the
    // oil, actually simulated." So these droplets are real particles: they
    // ride the oil they sit in, nucleate, attract, coalesce (area-conserving)
    // and dissolve, and they are added INTO THE SAME metaball field as the
    // big blobs before the threshold -- a droplet is literally a hole in (or
    // a bump of) the oil surface, so it inherits the soft thickness edge, the
    // thin-oil fringe, the emergent halo and the lens highlight for free.
    // `droplets` = 0 leaves the shipped procedural swarms untouched; above 0
    // the swarms are forced off, because ONE system is the whole point.
    int   droplets      = 0;        // target population (0 = off)      droplets
    float dropletSpawn  = 40.0f;    // nucleations / s          droplet_spawn_rate
    float dropletRMin   = 0.0009f;  // uv-y (~1.3 px at 1440p)     droplet_r_min
    float dropletRMax   = 0.0120f;  // uv-y (1.2% of frame height) droplet_r_max
    float dropletBias   = 3.2f;     // pow(U,bias): heavy tail to small  droplet_bias
    float dropletSupport= 2.20f;    // kernel support / visible radius droplet_support
    float dropletWeight = 1.25f;    // hole punch, x the LOCAL oil field droplet_weight
    float dropletOilW   = 1.20f;    // field weight of an oil droplet droplet_oil_weight
    float dropletInkFrac= 0.22f;    // share of spawns that sit on open ink
    float dropletLife   = 120.0f;   // s before a droplet dissolves     droplet_life
    float dropletDamping= 4.0f;     // velocity relaxation rate      droplet_damping
    float dropletJitter = 0.0022f;  // brownian, uv/s                droplet_jitter
    float dropletAttract= 0.40f;    // same-kind pull within ~3 radii droplet_attract
    float dropletMerge  = 0.30f;    // overlap fraction that coalesces droplet_merge
    float dropletRise   = 0.30f;    // trapped water lags the oil, x rise_speed

    // --- HOLLOW "LENS" droplets: the empty doubles the user liked ---------
    // A ring droplet is drawn as a thin dark ANNULUS with the oil colour
    // showing through its middle: the negative punch is a band centred on the
    // droplet's own visible radius instead of a filled disc, so the interior
    // never crosses the isoline. Everything else about it is a droplet --
    // it drifts with the oil, it is confined to the film, it dissolves.
    // Ring-ness is drawn once at nucleation and fixed for the droplet's life.
    // Rings behave like BUBBLES: they attract each other and pack into rafts
    // with a shared dark wall (two touching rings do NOT merge), while a
    // SOLID droplet meeting a ring absorbs it, area-conserving.
    // The user, on the first shipped rings: "these are pixel perfect circles."
    // They were: a thick wall of uniform width on a mathematically round
    // band, which reads as a stamped "O" glyph next to the soft, lopsided
    // bubbles they liked. So the wall is THIN by default and the radius is
    // never constant -- dropletRingWobble drives a per-ring seeded ellipse
    // (1.02..1.25) whose axis turns over a minute or two, plus a breathing
    // 3-lobe wobble of a few percent, and the wall thins where the ring
    // bulges the way a stretched film does.
    float dropletRingFrac  = 0.0f;  // 0..1 of new kind-0 droplets  droplet_ring_frac
    float dropletRingWidth = 0.08f; // rim width / radius        droplet_ring_width
    float dropletRingLift  = 0.10f; // interior lightening 0..1   droplet_ring_lift
    float dropletRingClump = 0.0f;  // raft attraction 0..1       droplet_ring_clump
    // --- BIG HOLLOW BUBBLES (user 2026-09-18, panel photo
    // --- 2026-09-18-big-hollow-bubble-liked.jpg: the big faint transparent
    // --- lens in the middle-left is "exactly what I want more of") ---------
    // Rings were drawn from the same radius range as the solid droplets, and
    // that range is itself clamped by the shader's 3x3 cell walk (a droplet's
    // support may not exceed one grid cell), so a hollow bubble could never be
    // more than a small ring. A ring that is bigger than a cell is instead
    // registered in EVERY cell its support disc overlaps, which restores the
    // walk's invariant at a cost of a few hundred extra table entries -- and
    // nothing per pixel, because the cells it lands in are the ones its own
    // pixels are in.
    //   droplet_ring_r_mul    1 = today. Up to this multiple of droplet_r_max
    //                         for a BIG ring (2-4 is the look the user asked
    //                         for); the wall stays droplet_ring_width of the
    //                         radius and the wobble scales with it.
    //   droplet_ring_big_frac share of NEW rings that are big. Keep it small:
    //                         a couple visible at any moment is the point.
    // --- RACING MICRO-BUBBLES (brief Y, user 2026-09-18 19:50) -----------
    // "I just saw racing small bubbles -- add more of those, like in water the
    // uber small bubbles that go up: still slowish but considerably faster
    // than the others." A class of its own, not a tail of depth_rise: a share
    // of the SMALLEST trapped droplets climb droplet_racer_speed times as fast
    // as an ordinary one and wobble sideways on the way up, the way a real
    // micro-bubble spirals. They are born off-frame below the bottom edge
    // (conserve_mass) and wrap, so one is never seen to appear mid-climb.
    //   droplet_racer_frac    0 = today. Share of qualifying droplets.
    //   droplet_racer_speed   x an ordinary droplet's NET climb.
    //   droplet_racer_wobble  lateral zigzag, as a fraction of that climb.
    //   droplet_racer_r_max   only droplets at or below this radius qualify.
    //                         0 = twice droplet_r_min.
    // --- BUBBLE WEATHER (U9, 2026-09-18) ---------------------------------
    // The droplet population has one steady state and settles into it: after
    // a couple of minutes the frame's density and its ring/solid mix never
    // change again. Weather gives it SEASONS on a minutes-long clock -- a
    // spell of rings, then of solids, a sparse spell, a crowded one -- by
    // walking the target count and the ring share between hashed per-phase
    // targets. It never jumps and it never kills a population: the target
    // moves smoothly and the conserve_mass machinery does the rest (surplus
    // shrinks away over dissolve_s, shortfall grows in over spawn_grow_s),
    // which is why the DENSITY half only runs when conserve_mass is on.
    //   weather           0 = today. Amplitude of the whole effect.
    //   weather_period_s  mean seconds per phase (the phase's hold/ramp
    //                     split is hashed too, so they are not all alike).
    float weather         = 0.0f;   // 0..1                          weather
    float weatherPeriodS  = 300.0f; // s per phase          weather_period_s

    float dropletRacerFrac  = 0.0f;  // 0..1            droplet_racer_frac
    float dropletRacerSpeed = 2.5f;  // x                 droplet_racer_speed
    float dropletRacerWobble= 0.5f;  // 0..2             droplet_racer_wobble
    float dropletRacerRMax  = 0.0f;  // uv-y, 0 = 2*r_min droplet_racer_r_max

    float dropletRingRMul   = 1.0f;  // 1..6              droplet_ring_r_mul
    float dropletRingBigFrac= 0.0f;  // 0..1           droplet_ring_big_frac

    // --- CONSERVATION OF MASS (brief S, 2026-09-18) ---------------------
    // "matter cannot be created or destroyed": nothing may appear at a
    // visible size in the open, and nothing may vanish from it. 0 = exactly
    // today's behaviour, every branch below skipped.
    //   conserve_mass   master, 0..1 (used as a switch today)
    //   spawn_grow_s    seconds a NEW droplet takes to swell from nothing to
    //                   its target radius (it is below the visible floor for
    //                   the first seconds of that). 0 = today's 0.34 s.
    //   dissolve_s      seconds a DYING droplet (old age / stranded) takes to
    //                   shrink away. 0 = today's 0.34 s. Coalescence keeps
    //                   its own 0.34 s: pouring into a neighbour is not a
    //                   disappearance and it should still read as fast.
    // --- COALESCENCE (user 2026-09-18, panel photo lobed-groups-of-5) ----
    // "all of these things are groups of 5, like what". droplet_merge is an
    // OVERLAP fraction: the merge fires at dd < (ri+rj) * (1 - droplet_merge),
    // i.e. only when two droplets interpenetrate by 30% of the sum of their
    // radii. But the contact repulsion starts at dd = ri+rj and is several
    // times stronger than droplet_attract, so a pair settles at exactly
    // touching and can never reach the merge distance. The result is a
    // permanent lumpy cluster of 4-6 same-size lobes necked together in the
    // metaball field, which is what the user photographed.
    //   droplet_coalesce    0 = today. > 0: same-kind SOLID droplets that
    //                       TOUCH coalesce at a rate of 8 * this per second,
    //                       area-conserving, so a contact lasts a moment and
    //                       becomes one round droplet instead of a lobe.
    //                       Rings are untouched -- a foam raft is meant to
    //                       keep its shared walls.
    //   droplet_coalesce_s  seconds the neck takes to close (the absorbed
    //                       droplet's slide-in and both radii). 0 = today's
    //                       0.18 s pull / 0.34 s radii.
    float dropletCoalesce  = 0.0f;  // 0..1                   droplet_coalesce
    float dropletCoalesceS = 0.0f;  // s, 0 = today         droplet_coalesce_s

    float conserveMass  = 0.0f;     // 0..1                        conserve_mass
    float spawnGrowS    = 0.0f;     // s, 0 = today (0.34)         spawn_grow_s
    float dissolveS     = 0.0f;     // s, 0 = today (0.34)         dissolve_s
    float dropletRingWobble= 1.0f;  // out-of-round 0..1        droplet_ring_wobble

    // --- BACKLIGHT PENUMBRA (oil_penumbra) --------------------------------
    // The user's diagram: the lamp is under the middle of the dish, so the
    // oil immediately around a black mass is lit less than the open sheet --
    // and a pigment lit less does not merely dim, it shifts hue ("like the
    // sky and the sun angle"). A soft S-curved band on the DYE side of every
    // isoline, a few px wide, that darkens and turns the oil colour a little.
    // Very subtle by design: it should register as depth, never as an outline.
    float oilPenumbra   = 0.0f;     // 0..1 amount              oil_penumbra
    float oilPenumbraPx = 8.0f;     // band width, px at 1440p  oil_penumbra_px
    float oilPenumbraHue= 12.0f;    // degrees                  oil_penumbra_hue
    float oilPenumbraDark = 0.15f;  // 0..1 darkening          oil_penumbra_dark

    // --- MACRO "CELLULOSE" SURFACE TEXTURE (cellulose) --------------------
    // The user, on the Requiem microscope frame: the cell body is not smooth,
    // it is fibrous and mottled, like paper fibres seen through a lens -- and
    // they want it "in the black oil more than the oil". So this is an
    // anisotropic fBm (stretched along a slowly turning direction, which is
    // what makes it read as STRANDS rather than blobs), drifting with the
    // rise so it belongs to the masses instead of to the screen, added in the
    // DISPLAY pass -- i.e. under the film grain, because it is the surface
    // and the grain is the camera.
    //
    // On the black side it is deliberately a THICKNESS effect: the strands
    // are strongest just inside the edge of a black mass (where the layer is
    // thin and light gets through) and fade to nothing deep inside it. That
    // is what keeps the OLED's true black -- the best thing on this panel --
    // from turning into a grey wash. On the film it is a faint mottling of
    // the colour, not a lift.
    float cellulose      = 0.0f;    // 0..1 master                cellulose
    float celluloseInk   = 1.0f;    // weight on the black side   cellulose_ink
    float celluloseOil   = 0.35f;   // weight on the film         cellulose_oil
    float celluloseScale = 40.0f;   // feature size, px at 1440p  cellulose_scale
    float celluloseDrift = 1.0f;    // 0..1 with the rise motion  cellulose_drift

    // --- PER-DROPLET DEPTH (item N) ---------------------------------------
    // Every droplet and ring gets a depth in 0..1, hashed from its birth
    // position (so it is fixed for life and costs the sim no random draws) and
    // spread about the masses' own base depth of 0.5 by droplet_depth. The
    // display pass turns it into a circle of confusion (see [post] dof_max_px
    // and the camera keys), so the frame has a real near/far instead of one
    // global defocus. depth_rise is the user's own idea: the BACK layer climbs
    // a little faster than the front, which reads as parallax the moment the
    // eye has a sharp plane to compare it against. Both 0 = today.
    float dropletDepth = 0.0f;      // 0..1 spread about 0.5     droplet_depth
    float depthRise    = 0.0f;      // back-layer rise bonus        depth_rise

    // --- DIFFRACTION: SIZE-DEPENDENT CONTRAST (item W) --------------------
    // The other half of the point spread. A feature narrower than the spread
    // has its light filled in from around it, so it cannot reach full
    // darkness: a hair on film is grey, not black, and only something several
    // spreads across is truly opaque. So a droplet's -- and a ring wall's --
    // contribution to the field is scaled by its own size against the spread,
    // 1 - exp(-(size/kP)^2): tiny droplets are soft grey dots, medium ones
    // dark with soft edges, only the big masses reach black. It only ever
    // takes darkness AWAY from small things, so the OLED's true black under
    // the big masses is untouched. 0 = off.
    float diffraction   = 0.0f;     // 0..1 master                 diffraction
    float diffractionPx = 1.5f;     // spread width, px at 1440p diffraction_px

    // --- DROPLET LENS SHADING (item X) ------------------------------------
    // The user, holding the oil-and-water reference against our live frame:
    // "I was thinking of the oil boundary layer -- some mechanics of how it
    // should be gradual -- and it translated to it just being blurry. The
    // ratio of blurry to focused is off." In the reference every droplet down
    // to four pixels is CRISP at its edge and GRADUAL inside it: a thin bright
    // refractive rim just outside, a darker band just inside, a lighter centre
    // because the droplet is a lens focusing the backlight, and a small
    // specular from the lamp. Ours were soft-edged flat fills. None of this
    // adds any blur -- the crispness comes from the edge, the gradualness from
    // the interior, and they are different things.
    //   droplet_lens        0..1 master; 0 = off
    //   droplet_lens_centre how much lighter the middle is than the shoulder
    //   droplet_lens_band   width of the dark inner band AND of the bright
    //                       rim outside it, px at 1440p (floored by band_min,
    //                       so a 4-px droplet gets the same shading a mass does)
    //   droplet_spec        a small highlight, offset toward the rig's lamp,
    //                       so it swings when the lamp does
    float dropletLens       = 0.0f;   //                     droplet_lens
    float dropletLensCentre = 0.30f;  //              droplet_lens_centre
    float dropletLensBand   = 3.0f;   // px at 1440p    droplet_lens_band
    float dropletSpec       = 0.0f;   //                     droplet_spec

    // --- BUBBLE CRUST ON THE MASSES (brief AB, the lava-lamp photos) ------
    // The user's refs are a real lava lamp: the big wax mass carries a dense
    // crust of small bubbles inside it and on it, while the liquid AROUND it
    // is nearly clean. Ours was the inverse -- droplets on the open film, the
    // dye masses empty black. Their clarification: "it can be inside and
    // outside", i.e. this is NOT a swap. Today's film population stays and a
    // crust is ADDED on top of it, which is why the crust has a target of its
    // own rather than stealing spawns from the film.
    //   droplet_mass_bias      0 = today. How much crust there is, as a share
    //                          of the film population.
    //   droplet_crust_density  density multiplier inside a mass against the
    //                          film. 1 = today (no crust at all).
    //   droplet_crust_r        crust droplets are SMALL: r_max times this.
    //   mass_rim               display pass: a thin bright refracted rim on
    //                          the LAMP side of a mass edge, so the mass
    //                          reads as a translucent body and not as a flat
    //                          black cut-out.
    // "Not as extreme" (the user): the shipped values sit well under the
    // photos.
    float dropletMassBias   = 0.0f;   //  0..1        droplet_mass_bias
    float dropletCrustDens  = 1.0f;   //  1..4    droplet_crust_density
    float dropletCrustR     = 1.0f;   //  0..1        droplet_crust_r
    float massRim           = 0.0f;   //  0..1             mass_rim

    // --- CAST SHADOWS (brief BC) ------------------------------------------
    // The user: "the light should cast shadows essentially... it can be from
    // behind, or the bottom, or the top or side." The lamp already drives the
    // specular, the mass rim, the penumbra, the haze and the bloom; nothing
    // in the frame blocked any of it. Every mass and every droplet now
    // darkens the film in the direction AWAY from the lamp, tightest and
    // darkest at the caster and softening as it runs out. The length comes
    // from the caster's own height (a droplet's radius, a mass's thickness)
    // and from [post] light_z, the lamp's stand-off from the dish.
    //   shadow_amt   0 = today, and the whole block is branched out
    //   shadow_len   how far the longest shadow reaches, as a fraction of
    //                the screen HEIGHT, before light_z lengthens it
    //   shadow_soft  how fast the penumbra opens up with distance
    float shadowAmt         = 0.0f;   //  0..1           shadow_amt
    float shadowLen         = 0.12f;  //  frac of height shadow_len
    float shadowSoft        = 0.50f;  //  0..1           shadow_soft

    // --- MULTICOLOUR OIL (brief AE, the AE+AH decision) -------------------
    // Ref oil-colour-combo-ref-1.jpg, which the user rated positive: a
    // magenta film carrying soft CYAN patches that read like a light leak,
    // with the black masses sitting over both and the droplets inside the
    // black lit in the second colour. The decision block is explicit about
    // WHY this is a dye in the film and not a film-stock overlay: an additive
    // leak lifts the black corners, and on this panel the black has to stay
    // black; and in the keeper the cyan sits UNDER the masses, which only a
    // dye can do.
    //
    // So: a second hue lives in the thin film as a slow scalar field that the
    // sim's own velocity advects, so the patches stretch and fold with the
    // flow instead of sliding over it. Patch scale is a fraction of the
    // frame; the field decays toward a base so it can never wash out to a
    // uniform tint, and its noise phase jumps on the rig's readjustment so
    // the patches re-lay all at once with everything else (the OLED rule).
    //   film_hue2       degrees off the palette's own hue for the patches
    //   film_hue2_amt   0 = today. How far the film goes toward that hue.
    //   film_hue2_scale patch size as a fraction of the frame (1/4 .. 1/2)
    //   film_hue2_drift how fast the patches wander on their own
    //   film_hue2_decay how fast the field falls back to its base
    //   film_hue3 / _amt an optional third hue off a second threshold of the
    //                   SAME field -- no extra field, no extra per-pixel cost
    //   crust_hue_mix   how much of the shift the droplets INSIDE a mass take
    //                   (1 = the same as the film, which is the ref's look)
    float filmHue2      = 0.0f;   // degrees            film_hue2
    float filmHue2Amt   = 0.0f;   // 0..1           film_hue2_amt
    float filmHue2Scale = 0.35f;  // frac of frame film_hue2_scale
    float filmHue2Drift = 1.0f;   // 0..2           film_hue2_drift
    float filmHue2Decay = 0.35f;  // 0..2           film_hue2_decay
    float filmHue3      = 0.0f;   // degrees            film_hue3
    float filmHue3Amt   = 0.0f;   // 0..1           film_hue3_amt
    float crustHueMix   = 1.0f;   // 0..1        crust_hue_mix
    //   film_hue2_wobble        the contrast hue is not NAILED to an angle:
    //                           it wanders a few degrees either side of it,
    //                           so a 180 deg pair lives in 170..190 and the
    //                           combo keeps changing even when the global
    //                           rotation is off. Two incommensurate sines, so
    //                           it never repeats exactly, plus the rig's
    //                           readjustment phase, so it re-aims all at once
    //                           with the lamp and the focus.
    //   film_hue2_wobble_period seconds of the slower of those two sines
    float filmHue2Wobble  = 0.0f;   // degrees      film_hue2_wobble
    float filmHue2WobbleP = 300.0f; // s     film_hue2_wobble_period
    //   film_hue2_seed_rows  NOTHING is laid inside the frame. New patch
    //                        material is made only in hidden rows BELOW the
    //                        bottom edge and enters by rising -- the user's
    //                        standing rule: "everything needs to be generated
    //                        off screen, and within the screen needs to just
    //                        move up". 0 restores the old in-frame injection
    //                        and exists only so the two can be A/B'd.
    //   film_hue2_rise       extra upward drift, screen heights per minute,
    //                        on top of the oil's own rise.
    float filmHue2SeedRows = 2.0f;  // hidden rows   film_hue2_seed_rows
    float filmHue2Rise     = 0.25f; // screens/min        film_hue2_rise
    //   boundary_reflect_r   brief AJ. How far a hue2 SEAM reaches into the
    //       droplets around it, as a fraction of the SCREEN HEIGHT. Today a
    //       droplet's lit rim, lens highlight and glow are tinted with the
    //       film colour at that droplet's own pixel, so a droplet carries
    //       the seam's magenta only while it is standing in the seam -- about
    //       one droplet across at film_hue2_scale 0.35. That band width is
    //       today's reach and 0 keeps exactly it: every existing preset is
    //       byte-identical until a preset raises this. Raised, the RIMS (never
    //       the film, and nothing is blurred) rotate toward the seam's own hue
    //       with a smooth falloff, so the nearest droplets stay strongest.
    //   boundary_reflect_amt  how much of the seam's hue a rim right next to
    //       it takes. Only does anything where boundary_reflect_r > 0.
    float boundaryReflectR   = 0.0f;  // frac of height boundary_reflect_r
    float boundaryReflectAmt = 1.0f;  // 0..1        boundary_reflect_amt

    // --- THE DYE'S OWN DEPTH (item AA) ------------------------------------
    // The user, on the halation frame: "the bottom right blob isn't getting
    // more out of focus even as it approaches the edge." True, and by
    // construction: the per-pixel depth accumulator was primed with a
    // CONSTANT 0.5, which is exactly camera_focus, so every pixel with no
    // droplet in it -- i.e. every big dye mass -- sat on the plane of focus by
    // definition. A mass could only ever defocus through the curvature and
    // tilt of the focus SURFACE, never through its own position, and no
    // slider could fix that.
    // So the dye is a layer with a depth of its own. dye_depth is where it
    // floats, and dye_depth_tilt gives it a gentle slope along the direction
    // of the RIG's lamp -- which means the dye slab and the focus surface are
    // not parallel, so they cross on a LINE rather than agreeing over a whole
    // region, and the crossing moves when the rig does. dye_depth_w is the
    // weight the dye carries where droplets overlap it (0.25 = as before).
    // Defaults reproduce today's frame exactly.
    float dyeDepth     = 0.5f;      // 0..1, 0.5 = the old constant  dye_depth
    float dyeDepthTilt = 0.0f;      // slope across the frame   dye_depth_tilt
    float dyeDepthW    = 0.25f;     // its weight in the blend     dye_depth_w

    // --- DYE THE BLACK (brief AG / AM) ------------------------------------
    // The dark masses are always near-black. The user: "add ability to dye the
    // black ink", and note 11's example, "make the currently black oil mostly
    // purple". The references are lava lamps -- amber wax over orange -- where
    // the dark body is not black at all but a DEEP, translucent colour you can
    // almost see the lamp through. So the negative masses take a colour of
    // their own, applied in the DISPLAY PASS on inkC (full trace at "DYE THE
    // DARK MASSES" in shaders.h: the ink RAMP, which this first shipped
    // against, is not read at all in ink_mode=water, so it could never colour
    // a mass -- two rounds of ramp edits rendered byte-identical).
    // dye_lum is the light the THIN edge of the wax passes; the thick core
    // keeps a floor of it, so thickness reads as translucency and the crust
    // and mass_rim still read against the body.
    // dye_sat 0 OR dye_lum 0 is exactly today's black, which is why every
    // existing preset and style=fluid are untouched to the bit.
    //   dye_hue_follow 1 makes dye_hue an OFFSET from the film's current hue,
    //   so the dye rotates with the pair as hue_rotate_period and the sweep
    //   turn the film -- the two stay a designed pair instead of drifting
    //   into whatever clash the clock lands on.
    float dyeHue       = 0.0f;      // degrees                          dye_hue
    float dyeSat       = 0.0f;      // 0..1, 0 = today's neutral        dye_sat
    float dyeLum       = 0.0f;      // 0..1, 0 = today's black          dye_lum
    int   dyeHueFollow = 0;         // 1 = offset from the film   dye_hue_follow
    // --- brief BK ("too bubbly", lost the sense of darkness) ---------------
    // dye_droplets: how much of the dye the droplet HOLES take (1 = today,
    //   every droplet dyed like a mass; 0 = droplets stay black and only the
    //   masses carry colour). Mass vs droplet = the blob-only field: a gap
    //   between blobs is a mass, a hole punched into the sheet is a droplet.
    // dye_masses: the same for the masses (1 = today; 0 = masses black).
    // dye_droplet_hue/sat/lum: the droplets' OWN dye; -1 = inherit dye_hue /
    //   dye_sat / dye_lum (so the split costs nothing until it is asked for).
    //   dye_droplet_hue follows the film exactly as dye_hue does when
    //   dye_hue_follow is on. Three roles (black / main / accent) over three
    //   elements (film / masses / droplets) is the user's design space.
    // dye_smoke: 0 = today's wax (hard stop at the isoline, brightest at the
    //   thin edge); up to 1 = the dye thickens gradually inward, leaks a
    //   little out under the thin film and breaks into slow fbm wisps.
    float dyeDroplets  = 1.0f;      // 0..1, 1 = today            dye_droplets
    float dyeMasses    = 1.0f;      // 0..1, 1 = today            dye_masses
    float dyeDropHue   = -1.0f;     // degrees, -1 = dye_hue      dye_droplet_hue
    float dyeDropSat   = -1.0f;     // 0..1, -1 = dye_sat         dye_droplet_sat
    float dyeDropLum   = -1.0f;     // 0..1, -1 = dye_lum         dye_droplet_lum
    float dyeSmoke     = 0.0f;      // 0..1, 0 = today            dye_smoke
    // film_level: the FLAT oil film's level (the background sheet), applied
    // in the display pass just before the film is composited. The edge light
    // (mass_rim, oil_glow, halo, lens highlights) keeps the full palette and
    // the mass/droplet dye is untouched. 1 = today; ~0.05 = film pitch black.
    float filmLevel    = 1.0f;      // 0..1, 1 = today            film_level
    // --- brief BL: fluorescent oil under the lamp, ambient-lit fluid -------
    // oil_fluor: EMISSIVE gain on the oil film from the rig lamp (the same
    //   lamp position the rim / shadows / specular use). The film glows in
    //   its OWN saturated hue (no hue key: the palette rotates), strongest at
    //   its thin edge -- fluorescent sheet traps its light and leaks it at
    //   the edges -- and near the lamp, falling off over oil_fluor_reach.
    //   Added after film_level, so a dimmed film + fluor = dark sheet, lit
    //   edges. The edge band also gets HDR headroom (small area, ABL).
    // oil_fluor_reach: Gaussian falloff radius from the lamp, screen heights.
    // dye_lamp_follow: 1 = today (the mass dye is lit by the lamp ramp and
    //   brightest at its thin edge); 0 = the dye ignores the lamp and the
    //   thin-edge profile and sits at its core level everywhere: flat,
    //   ambient colour that the lamp never lifts.
    float oilFluor      = 0.0f;     // 0..1, 0 = today            oil_fluor
    float oilFluorReach = 0.80f;    // screen heights             oil_fluor_reach
    float dyeLampFollow = 1.0f;     // 0..1, 1 = today            dye_lamp_follow
    // --- brief BP ("darker must mean MORE saturated, not less") -----------
    // dark_sat: chroma gain k = 1 + dark_sat * (1 - level), keyed on the
    //   ELEMENT's level (film_level for the film; the dye level for masses
    //   and droplets), never on per-pixel luminance. Applied as Y + (c-Y)*k,
    //   so the luma is kept exactly (mean_lum / HDR peak / ABL unchanged by
    //   construction); the only clamp is the gamut edge (no channel below 0),
    //   so HDR on and off agree. 0 = today.
    float darkSat       = 0.0f;     // 0..1, 0 = today            dark_sat
    // --- brief AG-b: dye gradients, so the masses are not all one wax -----
    // Each dye mass gets an IDENTITY in 0..1: the hole blobs carry a hash
    // (stable for the blob's life, re-rolled when it respawns under the
    // bottom edge) that the shader soft-maxes (w^4) inside its blob loop; the
    // gaps between oil blobs, where no hole reaches, fall back to a slow fbm
    // at mass scale that rises with the column. MASSES only -- droplets keep
    // their dye. All three need dye_lum > 0; all 0 = today (shader skips).
    // dye_lum_vary: per-mass level spread, relative: lum *= 1 +- value.
    // dye_hue_vary: per-mass hue spread, +- degrees around dye_hue.
    // dye_thick_hue: hue shift with depth into the mass (thin edge = dye_hue,
    //   thick core = dye_hue + value), like real dye density.
    float dyeLumVary    = 0.0f;     // 0..0.5, 0 = today          dye_lum_vary
    float dyeHueVary    = 0.0f;     // deg 0..90, 0 = today       dye_hue_vary
    float dyeThickHue   = 0.0f;     // deg -90..90, 0 = today     dye_thick_hue
    // --- brief BR: dye_core ------------------------------------------------
    // The mass dye is lit through a Beer-Lambert profile lerp(core, 1, Tw):
    // the thin rim (Tw = 1) is full level, the thick core (Tw -> 0) sits at
    // `core` times it. 0.42 is the old hard-wired floor (exact identity);
    // 1 = an even, opaque-reading fill. dye_smoke pulls the profile toward
    // 0.75, so with smoke up this key does less; dye_lamp_follow < 1 pushes
    // Tw toward 0, i.e. toward this floor (at dye_core 1 it thins nothing).
    float dyeCore       = 0.42f;    // 0.42..1, 0.42 = today      dye_core
    // --- brief BU: lamp grey + split tone (display pass, acid PSO only) ----
    // LAMP GREY: the dye far from the lamp "gets less light". A soft disc of
    // lamp_grey_size screen heights on the corner diagonally opposite the rig
    // lamp (CPU pick with hysteresis, slides corner to corner along the frame
    // edge over 90 s) is desaturated about its own LINEAR luminance by up to
    // 0.6 x lamp_grey at the corner, plus a Y-normalised cool shift of
    // lamp_grey_cool of that. Y is kept, so ABL / mean_lum do not move; never
    // the centre (the frame centre is > 1 screen height from any corner).
    // 0 = today (the shader skips the branch).
    float lampGrey      = 0.0f;     // 0..1, 0 = today            lamp_grey
    float lampGreySize  = 0.40f;    // 0.25..0.6 screen heights   lamp_grey_size
    float lampGreyCool  = 0.20f;    // 0..1                       lamp_grey_cool
    // SPLIT TONE: the black masses take the complement of the main (film)
    // hue, following hue_rotate / the sweep, lifted by at most
    // shadow_tone_lift (sRGB-encoded, of SDR white) -- the one BU piece that
    // gives up true black, by the user's call. Gated on/off with a 50% duty
    // on its own clock (shadow_tone_period, default 2225 s = 3600 / golden
    // ratio, so it never locks to the hue cycle), smoothstep edges of
    // shadow_tone_fade s; fully on for the first ~14.5 min after launch.
    // Mask = (1 - film alpha) x (1 - smoothstep(0.02, 0.15, luma)): film and
    // lit dye untouched, rims painted over it afterwards. 0 = today.
    float shadowTone       = 0.0f;     // 0..1, 0 = today          shadow_tone
    float shadowToneLift   = 0.06f;    // 0..0.15                  shadow_tone_lift
    float shadowToneSat    = 0.80f;    // 0..1                     shadow_tone_sat
    float shadowTonePeriod = 2225.0f;  // s, 0 = always on         shadow_tone_period
    float shadowToneFade   = 120.0f;   // s per on/off edge        shadow_tone_fade
    // shadow_tone_hue: < 0 = the complement of the main hue (above, turns
    // with it); 0..360 = a FIXED absolute HSV hue in degrees (275 violet,
    // 235 blue, 185 teal) that does not rotate. CPU only, no shader slot.
    float shadowToneHue    = -1.0f;    // deg, -1 = complement     shadow_tone_hue

    // --- edge PROFILE (oil_edge_curve) ------------------------------------
    // The user, on the live panel: "smudge the border more -- it looks like it
    // goes from green to black, then stops; it should be more S-curved".
    // Reshapes the edge band's ramp (film thickness and film alpha) from
    // smoothstep to smootherstep (6t^5-15t^4+10t^3) so both knees vanish.
    // Does NOT widen the band. 0 = the shipped profile, byte-identical.
    float oilEdgeCurve  = 0.0f;     //                        oil_edge_curve

    // How the oil's EDGE is drawn (the body -- transparency, absorption,
    // bump, refraction -- is the same either way).            oil_edge_mode
    //   0 the shipped soft film edge: the thickness band is proportional to
    //     the local lens radius, so a big disc fades out over a wide
    //     translucent gradient and a droplet over a couple of pixels.
    //   1 CRISP: one narrow, size-independent band a few rim-widths across,
    //     so a big hole ends on the same hard isoline a droplet does and
    //     carries only its meniscus.
    // The user, shown stills of both: "hard to say in stills, apply all" --
    // so both ship and the choice is made live from the tray.
    int   oilEdgeMode   = 0;

    // --- grain / speckle ---
    float grainAmt    = 0.030f;     // coarse animated film grain
    float grainScale  = 3.0f;       // px per grain cell (>1 = coarse)
    // Weight the grain into the SHADOWS (the refs' dark ink is visibly noisy
    // while the flat oil discs are clean â€” real sensor noise, not an overlay).
    // amplitude *= lerp(1, (1-luma)^2, w). 0 = the shipped uniform grain.
    float grainShadowW= 0.0f;       //                       grain_shadow_weight
    // Ink-tinted toe: lift the very darkest pixels toward a dark version of
    // the ink hue instead of neutral black (the refs' toe is #180808 warm,
    // never a pure crush). 0 = the shipped neutral toe.            toe_tint
    float toeTint     = 0.0f;
    // --- final composite trim. A transparent film lowers perceptual chroma
    // by 10-20% and lightness by ~5-13% against the same look opaque (OKLab
    // means over non-dark pixels). Rather than re-authoring every palette for
    // the film, scale the FINAL acid colour: chroma about the pixel's own
    // luma (hue and luma preserved) and a plain luma multiplier. The user's
    // pick is oil_transparency 0.5 held back to the opaque level, which
    // measured x1.19 / x1.18 -- hence the shipped 1.2 / 1.05 on the glass
    // inis. 1 = untouched.                          post_chroma   post_lift
    float postChroma  = 1.0f;
    float postLift    = 1.0f;
    float speckle     = 0.12f;      // cellular dots concentrated at interfaces
    float speckScale  = 240.0f;     // cells per uv unit
};

// ---------------------------------------------------------------------------
// "Ink in water" RENDER look (ini section [ink], enabled by [look] style=ink).
//
// Per reference/shots/photos/ink-in-water-ref-*.jpg: black acrylic ink dropped
// into backlit clear water. What defines it is ABSORPTION, not emission â€” thin
// veils are see-through grey, thick cores are opaque, and every fold reads as
// a darker outline because you are looking through more ink edge-on.
//
// So the dye field is not a colour any more, it is an optical DEPTH:
//     T = exp(-k * thickness)          (Beer-Lambert transmittance)
//     paper mode    out = paper * T            (dark ink on light ground)
//     inverted mode out = tint(op) * op        (pale ink on black; op = 1 - T)
// The inverted mode is the OLED-friendly one (no full-frame white field) and
// is what this look defaults to on the user's panel.
//
// The same block is shared with the Liquid Acid look via
// LiquidAcidConfig::inkMode = 1, so oil can float on ink-in-water.
// All of it is inert unless `enabled` â€” the display shader is then compiled
// without the INK macro and contains none of this code.
// ---------------------------------------------------------------------------
struct InkConfig {
    bool  enabled      = false;     // [look] style=ink  (or [look] ink=1)
    bool  inverted     = false;     // 0 = paper (dark ink on light ground),
                                    // 1 = pale ink on a black ground        inverted
    float density      = 3.0f;      // k: T(1.35)=e^-4=0.02 opaque core,
                                    //    T(0.05)=0.86 see-through veil      density
    float chroma       = 0.0f;      // 0 = neutral ink; >0 lets the dye's own
                                    // hue tint the transmitted light (0..3)  chroma
    float edgeStrength = 0.35f;     // fold/sheet darkening                   edge_strength
    float edgeLo       = 0.02f;     // |grad density| window                  edge_lo
    float edgeHi       = 0.25f;     //                                        edge_hi
    float edgeScale    = 2.0f;      // gradient tap spacing, screen texels    edge_scale
    float paper[3]     = { 0.90f, 0.91f, 0.93f };   // paper (or, inverted,
                                    // the background) colour                 paper_color
    float vignette     = 0.15f;     // radial darkening of the paper          vignette
    float tintThin[3]  = { 0.70f, 0.85f, 1.00f };   // inverted: thin veil    tint_thin
    float tintThick[3] = { 1.00f, 1.00f, 1.00f };   // inverted: opaque core  tint_thick
    // Duotone PAIR ROTATION. With chroma=0 the ink is a fixed two-colour
    // duotone (thin veil / opaque core). This cross-fades that pair through
    // the SAME curated complementary list liquid_acid sweeps
    // (LiquidAcidConfig::sweepInk / sweepOil, hue-preserving HSV lerp â€” the
    // CSS hue matrix was tried and went muddy). Thin takes the ink anchor
    // (the darker half) and thick the oil anchor, which is exactly the
    // assignment the user's hand-picked duotone inis already use.
    // Seconds for one full trip through the list; 0 = off (fixed pair).
    float pairSweepPeriod = 0.0f;   //                     pair_sweep_period
    float coreKnee     = 0.30f;     // inverted: opacity where the tint
                                    // crosses from thin to thick             core_knee
    float hdrCore      = 1.0f;      // inverted: scale of the raw-dye level
                                    // that drives the HDR highlight gain     hdr_core
    // Motion gate on that HDR lift, in sim texels/s of local velocity. A drop
    // always leaves dye at the injection point that never got any momentum;
    // stationary, it has no business being the brightest thing on screen.
    // motion_hi <= motion_lo turns the gate off.            motion_lo motion_hi
    float motionLo     = 3.0f;
    float motionHi     = 40.0f;
    // How much of that gate also applies to the OPTICAL PATH (0 = HDR lift
    // only). 1 = stationary ink is fully transparent.        motion_opacity
    float motionOpacity = 0.0f;
    // Faint dye below this opacity goes to the background instead of a
    // semi-transparent wash (inverted). 0 = off.             veil_floor
    float veilFloor    = 0.0f;
    // Dip the duotone opacity around its midpoint so mid-density dye shows
    // the background colour instead of the grey-brown RGB midpoint. tint_mid_dip
    float tintMidDip   = 0.0f;
    float parallax     = 0.0f;      // cheap 2nd layer: 0 = off               parallax
    float parallaxScale= 0.92f;     //                                        parallax_scale
    float parallaxDrift= 0.004f;    //                                        parallax_drift
    // -----------------------------------------------------------------------
    // HDR RANGE MAPPING for the ink look ([ink] tonemap).
    //
    // The shared "parity-plus" highlight gain (see BuildDisplayConstantsEx)
    // only spends headroom on dye that is nearly at the brightness CAP, which
    // is the right model for the WE fluid look and useless for ink: the ink
    // composite is an absorption result that never gets near the cap (with
    // hdr_core 0.3 and [hdr] knee 0.70 the gain is identically 1), so the
    // whole frame lives inside SDR white and its tonal range is squeezed.
    //
    // tonemap=1 replaces that gain for style=ink with an explicit map of the
    // composite's own 0..1 range onto [black_nits, white_nits], with an
    // optional S-curve for mid separation and the parity-plus gain retargeted
    // above white_nits so hot moving cores still run to peak_nits.
    // Every value below is inert while tonemap = 0.
    int   tonemap      = 0;         // 0 = legacy gain, 1 = range map    tonemap
    float whiteNits    = 0.0f;      // nits at composite 1.0; 0 = SDR white
                                    // (80 * sdr_scale), i.e. today's level white_nits
    float blackNits    = 0.0f;      // nits at composite 0.0             black_nits
    float toneKnee     = 0.0f;      // 0 = straight ramp, 1 = full S-curve
                                    // (more steps through the mids)     tone_knee
    // Chroma hold, same math as [liquid_acid] post_chroma: scale the pixel
    // away from its own luma in linear light, so raising the white point can
    // never read as a wash. 1 = untouched. Applies to style=ink whatever
    // `tonemap` is.                                              tone_chroma
    float toneChroma   = 1.0f;
    // Duotone blend space. The thin/thick tint pair is cross-faded per pixel
    // by `tk`; done in RGB (0, the shipped behaviour) a COMPLEMENTARY pair
    // passes through grey at the midpoint, which is exactly why a teal/
    // vermillion ink reads as grey-blue and pale peach over most of a plume.
    // 1 blends hue/sat/value instead (short way round the wheel), so every
    // intermediate is as saturated as the two anchors.       tint_hue_blend
    float tintHueBlend = 0.0f;
};

// ---------------------------------------------------------------------------
// Ink DROPS â€” a style-agnostic emitter ([drops], usable with any [look]).
// One drop = a single downward Gaussian velocity impulse plus dye. After the
// pressure projection that impulse is a vortex dipole, which is what rolls the
// head into the mushroom cap in ref 1; dye-weighted gravity ([sim] gravity)
// then keeps the dense head sinking while the thin veils hang behind it.
// ---------------------------------------------------------------------------
struct DropConfig {
    bool  enabled     = false;      // drops=1
    float interval    = 14.0f;      // s between drops (jitter +-35%)  interval
    float xMin = 0.15f, xMax = 0.85f;   // entry band, uv              x_min x_max
    float yMin = 0.04f, yMax = 0.22f;   //                             y_min y_max
    float speed       = 700.0f;     // downward impulse (dx jitter +-60)  speed
    float radius      = 0.35f;      // splat radius, percent like splat_radius  radius
    float density     = 1.35f;      // dye intensity (capped by max_brightness) density
    float tailSec     = 0.6f;       // how long the entry keeps feeding  tail_sec
    float tailDensity = 0.25f;      //                                 tail_density
    // The entry is a THIN STREAM, not a second blob. Stamping the tail at the
    // head's own radius and density painted a fat radially-symmetric orb that
    // sat at the injection point for the drop's whole life (harmless mist on
    // paper, a blown-out white ball in inverted + HDR). Both of these keep it
    // a stream: a small fraction of the head radius, and a gentle downward
    // impulse so the dye is pulled into the stem instead of parking.
    float tailRadiusFrac = 0.22f;   // x drop radius                 tail_radius_frac
    float tailSpeed      = 0.0f;    // x drop speed, downward        tail_speed
    // The velocity impulse is SPATIALLY WIDER than the dye stamp, by this
    // ratio. With them the same size, the outer wings of the dye Gaussian sit
    // outside the moving core, get no momentum, and stay parked at the
    // injection point as a round blob for the drop's whole life â€” soft mist on
    // paper, a blown-out white orb in inverted + HDR. A real drop pushes a
    // volume of water larger than itself, so >1 is also the physical case.
    // (splat `radius` is a squared scale, hence the square here.) impulse_spread
    float impulseSpread  = 2.2f;
    int   spatter     = 0;          // satellite droplets, 0..12 (0 = off)  spatter
    float spatterRadius = 0.04f;    //                                 spatter_radius
    float spatterSpeed  = 500.0f;   //                                 spatter_speed
    float spatterSpread = 0.05f;    // uv                              spatter_spread
    int   colorMode   = 0;          // 0 = fixed `color`, 1 = wheel hue  color_mode
    float color[3]    = { 1.0f, 1.0f, 1.0f };   // dye-space colour     color = "r g b"
    bool  obeyGovernor= true;       // skip while the screen is too full  obey_governor
    // Lobe asymmetry. A single radially symmetric impulse makes a textbook
    // MIRROR-SYMMETRIC vortex pair, which reads as a glassy diagram; real ink
    // drops (the refs) make several unequal lobes with one side leading. This
    // splits the head's velocity impulse into two unequal, off-centre ones
    // around the same dye stamp. 0 = the symmetric single impulse. asymmetry
    float asymmetry   = 0.35f;
};

// Screen mirroring / kaleidoscope ([mirror]). A pure DISPLAY-pass uv
// transform, applied FIRST in PSMain: before the dye sample, before the acid
// metaball field (pp derives from uv) and before InkWater â€” so every look
// (fluid / liquid_acid / ink) folds consistently, blobs, rims, swarms and
// speckle included. mode 0 is a taken-early-out branch in the SHARED shader,
// not a macro or a second PSO, so style=fluid stays bit-identical with it off.
//
// Deliberately split for the SWEEP transition sketched in PROGRESS.md: the
// fold line's definition lives in this one struct (centre, angle, mode) and
// MirrorFold() in the shader returns the source uv AND the distance to the
// nearest fold line, so a later "side weight" can reuse the distance without
// touching the mirror modes.
// ---------------------------------------------------------------------------
// [post] â€” the LAST thing that happens to the composite, for every look.
// Applied after post_chroma / post_lift and after the acid's own grain, in
// screen space (never folded by the mirror: film sits in front of the lens).
// Both keys are 0 by default and the whole block is skipped when they are, so
// style=fluid is bit-identical unless they are named.
// ---------------------------------------------------------------------------
struct PostConfig {
    // Film grain over everything: luminance-weighted (mids and darks carry it,
    // peaks stay clean, true black is only barely lifted), animated.
    float filmGrain      = 0.0f;   // 0..1 amount                 film_grain
    float filmGrainSize  = 1.5f;   // px per grain cell      film_grain_size
    float filmGrainSpeed = 1.0f;   // multiplier on film_grain_fps    film_grain_speed
    // The grain's frame rate. Real film changes its grain once per FRAME of
    // the stock, not once per refresh of the panel: 24 (or 30) divides 240
    // exactly, so every pattern holds for a whole number of refreshes and
    // the chatter has a film cadence instead of a 144 Hz fizz. Both the grain
    // and film_noise quantise their time to this.
    float filmGrainFps   = 24.0f;  // patterns per second     film_grain_fps
    float filmGrainColor = 0.0f;   // 0 mono .. 1 RGB       film_grain_color
    // ---- brief BD: the grain must not read as high-ISO artifacting -------
    // NOT the same key as film_grain_color, which asks for three independent
    // noises. This one is about HOW one noise is applied. Today the grain is
    // an ADDITIVE offset in the sRGB-encoded domain, the same number on all
    // three channels -- which is not luminance-only: an equal encoded step on
    // unequal channels moves the pixel's saturation, and near black it is
    // clamped at zero, which lifts the mean as well. At 0 the grain instead
    // SCALES the encoded pixel, so its channel ratios (hue and saturation)
    // survive exactly, it cannot lift a black pixel, and it cannot clip.
    // 1 = today, and every existing preset is byte-identical at that value.
    float filmGrainChroma  = 1.0f; // 1 additive (today) .. 0 hue-preserving   film_grain_chroma
    // ...and how the amplitude is weighted. Today's curve is full from about
    // 0.06 encoded up and still has a 0.15 FLOOR on absolute black, so a
    // 1-nit mass carries the same grain as a 100-nit film -- which measured
    // at 4-6x pixel-to-pixel brightness in a dark mass against 1.2x on the
    // open film. At 1 the weight becomes the stock's own D-log-E hump: zero
    // in the dense shadow, peak in the mid-tones, gone on a clean highlight.
    float filmGrainDensity = 0.0f; // 0 today's curve .. 1 density hump      film_grain_density
    // Chromatic aberration, LATERAL and per-edge (the reference's warm/cool
    // fringe): R and B displaced in opposite directions along the local edge
    // normal, G untouched, so every hard edge gets one warm and one cool side.
    // Authored in px at 1440p and scaled with the frame, because it is an
    // optical effect and not a pixel one.
    float aberration     = 0.0f;   // 0..1 master                aberration
    float aberrationPx   = 0.8f;   // px at 1440p             aberration_px
    float aberrationField= 0.5f;   // extra toward the frame edge  aberration_field
    // brief BD. The split is a DIFFERENCE of two resamples of the SHARP
    // source added to a picture that has already been defocused, so at 0 it
    // stays razor-sharp on a bokeh disc -- which no lens does. At 1 it is
    // scaled by the pixel's own circle of confusion against the diffraction
    // floor: full on the sharp slice, gone where the picture is soft.
    // 0 = the uniform split the user approved on 2026-09-19, so any preset
    // that does not name this key keeps exactly that.
    float aberrationCoc  = 0.0f;   // 0 uniform (today) .. 1 fades with the blur  aberration_coc
    // A slight darkening toward the corners -- the field stop, never a circle.
    float vignette       = 0.0f;   // 0..1                         vignette
    // Lens defocus on the isolines themselves (the user: "these edges are way
    // too accurate and focused"). px at 1440p; widens the coverage AA band,
    // the film edge and the rim/meniscus, and leaves the grain sharp.
    float softness       = 0.0f;   // px at 1440p                  softness
    // --- BRIGHT-FIELD HALO (the microscope "double contour") --------------
    // The user's reference photos of oil under a macro lens: every dark shape
    // carries a soft bright glow hugging its outside, with a faint darker echo
    // beyond it -- phase contrast, not a stroked line. "Very weak, but quite
    // wide." Rendered off the SAME signed distance the rim and meniscus use,
    // so it belongs to the surface instead of being pasted over it. The keys
    // live in [post] with the rest of the camera family; only the liquid_acid
    // look has an isoline to hang them on, so only it implements them.
    float halo    = 0.0f;           // 0..1 lift toward oil/white     halo
    float haloPx  = 12.0f;          // band width in px at 1440p      halo_px
    // --- MINIMUM BAND WIDTHS (the user: "the bubbles got a solid outline";
    // "whatever shading is in this photo needs to be everywhere") ----------
    // Every optical band around an isoline -- the film edge, the dark rim,
    // the meniscus, the halo, the penumbra -- was authored as a fraction of
    // the LENS radius, so a big mass got a wide soft gradient and a small
    // droplet got the same shading squeezed into a sub-pixel line, i.e. a
    // hard bright outline. This gives each band a floor in px at 1440p
    // (scaled with the frame), so the small elements are shaded like the big
    // ones. 1 = the floors as authored, 0 = the old size-proportional bands.
    float bandMin = 1.0f;           // 0..1 scale on the floors      band_min
    // --- IMAGE-SPACE camera pass (kPostSrc) -------------------------------
    // Everything above shades edges analytically off their isoline, which is
    // exact on a big mass and collapses on anything narrower than the band
    // (a 2-px ring wall rendered as a hard stroked line beside softly shaded
    // droplets). These run on the FINISHED frame, so every feature gets the
    // same optics whatever its size, and the film grain moves after them so
    // it stays crisp. Either non-zero turns the pass on; style=fluid never
    // names them, so it never enters it.
    float postBlurPx = 0.0f;        // defocus disc radius, px at 1440p  post_blur_px
    float postGlow   = 0.0f;        // 0..1 veiling glare weight          post_glow
    float postGlowPx = 10.0f;       // its radius, px at 1440p            post_glow_px
    float postGlowDark = 0.5f;      // 0..1 lean the glare to the dark side post_glow_dark

    // --- FILM OVERLAY ARTEFACTS (kPostSrc) --------------------------------
    // What the user meant by "macro-ish cellulose noise" the second time
    // round: the artefacts of a projected or scanned FILM -- hairs caught in
    // the gate, dust, fine scratches, the odd light leak. Procedural (no
    // textures), authored at 1440p and scaled with the frame, additive and
    // weighted toward the DARK pixels ("the black oil more than the oil"), so
    // the bright film keeps its colour. The population CHANGES: specks
    // flicker at 24 fps, a hair sticks for a few seconds then is gone, a
    // scratch persists for a stretch then drifts away -- every artefact's
    // seed is hashed from floor(time / rate), so nothing sits still.
    // "A very very subtle thing" (the user): ship values are barely there.
    float filmDust      = 0.0f;   // 0..1 how many specks        film_dust
    float filmHairs     = 0.0f;   // 0..1 chance of a hair       film_hairs
    float filmScratches = 0.0f;   // 0..1 chance of a scratch    film_scratches
    float filmLeak      = 0.0f;   // 0..1 edge light leak        film_leak
    float filmArtefactRate = 5.0f;// s per population       film_artefact_rate
    // A SECOND, finer and faster noise layer under the coarse grain: the
    // emulsion's own fizz as against the stock's grain structure. Mono.
    float filmNoise     = 0.0f;   // 0..1 amount                 film_noise
    float filmNoiseSize = 1.0f;   // px at 1440p            film_noise_size
    // A stock's own colour: lifted teal shadows, warm highlights, slightly
    // different curve per channel (the cross-process feel). Applied last,
    // after every artefact, just before the frame leaves the pass.
    float filmStock     = 0.0f;   // 0..1                        film_stock

    // --- LIGHT IN THE WATER (fog + bloom) ---------------------------------
    // The user's reference: a glass of cloudy water lit by a lamp from below.
    // One off-view light position drives both a volumetric haze (the water
    // itself glowing, brightest near the lamp, falling to nothing with
    // distance and added only into the dark) and a very wide, very weak bloom
    // from the bright film into the black. The light never sits still: a sum
    // of slow sines gives the frame an idle animation of its own.
    float fog        = 0.0f;      // 0..1 haze master                fog
    float fogPx      = 700.0f;    // 1/e distance, px at 1440p       fog_px
    // The haze is the WATER, and a dye mass floats in FRONT of it, so the
    // glow has no business shining through the middle of one (brief BD: the
    // post lifts were measured at 47% of a dark mass's level). At 0 the haze
    // lands wherever it is dark, which is today's behaviour; turned up, a
    // pixel that is dark AND whose wide surroundings are dark -- i.e. deep
    // inside a mass -- keeps its black, while a dark pixel beside bright film
    // is the water itself and still glows.
    float fogMassGate= 0.0f;      // 0 today .. 1 no haze inside masses  fog_mass_gate
    // brief BO: inside a mass, the grain, the lens split and the cover's
    // sheen / iridescence / glint halo follow the mass's own brightness and
    // colour (the massDeep ring, plus a saturation lift), so a pure black
    // mass stays clean and a dyed one keeps its texture. 0 = today.
    float artefactLumGate = 0.0f; // 0 today .. 1 artefacts follow mass lum/colour  artefact_lum_gate
    // brief BN, instrument optics (post pass, packed in rig rg1.x / rg1.y as
    // 6-bit fields; every one 0 = today, bit for bit):
    //   corner_warp      tangential stretch of the frame's corners outside
    //                    corner_warp_r (1 = a corner); the centre is untouched
    //   bloom_warmth     the keyed bloom takes the lamp's colour (hot yellow
    //                    on its side, red away from it), luminance unchanged
    //   glass_streaks    two thin bright wavy lines on the lid glass (needs lid)
    //   halation_threshold  lowers halation's knee so it becomes a diffusion haze
    float cornerWarp        = 0.0f;  // 0 off .. 1 strong                     corner_warp
    float cornerWarpR       = 0.6f;  // 0.4..0.9, where the warp starts       corner_warp_r
    float bloomWarmth       = 0.0f;  // 0 film colour .. 1 lamp colour        bloom_warmth
    float glassStreaks      = 0.0f;  // 0 off .. 1 bright                     glass_streaks
    float halationThreshold = 0.0f;  // 0 highlights only .. 1 mid-tones too  halation_threshold
    float bloom      = 0.0f;      // 0..1 wide wash master           bloom
    float bloomPx    = 140.0f;    // its radius, px at 1440p         bloom_px
    float lightX     = 0.5f;      // uv; off-frame below the middle  light_x
    float lightY     = 1.20f;     //                                 light_y
    float lightDrift = 1.0f;      // 0..1 idle motion            light_drift
    // Where the lamp stands relative to the PLANE of the dish (brief BC).
    // > 0 in front of / above it, so a cast shadow rakes away from the lamp
    // and shortens as the lamp rises; 0 = in the plane, the longest shadows;
    // < 0 behind the dish = backlit, where the shadow has no direction left
    // and spills evenly round each caster toward the camera. Read by the
    // acid display pass, and inert until shadow_amt is turned up.
    float lightZ     = 0.35f;     // -1..1                           light_z

    // --- PERSPECTIVE CAMERA + DEPTH OF FIELD + TILT (items N + R) ---------
    // Up to here the camera was an orthographic scanner with ONE global
    // defocus: every element the same distance away, every ring a stamped
    // circle, one blur radius for the whole frame. The user's sketch is a
    // LENS at the tip of a view cone looking down at a flat dish, so:
    //   * each droplet/ring carries its own depth (droplet_depth), the display
    //     pass turns |depth - focus| into a CIRCLE OF CONFUSION and writes it
    //     into the FP16 post target's ALPHA, and the post pass sizes its
    //     defocus disc per pixel from that -- so one element can be sharp
    //     while the ring behind it is a ghost (the Requiem reference);
    //   * the focal "plane" is not flat relative to the dish: camera_field_
    //     curve bends it with distance from the optical axis, so the centre
    //     and the corners of the frame cannot both be sharp;
    //   * focus_tilt slants it (a Lensbaby / freelensing sweet spot), and both
    //     the tilt and the focus distance move by OCCASIONAL READJUSTMENT --
    //     still for focus_tilt_period seconds, then a short eased move with a
    //     slight overshoot and settle, then still again -- never a continuous
    //     drift. (Drifting is the lamp's job, not the focus ring's.)
    //   * camera_fov > 0 makes the view PERSPECTIVE: an off-axis ring is seen
    //     obliquely, so it foreshortens radially toward the axis, its wall
    //     reads thicker on the far side, and its highlight favours the side
    //     facing the axis. 0 = orthographic = exactly today's geometry.
    // Every key defaults to today's behaviour, and dof_max_px = 0 keeps the
    // CoC channel and the per-pixel disc entirely out of the frame.
    float cameraFov       = 0.0f;   // deg across the frame; 0 = ortho     camera_fov
    float cameraFocus     = 0.5f;   // depth the lens is focused on      camera_focus
    float cameraFieldCurve= 0.0f;   // focus depth per (dist from axis)^2 camera_field_curve
    float cameraAxisX     = 0.5f;   // where the optical axis meets the dish camera_axis_x
    float cameraAxisY     = 0.5f;   //                                    camera_axis_y
    float focusTilt       = 0.0f;   // focus depth per unit along the tilt  focus_tilt
    float focusTiltAngle  = 0.0f;   // deg; direction that gradient points focus_tilt_angle
    float focusBandPx     = 260.0f; // width of the sharp strip, px at 1440p focus_band_px
    float focusTiltPeriod = 0.0f;   // mean s between readjustments; 0 = never focus_tilt_period
    float focusTiltMoveS  = 1.4f;   // how long one readjustment takes, s  focus_tilt_move_s
    float dofMaxPx        = 0.0f;   // CoC clamp, px at 1440p; 0 = no DoF   dof_max_px

    // --- DIFFRACTION: the POINT SPREAD (item W) ---------------------------
    // The user, photographing an in-focus ring on the panel: "a thin hair on a
    // film will never cause pure darkness because the light bends around it;
    // it's not out of focus per se." No optical system resolves a point to a
    // point, so nothing in the frame can be both fully dark and hard-edged,
    // however well it is focused. This is the floor under the per-pixel
    // defocus radius, applied regardless of focus: "razor sharp" means
    // DIFFRACTION-limited, not pixel-limited, and at >= 1 px the in-focus
    // isoline can never come out as raw coverage AA -- the stair-stepped "O"
    // in the photo. 0 = off, and style=fluid never names it.
    float psfPx = 0.0f;             // point spread radius, px at 1440p  psf_px

    // --- OUTPUT DITHER (item V0) ------------------------------------------
    // The frame leaves here as FP16 scRGB, but the panel quantises it to 10
    // bits in a perceptual domain, and on a big saturated flat -- which is
    // most of this look -- 10 bits is not enough: the user can see the steps
    // on the OLED. Half an LSB of ordered noise on the final output breaks
    // them into a dither pattern the eye integrates away. It is applied in the
    // ENCODED domain, because that is where the quantiser lives, and it fades
    // out into true black, because a lifted black would be a far worse bug
    // than a band. 1 = half an LSB; higher is for looking at it.
    float dither = 0.0f;           //                                dither

    // --- HALATION (item V1) -----------------------------------------------
    // CineStill 800T is ordinary cinema stock with the anti-halation backing
    // removed: light from a highlight passes through the emulsion, reflects
    // off the base and comes back a few pixels out, reddened by the layers it
    // crossed twice. So every hot area carries a TIGHT warm glow -- not the
    // wide weak wash `bloom` already does, and taken only from what is
    // actually bright. It lands in the DARK around a highlight rather than on
    // the highlight itself, which is what makes it read as the film glowing
    // rather than as more exposure, and it leans toward the rig's lamp, so
    // nothing about it sits still.
    float halation       = 0.0f;   // 0..1 master                    halation
    float halationPx     = 14.0f;  // radius, px at 1440p         halation_px
    float halationWarmth = 0.75f;  // 0 white .. 1 red-orange halation_warmth

    // --- THE LID (task V2, brief T note 3 + V key ref) --------------------
    // The user: NO visible instrument, NO petri-dish rim, NO grid or UI
    // marks, NO dark corners -- the oil stays full-bleed edge to edge. What
    // may be simulated is the transparent glass/plastic CAP over the dish,
    // i.e. the picture seen THROUGH a cover: the internal reflections of the
    // bright film in it, a broad sheen from the lamp, the lamp's own glint,
    // the faint iridescence of an oil film on its surface, and the wobble a
    // sheet of cheap plastic gives its own reflections. All FULL-FRAME and
    // ADDITIVE -- never a mask, never a border, nothing that reads as a hole
    // in the display. The reference is the LAPD optics sheet
    // (endgoal-ref-13): concentric coloured ring ghosts, a warm veiling
    // flare, a hot spot with a wide amber glow, iridescent colour sweeps.
    //
    // NOTHING SITS STILL (T note 4, OLED): every element's position comes
    // from the RIG -- the lamp's drift, the lens axis, and the lid's own slow
    // idle wander -- and every element is shoved at once at each of the rig's
    // occasional readjustments. There is no fixed screen position anywhere in
    // this block.
    float lid          = 0.0f;   // 0..1 master; 0 = the block never runs  lid
    float lidGhost     = 0.0f;   // 0..1 offset copies of the bright film
    float lidGhostSpread = 1.0f; // 0..2 how far the ghost chain runs
    float lidRings     = 0.0f;   // 0..1 concentric coloured ring ghosts
    float lidSheen     = 0.0f;   // 0..1 broad soft specular off the cover
    float lidSheenPx   = 420.0f; // px at 1440p: the sheen's width
    float lidGlint     = 0.0f;   // 0..1 the lamp's own reflection + halo
    float lidIris      = 0.0f;   // 0..1 oil-film iridescence on the cover
    float lidRefractPx = 0.0f;   // px at 1440p: wobble of the REFLECTIONS
    // SCRATCHES (brief BM): the WEAR on the cover -- fine hairline grooves
    // fixed to the lid (they ride its drift, never the fluid) that light up
    // only where they run perpendicular to the line to the lamp's glint, so
    // the pattern breathes as the lamp moves. Additive, clear over the dark,
    // nearly gone over the bright film. Needs lid > 0 (no cover, no wear) but
    // is NOT scaled by it. lidScratch 0 = off, byte-identical.
    float lidScratch        = 0.0f;  // 0..1 amount                      lid_scratch
    float lidScratchDensity = 0.5f;  // 0..1 how many grooves            lid_scratch_density
    float lidScratchLen     = 0.3f;  // 0..1 0 = micro only, 1 = + 8 long gouges  lid_scratch_len
    float lidScratchCorner  = 0.7f;  // 0..1 0 = uniform, 1 = corners only  lid_scratch_corner
    float lidScratchSoft    = 0.2f;  // 0..1 groove width + light lobe   lid_scratch_soft
    float lidScratchTint    = 0.0f;  // 0..1 0 = white, 1 = film colour  lid_scratch_tint
    // --- MOTION (item V3) -------------------------------------------------
    // The user's rule for this whole family: nothing may sit at a fixed
    // screen position on an OLED, ever. And the motion model they chose is
    // specific -- a SLIGHT, SLOW drift all the time, plus OCCASIONAL
    // readjustments where EVERYTHING moves at once, eased, with a settle.
    // Never constant visible motion, and never one effect moving alone.
    //   shimmer        heat above the lamp: a very fine refractive wobble of
    //                  a pixel or two, strongest near the lamp, and ADVECTED
    //                  by the sim's own low-res velocity field, so a burst
    //                  that moves the oil pushes the shimmer ahead of it.
    //   vignette_wander  the vignette's centre follows the rig's lens, so the
    //                  darkest corner rotates over minutes instead of being
    //                  burnt into one corner of the panel.
    //   pixel_shift_px the classic OLED safety net: the entire finished image
    //                  translates on a many-minute orbit, in sub-pixel steps,
    //                  so no feature ever holds one pixel.
    //   rig_readjust   how far the LAMP and the LENS CENTRE re-aim when the
    //                  focus readjusts. 0 = only the focus and the tilt move
    //                  (the old behaviour); above 0 the whole rig moves as
    //                  one body, which is the point of having a rig.
    float shimmer        = 0.0f;   // 0..1 master                     shimmer
    float shimmerPx      = 1.5f;   // warp amplitude, px at 1440p  shimmer_px
    float vignetteWander = 0.0f;   // 0..1                   vignette_wander
    float pixelShiftPx   = 0.0f;   // orbit radius, px at 1440p pixel_shift_px
    float rigReadjust    = 0.0f;   // 0..1                      rig_readjust
};

struct MirrorConfig {
    // 0 off | 1 horizontal | 2 vertical | 3 quad (both) | 4 kaleidoscope
    int   mode = 0;
    int   segments = 6;          // kaleidoscope wedges (mode 4)
    // Which quadrant of the SIM is the one that gets shown and copied around:
    // bit 0 = right half, bit 1 = bottom half. The shown region is stretched
    // to fill its mirrored tile, so the full sim resolution is used.
    int   source = 0;
    float centerX = 0.5f;        // the fold point, uv
    float centerY = 0.5f;
    float rotatePeriod = 0.0f;   // s per turn of the fold axes (mode 4; 0 = fixed)
    float drift = 0.0f;          // 0..1: slow wander of the centre (sines of time)
    float soft = 0.0f;           // uv units: rounds the fold crease off (0 = hard)
};

// Defaults mirror reference/project.json (the shipped Wallpaper Engine values),
// falling back to reference/script.js config for values project.json doesn't set.
struct FluidConfig {
    int   simRes = 256;
    int   dyeRes = 1024;            // project.json ships 4096; raise after perf pass
    float densityDissipation = 0.999f;
    float velocityDissipation = 0.999f;
    float pressureDissipation = 0.85f;
    int   pressureIterations = 20;
    float curl = 48.0f;
    float baroclinic = 0.0f;    // dye-front torque: wakes bend around dye masses (0 = off)
    // Dye-weighted gravity ([sim] gravity / gravity_pow). Style-agnostic: any
    // look can use it. Units are sim texels/s^2 per unit density, +y = DOWN.
    // Velocity dissipation 0.999/step is an ~8 s drag time constant, so terminal
    // speed is ~8x gravity â€” tens, not thousands. 0 = the sim is untouched
    // (the branch in CSVorticity is not taken and the fluid look is identical).
    float gravity = 0.0f;
    float gravityPow = 1.5f;    // rho^p: thin veils hang, dense cores fall
    // Blur radius (sim texels) of the density the gravity force reads. Driving
    // it from the raw per-texel density seeds grid-scale Rayleigh-Taylor
    // fingers that vorticity confinement then amplifies â€” the plume comes out
    // a fuzzy cauliflower instead of the references' smooth sheets. Blurring
    // raises the instability wavelength to this scale. [sim] gravity_blur
    float gravityBlur = 3.0f;
    float flowSpeed = 1.0f;     // global impulse multiplier â€” slows/strengthens all currents
    float splatRadius = 0.64f;      // percent, /100 like reference
    bool  shading = true;
    float dyeDiffusion = 0.0f;      // Dâˆ‡Â²c strength: 0 = classic sharp look, >0 = smoke-like spread
    float decayFast = 1.0f;         // 1.0 = WE-original balance. 0.90 starved the field to black:
                                    // wanderer dye (0.1 * 0.15 intensity) died before accumulating.
                                    // Tail snappiness is a settings slider now â€” user taste, not a default.
    float decayThreshold = 0.29f;
    // M4 HDR output mapping (controlled live from the tray menu)
    int   gamutMode = 1;            // 0 sRGB, 1 Display-P3 (WE parity, default), 2 BT.2020 (over-saturates vs WE)
    float hdrPeakNits = 0.0f;       // resolved target nits for hot spots; 0 = off (match SDR)
    float hdrKnee = 0.6f;           // dye brightness where highlight expansion starts
    float maxBrightness = 1.35f;
    float satRestore = 0.93f;
    float colorCyclePeriod = 19.0f;
    bool  idleSplats = true;
    float idleInterval = 9.6f;
    int   idleAmount = 8;
    float idleBrightness = 1.5f;   // burst intensity (wanderers paint at 0.15)
    // post color filter â€” the equivalent of Wallpaper Engine's right-panel
    // color controls the user ran the original with (1/1/1/0 = neutral)
    float postSaturation = 1.0f;
    float postContrast = 1.0f;
    float postBrightness = 1.0f;
    float postHue = 0.0f;           // degrees
    // response curve (Lightroom-style brightness hump -> glowing splat rims)
    bool  curveEnabled = false;
    float curveCenter = 0.30f;      // input brightness the hump peaks at
    float curveWidth = 0.10f;       // hump half-width (smaller = tighter rims)
    float curveHeight = 1.3f;       // output brightness at the peak
    // shadow floor ("bottom knee"): lift near-black toward a neutral gray
    // floor so dark regions keep visible marbling. 0 = off (pure black).
    float shadowFloor = 0.0f;
    float shadowKnee = 0.15f;       // brightness range the lift fades over
    // hue band: constrain the color wheel to a slice around hueCenter.
    // range 180 = the classic full wheel; smaller = themed (e.g. only oranges)
    float hueCenter = 0.0f;         // degrees
    float hueRange = 180.0f;        // degrees half-width
    float hueLinger = 0.0f;         // banded moods: fraction of each half-lap
                                    // spent resting at a band edge (0 = off)
    // color source: random wheel (colorful) vs fixed palette
    bool  colorful = true;
    bool  moreColors = true;
    float splatColors[15] = { 0,1,0,  0,1,1,  0,0,1,  1,0,0,  0.9412f,1,0 };
    bool  splatOnClick = true;     // click burst when holdToSplat is off
    // perf
    float fpsLimit = 60.0f;
    // second monitor: mirror the fluid there (same field, own HDR mapping)
    bool  mirrorSecond = false;
    // wanderers (autonomous roaming splats)
    bool  wanderers = true;
    int   wandererCount = 2;
    int   wandererMode = 0;          // 0 random, 1 circle, 2 figure8
    float wandererSpeed = 246.0f;    // px/s
    float wandererScale = 0.1f;      // path size for circle/figure8
    float wandererResumeDelay = 4.5f;// s of no user input before wanderers resume
    float wandererBrightness = 0.1f;
    // screen-fullness governor
    bool  autoPause = true;
    float darkFloor = 9.0f;          // pause when dark area < this %
    float darkLevel = 0.07f;         // pixel counts as dark below this brightness
    float survDarkFloor = 8.0f;      // survivor wanderer's own floor
    float contrastReq = 30.0f;       // % brightest tile must beat the rest (0 = off)
    // separating dart
    bool  dartEnabled = true;
    float dartInterval = 7.0f;       // s between darts (while group paused)
    float dartSpeed = 967.0f;        // px/s
    // hue-shift cycler (post-process palette rotation)
    bool  hsEnabled = true;
    float hsStep = 83.0f;            // degrees per step
    float hsLinger = 6.5f;
    float hsGlide = 7.2f;
    int   hsBurstSteps = 2;
    float hsOffTime = 10.0f;
    // HDR compensation (the WE original's CSS filter, applied when HDR is on)
    bool  hdrCompensation = true;
    float hdrSaturation = 1.2f;
    float hdrBrightness = 1.08f;
    float hdrContrast = 1.0f;
    // mouse
    bool  showMouse = true;          // cursor movement splats
    bool  holdToSplat = true;        // hold LMB on desktop = continuous splat
    // debug / test switches
    bool  gradientMode = false;     // render the M1 HDR test gradient instead
    int   calibratePage = 0;        // >0: render quiz pattern page N (--calibrate N)
    bool  stats = false;            // periodic dye-field readback stats to stdout
    // "Liquid Acid" render look â€” additive; inert unless acid.enabled
    LiquidAcidConfig acid;
    // "Ink in water" render look â€” additive; inert unless ink.enabled
    InkConfig ink;
    // drop emitter â€” style-agnostic; inert unless drops.enabled
    DropConfig drops;
    // screen mirroring / kaleidoscope â€” display-only; inert unless mirror.mode
    MirrorConfig mirror;
    // final composite trim (film grain, aberration vignette) â€” every look
    PostConfig post;
};

// Per-frame input from the app shell (global cursor, desktop focus).
struct FrameInput {
    float mouseX = 0, mouseY = 0;    // px on our monitor
    float mouseDx = 0, mouseDy = 0;  // px delta * 5, reference scaling
    bool  mouseMoved = false;
    bool  mouseDown = false;         // LMB held with desktop focused
    bool  userInteracted = false;    // resets the wanderer resume timer
};

class FluidRenderer {
public:
    void Init(HWND hwnd, int width, int height, const FluidConfig& cfg);
    // Init() and Reattach() for the paths that MUST NOT kill the process.
    // Creating a swap chain can be refused by DXGI for reasons that have
    // nothing to do with us and everything to do with what else owns the
    // screen: a fullscreen game that still holds the output exclusively
    // returns E_ACCESSDENIED, and the resume out of a fullscreen pause fires
    // at exactly the moment that game is handing the output back. That used
    // to reach Fail() and put up a fatal dialog over a black desktop. These
    // return false instead, leaving the renderer fully torn down and safe to
    // try again; the caller backs off and retries.
    bool TryInit(HWND hwnd, int width, int height, const FluidConfig& cfg);
    bool TryReattach(HWND hwnd);
    // Headless capture mode (--shot): device WITHOUT a swap chain, no window.
    // The display pass renders into an FP16 (R16G16B16A16_FLOAT) offscreen RT
    // of the requested size; CaptureOffscreen() reads it back as linear scRGB.
    void InitOffscreen(int width, int height, const FluidConfig& cfg);
    bool IsHeadless() const { return m_headless; }
    // Where the camera rig currently is. Everything in the [post] family hangs
    // off these eight numbers, and all of them are supposed to be MOVING --
    // so a single still says nothing about whether the motion works. --shot
    // prints them, which makes a short series at different t a real test.
    // { lampX, lampY, lensX, lensY, tiltDeg, focus, shiftX, shiftY }
    void RigState(float out[8]) const;
    // brief BU: the lamp-grey region centre (p-units), its target corner
    // (0 TL, 1 TR, 2 BR, 3 BL), whether it is sliding, and the split tone's
    // current pre-multiplied add. --shot prints them beside the rig.
    void BuState(float out[7]) const {
        out[0] = m_greyCx; out[1] = m_greyCy; out[2] = (float)m_greyCorner;
        out[3] = (m_greySlideT >= 0.0f) ? 1.0f : 0.0f;
        out[4] = m_toneAdd[0]; out[5] = m_toneAdd[1]; out[6] = m_toneAdd[2];
    }
    // width*height*4 floats, row-major RGBA, linear scRGB (1.0 = 80 nits).
    bool CaptureOffscreen(std::vector<float>& outRgba);

    // DIAGNOSTIC ONLY (brief A, 2026-09-18): dump the acid sim's CPU state --
    // every blob, every droplet and the whole 64x36 velocity readback -- as a
    // CSV beside a --shot capture. Called only from the headless shot loop and
    // only when FW_ACID_DUMP is set in the environment; nothing in the live
    // path reaches it and it changes no sim state.
    void DumpAcidCsv(const wchar_t* path) const;
    // Deterministic runs: seed every rand()-based behavior. 0 (default) keeps
    // the normal time-seeded startup. Must be set before Init/InitOffscreen.
    static void SetRandomSeed(unsigned seed);
    void Frame(float dtSec, float sdrScale, bool hdrActive, const FrameInput& input);
    void ReassertColorSpace();
    // Full GPU teardown (swapchain, all textures, heaps, PSOs, device): frees
    // all RAM/VRAM. Idempotent, and Init() may be called again afterwards.
    void Shutdown();
    // live tray-menu controls (peakNits already resolved: actual nits, 0 = off)
    void SetHdrOptions(float peakNits, int gamutMode) {
        m_cfg.hdrPeakNits = peakNits;
        m_cfg.gamutMode = gamutMode;
    }
    // live settings-window access; fields are read per frame, so edits apply
    // immediately (resolution fields excluded from the UI â€” they need recreate)
    FluidConfig& Config() { return m_cfg; }
    void ReinitWanderers() { InitWanderers(); }
    // One ink drop at (x, y) PIXELS: a downward velocity impulse plus dye, and
    // optional satellite splash droplets. Any argument left at its sentinel
    // (-1 / nullptr) is taken from FluidConfig::drops, so the scheduler, the
    // settings window and --shot-drop all produce the same drop.
    void InjectDrop(float x, float y, float vx = 0.0f, float vy = -1.0f,
                    float radiusPct = -1.0f, float density = -1.0f,
                    const float* rgb = nullptr, int spatter = -1);
    // Same, but deferred to the start of the next Frame(). Use this from
    // OUTSIDE the render loop (--shot-drop, tray, settings): Splat() records
    // into the frame's command list, which is only open during Frame().
    void QueueDrop(float x, float y, float vy = -1.0f) {
        m_dropQueued = true; m_dropQx = x; m_dropQy = y; m_dropQvy = vy;
    }
    void SetResolutions(int simRes, int dyeRes);   // recreates sim textures live
    // Make the CURRENT [look] renderable without restarting the app.
    // Idempotent and cheap when nothing is missing: the extra display PSOs
    // (LIQUID_ACID / INK) are compiled at device creation only for the look
    // that was enabled then, so a preset or a settings checkbox that turns a
    // look ON later must call this or DisplayPso() silently falls back to the
    // fluid PSO. Waits for GPU idle first (same rule as SetResolutions).
    // Switching a look OFF needs nothing: the PSOs are just not selected, and
    // the fluid path never reads any of their state.
    void EnsureLookResources();
    // Manual pause used to freeze the LAST FRAME on the panel. Present ONE
    // black frame instead, so an OLED left paused goes dark (and stays dark:
    // the shell then stops presenting entirely until the pause is lifted).
    // No-op headless or while the swap chain is gone.
    void PresentBlack();
    void Reattach(HWND hwnd);   // new swapchain after Explorer restart; sim state survives
    // Shared by CreateDevice and Reattach; soft-fails under TryInit/TryReattach.
    bool CreateSwapChainSoft(HWND hwnd, const DXGI_SWAP_CHAIN_DESC1& sd,
                             Microsoft::WRL::ComPtr<IDXGISwapChain1>& out);
    bool PresentBroken() const { return m_presentBroken; }   // window died mid-frame
    // second-monitor mirror
    void EnableMirror(HWND hwnd, int width, int height);
    void DisableMirror();
    bool MirrorActive() const { return m_mirrorChain != nullptr; }
    bool MirrorBroken() const { return m_mirrorBroken; }
    void SetMirrorHdr(float sdrScale, float peakNits) {
        m_mirrorSdrScale = sdrScale;
        m_mirrorPeakNits = peakNits;
    }

    // mood-conductor primitives
    // Directed hue-shift glide to targetDeg over durationSec; holds at the
    // target until ReleaseHueShift. Works even when the cycler is disabled.
    void CommandHueShift(float targetDeg, float durationSec);
    // returnHome=true rotates forward to the next full turn first, then hands
    // the angle back to the scheduled cycler (or zero when hsEnabled=false).
    void ReleaseHueShift(bool returnHome);
    float HueAngleDeg() const { return m_hueAngle; }
    // Run the 1 Hz coverage readback even when the auto-pause governor is off
    // (the mood conductor needs fill % + average hue for its triggers).
    void SetCoverageWanted(bool on) { m_coverageWanted = on; }
    float CoverageDarkPct() const { return m_darkPct; }
    bool ScreenTooFull() const { return m_screenTooFull; }
    float FieldAvgHueDeg() const { return m_avgHue; }   // circular mean hue of lit dye

    // HDR analyzer: parallel low-res render of the FINAL scRGB output
    // (post gamut/peak mapping), read back ~10x/s for the analyzer window.
    static const int kAnaW = 640, kAnaH = 360;
    void EnableAnalyzer(bool on) { m_anaEnabled = on; }
    bool ReadAnalyzerFrame(std::vector<float>& outRgba);  // true if a new frame landed
    IDXGISwapChain3* SwapChain() { return m_swapChain.Get(); }

private:
    struct Tex {
        Microsoft::WRL::ComPtr<ID3D12Resource> res;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        D3D12_GPU_DESCRIPTOR_HANDLE srv = {};
        D3D12_GPU_DESCRIPTOR_HANDLE uav = {};
        int w = 0, h = 0;
    };
    struct DoubleTex { Tex a, b; Tex* read = &a; Tex* write = &b; void Swap() { Tex* t = read; read = write; write = t; } };

    void InitCommon(HWND hwnd, int width, int height, const FluidConfig& cfg);
    // Set while TryInit/TryReattach are running: the swap-chain creation then
    // records its HRESULT in m_initHr and unwinds instead of calling Fail().
    bool     m_softInit = false;
    HRESULT  m_initHr = S_OK;
    void CreateDevice(HWND hwnd, int width, int height);
    void CreateOffscreenTarget();      // headless render target + readback
    // One display/gradient graphics PSO from `src`, optionally with defines.
    // Factored out of CreateDevice so EnsureLookResources() can compile a
    // look's variant later with byte-identical settings.
    void MakeGraphicsPso(const char* src,
                         Microsoft::WRL::ComPtr<ID3D12PipelineState>& pso,
                         const D3D_SHADER_MACRO* defines = nullptr);
    void RenderDisplayOffscreen();     // display pass -> m_shotTex
    // [post] image-space camera pass (kPostSrc). PostActive() says whether
    // this frame routes the display pass through m_postTex; BeginPostTarget()
    // makes that texture the render target and returns its RTV; RunPostPass()
    // then draws the finished frame through the post shader into `dst`.
    bool PostActive() const;
    void EnsurePostTex();
    D3D12_CPU_DESCRIPTOR_HANDLE BeginPostTarget();
    void RunPostPass(D3D12_CPU_DESCRIPTOR_HANDLE dst);
    void CreateSimResources();
    Tex  CreateTex(int w, int h, DXGI_FORMAT fmt, int heapSlot);
    void Transition(Tex& t, D3D12_RESOURCE_STATES to);
    void UavBarrier(ID3D12Resource* res);
    void BeginFrame();
    void EndFrameAndPresent();
    void SimStep(float dt);
    // radiusPct / cap default to cfg.splatRadius / cfg.maxBrightness when < 0,
    // so every existing call site is unchanged.
    // which: bit 0 = velocity pass, bit 1 = dye pass, bit 2 = use the
    // compact (finite-support) dye kernel.
    void SplatImpl(int which, float x, float y, float dx, float dy,
                   float r, float g, float b, float radiusPct, float cap);
    void Splat(float x, float y, float dx, float dy, float r, float g, float b,
               float radiusPct = -1.0f, float cap = -1.0f);
    // The two halves of Splat(), separately. The velocity impulse runs on the
    // SIM grid (256) and the dye stamp on the DYE grid (4096), so a drop can
    // afford several unequal velocity impulses around one dye stamp - which is
    // how the drop gets asymmetric lobes without paying extra dye-res passes.
    void SplatVelocity(float x, float y, float dx, float dy, float radiusPct = -1.0f);
    void SplatDye(float x, float y, float r, float g, float b,
                  float radiusPct = -1.0f, float cap = -1.0f, bool compact = false);
    void MultipleSplats(int amount);
    void RenderDisplay();
    void RenderMirror();
    void BuildDisplayConstants(float out[32]);
    void BuildDisplayConstantsEx(float out[32], int w, int h, float sdrScale, float peakNits);
    void MaybeRenderAnalyzer();
    void CreateAnalyzerResources();
    void RenderGradient(float timeSec);
    void ReportStats();
    void WaitForGpuIdle();
    // M3 behaviors (ports of the reference's custom features)
    void InitWanderers();
    float WheelHue(float offset);
    void PickSplatColor(float hueOffset, float out[3]);
    void UpdateWanderers(float dt);
    void UpdateDart(float dt);
    void UpdateHueShift(float dt);
    void UpdateCoverage();          // schedule/process the 48x27 governor readback
    void ProcessCoverage(const uint8_t* data, UINT pitch);
    void HandleInput(const FrameInput& in);
    // --- Liquid Acid look ---
    void CreateAcidBuffers();       // blob SRV + param CBV upload rings (always)
    void SeedAcidBlobs();           // deterministic under the shot seed
    // Walk the blob population toward `want` without a re-seed: the surplus
    // is marked to shrink away, a shortfall is grown in from under the bottom
    // edge. conserve_mass only; see the brief S residue.
    void AdjustAcidBlobCount(int want);
    void StepAcidBlobs(float dt);   // CPU sim: fluid advection + curl + repulsion
    // Droplet particle sim: nucleation, advection by the oil, attraction,
    // coalescence, dissolution; then the uniform-grid bin the shader reads.
    void StepAcidDroplets(float dt);

    // ---- THE RIG ---------------------------------------------------------
    // One physical assembly -- lamp, lens, focus ring -- stepped once a frame
    // on the CPU and uploaded to the post pass as ONE contiguous block of
    // constants (b3, where the display pass keeps its mirror block; the post
    // pass never binds that one). The lamp's idle drift, the focus/tilt
    // readjustment and, later, the lid ghosts, the flare and the vignette
    // centre are all motions of the SAME object: if they read different
    // numbers they disagree on screen and the illusion is over. So they read
    // these. A later task extends this struct and the block; it must not have
    // to touch the camera code to do it.
    struct CameraRig {
        float lampX = 0.5f, lampY = 1.20f;  // the off-view lamp, uv, drifted
        float axisX = 0.5f, axisY = 0.5f;   // where the optical axis meets the dish
        float tiltAngle = 0.0f;             // radians; direction of the focus gradient
        float tiltAmt   = 0.0f;             // focus depth gained per unit along it
        float focus     = 0.5f;             // focus depth
        float movePhase = 1.0f;             // 0..1 through the current readjustment
        // The LID rests on the same body: a slow idle wander of its own, plus
        // a shove at every readjustment, so its ghosts and sheen travel with
        // the lamp and the focus instead of beside them.
        float lidX = 0.0f, lidY = 0.0f;     // uv offset of the cover
        float lidRot = 0.0f;                // rad; orientation of its sheen
        float shiftX = 0.0f, shiftY = 0.0f; // OLED pixel-shift orbit, px at 1440p
    };
    CameraRig m_rig;
    // Steps the whole rig: the lamp's continuous idle drift (brief Q) and the
    // focus/tilt OCCASIONAL READJUSTMENT (brief R correction) -- deliberately
    // two different kinds of motion on one body, which is what makes it read
    // as a rig somebody is operating rather than a shader.
    void StepCameraRig(float dt);
    float m_camAngleA = 0.0f, m_camAngleB = 0.0f;  // readjustment endpoints
    float m_camFocusA = 0.5f, m_camFocusB = 0.5f;
    float m_camHold = 0.0f;         // s left of the still hold
    float m_camMoveT = -1.0f;       // >= 0 while a readjustment is running
    float m_camMoveDur = 1.0f;
    // Lid offset endpoints for the readjustment shove (eased on the same
    // spring as the focus, so everything lands together).
    float m_lidIdleX = 0.0f, m_lidIdleY = 0.0f, m_lidIdleR = 0.0f;
    float m_lidPrevX = 0.0f, m_lidPrevY = 0.0f, m_lidPrevR = 0.0f;
    float m_lidTargX = 0.0f, m_lidTargY = 0.0f, m_lidTargR = 0.0f;
    bool  m_camInit = false;
    uint32_t m_camRng = 0x9E3779B9u;
    // brief BU: lamp-grey corner state (UploadAcidConstants). m_greyS is the
    // region centre's position along the frame perimeter in p-units (0 = TL,
    // clockwise); a corner change slides it the shorter way round.
    static constexpr float kGreySlideS = 90.0f;
    int   m_greySX = 0, m_greySY = 0;   // hysteretic lamp side, +-1 (0 = unset)
    int   m_greyCorner = -1;
    float m_greyS = 0.0f, m_greySFrom = 0.0f, m_greySDelta = 0.0f;
    float m_greySlideT = -1.0f;         // >= 0 while sliding
    float m_greyLastT = -1.0f;
    float m_greyCx = 0.0f, m_greyCy = 0.0f;
    float m_toneAdd[3] = { 0.0f, 0.0f, 0.0f };
    // A readjustment moves the WHOLE rig, not just the focus: these are the
    // lamp's and the lens centre's endpoints for the current eased move, so
    // they travel on the same spring and arrive together.
    float m_camLampAX = 0.0f, m_camLampAY = 0.0f, m_camLampBX = 0.0f, m_camLampBY = 0.0f;
    float m_camAxAX = 0.0f, m_camAxAY = 0.0f, m_camAxBX = 0.0f, m_camAxBY = 0.0f;
    float m_camLampOX = 0.0f, m_camLampOY = 0.0f;   // current readjust offsets
    float m_camAxOX = 0.0f, m_camAxOY = 0.0f;
    // Blob field + gradient + the local oil's own velocity at one uv point.
    // The CPU twin of the shader's metaball loop, used to keep a trapped
    // droplet inside the oil and to make it ride that oil.
    // outHx/outHy (optional): the same soft-max blend over the NEGATIVE blobs
    // -- the velocity of the dark MASS at this point, which is what a crust
    // bubble rides. Null for every caller that only wants the oil.
    void AcidFieldAt(float x, float y, float aspect, float& outField,
                     float& outGx, float& outGy, float& outVx, float& outVy,
                     float* outHx = nullptr, float* outHy = nullptr) const;
    void UpdateVelocityReadback();  // 64x36 velocity downsample -> CPU (1 frame late)
    void UploadAcidConstants();     // fills this frame's blob + param upload buffers
    void BindAcid();                // root SRV/CBV for the display draw
    // --- "Ink in water" look (shared with Liquid Acid's ink_mode=water) ---
    void UploadInkConstants();      // fills this frame's InkCB upload buffer
    void BindInk();                 // root CBV b2 for the display draw
    // [mirror] root constants (b3) for the display draw, and the CPU twin of
    // the shader's fold used to put a pointer splat where the user sees it.
    // b3 carries the mirror fold AND the [post] final-composite block (film
    // grain, aberration vignette): one root-constants slot, bound on every
    // display draw, shared by all three looks.
    void BuildMirrorConstants(float out[20], int w, int h) const;
    void BindMirrorFold(int w = 0, int h = 0);   // 0 = this monitor's size
    // px in/out: the SOURCE pixel the pointer's screen pixel is showing.
    // flipX/flipY come back as the local d(source)/d(screen) for each axis, so
    // a drag impulse in a reflected copy pushes the dye the way it looks.
    void MirrorMapPointer(float& px, float& py, float& flipX, float& flipY) const;
    void UpdateDrops(float dt);     // the [drops] scheduler (any style)
    // Which display PSO this frame uses. Identical to m_psoDisplay unless one
    // of the extra looks is on AND its variant compiled.
    ID3D12PipelineState* DisplayPso() const {
        if (m_cfg.acid.enabled && m_psoLiquidAcid) return m_psoLiquidAcid.Get();
        if (m_cfg.ink.enabled && m_psoInk)         return m_psoInk.Get();
        return m_psoDisplay.Get();
    }

    FluidConfig m_cfg;
    int m_width = 0, m_height = 0;
    int m_simW = 0, m_simH = 0, m_dyeW = 0, m_dyeH = 0;
    float m_time = 0.0f;
    float m_emitScale = 1.0f;   // dt / (1/60): keeps per-second dye emission fps-independent
    float m_sdrScale = 1.0f;
    float m_globalHue = 0.0f;
    float m_idleTimer = 0.0f;
    float m_statsTimer = 0.0f;
    bool  m_hdrActive = false;
    bool  m_firstFrame = true;
    bool  m_headless = false;   // --shot: no swap chain, no window, no Present

    static const UINT kFrames = 3;
    Microsoft::WRL::ComPtr<IDXGIFactory6> m_factory;
    Microsoft::WRL::ComPtr<ID3D12Device> m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_backBuffers[kFrames];
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocators[kFrames];
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmd;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_fenceValues[kFrames] = {};
    UINT64 m_nextFence = 1;
    UINT m_rtvStride = 0;
    UINT m_frameIndex = 0;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;   // shader visible
    UINT m_srvStride = 0;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_computeRS;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_graphicsRS;
    // compute PSOs
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoClearV, m_psoClear4, m_psoClear1,
        m_psoCurl, m_psoVorticity, m_psoDivergence, m_psoClearPressure, m_psoPressure,
        m_psoGradSub, m_psoAdvectVel, m_psoAdvectDye, m_psoSplatVel, m_psoSplatDye,
        m_psoDownsample, m_psoDiffuseDye;
    // CSSplatDye compiled with DROP_COMPACT: finite-support dye stamp, used
    // only by InjectDrop so a drop leaves nothing parked at the entry.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoSplatDyeCompact;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoDisplay;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoGradient;

    DoubleTex m_velocity, m_dye, m_pressure;
    Tex m_divergence, m_curl;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_readback;  // stats
    UINT m_readbackPitch = 0;
    bool m_readbackPending = false;

    // headless capture target (--shot)
    Microsoft::WRL::ComPtr<ID3D12Resource> m_shotTex;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_shotReadback;
    D3D12_RESOURCE_STATES m_shotState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    UINT m_shotPitch = 0;

    // [post] image-space pass: the display pass renders into m_postTex (RTV
    // slot kFrames*2+1, SRV heap slot 15) and kPostSrc draws it into the real
    // target. m_postGrainDeferred zeroes the display pass's own film grain on
    // exactly the draws the post pass follows, so the grain is applied once,
    // after the blur.
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoPost;
    // brief BN: kPostSrc with BN_OPTICS, built on demand when a BN key is live
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoPostBN;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_postTex;
    D3D12_RESOURCE_STATES m_postState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    D3D12_GPU_DESCRIPTOR_HANDLE m_postSrv = {};
    int  m_postW = 0, m_postH = 0;
    bool m_postGrainDeferred = false;

    // --- M3 state ---
    bool m_prevMouseDown = false;
    bool m_presentBroken = false;

    // second-monitor mirror
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_mirrorChain;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_mirrorBuffers[3];
    int   m_mirrorW = 0, m_mirrorH = 0;
    float m_mirrorSdrScale = 1.0f, m_mirrorPeakNits = 0.0f;
    bool  m_mirrorBroken = false;
    struct Wanderer {
        float x, y, heading, turn;   // random-wander state
        float cx, cy, R, phase;      // circle/figure8 state
        int   dir;
        float hueOffset;
    };
    std::vector<Wanderer> m_wanderers;
    float m_lastInteraction = -1000.0f;

    // coverage governor (48x27 downsample, async readback, 1 Hz)
    static const int kCovW = 48, kCovH = 27;
    Tex m_coverage;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_covReadback;
    UINT   m_covPitch = 0;
    UINT64 m_covFence = 0;
    bool   m_covPending = false;
    float  m_lastCovTime = -10.0f;
    bool   m_screenTooFull = false;
    bool   m_survivorTooFull = false;
    bool   m_coverageWanted = false;   // conductor override: readback w/o governor
    float  m_darkPct = 100.0f;         // last measured dark-area %
    float  m_avgHue = 0.0f;            // circular mean hue (deg) of lit dye

    struct { float x, y, ux, uy, left; bool active = false; } m_dart;
    float m_lastDartTime = -1000.0f;

    // hue-shift cycler: 0 off, 1 glide, 2 linger, 3 return
    int   m_hsPhase = 0;
    float m_hsTimer = 0, m_hsFrom = 0, m_hsTo = 0;
    int   m_hsStep = 0;
    float m_hueAngle = 0;
    bool  m_hsCommanded = false;       // external (conductor) owns the angle
    float m_hsGlideOverride = 0.0f;    // 0 = use cfg.hsGlide

    // --- Liquid Acid look (all inert unless m_cfg.acid.enabled) ---
    static const int kAcidMaxBlobs = 128;
    static const int kVelW = 64, kVelH = 36;
    struct AcidBlob {
        float x, y;          // centre, uv (y down)
        float vx, vy;        // uv / s
        float baseR;         // uv (y units)
        float phase;         // breathing phase
        float breathRate;    // rad / s
        float wgt;           // +1 (oil) or -holeWeight (hole / bubble of water)
        int   colIdx;        // oil palette slot; resolved at upload so a swept
                             // palette reaches the blobs too
        float s1, s2;        // curl-drift phase offsets
        int   kind;          // 0 disc, 1 web, 2 bubble, 3 hole
        // mouse_oil_mode = 2 (comb): how far this blob is currently stretched
        // along `cdx, cdy` (a unit vector in p-space). Ramps up with pointer
        // speed while the comb passes and relaxes back to 0 over ~3 s. 0 for
        // every blob unless the comb is active, and the shader's round path
        // is then taken untouched.
        float comb = 0.0f;
        float cdx = 0.0f, cdy = 0.0f;
        // accent_mode 1: how far this blob is toward the ACCENT shade, and
        // the latched side of the accent_max_r test that drives it. The mix
        // ramps over ~2 s, so a blob that changes class (it respawns at a new
        // radius) crossfades between the two shades instead of popping.
        // -1 = "not evaluated yet": the first step lands on the target
        // directly, so the population does not fade in at t = 0.
        float accentMix   = -1.0f;
        int   accentSmall = -1;
        // CONSERVATION (brief S residue): baseR relaxes toward rTarget so a
        // blob can be grown in or shrunk away instead of appearing and
        // vanishing when the blob_count slider moves. 0 = "retire me".
        // Equal to baseR everywhere else, so nothing relaxes on its own.
        float rTarget = 0.0f;
        // brief AG-b: the dye IDENTITY a hole blob hands the mass it opens,
        // in (0,1). Drawn from a hash of a serial number (never from the sim's
        // own RNG streams, so the motion is untouched), fixed for the blob's
        // life and re-rolled only when it respawns off-screen. Uploaded as
        // AcidBlobGPU.c.w; the shader reads it for negative-weight blobs only.
        float dyeId = 0.5f;
    };
    std::vector<AcidBlob> m_acidBlobs;
    uint32_t m_acidDyeSerial = 0;    // next dye identity serial (brief AG-b)
    uint32_t m_acidDyeSalt   = 0;    // per-seed salt for the identity hash
    float NextAcidDyeId();
    // DIAGNOSTIC ONLY (FW_ACID_DUMP): a copy of the last AcidBlobGPU upload,
    // 12 floats per blob, so DumpAcidCsv can write exactly what the shader
    // saw (breathing radius, stretch, dye identity). Empty unless the env
    // var is set.
    std::vector<float> m_acidBlobGpuCopy;
    bool   m_acidSeeded = false;
    // blob_count, as last requested. Under conserve_mass the population walks
    // toward it rather than being re-seeded in one frame.
    int    m_acidWantBlobs = -1;
    // ---- the hue2 MIX FIELD (brief AE) -----------------------------------
    // Deliberately tiny: the patches the reference shows are a quarter to a
    // half of the frame across, so a 20x12 grid carries them with room to
    // spare and the whole field fits in the acid cbuffer's slack -- no new
    // texture, no new descriptor, nothing added to a root signature that is
    // already at its 64-DWORD limit. Advected semi-Lagrangian on the CPU off
    // the 64x36 velocity readback that the blob sim already keeps.
    static const int kMixW = 20, kMixH = 12;   // kMixH = the VISIBLE rows
    static const int kMixMaxSeed = 8;          // hidden rows below the edge
    std::vector<float> m_mixField;      // kMixW*kMixH, 0..1
    float    m_mixPhase = 0.0f;         // noise phase; jumps on a readjust
    float    m_mixPhaseTarget = 0.0f;
    bool     m_mixSeeded = false;
    void StepHueField(float dt);
    // Private stream for rise_respawn draws. Seeded from the same seed as the
    // population, stepped only by respawns, so a --shot replays exactly and
    // the fluid's own rand() sequence is never touched.
    uint32_t m_acidRespawnRng = 0x9E3779B9u;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_acidBlobUpload[kFrames];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_acidParamUpload[kFrames];
    void*  m_acidBlobData[kFrames] = {};
    void*  m_acidParamData[kFrames] = {};

    // --- droplet particle sim ([liquid_acid] droplets) ------------------
    // Particles rendered INTO the metaball field. Binned into a uniform grid
    // each frame so the pixel shader only walks the 3x3 cells around it; the
    // support radius is clamped to one cell, which is exactly what makes 3x3
    // sufficient (and what caps the per-pixel cost).
    static const int kAcidMaxDrops = 4096;
    static const int kDropGridW = 64, kDropGridH = 36;
    static const int kDropCellCap = 40;     // per-cell entry cap (shader cost bound)
    // Nothing here ever appears or disappears in one frame -- that was the
    // user's "flickering". A droplet is BORN at r = 0 and grows into rt, a
    // dissolving one has rt = 0 and shrinks away, and a coalescence is the
    // same mechanism: the survivor's rt jumps to the area-conserving radius
    // while the absorbed one's rt goes to 0 and its centre is pulled into the
    // survivor, so the metaball union necks the two together continuously
    // instead of one of them being deleted mid-frame.
    struct AcidDrop {
        float x, y;      // centre, uv (y down)
        float r;         // DRAWN radius, uv-y units (relaxes toward rt)
        float rt;        // target radius; 0 = dissolving
        float vx, vy;    // uv / s
        float age;       // s since nucleation
        float out;       // s spent stranded on the wrong side of the interface
        float gate;      // 0..1 contribution scale; see StepAcidDroplets
        int   kind;      // 0 = water trapped in oil (hole), 1 = oil on ink
        int   mergeTo;   // index of the droplet this one is pouring into, else -1
        int   ring;      // 1 = hollow "lens" droplet (drawn as an annulus)
        float seed;      // 0..1, hashed from the birth position; fixed for
                         // life. Drives the ring's out-of-round shape, so a
                         // ring never changes its identity frame to frame.
        float depth;     // 0..1, 0.5 = the masses' plane; hashed at birth from
                         // the same position, so switching droplet_depth on
                         // never moves the population. Drives the circle of
                         // confusion and the depth_rise speed bonus.
        int   touch;     // ring neighbours in CONTACT last frame (raft size cap)
        int   racer;     // 1 = a racing micro-bubble (droplet_racer_frac);
                         // hashed at birth from the position, fixed for life.
        int   crust;     // 1 = a crust bubble ON a dye mass (brief AB): it
                         // rides the mass instead of the water, it is small,
                         // and it clumps without ever coalescing.
        float tauR;      // seconds for r to relax toward rt. 0 = the shared
                         // 0.34 s default; set per droplet by the birth and
                         // death sites when conserve_mass is on.
    };
    std::vector<AcidDrop> m_acidDrops;
    std::vector<int>   m_dropletCellStart;  // kDropGridW*kDropGridH + 1
    std::vector<int>   m_dropletCellCount;
    std::vector<int>   m_dropletOrder;      // droplet indices, sorted by cell
    uint32_t m_dropletRng = 0x1234567u;
    float    m_dropletSpawnAcc = 0.0f;
    int      m_dropletSeededFor = -1;       // population the bulk fill ran for
    Microsoft::WRL::ComPtr<ID3D12Resource> m_dropletUpload[kFrames];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_dropletCellUpload[kFrames];
    void*  m_dropletData[kFrames] = {};
    void*  m_dropletCellData[kFrames] = {};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoLiquidAcid;

    // --- oil_drag / oil_dye_block ---------------------------------------
    // Sim-res coverage mask (1 = under the oil, 0 = open ink) plus the three
    // compute PSOs that build and apply it. Created lazily the first frame
    // either key is non-zero, and NOTHING here is dispatched while both are
    // 0 -- the fluid look never sees a single extra instruction.
    Tex    m_oilMask;
    bool   m_oilMaskMade = false;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoOilMask;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoOilDrag;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoOilDyeBlock;
    void EnsureOilMask();
    void StepOilDrag(float dt);

    // --- mouse_oil_mode: the pointer, in uv, and its own velocity --------
    // Recorded by HandleInput for StepAcidBlobs. This NEVER splats: the oil
    // modes must not touch the ink.
    float m_ptrX = 0.5f, m_ptrY = 0.5f;
    float m_ptrPx = 0.5f, m_ptrPy = 0.5f;   // previous frame
    float m_ptrVx = 0.0f, m_ptrVy = 0.0f;   // uv / s, smoothed
    bool  m_ptrMoved = false, m_ptrHave = false;

    // low-res velocity readback (same async pattern as the coverage governor)
    Tex    m_velLow;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_velReadback;
    UINT   m_velPitch = 0;
    UINT64 m_velFence = 0;
    float  m_lastVelTime = -10.0f;
    bool   m_velPending = false;
    std::vector<float> m_velCpu;    // kVelW*kVelH*2, sim texels / s

    // --- "Ink in water" look + drop emitter -----------------------------
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psoInk;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_inkParamUpload[kFrames];
    void*  m_inkParamData[kFrames] = {};
    float  m_dropTimer = 0.0f;       // s until the next drop
    bool   m_dropPrimed = false;     // first interval drawn yet?
    float  m_dropTailLeft = 0.0f;    // s of dye-only tail still to paint
    float  m_dropTailX = 0, m_dropTailY = 0;
    float  m_dropTailCol[3] = { 1, 1, 1 };
    bool   m_dropQueued = false;     // QueueDrop() pending for the next frame
    float  m_dropQx = 0, m_dropQy = 0, m_dropQvy = -1.0f;

    // HDR analyzer
    bool   m_anaEnabled = false;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_anaTex;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_anaReadback;
    D3D12_RESOURCE_STATES m_anaState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    UINT   m_anaPitch = 0;
    UINT64 m_anaFence = 0;
    bool   m_anaPending = false;
    float  m_lastAnaTime = -10.0f;
};
