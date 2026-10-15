# NEXT — Oil3D inside the live oil: one layer = today's look, then add depth

Status: OPEN, 2026-10-14. Local Opus executor (worktree C:\Users\abg77\fw-oil3d-depth, branch
`oil3d-depth`). Supersedes the standalone-exe plan in NEXT-oil3d.md as the MAIN path (the
standalone exe and the lab stay as side projects / a menu of looks).

## The user's requirement (2026-10-14, verbatim gist)
"It should be able to imitate the 2D version exactly at first. Then add more dimensions."
"The current conditions should basically be able to imitate the current version. Then you can
add depth and more effects on top of that. Essentially, if you make the 3D plane so that
everything spawns in 1 layer, it looks the same. Small differences in lighting are fine, but
regardless it should be able to look the same in theory."
Earlier: a TRUE 3D lava lamp (blobs pass in front of and behind each other, tunnels through
masses, glossy light) using all the live oil's colours and modes. The first standalone renders
were rejected ("none of these look good"): they looked like clean CG bubblegum, not the oil.

## Architecture: extend FluidWallpaper's `style=liquid_acid`, never fork it
Every new key defaults to today's look. With the new keys at their defaults the acid PSO
compiles and renders exactly as today (acid identity byte-identical; fluid parity md5
10E36EBF1A74EDFE609065D757300054 holds). Read reference/briefs/EXECUTOR-CARD.md first and follow
it (build2 only, gpu.lock, proof block, identity).

The oil already has depth machinery to build on, so reuse it rather than inventing new knobs:
per-droplet depth (`droplet_depth`, `depth_rise`), `rise_parallax(_dim)`, the dye's depth
(`dye_depth*`), the PERSPECTIVE CAMERA + DEPTH OF FIELD (`camera_focus`, `dof_max_px`, focus tilt,
the CoC channel), and the parallax layer. See LiquidAcidConfig in src/fluid.h, the
`#ifdef LIQUID_ACID` blocks of kDisplaySrc in src/shaders.h, the blob sim in src/fluid.cpp.

### Stage 1: the 3D field, proven identical in one layer (THE gate)
- Blobs get a z (CPU sim): `[liquid_acid] blob_depth` = z spread (0 = every blob on the plane
  z=0, i.e. today). Seeded per blob, slow z drift, re-entry/turnover keep their z rules.
  AcidBlobGPU has no free slot for z: add one (a 4th float4 or a packed lane), and say which.
- `[liquid_acid] oil3d = 0|1`. 0 = today's 2D blob loop, untouched. 1 = the 3D evaluator: per
  pixel, a ray (orthographic by default; see `oil3d_persp` in Stage 2) through the blob field
  F3(x,y,z) = sum of the SAME Wyvill kernels in 3D (the xy anisotropy of rise_stretch and the comb
  apply in xy, z isotropic). Feed the downstream 2D shading THE PROJECTED FIELD:
      P(x,y) = max over the ray of F3,   grad P = (dF3/dx, dF3/dy) at the arg-max z*
  (the envelope theorem: dF3/dz = 0 at the max), with the colour soft-max (w^4), the hole/dye
  identity accumulators and everything else the 2D loop accumulates, evaluated at z*. With
  blob_depth = 0 and an orthographic ray, F3(x,y,0) == F2(x,y) and the max is at z = 0, so
  `field`, `grad`, `colSum/colW`, `idS/idW/idH`... equal today's values up to float rounding,
  and the entire 2D look downstream (rims, film, meniscus, droplets, lens, ink, post) runs
  UNCHANGED. Find z* with a few samples inside each blob's support interval plus 2-3 Newton
  steps (or analytically for one dominant blob); keep it cheap.
- PROOF (the user's "1 layer looks the same"): the live oil stage rendered with oil3d=0 and with
  oil3d=1 blob_depth=0: MAD and max diff (expect rounding-level), a difference image, and the two
  frames side by side. Config for every render in this brief: base
  reference/configs/monotone-post-0924.ini + reference/presets/Layout - Few giants.ini +
  reference/presets/Scheme - Magenta Cyan.ini (the cycle's layering: base, layout, scheme).
  If it is NOT near-identical, stop and report why before Stage 2.

### Stage 2: depth (the first new dimension)
- `blob_depth` > 0 spreads the blobs in z. Blobs far apart in z no longer merge or neck (P takes
  the max, not the sum), so they read as separate bodies overlapping. Blobs close in z still merge.
- `oil3d_occlude = 0|1`: 1 = the FRONT-most surface along the ray wins the colour and identity
  (first threshold crossing), so a near blob covers a far one instead of the bigger one winning.
- `oil3d_persp` 0..1: perspective (far blobs smaller, parallax under the existing camera drift /
  rise parallax). Hook the per-blob z into the EXISTING depth-of-field: the CoC from |z - focus|
  through camera_focus / dof_max_px, so far blobs soften like the macro lens already does for
  droplets.
- Renders (1440p, t ~ 60-120 s, full GPU allowed now: --shot-yield 0): blob_depth at 3 values,
  with occlusion on, perspective at 1, the DoF on. One plain sentence per render on what
  changed. Measure the cost of oil3d=1 at 1440p (budget: +2 ms over oil3d=0).

### Stage 3 (NOT this round; listed so the design leaves room)
3D lighting from the front-surface normal (`oil3d_light` 0..1, 0 = today's flat colours), a small
specular, translucency, holes with z extent (tunnels through the masses), shadows between
depths. The proven 3D raymarch core (directional Lipschitz step, 64-bit blob-mask iteration,
the fxc SM5.1 traps) is on origin/oil3d-lab in src_oil3d_lab/shaders.h and its header comment:
reuse it when Stage 3 needs a true surface hit. The lab's 8-direction sheet
(build2/shots/oil3d/sheet-directions.png) is the user's menu for which effects come next.

## Hand-back after Stage 2 (then stop)
Commit on `oil3d-depth` (no push, no merge). Report: the Stage 1 proof numbers + images, the
Stage 2 renders with a sentence each, the cost in ms, every new key with its default and its
useful range, what you would add first in Stage 3. Finally, honestly, in your own words: what do
you think of the project so far, what worries you, the one thing you would change first.
