// src/ui/keys.inc -- THE key table of the settings window: one X-macro row per ini key.
// Seeded once by tools/gen-keymeta.py from the old src/settings.cpp tables + reference/FEATURES.md;
// hand-maintained from here on (this file is the source of truth). tools/keymeta-check.ps1 validates it.
//
// KEY_SLIDER(label, min, max, step, decimals, floatPtr, intPtr, section, key, reinitWanderers, tip,
//            group, looks, gate, flags, special, front)
// KEY_CHECK (label, boolPtr, section, key, tip, group, looks, gate, flags, special, front)
//   group   G_* (ui_model.h)            looks  which look's render reads it: F fluid, A liquid acid, I ink
//   gate    "sec.key<op>num" joined by && / || (parens ok); pseudo-key look==F|A|I. False -> row disabled
//   flags   KF_MOTION (effect only visible over time, never hidden) | KF_UNVERIFIED (looks/gate not traced)
//           | KF_SUPERSEDED | KF_SHELL (read-only) | KF_MACHINE (shell key, never in a preset, not dirty)
//           | KF_GLOBAL (lives OUTSIDE any mode: the window's right column, never a centre knob)
//   special enum:v=Name|.. | peak | neg:<name> (value<0 shown as name) | zero:<word> | look | autostart
//   front   looks whose front page carries this knob (brief 3B proposal, used by phase 1b)
// Pointers: expressions over `c` (the FluidConfig being edited) or a listed global (ui_model.cpp asserts);
// the cycle keys go through the ui_cycle.h adapter, never the director internals directly.

KEY_SLIDER("Vorticity (swirl strength)", 0, 50, 0.5f, 1, &c.curl, nullptr, "sim", "vorticity", false, "Small-scale swirl. High = cauliflower billows, low = smooth streams",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Form resistance (baroclinic)", 0, 200, 5, 0, &c.baroclinic, nullptr, "sim", "baroclinic", false, "Wakes bend around dye masses instead of cutting through. 0 = off",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Flow speed (all currents)", 0.2f, 2, 0.05f, 2, &c.flowSpeed, nullptr, "sim", "flow_speed", false, "Global current multiplier. Lower = slower evolution, same shapes",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Splat radius", 0.01f, 1, 0.005f, 3, &c.splatRadius, nullptr, "sim", "splat_radius", false, "Size of emitted blobs",
           G_FLUID, "FAI", "", 0, "", "F")
KEY_SLIDER("Density diffusion (dye linger)", 0.95f, 1, 0.0001f, 4, &c.densityDissipation, nullptr, "sim", "density_diffusion", false, "How long dye lingers. Higher = longer trails",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Velocity diffusion", 0.95f, 1, 0.0001f, 4, &c.velocityDissipation, nullptr, "sim", "velocity_diffusion", false, "How long currents persist. Higher = smoother flow",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Pressure diffusion", 0, 1, 0.005f, 3, &c.pressureDissipation, nullptr, "sim", "pressure_diffusion", false, "Flow smoothing. Lower = sharper blob edges",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Pressure iterations", 10, 60, 1, 0, nullptr, &c.pressureIterations, "sim", "pressure_iterations", false, "Solver quality. Rarely needs changing",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Tail decay speed (lower=snappier)", 0.5f, 1, 0.002f, 3, &c.decayFast, nullptr, "sim", "decay_fast", false, "How fast faint haze clears. 1.0 = never (deep layers)",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Decay threshold", 0, 0.3f, 0.002f, 3, &c.decayThreshold, nullptr, "sim", "decay_threshold", false, "Below this brightness the fast decay acts",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Saturation restore /s", 0, 1, 0.005f, 3, &c.satRestore, nullptr, "sim", "saturation_restore", false, "Re-saturates aging dye so old layers stay colorful",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Color intensity cap", 0.3f, 4, 0.05f, 2, &c.maxBrightness, nullptr, "sim", "max_brightness", false, "Max dye brightness (hue-preserving clip)",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Dye diffusion (smoke spread)", 0, 0.5f, 0.005f, 3, &c.dyeDiffusion, nullptr, "sim", "dye_diffusion", false, "Blurs dye. 0 = sharp marbling, high = soft mush",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Gravity (dye sinks / rises)", -200, 200, 1, 1, &c.gravity, nullptr, "sim", "gravity", false, "Dye-weighted gravity. Positive = ink sinks, negative = smoke rises. 0 = off",
           G_FLUID, "FAI", "", 0, "", "I")
KEY_SLIDER("Gravity power (rho^p)", 0.5f, 3, 0.1f, 1, &c.gravityPow, nullptr, "sim", "gravity_pow", false, "Higher = only dense cores fall, thin veils hang",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Gravity smoothing (sim texels)", 0, 12, 0.5f, 1, &c.gravityBlur, nullptr, "sim", "gravity_blur", false, "Blurs the density gravity reads. Low = grid-scale fingering, high = whole lobes sink",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("FPS limit", 30, 260, 1, 0, &c.fpsLimit, nullptr, "general", "fps_limit", false, "Frame rate cap",
           G_SYSTEM, "FAI", "", KF_MACHINE|KF_GLOBAL, "", "")
KEY_SLIDER("Count", 1, 8, 1, 0, nullptr, &c.wandererCount, "behavior", "wanderer_count", true, "Number of autonomous emitters",
           G_FLUID, "FAI", "behavior.wanderers>0", 0, "", "F")
KEY_SLIDER("Speed (px/s)", 50, 1200, 10, 0, &c.wandererSpeed, nullptr, "behavior", "wanderer_speed", false, "Emitter speed = current strength",
           G_FLUID, "FAI", "behavior.wanderers>0", 0, "", "")
KEY_SLIDER("Brightness", 0.05f, 1, 0.01f, 2, &c.wandererBrightness, nullptr, "behavior", "wanderer_brightness", false, "Paint per emitter per step",
           G_FLUID, "FAI", "behavior.wanderers>0", 0, "", "")
KEY_SLIDER("Path size (circle / figure-8)", 0.1f, 0.9f, 0.01f, 2, &c.wandererScale, nullptr, "behavior", "wanderer_scale", true, "Roam area for circle / figure-8 paths",
           G_FLUID, "FAI", "behavior.wanderers>0", 0, "", "")
KEY_SLIDER("Resume after idle (s)", 0, 30, 0.5f, 1, &c.wandererResumeDelay, nullptr, "behavior", "wanderer_resume_delay", false, "Quiet time after your input before emitters resume",
           G_FLUID, "FAI", "behavior.wanderers>0", 0, "", "")
KEY_SLIDER("Min dark area % before pause", 5, 60, 1, 0, &c.darkFloor, nullptr, "behavior", "dark_floor", false, "Emitters pause when dark area falls below this",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Dark pixel cutoff", 0.005f, 0.1f, 0.005f, 3, &c.darkLevel, nullptr, "behavior", "dark_level", false, "Brightness that still counts as dark",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Survivor wanderer dark floor %", 0, 40, 1, 0, &c.survDarkFloor, nullptr, "behavior", "surv_dark_floor", false, "One survivor emitter paints until this darkness",
           G_FLUID, "FAI", "", 0, "", "")
KEY_SLIDER("Contrast required % (0 = off)", 0, 100, 1, 0, &c.contrastReq, nullptr, "behavior", "contrast_req", false, "Require a bright focal region, else emitters pause",
           G_FLUID, "FAI", "", 0, "zero:off", "")
KEY_SLIDER("Interval (s)", 1, 30, 1, 0, &c.dartInterval, nullptr, "behavior", "dart_interval", false, "Seconds between piercing darts",
           G_FLUID, "FAI", "behavior.dart_enabled>0", 0, "", "")
KEY_SLIDER("Speed (px/s)", 500, 6000, 50, 0, &c.dartSpeed, nullptr, "behavior", "dart_speed", false, "Dart travel speed",
           G_FLUID, "FAI", "behavior.dart_enabled>0", 0, "", "")
KEY_SLIDER("Peak brightness (nits, 0 = off)", 0, 1500, 5, 0, &g_hdrPeakNits, nullptr, "hdr", "peak_nits", false, "HDR hot-spot target. 0 = match SDR",
           G_OUTPUT, "FAI", "", KF_GLOBAL, "peak", "FAI")
KEY_SLIDER("Knee (boost starts at)", 0.1f, 1.3f, 0.02f, 2, &c.hdrKnee, nullptr, "hdr", "knee", false, "Dye level where HDR highlight boost begins",
           G_OUTPUT, "FAI", "hdr.compensation>0", KF_UNVERIFIED|KF_GLOBAL, "", "")
KEY_SLIDER("Saturation boost", 1, 2, 0.01f, 2, &c.hdrSaturation, nullptr, "hdr", "saturation", false, "Extra punch while Windows HDR is on",
           G_OUTPUT, "FAI", "hdr.compensation>0", KF_UNVERIFIED|KF_GLOBAL, "", "")
KEY_SLIDER("Brightness boost", 0.8f, 1.5f, 0.01f, 2, &c.hdrBrightness, nullptr, "hdr", "brightness", false, "Extra punch while Windows HDR is on",
           G_OUTPUT, "FAI", "hdr.compensation>0", KF_UNVERIFIED|KF_GLOBAL, "", "")
KEY_SLIDER("Contrast", 0.8f, 1.5f, 0.01f, 2, &c.hdrContrast, nullptr, "hdr", "contrast", false, "Extra punch while Windows HDR is on",
           G_OUTPUT, "FAI", "hdr.compensation>0", KF_UNVERIFIED|KF_GLOBAL, "", "")
KEY_SLIDER("Saturation", 0.5f, 2, 0.01f, 2, &c.postSaturation, nullptr, "color", "post_saturation", false, "WE-style whole-frame filter",
           G_COLOUR, "FAI", "", 0, "", "F")
KEY_SLIDER("Contrast", 0.5f, 2, 0.01f, 2, &c.postContrast, nullptr, "color", "post_contrast", false, "WE-style whole-frame filter",
           G_COLOUR, "FAI", "", 0, "", "")
KEY_SLIDER("Brightness", 0.5f, 1.5f, 0.01f, 2, &c.postBrightness, nullptr, "color", "post_brightness", false, "WE-style whole-frame filter",
           G_COLOUR, "FAI", "", 0, "", "")
KEY_SLIDER("Hue rotate (deg)", 0, 360, 1, 0, &c.postHue, nullptr, "color", "post_hue", false, "Rotates all colors. Warning: rotates outside the hue band",
           G_COLOUR, "FAI", "", 0, "", "")
KEY_SLIDER("Stage dwell (s)", 30, 240, 10, 0, UiCycleDwellPtr(), nullptr, "cycle", "dwell", false, "Seconds on a cycle stage before the next (stages may set their own; 240 = the 4-minute maximum)",
           G_SYSTEM, "FAI", "cycle.enabled>0", KF_MACHINE, "", "")
KEY_SLIDER("Fluid transition (s)", 1, 20, 1, 0, UiCycleTransitionPtr(), nullptr, "cycle", "lerp", false, "Seconds a fluid-to-fluid stage change glides (hue bridge + lerp); look changes fade through black",
           G_SYSTEM, "FAI", "cycle.enabled>0", KF_MACHINE, "", "")
KEY_SLIDER("Timing jitter (± fraction)", 0, 0.5f, 0.05f, 2, UiCycleJitterPtr(), nullptr, "cycle", "jitter", false, "Random +/- on dwell time so switches feel organic",
           G_SYSTEM, "FAI", "cycle.enabled>0", KF_MACHINE, "", "")
KEY_SLIDER("Cycle time (s per lap)", 2, 120, 1, 0, &c.colorCyclePeriod, nullptr, "behavior", "color_cycle_period", false, "Seconds for emitted hue to sweep its band",
           G_COLOUR, "FAI", "color.colorful>0", 0, "", "F")
KEY_SLIDER("Hue band center (deg)", 0, 360, 1, 0, &c.hueCenter, nullptr, "color", "hue_center", false, "Where on the color wheel emission lives (0=red 120=green 240=blue)",
           G_COLOUR, "FAI", "color.colorful>0", KF_UNVERIFIED, "", "F")
KEY_SLIDER("Hue band range (180 = full wheel)", 5, 180, 1, 0, &c.hueRange, nullptr, "color", "hue_range", false, "Half-width of the emission band. 180 = full wheel",
           G_COLOUR, "FAI", "color.colorful>0", KF_UNVERIFIED, "", "F")
KEY_SLIDER("Hue linger (rest at band edges)", 0, 0.45f, 0.05f, 2, &c.hueLinger, nullptr, "color", "hue_linger", false, "Fraction of each half-lap spent resting on one hue. 0 = off",
           G_COLOUR, "FAI", "color.colorful>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Step (deg)", 10, 180, 1, 0, &c.hsStep, nullptr, "behavior", "hueshift_step", false, "Palette rotation per burst step",
           G_COLOUR, "FAI", "behavior.hueshift_enabled>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Linger (s)", 0, 15, 0.5f, 1, &c.hsLinger, nullptr, "behavior", "hueshift_linger", false, "Hold time between burst steps",
           G_COLOUR, "FAI", "behavior.hueshift_enabled>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Glide (s)", 0.1f, 10, 0.1f, 1, &c.hsGlide, nullptr, "behavior", "hueshift_glide", false, "Rotation speed of each step",
           G_COLOUR, "FAI", "behavior.hueshift_enabled>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Steps per burst", 1, 12, 1, 0, nullptr, &c.hsBurstSteps, "behavior", "hueshift_burst_steps", false, "Steps before rotating home",
           G_COLOUR, "FAI", "behavior.hueshift_enabled>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Off time between bursts (s)", 0, 120, 1, 0, &c.hsOffTime, nullptr, "behavior", "hueshift_off_time", false, "Quiet time between hue-shift bursts",
           G_COLOUR, "FAI", "behavior.hueshift_enabled>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Interval (s)", 0.5f, 30, 0.5f, 1, &c.idleInterval, nullptr, "behavior", "idle_interval", false, "Seconds between random blob bursts",
           G_FLUID, "FAI", "behavior.idle_splats>0", 0, "", "")
KEY_SLIDER("Amount per burst", 1, 30, 1, 0, nullptr, &c.idleAmount, "behavior", "idle_amount", false, "Blobs per burst",
           G_FLUID, "FAI", "behavior.idle_splats>0", 0, "", "")
KEY_SLIDER("Burst brightness", 0.2f, 3, 0.05f, 2, &c.idleBrightness, nullptr, "behavior", "idle_brightness", false, "Idle blob intensity (emitters paint at 0.15)",
           G_FLUID, "FAI", "behavior.idle_splats>0", 0, "", "")
KEY_SLIDER("Hump center (input brightness)", 0.05f, 1, 0.01f, 2, &c.curveCenter, nullptr, "color", "curve_center", false, "Response curve shape: glowing rims when enabled",
           G_COLOUR, "FAI", "color.curve_enabled>0", 0, "", "")
KEY_SLIDER("Hump width", 0.02f, 0.5f, 0.01f, 2, &c.curveWidth, nullptr, "color", "curve_width", false, "Response curve shape: glowing rims when enabled",
           G_COLOUR, "FAI", "color.curve_enabled>0", 0, "", "")
KEY_SLIDER("Hump height (output brightness)", 0.1f, 2, 0.05f, 2, &c.curveHeight, nullptr, "color", "curve_height", false, "Response curve shape: glowing rims when enabled",
           G_COLOUR, "FAI", "color.curve_enabled>0", 0, "", "")
KEY_SLIDER("Shadow floor (colour lift)", 0, 0.25f, 0.005f, 3, &c.shadowFloor, nullptr, "color", "shadow_floor", false, "Lift near-black along its own hue so dark marbling stays visible",
           G_COLOUR, "FAI", "", 0, "", "")
KEY_SLIDER("Shadow knee (lift range)", 0.02f, 0.6f, 0.01f, 2, &c.shadowKnee, nullptr, "color", "shadow_knee", false, "Brightness range the lift fades over",
           G_COLOUR, "FAI", "color.shadow_floor>0", 0, "", "")
KEY_SLIDER("Blob count", 16, 128, 1, 0, nullptr, &c.acid.blobCount, "liquid_acid", "blob_count", false, "Oil blobs stepped and evaluated per pixel. Higher = busier and slower",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Big discs (fraction)", 0, 0.5f, 0.01f, 2, &c.acid.discFrac, nullptr, "liquid_acid", "disc_frac", false, "Share of blobs that are big flat discs",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Web blobs (fraction)", 0, 0.6f, 0.01f, 2, &c.acid.webFrac, nullptr, "liquid_acid", "web_frac", false, "Share seeded in chains so they merge into veined webs",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Bubbles (fraction)", 0, 0.8f, 0.01f, 2, &c.acid.bubbleFrac, nullptr, "liquid_acid", "bubble_frac", false, "Share that are small round bubbles. Remainder = holes",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Hole bite (negative weight)", 0, 3, 0.05f, 2, &c.acid.holeWeight, nullptr, "liquid_acid", "hole_weight", false, "How hard negative blobs eat round holes out of the oil",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Bubble size bias (higher = tiny)", 0.5f, 4, 0.1f, 1, &c.acid.sizeBias, nullptr, "liquid_acid", "size_bias", false, "Skews bubble/hole radii toward the small end",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Disc size bias (lower = bigger)", 0.2f, 2, 0.05f, 2, &c.acid.bigBias, nullptr, "liquid_acid", "big_bias", false, "Skews disc/web radii toward the large end. Below 1 = mostly huge",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Surface level (higher = smaller)", 0.1f, 1.2f, 0.01f, 2, &c.acid.threshold, nullptr, "liquid_acid", "threshold", false, "Field level of the oil surface. Higher = tighter, more separate blobs",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Stickiness (support radius)", 1.2f, 4, 0.05f, 2, &c.acid.supportScale, nullptr, "liquid_acid", "support_scale", false, "How far a blob's influence reaches. Higher = blobs bridge from further apart",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Flow gain (fluid drags the oil)", 0, 4, 0.05f, 2, &c.acid.flowGain, nullptr, "liquid_acid", "flow_gain", false, "How strongly the sim's velocity advects the blobs",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Curl drift", 0, 0.01f, 0.0002f, 4, &c.acid.curlDrift, nullptr, "liquid_acid", "curl_drift", false, "Analytic swirl on top, so oil still creeps in still water",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Repulsion (same-sign blobs)", 0, 3, 0.05f, 2, &c.acid.repulsion, nullptr, "liquid_acid", "repulsion", false, "Keeps blobs from collapsing into a single mass",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Rim width", 0.001f, 0.03f, 0.0005f, 4, &c.acid.rimWidth, nullptr, "liquid_acid", "rim_width", false, "Half-width of the dark rim band at the oil edge",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Rim darkness", 0, 1, 0.02f, 2, &c.acid.rimDark, nullptr, "liquid_acid", "rim_dark", false, "How dark the thin rim just inside the oil edge goes",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Rim variation", 0, 1, 0.05f, 2, &c.acid.rimVary, nullptr, "liquid_acid", "rim_vary", false, "Break the rim up: low-frequency noise on its width and on the halo, so it thickens, thins and dies out along the edge",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Rim follows ink", 0, 1, 0.05f, 2, &c.acid.rimInkFollow, nullptr, "liquid_acid", "rim_ink_follow", false, "The bright halo is refracted ink: scale it by how bright the ink just outside the edge is",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Meniscus (bright halo)", 0, 1, 0.02f, 2, &c.acid.meniscus, nullptr, "liquid_acid", "meniscus", false, "Thin bright ink-coloured halo just outside the oil's dark rim",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Meniscus width", 0.0005f, 0.01f, 0.0005f, 4, &c.acid.meniscusW, nullptr, "liquid_acid", "meniscus_width", false, "Half-width of the bright halo",
           G_FILM, "A", "liquid_acid.meniscus>0", 0, "", "")
KEY_SLIDER("Hole swarm (bubbles in the oil)", 0, 1, 0.02f, 2, &c.acid.swarmHoles, nullptr, "liquid_acid", "swarm_holes", false, "Hundreds of water droplets trapped inside the oil",
           G_DROPLETS, "A", "liquid_acid.droplets==0", 0, "", "")
KEY_SLIDER("Droplet swarm (oil on the ink)", 0, 1, 0.02f, 2, &c.acid.swarmDrops, nullptr, "liquid_acid", "swarm_drops", false, "Small oil droplets floating on the open ink",
           G_DROPLETS, "A", "liquid_acid.droplets==0", 0, "", "")
KEY_SLIDER("Swarm clumping", 0, 1, 0.02f, 2, &c.acid.swarmClump, nullptr, "liquid_acid", "swarm_clump", false, "0 = even blanket of droplets, 1 = droplets only in patches",
           G_DROPLETS, "A", "liquid_acid.droplets==0", 0, "", "")
KEY_SLIDER("Swarm density", 0.05f, 1, 0.02f, 2, &c.acid.swarmDensity, nullptr, "liquid_acid", "swarm_density", false, "Fraction of swarm cells that carry a droplet",
           G_DROPLETS, "A", "liquid_acid.droplets==0", 0, "", "")
KEY_SLIDER("Swarm hole size (higher=smaller)", 6, 90, 1, 0, &c.acid.swarmScaleA, nullptr, "liquid_acid", "swarm_scale_holes", false, "Cells per unit for the hole swarm. Higher = smaller, denser holes",
           G_DROPLETS, "A", "liquid_acid.droplets==0", 0, "", "")
KEY_SLIDER("Swarm droplet size (higher=small)", 6, 90, 1, 0, &c.acid.swarmScaleB, nullptr, "liquid_acid", "swarm_scale_drops", false, "Cells per unit for the droplet swarm",
           G_DROPLETS, "A", "liquid_acid.droplets==0", 0, "", "")
KEY_SLIDER("Refraction through the rim", 0, 0.2f, 0.005f, 3, &c.acid.refraction, nullptr, "liquid_acid", "refraction", false, "Shifts the ink seen through the thinning oil near edges",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Oil translucency", 0, 1, 0.02f, 2, &c.acid.translucency, nullptr, "liquid_acid", "translucency", false, "How much the ink underneath modulates the oil fill",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Ink emboss (0 = flat bands)", 0, 1, 0.02f, 2, &c.acid.inkShading, nullptr, "liquid_acid", "ink_shading", false, "How much of the fluid look's pseudo-3D shading survives. The refs are flat",
           G_MASSES, "A", "sim.shading>0", 0, "", "")
KEY_SLIDER("Ink bands (posterise)", 2, 24, 1, 0, &c.acid.inkLevels, nullptr, "liquid_acid", "ink_levels", false, "Flat colour plateaus in the ink. Low = poster, high = smooth",
           G_MASSES, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_SLIDER("Ink band softness", 0.01f, 1.0f, 0.01f, 2, &c.acid.inkSoft, nullptr, "liquid_acid", "ink_soft", false, "0 = hard steps between bands",
           G_MASSES, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_SLIDER("Ink palette strength", 0, 1, 0.02f, 2, &c.acid.inkMix, nullptr, "liquid_acid", "ink_mix", false, "0 = keep the normal fluid colours, 1 = full duotone ramp",
           G_MASSES, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_SLIDER("Ink hue variation (deg)", 0, 90, 1, 0, &c.acid.inkHueVary, nullptr, "liquid_acid", "ink_hue_vary", false, "Rotates the ramp by the dye's own hue so the ink still drifts",
           G_MASSES, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_SLIDER("Complement window (deg)", 5, 180, 1, 0, &c.acid.inkComplementSpan, nullptr, "liquid_acid", "ink_complement_span", false, "How far the ink hue may wander from the oil's opposite. Small = strictly two-hue",
           G_COLOUR, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_SLIDER("Palette sweep (s per cycle, 0=off)", 0, 1800, 5, 0, &c.acid.hueSweepPeriod, nullptr, "liquid_acid", "hue_sweep_period", false, "Cross-fade through the curated vivid palette list. Long settled stretches, short fades. 0 = fixed palette",
           G_COLOUR, "A", "", 0, "zero:off", "")
KEY_SLIDER("Palettes in the sweep list", 1, 12, 1, 0, nullptr, &c.acid.sweepCount, "liquid_acid", "sweep_count", false, "How many entries of the sweep list are used (the shipped list is 5; the tile9 set is 8)",
           G_COLOUR, "A", "liquid_acid.hue_sweep_period>0", 0, "", "")
KEY_SLIDER("Hue rotation (s per turn, 0=off)", 0, 3600, 10, 0, &c.acid.hueRotatePeriod, nullptr, "liquid_acid", "hue_rotate_period", false, "Continuously rotates the whole oil palette's hue. The ink is untouched, so mono ink stays grey and the holes stay black. 0 = off",
           G_COLOUR, "A", "", 0, "zero:off", "A")
KEY_SLIDER("Oil saturation", 0, 2, 0.05f, 2, &c.acid.oilSaturation, nullptr, "liquid_acid", "oil_saturation", false, "Vividness of the oil palette, whichever source it came from. 1 = the authored colours",
           G_COLOUR, "A", "", 0, "", "")
KEY_SLIDER("Film level", 0, 1, 0.02f, 2, &c.acid.filmLevel, nullptr, "liquid_acid", "film_level", false, "Brightness of the flat oil film (the background sheet). 1 = today; ~0.05 = the film pitch black so the dyed masses/droplets carry the colour. The edge light (mass rim, oil glow, halo, lens highlights) keeps the full palette and the mass/droplet dye is not touched",
           G_FILM, "A", "", 0, "", "A")
KEY_SLIDER("Oil fluorescence (lamp)", 0, 1, 0.05f, 2, &c.acid.oilFluor, nullptr, "liquid_acid", "oil_fluor", false, "The lamp makes the oil glow in its own saturated colour, strongest at the film's thin edge and near the lamp, with HDR headroom on that edge band. Added after Film level, so a dimmed film + fluorescence = dark sheet, glowing edges. 0 = today (brief BL)",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Fluorescence reach (screen h)", 0.3f, 2, 0.05f, 2, &c.acid.oilFluorReach, nullptr, "liquid_acid", "oil_fluor_reach", false, "How far from the lamp the oil still fluoresces, in screen heights (Gaussian falloff). Low = only the lamp side glows, high = the whole frame. Inert while Oil fluorescence is 0",
           G_FILM, "A", "liquid_acid.oil_fluor>0", 0, "", "")
KEY_SLIDER("Rise speed (lava lamp, uv/s)", 0, 0.08f, 0.001f, 3, &c.acid.riseSpeed, nullptr, "liquid_acid", "rise_speed", false, "Constant upward drift on every blob. 0.015 = one screen height in about 65 s. 0 = the shipped free drift",
           G_OIL, "A", "", 0, "", "A")
KEY_SLIDER("Rise wobble", 0, 1, 0.05f, 2, &c.acid.riseWobble, nullptr, "liquid_acid", "rise_wobble", false, "A lazy sideways sway on the way up, out of step from blob to blob",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Rise stretch (teardrops)", 0, 1, 0.05f, 2, &c.acid.riseStretch, nullptr, "liquid_acid", "rise_stretch", false, "Moving blobs elongate along their travel and relax round as they slow. Small fast ones stretch most",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Lamp base light", 0, 1, 0.05f, 2, &c.acid.riseBottomLight, nullptr, "liquid_acid", "rise_bottom_light", false, "Brightens the oil near the bottom of the frame and cools it toward the top, like a lamp heated from below",
           G_OPTICS, "A", "", 0, "", "")
KEY_SLIDER("Rise parallax (small = far)", 0, 1, 0.05f, 2, &c.acid.riseParallax, nullptr, "liquid_acid", "rise_parallax", false, "Small blobs rise, sway and follow the water more slowly, as if further away. Scaled by radius against the largest disc",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Parallax dimming", 0, 1, 0.05f, 2, &c.acid.riseParallaxDim, nullptr, "liquid_acid", "rise_parallax_dim", false, "Far (small) blobs are slightly darker as a depth cue. Keep it subtle; past ~0.4 they read as a different palette",
           G_OIL, "A", "liquid_acid.rise_parallax>0", 0, "", "")
KEY_SLIDER("Oil drag on the ink", 0, 1, 0.05f, 2, &c.acid.oilDrag, nullptr, "liquid_acid", "oil_drag", false, "Friction under the oil: ink beneath an island is brought to rest and deflected around its rim, so the water cannot stream in underneath",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Oil blocks the ink", 0, 1, 0.05f, 2, &c.acid.oilDyeBlock, nullptr, "liquid_acid", "oil_dye_block", false, "Ink that ends up under the oil fades out over a second or two, so the oil reads as sitting ON the water. Droplet holes are not oil and keep their ink",
           G_OIL, "A", "", KF_UNVERIFIED, "", "")
KEY_SLIDER("Oil viscosity", 0, 1, 0.05f, 2, &c.acid.oilViscosity, nullptr, "liquid_acid", "oil_viscosity", false, "Thick liquid: blobs lag the water, accelerate and coast slowly, breathe and sway slowly, neck together over seconds, and the droplets stop jittering",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Mouse oil mode", 0, 2, 1, 0, nullptr, &c.acid.mouseOilMode, "liquid_acid", "mouse_oil_mode", false, "What the cursor does to the OIL (never to the ink). 0 = nothing at all, 1 = drags and parts it, 2 = a marbling comb that leaves streaks which round back up over about 3 s",
           G_OIL, "A", "", 0, "enum:0=None|1=Push|2=Comb", "")
KEY_SLIDER("Mouse oil radius (uv)", 0.02f, 0.5f, 0.01f, 2, &c.acid.mouseOilRadius, nullptr, "liquid_acid", "mouse_oil_radius", false, "Reach of the push, or the length of the comb's band along the pointer path",
           G_OIL, "A", "liquid_acid.mouse_oil_mode>0", 0, "", "")
KEY_SLIDER("Mouse oil gain", 0, 3, 0.05f, 2, &c.acid.mouseOilGain, nullptr, "liquid_acid", "mouse_oil_gain", false, "Strength multiplier for the push or the comb",
           G_OIL, "A", "liquid_acid.mouse_oil_mode>0", 0, "", "")
KEY_SLIDER("Final chroma", 0.5f, 2, 0.02f, 2, &c.acid.postChroma, nullptr, "liquid_acid", "post_chroma", false, "Scales the finished frame's colourfulness about its own brightness. About 1.2 restores what a transparent film costs",
           G_COLOUR, "A", "", 0, "", "")
KEY_SLIDER("Final lift", 0.5f, 1.6f, 0.02f, 2, &c.acid.postLift, nullptr, "liquid_acid", "post_lift", false, "Brightness multiplier on the finished frame",
           G_COLOUR, "A", "", 0, "", "")
KEY_SLIDER("Ink ramp gain", 0.3f, 3, 0.05f, 2, &c.acid.inkGain, nullptr, "liquid_acid", "ink_gain", false, "Maps dye brightness onto the ramp. Higher = more bright ink",
           G_MASSES, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_SLIDER("Seam strength (dark edging)", 0, 1, 0.02f, 2, &c.acid.seamStrength, nullptr, "liquid_acid", "seam_strength", false, "Dark seams where the dye gradient is steep (acrylic-pour edging)",
           G_MASSES, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_SLIDER("Seam threshold", 0, 0.5f, 0.01f, 2, &c.acid.seamLo, nullptr, "liquid_acid", "seam_lo", false, "Gradient magnitude a seam starts at",
           G_MASSES, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_SLIDER("Film grain", 0, 0.2f, 0.005f, 3, &c.acid.grainAmt, nullptr, "liquid_acid", "grain", false, "Coarse animated grain over the whole frame",
           G_POST, "A", "post.film_grain==0", 0, "", "")
KEY_SLIDER("Grain coarseness (px)", 1, 6, 0.5f, 1, &c.acid.grainScale, nullptr, "liquid_acid", "grain_scale", false, "Pixels per grain cell. 1 = fine, 4 = chunky macro-film",
           G_POST, "A", "post.film_grain==0 && liquid_acid.grain>0", 0, "", "")
KEY_SLIDER("Grain into shadows", 0, 1, 0.02f, 2, &c.acid.grainShadowW, nullptr, "liquid_acid", "grain_shadow_weight", false, "Weights the grain by (1-luminance)^2 so flat bright oil stays clean",
           G_POST, "A", "post.film_grain==0 && liquid_acid.grain>0", 0, "", "")
KEY_SLIDER("Ink-tinted black toe", 0, 1, 0.02f, 2, &c.acid.toeTint, nullptr, "liquid_acid", "toe_tint", false, "Lifts the darkest pixels toward a dark version of the ink hue instead of neutral black",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Oil edge", 0, 1, 1, 0, nullptr, &c.acid.oilEdgeMode, "liquid_acid", "oil_edge_mode", false, "0 = the film thins out over a wide band that scales with the blob size. 1 = every surface ends on the same hard isoline the droplets do, with only a thin meniscus",
           G_FILM, "A", "", 0, "enum:0=Soft film|1=Crisp", "")
KEY_SLIDER("Accent colour on", 0, 1, 1, 0, nullptr, &c.acid.accentMode, "liquid_acid", "accent_mode", false, "Who may take the palette's 4th shade (the complement in the 80-20 palettes). 0 = whoever drew it, big masses included. 1 = only elements under the accent size, so the frame is one colour with small scattered accents",
           G_OIL, "A", "", 0, "enum:0=Any size|1=Small only", "")
KEY_SLIDER("Accent max size (of disc max)", 0.05f, 1, 0.01f, 2, &c.acid.accentMaxR, nullptr, "liquid_acid", "accent_max_r", false, "How small a blob has to be to carry the accent colour, as a fraction of the biggest disc. Anything larger uses the base shades",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Accent share (of the small ones)", 0, 1, 0.02f, 2, &c.acid.accentFrac, nullptr, "liquid_acid", "accent_frac", false, "Fraction of the small blobs that actually take it, so it stays an accent and not a rule",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Backlight penumbra", 0, 1, 0.02f, 2, &c.acid.oilPenumbra, nullptr, "liquid_acid", "oil_penumbra", false, "The lamp is under the middle of the dish: the oil right beside a black mass is lit less, so it darkens and shifts hue a little over a soft band",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Penumbra width (px at 1440p)", 1, 40, 1, 0, &c.acid.oilPenumbraPx, nullptr, "liquid_acid", "oil_penumbra_px", false, "How far that shading reaches into the oil",
           G_FILM, "A", "liquid_acid.oil_penumbra>0", 0, "", "")
KEY_SLIDER("Penumbra hue shift (deg)", -90, 90, 1, 0, &c.acid.oilPenumbraHue, nullptr, "liquid_acid", "oil_penumbra_hue", false, "Which way the less-lit pigment turns. Arbitrary but consistent for a look",
           G_FILM, "A", "liquid_acid.oil_penumbra>0", 0, "", "")
KEY_SLIDER("Penumbra darkening", 0, 1, 0.02f, 2, &c.acid.oilPenumbraDark, nullptr, "liquid_acid", "oil_penumbra_dark", false, "How much level the band loses as well as hue",
           G_FILM, "A", "liquid_acid.oil_penumbra>0", 0, "", "")
KEY_SLIDER("Cellulose texture", 0, 1, 0.02f, 2, &c.acid.cellulose, nullptr, "liquid_acid", "cellulose", false, "A fibrous, mottled macro texture on the surface: strands of the kind a cell body shows under a microscope, drifting with the rise. 0 = off",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Cellulose on the black", 0, 2, 0.05f, 2, &c.acid.celluloseInk, nullptr, "liquid_acid", "cellulose_ink", false, "How much of it lands on the black side. It fades out deep inside a black mass, so the strands read as a thin layer near its edge and never as a grey wash",
           G_FILM, "A", "liquid_acid.cellulose>0", 0, "", "")
KEY_SLIDER("Cellulose on the film", 0, 2, 0.05f, 2, &c.acid.celluloseOil, nullptr, "liquid_acid", "cellulose_oil", false, "How much of it lands on the coloured film, where it only mottles the colour",
           G_FILM, "A", "liquid_acid.cellulose>0", 0, "", "")
KEY_SLIDER("Cellulose scale (px at 1440p)", 8, 140, 2, 0, &c.acid.celluloseScale, nullptr, "liquid_acid", "cellulose_scale", false, "How long and how far apart the fibres are",
           G_FILM, "A", "liquid_acid.cellulose>0", 0, "", "")
KEY_SLIDER("Cellulose drift", 0, 1, 0.05f, 2, &c.acid.celluloseDrift, nullptr, "liquid_acid", "cellulose_drift", false, "1 = the texture travels with the rise, so it belongs to the masses; 0 = it is pinned to the screen",
           G_MOTION, "A", "liquid_acid.cellulose>0", 0, "", "")
KEY_SLIDER("Edge S-curve", 0, 1, 0.05f, 2, &c.acid.oilEdgeCurve, nullptr, "liquid_acid", "oil_edge_curve", false, "Reshapes the edge ramp from smoothstep to smootherstep, so the slide from oil colour to black arrives without a knee at either end. Same band width",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Soft thin edge (real oil)", 0, 1, 0.02f, 2, &c.acid.oilThinEdge, nullptr, "liquid_acid", "oil_thin_edge", false, "The film thins at its edge: the colour slides toward the ink beneath and the disc fades over a band, instead of a hard cut-out with a stroked rim",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Thin-edge width", 0.02f, 0.40f, 0.01f, 2, &c.acid.oilEdgeFrac, nullptr, "liquid_acid", "oil_edge_frac", false, "That band as a fraction of each blob's own radius",
           G_FILM, "A", "liquid_acid.oil_thin_edge>0", 0, "", "")
KEY_SLIDER("Oil specular", 0, 1, 0.02f, 2, &c.acid.oilSpecular, nullptr, "liquid_acid", "oil_specular", false, "Broad soft highlight off the lens curvature and a slow thickness ripple (the rig is backlit, so keep it low)",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Oil iridescence", 0, 1, 0.02f, 2, &c.acid.oilIrid, nullptr, "liquid_acid", "oil_iridescence", false, "Thin-film hue shimmer indexed by film thickness, strongest at the thin edges",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Droplets as lenses", 0, 1, 0.02f, 2, &c.acid.swarmLens, nullptr, "liquid_acid", "swarm_lens", false, "Trapped droplets become soft-edged holes in the film with a thin-oil fringe and a small highlight, not flat black discs",
           G_DROPLETS, "A", "liquid_acid.droplets==0", 0, "", "")
KEY_SLIDER("Emergent halo (from the ink)", 0, 1, 0.02f, 2, &c.acid.meniscusFromInk, nullptr, "liquid_acid", "meniscus_from_ink", false, "The halo takes the colour and the brightness of the ink just outside the edge, and disappears (with the dark hairline) over dark ink",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("  halo colour from the film", 0, 1, 0.05f, 2, &c.acid.meniscusFilmMix, nullptr, "liquid_acid", "meniscus_film_mix", false, "The bright halo is the widest, strongest thing painted on a droplet's boundary, and its colour comes from the INK -- so no film-hue feature (a second hue, a seam reflect, the sweep) ever reaches it. Raised, the halo takes its colour from the FILM at that droplet instead, the same colour its lit rim already uses, so a hue2 seam colours the halos around it too, and the ink-brightness gate that hides the halo over black ink opens by the same amount -- that gate is there to stop a foreign INK colour landing on black ink, and over black ink it is what makes the halo invisible today. The band keeps its shape, its width and its position; the dark hairline is not touched",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Oil glow outside", 0, 1, 0.02f, 2, &c.acid.oilGlow, nullptr, "liquid_acid", "oil_glow", false, "Diffuse spill of the oil's own colour into the ink around it",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Refraction band width", 0, 40.0f, 1.0f, 1, &c.acid.refractionWidth, nullptr, "liquid_acid", "refraction_width", false, "Rim-width multiplier for the band where the ink is seen bending under the oil (0 = the shipped 7)",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Transparent film", 0, 1, 0.02f, 2, &c.acid.oilTransparency, nullptr, "liquid_acid", "oil_transparency", false, "The oil stops being a fill and becomes a coloured film: the ink's marbling reads through it, tinted by the oil's own hue",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Film absorption", 0, 6.0f, 0.1f, 2, &c.acid.oilAbsorb, nullptr, "liquid_acid", "oil_absorb", false, "How deeply the film absorbs. Low = a pale wash, high = a deep saturated glass",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Film thickness variation", 0, 1, 0.02f, 2, &c.acid.oilFilmBump, nullptr, "liquid_acid", "oil_film_bump", false, "Slow noise on the film's thickness, so it has islands of thick and thin instead of one even pane",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Refraction across the body", 0, 0.04f, 0.002f, 4, &c.acid.oilRefractBody, nullptr, "liquid_acid", "oil_refract_body", false, "Displaces what is seen through the whole film, not only its edge, so the marbling wobbles as it passes under",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Ink out of focus under the oil", 0, 1, 0.02f, 2, &c.acid.oilInkBlur, nullptr, "liquid_acid", "oil_ink_blur", false, "Softens the ink seen through the film, strongest where the film is thickest",
           G_OPTICS, "A", "", KF_UNVERIFIED, "", "")
KEY_SLIDER("Droplets (particles, 0 = off)", 0, 4096, 25, 0, nullptr, &c.acid.droplets, "liquid_acid", "droplets", false, "Simulated droplets rendered INTO the oil surface: water trapped in the oil and oil droplets on the ink. Above 0 the procedural swarms are switched off",
           G_DROPLETS, "A", "", 0, "zero:off", "A")
KEY_SLIDER("Droplet nucleation (per s)", 0, 300, 5, 0, &c.acid.dropletSpawn, nullptr, "liquid_acid", "droplet_spawn_rate", false, "How fast new droplets come out of solution, biased to thin oil near an edge and to shearing flow",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Droplet max radius", 0.002f, 0.03f, 0.001f, 4, &c.acid.dropletRMax, nullptr, "liquid_acid", "droplet_r_max", false, "Largest droplet, as a fraction of frame height. Clamped to one grid cell",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Droplet size bias (higher = tiny)", 1, 6, 0.1f, 1, &c.acid.dropletBias, nullptr, "liquid_acid", "droplet_bias", false, "Heavy tail toward small droplets",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Droplet hole depth", 0, 3, 0.05f, 2, &c.acid.dropletWeight, nullptr, "liquid_acid", "droplet_weight", false, "How hard a trapped water droplet punches through the oil, relative to the LOCAL field so it works in thick oil too",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Droplets on the ink (fraction)", 0, 1, 0.02f, 2, &c.acid.dropletInkFrac, nullptr, "liquid_acid", "droplet_ink_frac", false, "Share of droplets that are oil sitting on the open ink instead of water trapped in the oil",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Droplet life (s)", 0, 600, 10, 0, &c.acid.dropletLife, nullptr, "liquid_acid", "droplet_life", false, "Seconds before a droplet dissolves (shrinking away, never popping). 0 = never",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Droplet attraction", 0, 2, 0.05f, 2, &c.acid.dropletAttract, nullptr, "liquid_acid", "droplet_attract", false, "Short-range pull between droplets of the same kind, which is what makes them find each other and merge",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Droplet coalescence overlap", 0.05f, 0.9f, 0.02f, 2, &c.acid.dropletMerge, nullptr, "liquid_acid", "droplet_merge", false, "Overlap fraction at which two droplets become one, area-conserving",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Hollow ring droplets (fraction)", 0, 1, 0.02f, 2, &c.acid.dropletRingFrac, nullptr, "liquid_acid", "droplet_ring_frac", false, "Share of new trapped droplets drawn as a thin dark RING with the oil showing through the middle -- the empty doubles",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Ring width (x radius)", 0.02f, 0.6f, 0.01f, 2, &c.acid.dropletRingWidth, nullptr, "liquid_acid", "droplet_ring_width", false, "How thick a hollow droplet's dark band is, as a fraction of its radius",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_ring_frac>0", 0, "", "")
KEY_SLIDER("Ring interior lift", 0, 1, 0.02f, 2, &c.acid.dropletRingLift, nullptr, "liquid_acid", "droplet_ring_lift", false, "How much brighter (and slightly thicker) the middle of a hollow droplet reads -- a lens, not a hole",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_ring_frac>0", 0, "", "")
KEY_SLIDER("Ring clumping (bubble rafts)", 0, 1, 0.02f, 2, &c.acid.dropletRingClump, nullptr, "liquid_acid", "droplet_ring_clump", false, "Rings attract each other and pack into rafts with a shared dark wall instead of merging",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_ring_frac>0", 0, "", "")
KEY_SLIDER("Bubble weather (seasons)", 0, 1, 0.05f, 2, &c.acid.weather, nullptr, "liquid_acid", "weather", false, "Slow seasons in the droplet population: spells of more rings, more solids, sparser, denser, so the frame never settles (needs conservation of mass for the density half)",
           G_DROPLETS, "A", "liquid_acid.droplets>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("  weather period (s)", 30, 1200, 10.0f, 0, &c.acid.weatherPeriodS, nullptr, "liquid_acid", "weather_period_s", false, "Mean seconds per weather phase",
           G_DROPLETS, "A", "liquid_acid.droplets>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Second film hue (deg)", -180, 180, 5.0f, 0, &c.acid.filmHue2, nullptr, "liquid_acid", "film_hue2", false, "How far off the palette's own hue the second dye is. Roughly complementary reads like the reference: cyan patches in a magenta film",
           G_COLOUR, "A", "liquid_acid.film_hue2_amt>0", 0, "", "")
KEY_SLIDER("  second hue amount", 0, 1, 0.05f, 2, &c.acid.filmHue2Amt, nullptr, "liquid_acid", "film_hue2_amt", false, "How far the film goes toward that hue inside a patch. 0 = one colour, as before",
           G_COLOUR, "A", "", 0, "", "")
KEY_SLIDER("  patch size", 0.08f, 0.90f, 0.05f, 2, &c.acid.filmHue2Scale, nullptr, "liquid_acid", "film_hue2_scale", false, "Patch size as a fraction of the frame. A quarter to a half is the reference's scale",
           G_COLOUR, "A", "liquid_acid.film_hue2_amt>0", 0, "", "")
KEY_SLIDER("  patch drift", 0, 2, 0.05f, 2, &c.acid.filmHue2Drift, nullptr, "liquid_acid", "film_hue2_drift", false, "How fast the patches wander on their own, on top of being carried by the sim's flow",
           G_COLOUR, "A", "liquid_acid.film_hue2_amt>0", 0, "", "")
KEY_SLIDER("  patch decay", 0, 2, 0.05f, 2, &c.acid.filmHue2Decay, nullptr, "liquid_acid", "film_hue2_decay", false, "How fast the field falls back toward its base. Too low and the second hue spreads into a flat tint over minutes",
           G_COLOUR, "A", "liquid_acid.film_hue2_amt>0", 0, "", "")
KEY_SLIDER("  second hue wobble (deg)", 0, 60, 1.0f, 0, &c.acid.filmHue2Wobble, nullptr, "liquid_acid", "film_hue2_wobble", false, "The contrast hue is not nailed to one angle: it wanders this many degrees either side of it, so a 180 deg pair lives in 170-190 and the combo keeps changing. Two sines in the golden ratio, so it never repeats",
           G_COLOUR, "A", "liquid_acid.film_hue2_amt>0", 0, "", "")
KEY_SLIDER("  wobble period (s)", 10, 1800, 10.0f, 0, &c.acid.filmHue2WobbleP, nullptr, "liquid_acid", "film_hue2_wobble_period", false, "Seconds of the slower of the two wobble sines. It also re-aims on the rig's readjustment, with the lamp and the focus",
           G_COLOUR, "A", "liquid_acid.film_hue2_amt>0", 0, "", "")
KEY_SLIDER("  seed rows below screen", 0, 8, 1.0f, 0, &c.acid.filmHue2SeedRows, nullptr, "liquid_acid", "film_hue2_seed_rows", false, "New patch colour is generated only in this many hidden rows BELOW the bottom edge and rises into view. 0 = the old behaviour, which laid new patches anywhere in the frame",
           G_COLOUR, "A", "liquid_acid.film_hue2_amt>0", 0, "", "")
KEY_SLIDER("  patch rise (screens/min)", 0, 2, 0.05f, 2, &c.acid.filmHue2Rise, nullptr, "liquid_acid", "film_hue2_rise", false, "Extra upward drift of the patches on top of the oil's own rise. Patches can never travel down the screen",
           G_COLOUR, "A", "liquid_acid.film_hue2_amt>0", 0, "", "")
KEY_SLIDER("Third film hue (deg)", -180, 180, 5.0f, 0, &c.acid.filmHue3, nullptr, "liquid_acid", "film_hue3", false, "An optional third hue, taken off the other end of the SAME patch field so it costs nothing extra per pixel",
           G_COLOUR, "A", "liquid_acid.film_hue3_amt>0", 0, "", "")
KEY_SLIDER("  third hue amount", 0, 1, 0.05f, 2, &c.acid.filmHue3Amt, nullptr, "liquid_acid", "film_hue3_amt", false, "0 = off, which is the default",
           G_COLOUR, "A", "", 0, "", "")
KEY_SLIDER("  seam reflect reach", 0, 0.60f, 0.01f, 2, &c.acid.boundaryReflectR, nullptr, "liquid_acid", "boundary_reflect_r", false, "How far a hue2 SEAM reaches into the droplets around it, as a fraction of the screen HEIGHT. 0 = as before: a droplet carries the seam's colour only while it is standing in the seam, about one droplet across. Raised, the lit rims, lens highlights and glow of droplets this far away rotate toward the seam's own hue, fading smoothly so the nearest stay strongest. The film itself never changes and nothing is blurred",
           G_FILM, "A", "liquid_acid.boundary_reflect_amt>0", 0, "", "")
KEY_SLIDER("  seam reflect amount", 0, 1, 0.05f, 2, &c.acid.boundaryReflectAmt, nullptr, "liquid_acid", "boundary_reflect_amt", false, "How much of the seam's own hue a rim right beside it takes. 1 = the full seam colour. Only does anything where the reach above is above 0",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("  crust hue mix", 0, 1, 0.05f, 2, &c.acid.crustHueMix, nullptr, "liquid_acid", "crust_hue_mix", false, "How much of the hue shift the droplets INSIDE a dark mass take. 1 = the same as the film, which is the reference's cyan-lit specks in the black",
           G_DROPLETS, "A", "liquid_acid.droplets>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Bubble crust on the masses", 0, 1, 0.05f, 2, &c.acid.dropletMassBias, nullptr, "liquid_acid", "droplet_mass_bias", false, "How much crust of small bubbles the dye masses carry, as a share of the film population. The film keeps everything it has -- this is added, not swapped",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("  crust density (x film)", 1, 4, 0.1f, 2, &c.acid.dropletCrustDens, nullptr, "liquid_acid", "droplet_crust_density", false, "How dense the crust inside a mass is against the open film. 1 = no crust at all",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("  crust bubble size", 0.05f, 1, 0.05f, 2, &c.acid.dropletCrustR, nullptr, "liquid_acid", "droplet_crust_r", false, "Crust bubbles are small: droplet_r_max times this. They clump but never coalesce",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("  mass rim (lamp side)", 0, 1, 0.05f, 2, &c.acid.massRim, nullptr, "liquid_acid", "mass_rim", false, "A thin bright refracted rim on the lamp side of a mass edge, so a mass reads as a translucent body instead of a flat black cut-out",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Cast shadows", 0, 1, 0.02f, 2, &c.acid.shadowAmt, nullptr, "liquid_acid", "shadow_amt", false, "The lamp finally blocks: every mass and every droplet darkens the film on the side away from it. 0 = the flat, shadowless frame this look had until now",
           G_OPTICS, "A", "", 0, "", "")
KEY_SLIDER("  shadow length (frac of height)", 0, 0.30f, 0.01f, 2, &c.acid.shadowLen, nullptr, "liquid_acid", "shadow_len", false, "How far the longest shadow reaches, as a fraction of the screen height. A caster throws less than this in proportion to its own height, and Lamp z stretches it further",
           G_OPTICS, "A", "liquid_acid.shadow_amt>0", 0, "", "")
KEY_SLIDER("  shadow softness", 0, 1, 0.05f, 2, &c.acid.shadowSoft, nullptr, "liquid_acid", "shadow_soft", false, "How fast the penumbra opens up with distance. 0 keeps the shadow as sharp at its tip as at the caster; 1 has it dissolve",
           G_OPTICS, "A", "liquid_acid.shadow_amt>0", 0, "", "")
KEY_SLIDER("Racing micro-bubbles (share)", 0, 0.5f, 0.01f, 2, &c.acid.dropletRacerFrac, nullptr, "liquid_acid", "droplet_racer_frac", false, "Share of the smallest trapped droplets that race upward, the way uber-small bubbles do in water (0 = off)",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("  racer speed (x)", 1, 5, 0.1f, 2, &c.acid.dropletRacerSpeed, nullptr, "liquid_acid", "droplet_racer_speed", false, "How many times an ordinary droplet's climb a racer makes",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_racer_frac>0", 0, "", "")
KEY_SLIDER("  racer wobble", 0, 2, 0.05f, 2, &c.acid.dropletRacerWobble, nullptr, "liquid_acid", "droplet_racer_wobble", false, "Lateral zigzag / spiral on the way up, as a fraction of the climb",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_racer_frac>0", 0, "", "")
KEY_SLIDER("  racer max radius", 0, 0.01f, 0.0005f, 4, &c.acid.dropletRacerRMax, nullptr, "liquid_acid", "droplet_racer_r_max", false, "Only droplets at or below this radius race (0 = twice droplet_r_min)",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_racer_frac>0", 0, "", "")
KEY_SLIDER("Big hollow bubbles (size x)", 1, 6, 0.1f, 2, &c.acid.dropletRingRMul, nullptr, "liquid_acid", "droplet_ring_r_mul", false, "How many times droplet_r_max a BIG hollow ring may be (1 = today: rings are limited to the solid droplets' size range)",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_ring_frac>0", 0, "", "")
KEY_SLIDER("  big-ring share", 0, 0.3f, 0.005f, 3, &c.acid.dropletRingBigFrac, nullptr, "liquid_acid", "droplet_ring_big_frac", false, "Share of new rings that are big -- keep it small, a couple visible at a time is the point",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_ring_frac>0", 0, "", "")
KEY_SLIDER("Coalescence (touching drops merge)", 0, 1, 0.05f, 2, &c.acid.dropletCoalesce, nullptr, "liquid_acid", "droplet_coalesce", false, "Same-kind solid droplets that touch pour into one round droplet of the combined area instead of parking as a lumpy cluster of lobes (rings keep their foam walls)",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("  neck close (s)", 0, 4, 0.1f, 2, &c.acid.dropletCoalesceS, nullptr, "liquid_acid", "droplet_coalesce_s", false, "Seconds the coalescence takes (0 = the old 0.18 s snap)",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_coalesce>0", 0, "", "")
KEY_SLIDER("Conservation of mass", 0, 1, 1.00f, 0, &c.acid.conserveMass, nullptr, "liquid_acid", "conserve_mass", false, "Nothing appears or vanishes at a visible size in the open: droplets enter and leave across the frame edges, blobs wrap only once their field is clear of the frame, and births swell from below the visible floor",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("  spawn grow (s)", 0, 20, 0.5f, 1, &c.acid.spawnGrowS, nullptr, "liquid_acid", "spawn_grow_s", false, "Seconds a new droplet takes to swell from nothing to full size (0 = the old 0.34 s pop)",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("  dissolve (s)", 0, 20, 0.5f, 1, &c.acid.dissolveS, nullptr, "liquid_acid", "dissolve_s", false, "Seconds a droplet dying of old age takes to shrink away (0 = the old 0.34 s)",
           G_OIL, "A", "", 0, "", "")
KEY_SLIDER("Ring out-of-round", 0, 1, 0.05f, 2, &c.acid.dropletRingWobble, nullptr, "liquid_acid", "droplet_ring_wobble", false, "Stops a ring being a perfect circle: a seeded ellipse whose axis turns over a minute or two, plus a breathing 3-lobe wobble, and a wall that thins where the ring bulges. 0 = perfect circles",
           G_DROPLETS, "A", "liquid_acid.droplets>0 && liquid_acid.droplet_ring_frac>0", 0, "", "")
KEY_SLIDER("Droplet depth spread", 0, 1, 0.05f, 2, &c.acid.dropletDepth, nullptr, "liquid_acid", "droplet_depth", false, "How far droplets and rings are spread in front of and behind the masses' own plane. 0 = everything at one depth, which is what the camera saw before. Needs [post] dof_max_px to be visible",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Back layer rise bonus", 0, 1, 0.05f, 2, &c.acid.depthRise, nullptr, "liquid_acid", "depth_rise", false, "The far layer climbs faster than the near one, so depth reads in the motion as well as in the blur. Symmetric, so the average rise speed is unchanged",
           G_DROPLETS, "A", "", 0, "", "")
KEY_SLIDER("Diffraction (size vs the point spread)", 0, 1, 0.05f, 2, &c.acid.diffraction, nullptr, "liquid_acid", "diffraction", false, "Light bends round anything narrow, so a feature smaller than the lens's point spread cannot reach full darkness: tiny droplets come out as soft grey dots, medium ones dark with soft edges, only the big masses go truly black. Never lifts the black under a mass",
           G_DROPLETS, "A", "", 0, "", "")
KEY_SLIDER("Diffraction width (px at 1440p)", 0.2f, 8, 0.1f, 2, &c.acid.diffractionPx, nullptr, "liquid_acid", "diffraction_px", false, "How wide that point spread is. A droplet about this radius comes out at two thirds of its full darkness; anything several times it is unaffected",
           G_DROPLETS, "A", "", 0, "", "")
KEY_SLIDER("Droplet lens shading", 0, 1, 0.05f, 2, &c.acid.dropletLens, nullptr, "liquid_acid", "droplet_lens", false, "Every droplet is a lens over the backlight: a thin bright refractive rim hugging its edge, a darker band just inside, a lighter middle, and a small specular. Crisp at the edge, gradual inside -- it adds no blur at all. 0 = flat fills",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Lens centre lift", 0, 1, 0.05f, 2, &c.acid.dropletLensCentre, nullptr, "liquid_acid", "droplet_lens_centre", false, "How much lighter the middle of a droplet is than its shoulder. A hole lifts toward the film colour, an oil droplet toward the backlight. Dies out where the surface stops being curved, so a big mass stays flat and its black stays black",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Lens band width (px at 1440p)", 0.5f, 12, 0.25f, 2, &c.acid.dropletLensBand, nullptr, "liquid_acid", "droplet_lens_band", false, "Width of the dark inner band and of the bright rim just outside it. Authored in pixels and floored by band_min, never scaled by the element, so a 4-px droplet carries the same crisp rim a big mass does",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Droplet specular", 0, 1, 0.05f, 2, &c.acid.dropletSpec, nullptr, "liquid_acid", "droplet_spec", false, "A small highlight on the side of each droplet facing the lamp. It reads the same rig the haze and the bloom do, so it swings when the lamp moves instead of sitting still",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "", "")
KEY_SLIDER("Dye layer depth", 0, 1, 0.02f, 2, &c.acid.dyeDepth, nullptr, "liquid_acid", "dye_depth", false, "Where the dye masses float. This used to be pinned to the focus distance, so a big mass sat on the plane of focus by definition and could never go soft on its own. Move it off Focus depth and the masses leave focus like everything else",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Dye layer slope", 0, 1, 0.02f, 2, &c.acid.dyeDepthTilt, nullptr, "liquid_acid", "dye_depth_tilt", false, "Tips the dye layer along the direction of the lamp, so it is not parallel to the focus surface: the two cross on a line instead of agreeing over a whole region, and that line travels as the lamp drifts",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Dye depth weight", 0.05f, 2, 0.05f, 2, &c.acid.dyeDepthW, nullptr, "liquid_acid", "dye_depth_w", false, "How much the dye layer counts against the droplets where they overlap. Higher pulls a droplet's focus toward the film it sits in; 0.25 is the original blend",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Dye hue (deg)", 0, 360, 5, 0, &c.acid.dyeHue, nullptr, "liquid_acid", "dye_hue", false, "The colour the dark masses take. With Dye follows film on, this is an OFFSET from the film's own hue, so the two turn together; with it off it is an absolute hue. 0..360 covers the circle either way",
           G_MASSES, "A", "", 0, "", "A")
KEY_SLIDER("Dye saturation", 0, 1, 0.05f, 2, &c.acid.dyeSat, nullptr, "liquid_acid", "dye_sat", false, "How coloured the dark masses are. The full 0..1 is useful here because the dye sits at a very low value: even fully saturated it reads as a deep wax, not as a bright fill. 0 turns the dye OFF (today's neutral black), it is not a grey dye",
           G_MASSES, "A", "", 0, "", "A")
KEY_SLIDER("Dye brightness", 0, 0.50f, 0.02f, 2, &c.acid.dyeLum, nullptr, "liquid_acid", "dye_lum", false, "How much lamp the THIN edge of a mass passes; the thick core keeps about 40% of it, which is what reads as translucent wax. Range from the dye4 sheet: under ~0.10 the mass is still black, 0.28-0.34 is the deep wax, and past ~0.45 the mass stops reading as dark at all. 0 = today's black",
           G_MASSES, "A", "", 0, "", "A")
KEY_SLIDER("Dye follows film hue", 0, 1, 1, 0, nullptr, &c.acid.dyeHueFollow, "liquid_acid", "dye_hue_follow", false, "1 = Dye hue is an offset from the film's current hue, so the dye rotates with it under hue_rotate_period and the sweep and the pair stays designed. 0 = a fixed absolute hue",
           G_MASSES, "A", "", 0, "enum:0=Fixed hue|1=Follows film hue", "")
KEY_SLIDER("Dye on masses", 0, 1, 0.05f, 2, &c.acid.dyeMasses, nullptr, "liquid_acid", "dye_masses", false, "How much dye the big masses (the gaps between oil blobs) take. 1 = today; 0 = masses pitch black while the droplets can still carry a dye of their own",
           G_MASSES, "A", "", KF_SUPERSEDED, "", "")
KEY_SLIDER("Dye on droplets", 0, 1, 0.05f, 2, &c.acid.dyeDroplets, nullptr, "liquid_acid", "dye_droplets", false, "How much dye the small droplet holes take. 1 = today (every droplet dyed like a mass, which reads 'bubbly'); 0 = droplets stay black and only the big masses carry colour. A droplet within a few px of a mass shares the mass colour (more with Dye smoke)",
           G_MASSES, "A", "", KF_SUPERSEDED, "", "")
KEY_SLIDER("Droplet dye hue (deg)", -5, 360, 5, 0, &c.acid.dyeDropHue, nullptr, "liquid_acid", "dye_droplet_hue", false, "The droplets' own dye hue, so masses and droplets can be two colours (main + accent). Below 0 = same as Dye hue. Follows the film like Dye hue when Dye follows film is on",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "neg:inherit", "A")
KEY_SLIDER("Droplet dye saturation", -0.05f, 1, 0.05f, 2, &c.acid.dyeDropSat, nullptr, "liquid_acid", "dye_droplet_sat", false, "The droplets' own dye saturation. Below 0 = same as Dye saturation; 0 = droplets black",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "neg:inherit", "A")
KEY_SLIDER("Droplet dye brightness", -0.02f, 0.50f, 0.02f, 2, &c.acid.dyeDropLum, nullptr, "liquid_acid", "dye_droplet_lum", false, "The droplets' own dye level, same scale as Dye brightness. Below 0 = same as Dye brightness; 0 = droplets black",
           G_DROPLETS, "A", "liquid_acid.droplets>0", 0, "neg:inherit", "A")
KEY_SLIDER("Dye smoke", 0, 1, 0.05f, 2, &c.acid.dyeSmoke, nullptr, "liquid_acid", "dye_smoke", false, "0 = today's wax: the dye stops hard at the mass edge and is brightest at the thin rim. Up = the dye thickens gradually into the mass, leaks a little out under the thin film and breaks into slow wisps, so it reads as smoke in the body rather than a tinted disc. Masses only; droplets keep their own dye. Needs Dye brightness > 0",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Dye follows the lamp", 0, 1, 0.05f, 2, &c.acid.dyeLampFollow, nullptr, "liquid_acid", "dye_lamp_follow", false, "1 = today: the mass dye is lit by the lamp ramp and brightest at its thin edge. 0 = the dye ignores the lamp and the edge profile and sits at its core level everywhere -- a flat, dim ambient colour the lamp never lifts (brief BL). Inert while Dye brightness is 0",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Dark saturation (punch)", 0, 1, 0.05f, 2, &c.acid.darkSat, nullptr, "liquid_acid", "dark_sat", false, "Darker must mean MORE saturated: raises the chroma of the film, the masses and the droplets as their own level falls (k = 1 + value x (1 - level)), keeping their luminance exactly, clipped only at the gamut edge. 0 = today; inert on an element at level 1 (brief BP)",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Dye brightness spread", 0, 0.50f, 0.02f, 2, &c.acid.dyeLumVary, nullptr, "liquid_acid", "dye_lum_vary", false, "So the masses are not all one wax: the dye level becomes Dye brightness x (1 +- this). A mass a hole carved keeps one shade for its life; a big gap between oil blobs gets a slow gradient (about a quarter screen across) that rises with it. Subtle: 0.5 moves the mass level ~+-20%. 0 = today. Masses only; inert while Dye brightness is 0 (brief AG-b)",
           G_MASSES, "A", "liquid_acid.dye_lum>0", 0, "", "")
KEY_SLIDER("Dye hue spread (deg)", 0, 60, 1, 0, &c.acid.dyeHueVary, nullptr, "liquid_acid", "dye_hue_vary", false, "Hue spread: the dye sits at Dye hue +- up to this many degrees, per carved mass or as a slow gradient across a big gap (rises with it, no flicker). 25 = a hint, 50 = clearly two-tone; past ~60 it turns orange/teal mud (sheet). 0 = today. Masses only; inert while Dye brightness is 0 (brief AG-b)",
           G_MASSES, "A", "liquid_acid.dye_lum>0", 0, "", "")
KEY_SLIDER("Dye hue with thickness (deg)", -90, 90, 1, 0, &c.acid.dyeThickHue, nullptr, "liquid_acid", "dye_thick_hue", false, "Hue turns with depth into a mass, like real dye density: the thin edge keeps Dye hue and the thick core reaches Dye hue + this. Small masses stay edge-coloured, big ones get a core. 0 = today; inert while Dye brightness is 0 (brief AG-b)",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Dye core density", 0.42f, 1, 0.02f, 2, &c.acid.dyeCore, nullptr, "liquid_acid", "dye_core", false, "How bright the thick core of a mass is next to its thin rim. 0.42 = today (the rim is the brightest part, the core ~42% of it: a lit wash); 1 = an even, opaque fill that reads as dense pigment. Pulls the mean level up with it, so lower Dye brightness to hold it. Less effect with Dye smoke up; with Dye follows the lamp at 1 the lamp ramp stays. Inert while Dye brightness is 0 (brief BR)",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Lamp grey (far corner)", 0, 1, 0.05f, 2, &c.acid.lampGrey, nullptr, "liquid_acid", "lamp_grey", false, "As if the dye far from the lamp gets less light: the corner diagonally opposite the lamp is desaturated (1 = 60% at the corner, fading to none over Lamp grey size), luminance kept, so it greys without darkening. One corner at a time; when the lamp crosses to the other side the region slides along the frame edge to the new far corner over ~90 s. Never the centre. Shows only where film or lit dye sits in that corner. 0 = off (brief BU)",
           G_OPTICS, "A", "", KF_MOTION, "", "")
KEY_SLIDER("Lamp grey size (screen h)", 0.25f, 0.60f, 0.05f, 2, &c.acid.lampGreySize, nullptr, "liquid_acid", "lamp_grey_size", false, "Radius of the grey corner region in screen heights: full strength over the inner third, gone at this radius. 0.4 = about a rule-of-thirds corner cell; 0.6 reaches the frame middle band while it slides along the long edge (brief BU)",
           G_OPTICS, "A", "liquid_acid.lamp_grey>0", 0, "", "")
KEY_SLIDER("Lamp grey cool", 0, 1, 0.05f, 2, &c.acid.lampGreyCool, nullptr, "liquid_acid", "lamp_grey_cool", false, "How much of the grey is also a cool white balance (luminance kept). 0 = neutral grey; 1 = a blue-grey at the corner. Subtle: +1..3 deg of hue at 0.5 (brief BU)",
           G_OPTICS, "A", "liquid_acid.lamp_grey>0", 0, "", "")
KEY_SLIDER("Shadow tone (split tone)", 0, 1, 0.05f, 2, &c.acid.shadowTone, nullptr, "liquid_acid", "shadow_tone", false, "Tints the black masses and droplets with the complement of the main film hue (OKLab opposite, turns with Hue rotation), lifting black by up to Shadow tone lift. Film, lit dye and rims untouched. Switches itself on and off (half the time) on its own slow clock, out of step with the hue. Gives up true black on the masses. Multiplies with the lift (0.5 x lift 0.12 = 1 x lift 0.06). 0 = off (brief BU)",
           G_POST, "A", "", KF_MOTION, "", "")
KEY_SLIDER("Shadow tone lift", 0, 0.15f, 0.01f, 2, &c.acid.shadowToneLift, nullptr, "liquid_acid", "shadow_tone_lift", false, "How far black is lifted at full tone, fraction of SDR white (sRGB-encoded). 0.06 = a deep tint (masses +0.025 luma), 0.12 = clearly coloured bodies (+0.047). Inert while Shadow tone is 0 (brief BU)",
           G_POST, "A", "liquid_acid.shadow_tone>0", 0, "", "")
KEY_SLIDER("Shadow tone saturation", 0, 1, 0.05f, 2, &c.acid.shadowToneSat, nullptr, "liquid_acid", "shadow_tone_sat", false, "Saturation of the shadow tint. 0 = a neutral grey lift; 0.8 = the complement, rich. Inert while Shadow tone is 0 (brief BU)",
           G_POST, "A", "liquid_acid.shadow_tone>0", 0, "", "")
KEY_SLIDER("Shadow tone period (s)", 0, 3600, 25, 0, &c.acid.shadowTonePeriod, nullptr, "liquid_acid", "shadow_tone_period", false, "One on+off cycle of the shadow tone, seconds (on for half, starting ON). 2225 = the hue period / golden ratio, so the tint never meets the same hue twice (a 3:5 gear, 2160, repeats every 3.25 h). 0 = always on. Inert while Shadow tone is 0 (brief BU)",
           G_POST, "A", "liquid_acid.shadow_tone>0", KF_MOTION, "zero:always on", "")
KEY_SLIDER("Shadow tone fade (s)", 0, 600, 10, 0, &c.acid.shadowToneFade, nullptr, "liquid_acid", "shadow_tone_fade", false, "Length of each on/off crossfade, seconds (smoothstep). Minutes, so the switch is never seen. Inert while Shadow tone is 0 (brief BU)",
           G_POST, "A", "liquid_acid.shadow_tone>0", KF_MOTION, "", "")
KEY_SLIDER("Shadow tone hue (deg)", -5, 360, 5, 0, &c.acid.shadowToneHue, nullptr, "liquid_acid", "shadow_tone_hue", false, "Leftmost = complement: the tint is the OKLab opposite of the main film hue and turns with it (default). 0..360 = a FIXED hue in degrees that does not rotate: 275 violet, 235 blue, 185 teal. Inert while Shadow tone is 0 (brief BU/BV)",
           G_POST, "A", "liquid_acid.shadow_tone>0", 0, "neg:complement", "")
KEY_SLIDER("Interface speckle", 0, 1, 0.02f, 2, &c.acid.speckle, nullptr, "liquid_acid", "speckle", false, "Sparse dark dots hugging the oil/ink boundary",
           G_MASSES, "A", "", 0, "", "")
KEY_SLIDER("Oil HDR level (0 = follow ink)", 0, 1.4f, 0.02f, 2, &c.acid.oilHdr, nullptr, "liquid_acid", "oil_hdr", false, "Drives the HDR highlight gain for oil pixels. 0 = inherit the ink's",
           G_FILM, "A", "", 0, "", "")
KEY_SLIDER("Ink under the oil", 0, 1, 1, 0, nullptr, &c.acid.inkMode, "liquid_acid", "ink_water", false, "1 = the shared ink-in-water render (translucent veils) instead of flat bands",
           G_MASSES, "A", "", 0, "enum:0=Bands|1=Water", "")
KEY_SLIDER("Ink density k", 0.5f, 8, 0.1f, 1, &c.ink.density, nullptr, "ink", "density", false, "Beer-Lambert absorption. High = thin veils already opaque",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "I")
KEY_SLIDER("Ink chroma (0 = neutral black)", 0, 3, 0.05f, 2, &c.ink.chroma, nullptr, "ink", "chroma", false, "How much the dye's own hue tints the transmitted light",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "I")
KEY_SLIDER("Edge darkening (folds)", 0, 1, 0.02f, 2, &c.ink.edgeStrength, nullptr, "ink", "edge_strength", false, "Extra optical path where the density gradient is steep = sheets seen edge-on",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Edge threshold (lo)", 0, 0.5f, 0.005f, 3, &c.ink.edgeLo, nullptr, "ink", "edge_lo", false, "Gradient magnitude the fold darkening starts at",
           G_INK, "AI", "(look==I || liquid_acid.ink_water>0) && ink.edge_strength>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Edge threshold (hi)", 0, 0.5f, 0.005f, 3, &c.ink.edgeHi, nullptr, "ink", "edge_hi", false, "Gradient magnitude the fold darkening saturates at",
           G_INK, "AI", "(look==I || liquid_acid.ink_water>0) && ink.edge_strength>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Edge tap spacing (px)", 1, 6, 0.5f, 1, &c.ink.edgeScale, nullptr, "ink", "edge_scale", false, "Screen texels between the gradient taps",
           G_INK, "AI", "(look==I || liquid_acid.ink_water>0) && ink.edge_strength>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Paper vignette", 0, 0.6f, 0.02f, 2, &c.ink.vignette, nullptr, "ink", "vignette", false, "Radial darkening of the backlit paper (paper mode only)",
           G_INK, "AI", "(look==I || liquid_acid.ink_water>0) && ink.inverted==0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Core knee (inverted)", 0, 1, 0.02f, 2, &c.ink.coreKnee, nullptr, "ink", "core_knee", false, "Opacity where the tint crosses from the thin-veil colour to the core colour",
           G_INK, "AI", "(look==I || liquid_acid.ink_water>0) && ink.inverted>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Veil floor (inverted)", 0, 0.6f, 0.02f, 2, &c.ink.veilFloor, nullptr, "ink", "veil_floor", false, "Faint dye below this opacity goes to the background instead of a haze",
           G_INK, "AI", "(look==I || liquid_acid.ink_water>0) && ink.inverted>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Midpoint dip to dark", 0, 1, 0.05f, 2, &c.ink.tintMidDip, nullptr, "ink", "tint_mid_dip", false, "Darken the duotone blend around its midpoint so mid-density ink is dark, not grey-brown",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Duotone pair rotation (s, 0=off)", 0, 900, 10, 0, &c.ink.pairSweepPeriod, nullptr, "ink", "pair_sweep_period", false, "Cross-fade tint_thin/tint_thick through the curated complementary pairs. 0 = the fixed pair",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "zero:off", "I")
KEY_SLIDER("HDR core (inverted)", 0, 1.4f, 0.02f, 2, &c.ink.hdrCore, nullptr, "ink", "hdr_core", false, "How hard dense cores drive the HDR highlight gain. Veils stay SDR",
           G_INK, "AI", "(look==I || liquid_acid.ink_water>0) && ink.inverted>0", KF_UNVERIFIED, "", "I")
KEY_SLIDER("HDR motion gate: still (texels/s)", 0, 60, 1, 0, &c.ink.motionLo, nullptr, "ink", "motion_lo", false, "Below this local speed, ink gets no HDR lift at all - keeps the entry patch from blowing out",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("HDR motion gate: moving", 0, 200, 5, 0, &c.ink.motionHi, nullptr, "ink", "motion_hi", false, "Above this local speed the HDR lift is full. Set at or below the still value to disable the gate",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Motion gate on opacity", 0, 1, 0.02f, 2, &c.ink.motionOpacity, nullptr, "ink", "motion_opacity", false, "How much the motion gate also thins stationary ink. 0 = HDR lift only",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Parallax second layer", 0, 1, 0.02f, 2, &c.ink.parallax, nullptr, "ink", "parallax", false, "Adds the same dye at another scale as extra depth. 0 = off",
           G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Parallax scale", 0.8f, 1, 0.005f, 3, &c.ink.parallaxScale, nullptr, "ink", "parallax_scale", false, "How much bigger the second layer reads",
           G_INK, "AI", "(look==I || liquid_acid.ink_water>0) && ink.parallax>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Interval (s)", 2, 90, 1, 0, &c.drops.interval, nullptr, "drops", "interval", false, "Seconds between ink drops (jittered +-35%)",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "I")
KEY_SLIDER("Entry speed (downward)", 0, 3000, 25, 0, &c.drops.speed, nullptr, "drops", "speed", false, "Downward velocity impulse. This is what rolls the head into a cap",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Drop radius (%)", 0.02f, 2, 0.01f, 2, &c.drops.radius, nullptr, "drops", "radius", false, "Splat radius of the drop head, same units as splat radius",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Drop density", 0.1f, 4, 0.05f, 2, &c.drops.density, nullptr, "drops", "density", false, "Dye intensity of the head. 1.35 = a fully opaque core",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Entry trail (s)", 0, 4, 0.1f, 1, &c.drops.tailSec, nullptr, "drops", "tail_sec", false, "How long dye keeps feeding in at the entry point after the impulse",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Entry trail density", 0, 1, 0.02f, 2, &c.drops.tailDensity, nullptr, "drops", "tail_density", false, "Intensity of that trail",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Splash droplets", 0, 12, 1, 0, nullptr, &c.drops.spatter, "drops", "spatter", false, "Satellite droplets around the entry (ref 2). Each one costs a full dye pass",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Splash radius (%)", 0.01f, 0.4f, 0.005f, 3, &c.drops.spatterRadius, nullptr, "drops", "spatter_radius", false, "Size of each satellite droplet",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Splash speed", 0, 2000, 25, 0, &c.drops.spatterSpeed, nullptr, "drops", "spatter_speed", false, "Outward impulse of the satellites",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Entry stream width (x drop radius)", 0.05f, 1, 0.01f, 2, &c.drops.tailRadiusFrac, nullptr, "drops", "tail_radius_frac", false, "Keep this small: a wide entry stamp reads as a bright orb parked at the injection point",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Entry stream pull (x drop speed)", 0, 1, 0.02f, 2, &c.drops.tailSpeed, nullptr, "drops", "tail_speed", false, "Downward impulse on the entry stream so it feeds the stem instead of parking",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Impulse spread (x drop radius)", 0.5f, 4, 0.05f, 2, &c.drops.impulseSpread, nullptr, "drops", "impulse_spread", false, "How much wider the velocity impulse is than the dye stamp. Below ~1.5 the dye's outer halo parks at the entry as a bright orb",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Lobe asymmetry", 0, 1, 0.02f, 2, &c.drops.asymmetry, nullptr, "drops", "asymmetry", false, "0 = a textbook symmetric vortex pair. Higher = unequal lobes, one side leading",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Entry band bottom (uv y)", 0.02f, 0.9f, 0.01f, 2, &c.drops.yMax, nullptr, "drops", "y_max", false, "How far down the screen a drop may enter",
           G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_SLIDER("Mirror mode", 0, 4, 1, 0, nullptr, &c.mirror.mode, "mirror", "mode", false, "Folds the picture about a centre. 3 = the 4-fold quad; 4 = wedges around the centre",
           G_OUTPUT, "FAI", "", KF_GLOBAL, "enum:0=Off|1=Horizontal|2=Vertical|3=Quad|4=Kaleidoscope", "FAI")
KEY_SLIDER("Kaleidoscope segments", 2, 16, 1, 0, nullptr, &c.mirror.segments, "mirror", "segments", false, "Number of wedges in mode 4. Every other one is reflected, so there is no jump at a wedge edge",
           G_OUTPUT, "FAI", "mirror.mode==4", KF_GLOBAL, "", "")
KEY_SLIDER("Shown quadrant (0-3)", 0, 3, 1, 0, nullptr, &c.mirror.source, "mirror", "source", false, "Which quarter of the sim is the one you see, and is copied around: +1 = right half, +2 = bottom half",
           G_OUTPUT, "FAI", "mirror.mode>0", KF_GLOBAL, "", "")
KEY_SLIDER("Fold centre x", 0.1f, 0.9f, 0.01f, 2, &c.mirror.centerX, nullptr, "mirror", "center_x", false, "Where the vertical seam sits",
           G_OUTPUT, "FAI", "mirror.mode>0", KF_GLOBAL, "", "")
KEY_SLIDER("Fold centre y", 0.1f, 0.9f, 0.01f, 2, &c.mirror.centerY, nullptr, "mirror", "center_y", false, "Where the horizontal seam sits",
           G_OUTPUT, "FAI", "mirror.mode>0", KF_GLOBAL, "", "")
KEY_SLIDER("Rotation (s per turn, 0=fixed)", 0, 600, 5, 0, &c.mirror.rotatePeriod, nullptr, "mirror", "rotate_period", false, "Slowly turns the kaleidoscope's fold axes about the centre",
           G_OUTPUT, "FAI", "mirror.mode>0", KF_GLOBAL, "zero:fixed", "")
KEY_SLIDER("Centre drift", 0, 1, 0.02f, 2, &c.mirror.drift, nullptr, "mirror", "drift", false, "Slow wander of the fold point, so the seam is not glued to the middle of the screen",
           G_OUTPUT, "FAI", "mirror.mode>0", KF_GLOBAL, "", "")
KEY_SLIDER("Seam softness (uv)", 0, 0.05f, 0.002f, 3, &c.mirror.soft, nullptr, "mirror", "soft", false, "Rounds the fold off over this band so the mirror crease is not a hard line. 0 = a hard mirror",
           G_OUTPUT, "FAI", "mirror.mode>0", KF_GLOBAL, "", "")
KEY_SLIDER("Film grain", 0, 1, 0.01f, 3, &c.post.filmGrain, nullptr, "post", "film_grain", false, "Animated film grain over the finished frame, weighted into the mids and darks and kept off the peaks and off true black",
           G_POST, "FAI", "post.film_grain>0", 0, "", "A")
KEY_SLIDER("Film grain size (px)", 0.5f, 6, 0.25f, 2, &c.post.filmGrainSize, nullptr, "post", "film_grain_size", false, "Pixels per grain cell. 1 = per-pixel noise, larger = coarser stock",
           G_POST, "FAI", "post.film_grain>0", 0, "", "")
KEY_SLIDER("Film grain speed", 0, 2, 0.05f, 2, &c.post.filmGrainSpeed, nullptr, "post", "film_grain_speed", false, "Multiplier on the grain frame rate; 1 = the film_grain_fps rate, lower holds each pattern longer",
           G_POST, "FAI", "post.film_grain>0", 0, "", "")
KEY_SLIDER("Film grain frame rate (fps)", 1, 240, 1, 0, &c.post.filmGrainFps, nullptr, "post", "film_grain_fps", false, "How many grain patterns per second: 24 or 30 for a film cadence (both divide 240 exactly), 240 = every refresh. Also paces film_noise",
           G_POST, "FAI", "post.film_grain>0", 0, "", "")
KEY_SLIDER("Film grain colour (0 mono)", 0, 1, 0.05f, 2, &c.post.filmGrainColor, nullptr, "post", "film_grain_color", false, "0 = monochrome grain, 1 = independent RGB noise",
           G_POST, "FAI", "post.film_grain>0", 0, "", "")
KEY_SLIDER("Film grain chroma (1 = old)", 0, 1, 0.05f, 2, &c.post.filmGrainChroma, nullptr, "post", "film_grain_chroma", false, "1 = the grain is an equal step on all three channels, which moves saturation and clips against black. 0 = it scales the pixel instead, so hue and saturation survive and black stays black",
           G_POST, "FAI", "post.film_grain>0", 0, "", "")
KEY_SLIDER("Film grain density curve", 0, 1, 0.05f, 2, &c.post.filmGrainDensity, nullptr, "post", "film_grain_density", false, "0 = grain at full amplitude from just above black upward. 1 = the stock own density curve: nothing in the dense shadow, most of it in the mid-tones, nothing on a clean highlight",
           G_POST, "FAI", "post.film_grain>0", 0, "", "")
KEY_SLIDER("Lateral aberration", 0, 1, 0.02f, 2, &c.post.aberration, nullptr, "post", "aberration", false, "A lens focuses red and blue at slightly different magnifications, so the channels land at different scales: red pushed out from the optical axis, blue pulled in. Nothing at the axis, a couple of pixels at the corners. Centred on the rig's lens, which drifts, so the clean spot never sits still",
           G_OPTICS, "FAI", "post.aberration>0", 0, "", "")
KEY_SLIDER("Aberration width (px at 1440p)", 0, 6, 0.1f, 2, &c.post.aberrationPx, nullptr, "post", "aberration_px", false, "How far red and blue are displaced radially at the corners, in pixels at 1440p. Whether the fringe fades on defocused shapes is `aberration_coc`",
           G_OPTICS, "FAI", "post.aberration>0", 0, "", "")
KEY_SLIDER("Aberration growth to field edge", 0, 2, 0.1f, 2, &c.post.aberrationField, nullptr, "post", "aberration_field", false, "How fast the split grows from the optical axis outward. 0 = nearly uniform across the frame, 2 = clean in the middle and all of it in the corners",
           G_OPTICS, "FAI", "post.aberration>0", 0, "", "")
KEY_SLIDER("Aberration fades with defocus", 0, 1, 0.05f, 2, &c.post.aberrationCoc, nullptr, "post", "aberration_coc", false, "0 = the same split everywhere, in focus or not. 1 = it fades with this pixel own blur, so the fringe lives on the sharp slice and disappears on an out-of-focus shape, which is what a real lens does",
           G_OPTICS, "FAI", "post.aberration>0", 0, "", "")
KEY_SLIDER("Vignette", 0, 1, 0.02f, 2, &c.post.vignette, nullptr, "post", "vignette", false, "A gentle fall-off toward the corners -- a field stop, never a circle",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Lens softness (px at 1440p)", 0, 6, 0.1f, 2, &c.post.softness, nullptr, "post", "softness", false, "Defocuses the isolines themselves -- coverage band, film edge, rim and meniscus -- so no edge in the frame is razor-sharp. The grain stays sharp",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Bright-field halo", 0, 1, 0.01f, 3, &c.post.halo, nullptr, "post", "halo", false, "A soft bright glow hugging the outside of every dark shape, with a faint darker echo beyond it -- the microscope double contour (liquid_acid only)",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Halo width (px at 1440p)", 1, 40, 1, 0, &c.post.haloPx, nullptr, "post", "halo_px", false, "How wide that glow is. Wide and weak is the look; narrow and strong is a stroked line",
           G_OPTICS, "FAI", "post.halo>0", 0, "", "")
KEY_SLIDER("Minimum band widths", 0, 1, 0.05f, 2, &c.post.bandMin, nullptr, "post", "band_min", false, "Floors every band around an edge (film edge, rim, meniscus, halo, penumbra) at a few px, so a small droplet is shaded like a big mass instead of getting a solid outline. 0 = bands proportional to each element's size",
           G_POST, "FAI", "", 0, "", "")
KEY_SLIDER("Camera defocus (px at 1440p)", 0, 4, 0.1f, 2, &c.post.postBlurPx, nullptr, "post", "post_blur_px", false, "Image-space disc blur of the finished frame: every feature, whatever its size, gets the same lens defocus. 0 = off",
           G_POST, "FAI", "", 0, "", "")
KEY_SLIDER("Camera glare", 0, 1, 0.02f, 2, &c.post.postGlow, nullptr, "post", "post_glow", false, "Weak wide veiling glare: the frame mixed with a wide blur of itself, so dark bleeds a little into bright and bright into dark around every edge, including a thin ring wall. 0 = off",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Camera glare radius (px at 1440p)", 2, 40, 0.5f, 1, &c.post.postGlowPx, nullptr, "post", "post_glow_px", false, "How far the glare spreads",
           G_OPTICS, "FAI", "post.post_glow>0", 0, "", "")
KEY_SLIDER("Light in the water: haze", 0, 1, 0.02f, 2, &c.post.fog, nullptr, "post", "fog", false, "The water itself glows near the off-view lamp and fades with distance, added only into the dark. It falls to exactly zero far from the lamp, so black stays black. 0 = off",
           G_OPTICS, "FAI", "", 0, "", "A")
KEY_SLIDER("Haze reach (px at 1440p)", 100, 2000, 25, 0, &c.post.fogPx, nullptr, "post", "fog_px", false, "How far the glow of the water carries from the lamp",
           G_OPTICS, "FAI", "post.fog>0", 0, "", "")
KEY_SLIDER("Haze: keep masses black", 0, 1, 0.05f, 2, &c.post.fogMassGate, nullptr, "post", "fog_mass_gate", false, "0 = the haze lands wherever it is dark. 1 = it is kept out of the inside of a dark mass, which floats in front of the water, and still glows in the water beside it",
           G_OPTICS, "FAI", "post.fog>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("Masses: artefacts follow colour", 0, 1, 0.05f, 2, &c.post.artefactLumGate, nullptr, "post", "artefact_lum_gate", false, "0 = grain, lens fringe and the cover's sheen land on every mass alike. 1 = inside a mass they follow its own brightness and colour: a pure black mass stays clean, a dyed one keeps its texture, the edge band is untouched",
           G_POST, "FAI", "", KF_UNVERIFIED, "", "")
KEY_SLIDER("Bloom (wide, weak)", 0, 1, 0.02f, 2, &c.post.bloom, nullptr, "post", "bloom", false, "The bright film bleeds a very wide, very weak wash into the black. Its radius breathes and the wash drifts with the lamp",
           G_OPTICS, "FAI", "", 0, "", "A")
KEY_SLIDER("Bloom radius (px at 1440p)", 40, 400, 5, 0, &c.post.bloomPx, nullptr, "post", "bloom_px", false, "How far that wash spreads",
           G_OPTICS, "FAI", "post.bloom>0", 0, "", "")
KEY_SLIDER("  bloom warmth (lamp colour)", 0, 1, 0.05f, 2, &c.post.bloomWarmth, nullptr, "post", "bloom_warmth", false, "Brief BN. 0 = the wash is the colour of the film it came from. 1 = it takes the lamp's colour: hot yellow-white on the lamp's side, red away from it (the LAPD microscope veil). Luminance unchanged, so it does not brighten the frame",
           G_OPTICS, "FAI", "post.bloom>0", 0, "", "")
KEY_SLIDER("Lamp x", -1, 2, 0.02f, 2, &c.post.lightX, nullptr, "post", "light_x", false, "Where the off-view lamp sits, in screen coordinates. 0.5 = the middle, outside 0..1 = off-frame",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Lamp y", -1, 2, 0.02f, 2, &c.post.lightY, nullptr, "post", "light_y", false, "1.2 = just below the bottom edge, which is where the reference lamp is",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Lamp idle drift", 0, 1, 0.05f, 2, &c.post.lightDrift, nullptr, "post", "light_drift", false, "How far the lamp wanders on its own: a sum of slow sines over seconds to a minute, so the light is never static",
           G_MOTION, "FAI", "", KF_MOTION, "", "")
KEY_SLIDER("Lamp z (in front / behind)", -1, 1, 0.05f, 2, &c.post.lightZ, nullptr, "post", "light_z", false, "How far the lamp stands off the plane of the dish. Above 0 it is in front of it and the cast shadows rake away from it, shortening as it rises; 0 is in the plane and throws the longest shadows; below 0 the lamp is behind the dish and every caster spills its shadow evenly onto the film in front of it",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Film dust", 0, 1, 0.02f, 2, &c.post.filmDust, nullptr, "post", "film_dust", false, "Specks of dust on the film: sparse bright points, a new scattering every film frame. Additive and weighted into the dark, so they are stars on the black and nothing on the bright film",
           G_POST, "FAI", "", 0, "", "")
KEY_SLIDER("Film hairs", 0, 1, 0.02f, 2, &c.post.filmHairs, nullptr, "post", "film_hairs", false, "How often a curly hair is caught in the gate. Each one sticks for a few seconds, flutters, and is gone",
           G_POST, "FAI", "", KF_UNVERIFIED, "", "")
KEY_SLIDER("Film scratches", 0, 1, 0.02f, 2, &c.post.filmScratches, nullptr, "post", "film_scratches", false, "Faint near-vertical scratches that persist for a stretch, drift sideways and disappear",
           G_POST, "FAI", "", 0, "", "")
KEY_SLIDER("Film light leak", 0, 1, 0.02f, 2, &c.post.filmLeak, nullptr, "post", "film_leak", false, "A coloured leak at one edge -- warm core, cool fringe, soft bands -- that swells and dies, and does not come back every time",
           G_POST, "FAI", "", KF_UNVERIFIED, "", "")
KEY_SLIDER("Film artefact rate (s)", 1, 30, 0.5f, 1, &c.post.filmArtefactRate, nullptr, "post", "film_artefact_rate", false, "How long one population of hairs lasts. Scratches change three times slower, the leak five times slower, the dust every film frame",
           G_POST, "FAI", "post.film_dust>0 || post.film_hairs>0 || post.film_scratches>0 || post.film_leak>0", 0, "", "")
KEY_SLIDER("Film noise", 0, 1, 0.01f, 3, &c.post.filmNoise, nullptr, "post", "film_noise", false, "A second, finer and faster noise layer under the coarse grain: the emulsion's own fizz as against the stock's grain structure",
           G_POST, "FAI", "", 0, "", "")
KEY_SLIDER("Film noise size (px at 1440p)", 0.5f, 4, 0.25f, 2, &c.post.filmNoiseSize, nullptr, "post", "film_noise_size", false, "Cell size of that finer layer, in px at 1440p",
           G_POST, "FAI", "post.film_noise>0", 0, "", "")
KEY_SLIDER("Film stock colour", 0, 1, 0.02f, 2, &c.post.filmStock, nullptr, "post", "film_stock", false, "The stock's own grade, applied last: lifted teal shadows, warm highlights, a slightly different curve per channel",
           G_POST, "FAI", "", 0, "", "")
KEY_SLIDER("Camera glare, dark bias", 0, 1, 0.05f, 2, &c.post.postGlowDark, nullptr, "post", "post_glow_dark", false, "Leans the glare toward the dark side: shadows of dark features bleed into the bright film more than the film's light bleeds into the black",
           G_OPTICS, "FAI", "post.post_glow>0", 0, "", "")
KEY_SLIDER("Depth of field (max CoC px at 1440p)", 0, 16, 0.5f, 1, &c.post.dofMaxPx, nullptr, "post", "dof_max_px", false, "Turns the one global defocus into a real depth of field: each element is blurred by how far its own depth is from the plane of focus, up to this radius. 0 = off, and the whole per-pixel path is skipped. liquid_acid only",
           G_OPTICS, "A", "", 0, "", "")
KEY_SLIDER("Focus depth", 0, 1, 0.02f, 2, &c.post.cameraFocus, nullptr, "post", "camera_focus", false, "Which depth is sharp. 0.5 is the plane the big masses sit on; lower favours the front droplets, higher the back ones",
           G_OPTICS, "A", "", 0, "", "")
KEY_SLIDER("Field curvature", 0, 2, 0.05f, 2, &c.post.cameraFieldCurve, nullptr, "post", "camera_field_curve", false, "Bends the surface of focus away from the dish with distance from the optical axis, so the centre and the corners of the frame cannot both be sharp -- what a real lens does",
           G_OPTICS, "A", "", 0, "", "")
KEY_SLIDER("Corner warp", 0, 1, 0.05f, 2, &c.post.cornerWarp, nullptr, "post", "corner_warp", false, "Brief BN. The microscope's field edge: outside the start radius the corners are stretched along the arcs round the centre, so they look oddly distorted while the middle stays exactly as it was. 0 = off",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("  corner warp start", 0.4f, 0.9f, 0.05f, 2, &c.post.cornerWarpR, nullptr, "post", "corner_warp_r", false, "Where the warp begins: 0.4 = just outside the middle 40% of the frame, 0.9 = only the very corners (1 = a corner)",
           G_OPTICS, "FAI", "post.corner_warp>0", 0, "", "")
KEY_SLIDER("Focus tilt", 0, 2, 0.05f, 2, &c.post.focusTilt, nullptr, "post", "focus_tilt", false, "Slants the surface of focus (Lensbaby / freelensing): a strip of sharpness across the frame with the focus falling off smoothly to either side. 0 = level",
           G_OPTICS, "A", "", 0, "", "")
KEY_SLIDER("Focus tilt angle (deg)", -180, 180, 5, 0, &c.post.focusTiltAngle, nullptr, "post", "focus_tilt_angle", false, "Which way that strip runs. With a readjustment period set, this is the angle it is re-aimed around",
           G_OPTICS, "A", "", 0, "", "")
KEY_SLIDER("Sharp band width (px at 1440p)", 40, 1200, 20, 0, &c.post.focusBandPx, nullptr, "post", "focus_band_px", false, "How wide the sharp strip is on screen. Wide is a gentle depth of field, narrow is the freelensing sliver",
           G_OPTICS, "A", "", 0, "", "")
KEY_SLIDER("Refocus period (s, 0 = never)", 0, 300, 5, 0, &c.post.focusTiltPeriod, nullptr, "post", "focus_tilt_period", false, "Mean seconds the focus plane holds still before somebody re-tilts the lens. Randomised around this, so it never feels scheduled. 0 = the lens is bolted down",
           G_MOTION, "A", "", KF_MOTION, "zero:never", "")
KEY_SLIDER("Refocus move (s)", 0.2f, 4, 0.1f, 2, &c.post.focusTiltMoveS, nullptr, "post", "focus_tilt_move_s", false, "How long one readjustment takes. A second or two, eased, with a slight overshoot and settle -- a hand letting go of a lens barrel",
           G_MOTION, "A", "", KF_MOTION, "", "")
KEY_SLIDER("Camera field of view (deg)", 0, 90, 2, 0, &c.post.cameraFov, nullptr, "post", "camera_fov", false, "0 = an orthographic scanner, every ring a symmetric circle. Above 0 the dish is seen from a lens at the tip of a view cone: off-axis rings foreshorten toward the axis, their far wall reads thicker and their highlight swings to the side facing the axis",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Optical axis x", 0, 1, 0.02f, 2, &c.post.cameraAxisX, nullptr, "post", "camera_axis_x", false, "Where the optical axis meets the dish: the one point seen face on, and the centre the field curvature and the tilt are measured from",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Optical axis y", 0, 1, 0.02f, 2, &c.post.cameraAxisY, nullptr, "post", "camera_axis_y", false, "The other half of that point",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Point spread (px at 1440p)", 0, 6, 0.1f, 2, &c.post.psfPx, nullptr, "post", "psf_px", false, "The floor under the defocus radius, applied whatever the focus: no lens resolves a point to a point, so the sharpest thing in the frame is still this wide. At 1 px and up an in-focus edge can never come out as stair-stepped coverage AA",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Output dither (LSB of 10 bit)", 0, 4, 0.25f, 2, &c.post.dither, nullptr, "post", "dither", false, "Half an LSB of ordered blue-ish noise on the finished frame, in the domain the panel quantises in. Breaks the 10-bit steps that show as bands on a big saturated flat. It fades out into true black, so an off pixel stays off. 1 = half an LSB",
           G_POST, "FAI", "", 0, "", "")
KEY_SLIDER("Halation", 0, 1, 0.02f, 2, &c.post.halation, nullptr, "post", "halation", false, "CineStill's missing anti-halation layer: a tight warm glow bleeding out of the bright film into the dark around it. Taken only from what is genuinely bright, and it never lands on the highlight itself. Much tighter than Bloom, which is the wide weak wash",
           G_OPTICS, "FAI", "", 0, "", "")
KEY_SLIDER("Halation radius (px at 1440p)", 4, 60, 1, 0, &c.post.halationPx, nullptr, "post", "halation_px", false, "How far the glow reaches. A dozen pixels is the film look; far more and it becomes a second bloom",
           G_OPTICS, "FAI", "post.halation>0", 0, "", "")
KEY_SLIDER("Lid (cover glass)", 0, 1, 0.05f, 2, &c.post.lid, nullptr, "post", "lid", false, "Master for the transparent cover over the dish: its internal reflections, sheen, glint and iridescence. 0 = no cover at all",
           G_LID, "FAI", "", 0, "", "A")
KEY_SLIDER("  lid ghosts", 0, 1, 0.05f, 2, &c.post.lidGhost, nullptr, "post", "lid_ghost", false, "Dim, offset, slightly magnified copies of the bright film reflected inside the cover, tinted by the coating they bounced off",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("  lid ghost spread", 0, 2, 0.05f, 2, &c.post.lidGhostSpread, nullptr, "post", "lid_ghost_spread", false, "How far the ghost chain runs along the line from the lamp reflection through the optical centre",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("  lid ring ghosts", 0, 1, 0.05f, 2, &c.post.lidRings, nullptr, "post", "lid_rings", false, "Concentric coloured arcs -- the field reflected off a curved element (the LAPD optics look)",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("  lid sheen", 0, 1, 0.05f, 2, &c.post.lidSheen, nullptr, "post", "lid_sheen", false, "A very wide, very weak warm smear where the lamp catches the cover",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("  lid sheen width (px)", 40, 1400, 20.0f, 0, &c.post.lidSheenPx, nullptr, "post", "lid_sheen_px", false, "Width of that smear, in px at 1440p",
           G_LID, "FAI", "post.lid>0", KF_UNVERIFIED, "", "")
KEY_SLIDER("  lid glint", 0, 1, 0.05f, 2, &c.post.lidGlint, nullptr, "post", "lid_glint", false, "The lamp's own reflection in the cover: a soft core with a wide amber halo",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("  lid iridescence", 0, 1, 0.05f, 2, &c.post.lidIris, nullptr, "post", "lid_iris", false, "Interference colours of the thin oil film on the cover, visible only across the sheen",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("  lid refraction (px)", 0, 12, 0.5f, 1, &c.post.lidRefractPx, nullptr, "post", "lid_refract_px", false, "How much the uneven cover wobbles its own reflections (the transmitted picture is left alone)",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("  lid scratches", 0, 1, 0.05f, 2, &c.post.lidScratch, nullptr, "post", "lid_scratch", false, "Wear on the cover: hairline scratches fixed to the lid that catch the lamp only where they run across its light, so the pattern shifts as the lamp drifts. Clear over the dark, nearly gone on the bright film. 0 = off (needs Lid above 0)",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("  lid scratch density", 0, 1, 0.05f, 2, &c.post.lidScratchDensity, nullptr, "post", "lid_scratch_density", false, "How many fine scratches there are where the wear is",
           G_LID, "FAI", "post.lid>0 && post.lid_scratch>0", 0, "", "")
KEY_SLIDER("  lid scratch long gouges", 0, 1, 0.05f, 2, &c.post.lidScratchLen, nullptr, "post", "lid_scratch_len", false, "0 = only short micro-swirls. Higher adds up to eight long straight gouges across the cover",
           G_LID, "FAI", "post.lid>0 && post.lid_scratch>0", 0, "", "")
KEY_SLIDER("  lid scratch corners", 0, 1, 0.05f, 2, &c.post.lidScratchCorner, nullptr, "post", "lid_scratch_corner", false, "Where the wear sits: 0 = all over the cover, 1 = only in the corners",
           G_LID, "FAI", "post.lid>0 && post.lid_scratch>0", 0, "", "")
KEY_SLIDER("  lid scratch softness", 0, 1, 0.05f, 2, &c.post.lidScratchSoft, nullptr, "post", "lid_scratch_soft", false, "0 = crisp 1 px hairlines that catch the light over a narrow angle; higher = wider, dimmer grooves that catch it over a wider one",
           G_LID, "FAI", "post.lid>0 && post.lid_scratch>0", 0, "", "")
KEY_SLIDER("  lid scratch tint", 0, 1, 0.05f, 2, &c.post.lidScratchTint, nullptr, "post", "lid_scratch_tint", false, "0 = neutral white. 1 = the scratch takes the colour of the film under it (over black it stays white)",
           G_LID, "FAI", "post.lid>0 && post.lid_scratch>0", 0, "", "")
KEY_SLIDER("  glass streaks", 0, 1, 0.05f, 2, &c.post.glassStreaks, nullptr, "post", "glass_streaks", false, "Brief BN. Two thin bright wavy lines on the cover glass (the LAPD sheet), brightest past the lamp's reflection. They ride the lid's drift and turn, so they never sit still. Needs Lid above 0. Not scaled by it",
           G_LID, "FAI", "post.lid>0", 0, "", "")
KEY_SLIDER("Halation warmth", 0, 1, 0.05f, 2, &c.post.halationWarmth, nullptr, "post", "halation_warmth", false, "0 = a colourless glow. 1 = the red-orange of light that has crossed the emulsion twice, which is the CineStill signature",
           G_OPTICS, "FAI", "post.halation>0", 0, "", "")
KEY_SLIDER("  halation threshold (haze)", 0, 0.6f, 0.05f, 2, &c.post.halationThreshold, nullptr, "post", "halation_threshold", false, "Brief BN. 0 = only the genuinely bright film halates. Higher lowers the knee so the mid-tones scatter too and halation becomes a diffusion haze (pro-mist): the picture stays sharp and is veiled, never blurred. Pair with a wider halation radius",
           G_OPTICS, "FAI", "post.halation>0", 0, "", "")
KEY_SLIDER("Thermal shimmer", 0, 1, 0.02f, 2, &c.post.shimmer, nullptr, "post", "shimmer", false, "The air above the lamp. A very fine refractive wobble, strongest near the light and fading to nothing away from it, and carried by the sim's own velocity -- a burst that shoves the oil shoves the heat above it too",
           G_MOTION, "FAI", "", KF_MOTION, "", "")
KEY_SLIDER("Shimmer amount (px at 1440p)", 0, 6, 0.1f, 2, &c.post.shimmerPx, nullptr, "post", "shimmer_px", false, "How far the wobble displaces the picture at its strongest. A pixel or two is heat; more is water",
           G_MOTION, "FAI", "post.shimmer>0", KF_MOTION, "", "")
KEY_SLIDER("Vignette wander", 0, 1, 0.05f, 2, &c.post.vignetteWander, nullptr, "post", "vignette_wander", false, "Lets the vignette's centre follow the rig's lens, so the darkest corner turns over minutes instead of sitting in one corner of the panel for hours. 0 = pinned to the middle",
           G_MOTION, "FAI", "", KF_MOTION, "", "")
KEY_SLIDER("Pixel-shift orbit (px at 1440p)", 0, 8, 0.5f, 1, &c.post.pixelShiftPx, nullptr, "post", "pixel_shift_px", false, "The OLED safety net under everything else: the whole finished frame walks a slow closed orbit of this radius, a few thousandths of a pixel per frame, so no feature ever holds one pixel. Invisible, and still moving",
           G_MOTION, "FAI", "", KF_MOTION, "", "")
KEY_SLIDER("Rig readjust reach", 0, 1, 0.05f, 2, &c.post.rigReadjust, nullptr, "post", "rig_readjust", false, "How far the lamp and the lens centre re-aim when the focus readjusts. 0 = only focus and tilt move; above 0 the whole rig moves as one body on the same eased spring, which is the point of having a rig",
           G_MOTION, "FAI", "", KF_MOTION, "", "")
KEY_SLIDER("Wanderer path", 0, 2, 1, 0, nullptr, &c.wandererMode, "behavior", "wanderer_mode", true, "Path the wanderers follow",
           G_FLUID, "FAI", "behavior.wanderers>0", 0, "enum:0=Random wander|1=Circle|2=Figure 8", "")
KEY_SLIDER("Colour gamut", 0, 2, 1, 0, nullptr, &g_gamutMode, "hdr", "gamut", false, "Output gamut while Windows HDR is on",
           G_OUTPUT, "FAI", "", KF_MACHINE|KF_GLOBAL, "enum:0=sRGB|1=Display-P3|2=BT.2020 (QD-OLED)", "")
KEY_SLIDER("Sim resolution", 32, 512, 1, 0, nullptr, &c.simRes, "sim", "sim_res", false, "Fluid grid size. Read-only here: 256 is the WE grid (AGENTS.md hard constraint)",
           G_FLUID, "FAI", "", KF_SHELL, "", "")
KEY_SLIDER("Dye resolution", 256, 4096, 1, 0, nullptr, &c.dyeRes, "sim", "dye_res", false, "Dye texture size. Read-only here: 4096 is the live value (AGENTS.md hard constraint)",
           G_FLUID, "FAI", "", KF_SHELL, "", "")
KEY_CHECK("Auto wanderer splats", &c.wanderers, "behavior", "wanderers", "Autonomous roaming emitters",
          G_FLUID, "FAI", "", 0, "", "")
KEY_CHECK("Auto-pause when screen full", &c.autoPause, "behavior", "auto_pause", "Stop painting when the field is full",
          G_FLUID, "FAI", "", 0, "", "")
KEY_CHECK("Separating dart while paused", &c.dartEnabled, "behavior", "dart_enabled", "Periodic shot that splits merged blobs",
          G_FLUID, "FAI", "", 0, "", "")
KEY_CHECK("Hue shift cycler", &c.hsEnabled, "behavior", "hueshift_enabled", "Palette rotation bursts (full-wheel moods only)",
          G_COLOUR, "FAI", "", KF_UNVERIFIED, "", "F")
KEY_CHECK("Idle random splats", &c.idleSplats, "behavior", "idle_splats", "Random blobs when the field is calm",
          G_FLUID, "FAI", "", 0, "", "")
KEY_CHECK("Shading", &c.shading, "sim", "shading", "Pseudo-3D emboss on dye edges",
          G_FLUID, "FA", "", 0, "", "")
KEY_CHECK("Hold left mouse = pour dye", &c.holdToSplat, "behavior", "hold_to_splat", "Hold the left button to pour a continuous dye stream",
          G_FLUID, "FAI", "", 0, "", "")
KEY_CHECK("Splat on click (if not holding)", &c.splatOnClick, "behavior", "splat_on_click", "Each click splats a single dye blob",
          G_FLUID, "FAI", "", 0, "", "")
KEY_CHECK("Mouse movement stirs fluid", &c.showMouse, "behavior", "show_mouse", "Moving the mouse pushes currents through the fluid",
          G_FLUID, "FAI", "", 0, "", "F")
KEY_CHECK("Random color (hue wheel)", &c.colorful, "color", "colorful", "Emit from the hue band instead of the fixed palette",
          G_COLOUR, "FAI", "", KF_UNVERIFIED, "", "")
KEY_CHECK("Use all 5 palette colors", &c.moreColors, "color", "more_colors", "Splats cycle all five palette colors instead of one",
          G_COLOUR, "FAI", "color.colorful==0", KF_UNVERIFIED, "", "")
KEY_CHECK("HDR compensation (sat/brightness)", &c.hdrCompensation, "hdr", "compensation", "Apply HDR boosts when Windows HDR is on",
          G_OUTPUT, "FAI", "", KF_UNVERIFIED|KF_GLOBAL, "", "")
KEY_CHECK("Response curve (bright rims)", &c.curveEnabled, "color", "curve_enabled", "Enable the brightness hump curve",
          G_COLOUR, "FAI", "", 0, "", "")
KEY_CHECK("Pause on fullscreen app", &g_pauseOnFullscreen, "general", "pause_on_fullscreen", "Pause the wallpaper while a fullscreen app has focus",
          G_SYSTEM, "FAI", "", KF_MACHINE|KF_GLOBAL, "", "")
KEY_CHECK("Pause on maximized app", &g_pauseOnMaximized, "general", "pause_on_maximized", "Pause the wallpaper while a maximized window has focus",
          G_SYSTEM, "FAI", "", KF_MACHINE|KF_GLOBAL, "", "")
KEY_CHECK("Cycle looks (auto-switch)", UiCycleEnabledPtr(), "cycle", "enabled", "Walk the [cycle] stage list: WE fluid, oil, ink, mirror overlays",
          G_SYSTEM, "FAI", "", KF_MACHINE, "", "")
KEY_CHECK("Mirror on second monitor", &c.mirrorSecond, "general", "mirror_second", "Also render the wallpaper on the second monitor",
          G_OUTPUT, "FAI", "", KF_MACHINE|KF_GLOBAL, "", "")
KEY_CHECK("Lock ink opposite the oil hue", &c.acid.inkComplementLock, "liquid_acid", "ink_complement_lock", "Hold the ink's hue on the far side of the wheel from the oil",
          G_COLOUR, "A", "liquid_acid.ink_water==0", 0, "", "")
KEY_CHECK("Rising blobs re-enter from below", &c.acid.riseRespawn, "liquid_acid", "rise_respawn", "With a rise speed set, a blob that climbs off the top comes back in under the bottom edge at a new place and size, instead of reappearing where it left",
          G_OIL, "A", "liquid_acid.rise_speed>0", 0, "", "")
KEY_CHECK("Paired rim (dark in, bright out)", &c.acid.rimOrder, "liquid_acid", "rim_order", "Forces the dark rim and the bright halo adjacent and ordered across the isoline, instead of wherever rim_inset/meniscus_offset put them",
          G_FILM, "A", "", 0, "", "")
KEY_CHECK("Liquid Acid look", &c.acid.enabled, "look", "liquid_acid", "Oil-on-inked-water render look. Switches live (first switch costs one shader compile)",
          G_SYSTEM, "FAI", "", 0, "look", "")
KEY_CHECK("Ink in water look", &c.ink.enabled, "look", "ink", "Beer-Lambert ink-in-water render look. Switches live (first switch costs one shader compile)",
          G_SYSTEM, "FAI", "", 0, "look", "")
KEY_CHECK("Inverted (pale ink on black)", &c.ink.inverted, "ink", "inverted", "Off = dark ink on backlit paper. On = pale ink on black (OLED-friendly)",
          G_INK, "AI", "look==I || liquid_acid.ink_water>0", KF_UNVERIFIED, "", "I")
KEY_CHECK("Ink drops", &c.drops.enabled, "drops", "drops", "Periodic falling ink drops. Works with any render look",
          G_FLUID, "FAI", "", 0, "", "I")
KEY_CHECK("Drops obey the fullness governor", &c.drops.obeyGovernor, "drops", "obey_governor", "Skip a drop while the water is already full of ink",
          G_FLUID, "FAI", "drops.drops>0", 0, "", "")
KEY_CHECK("Start with Windows", nullptr, "system", "autostart", "Start the wallpaper when you sign in (HKCU Run key)",
          G_SYSTEM, "FAI", "", KF_MACHINE|KF_GLOBAL, "autostart", "")
