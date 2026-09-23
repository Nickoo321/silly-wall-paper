#!/usr/bin/env python3
"""
camera-geometry.py -- to-scale diagram of the virtual camera used by the
`liquid_acid` look ([post] section of the ini), built directly from the
depth-of-field / perspective formulas in src/shaders.h (the acid pixel
shader, "PERSPECTIVE" and "CIRCLE OF CONFUSION" blocks) and the constants
that build them in src/fluid.cpp (UploadAcidConstants, ~line 4930-4980).

Run with the live values below (read by hand from reference/configs/
acid-rise-12.ini and cross-checked against reference/FEATURES.md section 5
"Camera" and section 4 "Per-droplet optics" on 2026-09-22). If the preset
changes, edit LIVE below and rerun -- every number on the page is computed
from it, nothing is hand-drawn.

Output: reference/diagrams/camera-geometry.svg and .png, 2400 px wide.

Panel 1 (three mini side views: flat / tilted one extreme / tilted the
other extreme) needs a real lens to draw a real FOV cone and a real
depth of field, and the shader has neither -- so it borrows an ASSUMED
physical macro-camera (named ASSUMED_* constants below: full-frame sensor,
100 mm macro, f/2.8, ~0.3 m) for that purpose ONLY. The three numbers that
matter -- the [post] focus_tilt slider's tilt-angle range, the sharp-band
width, and the droplet_depth range -- are all read from the live ini /
reference/FEATURES.md and carried into that assumed camera through ONE
calibration (matching the code's own axis tolerance to the assumed lens's
thin-lens depth of field); nothing else is fitted.

IMPORTANT MODELLING NOTE (why there are three unrelated "depth" ideas here):
The shader has no single 3D camera. Panel 2 and panel 3 stay code-only and
show two more, DIFFERENT approximations that also live on the same flat
dish and are not the same axis as panel 1's mm:
  1. PERSPECTIVE (camera_fov / camera_axis_*): a flat-plane, off-axis-view
     model used only to foreshorten droplet RINGS by their lateral distance
     from the optical axis. tan(theta) = fovK * r, r in p-space (p-space
     units = frame HEIGHTS: y spans 0..1 over the frame height, x is
     uv.x*aspect). Not drawn in this figure; printed to stdout only.
  2. DEPTH-OF-FIELD (camera_focus / dye_depth / droplet_depth / ...): a
     separate synthetic scalar in [0,1], carried per droplet and per dye
     pixel, used only to drive the blur radius (panel 2). It has no
     distance units of its own.
"""

import math
import textwrap
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, Circle

# ----------------------------------------------------------------------
# LIVE VALUES -- reference/configs/acid-rise-12.ini, cross-checked against
# reference/FEATURES.md section 5 "Camera" (camera_* / focus_* / dof_max_px
# / psf_px / light_*) and section 4 "Per-droplet optics" (dye_depth*,
# droplet_depth). All ini keys are [post] unless noted [liquid_acid].
# ----------------------------------------------------------------------
LIVE = dict(
    FRAME_W=2560, FRAME_H=1440,                      # the panel
    camera_fov=28.0,                                  # [post] camera_fov (deg)
    camera_axis_x=0.5, camera_axis_y=0.5,             # [post] camera_axis_x/y (uv)
    camera_focus=0.5,                                 # [post] camera_focus (depth 0..1)
    camera_field_curve=0.12,                          # [post] camera_field_curve
    focus_tilt=0.26,                                  # [post] focus_tilt
    focus_tilt_angle=25.0,                            # [post] focus_tilt_angle (deg)
    focus_band_px=340.0,                              # [post] focus_band_px (px at 1440p)
    dof_max_px=9.0,                                   # [post] dof_max_px (px at 1440p)
    psf_px=1.0,                                       # [post] psf_px (px at 1440p)
    light_x=0.5, light_y=1.2,                         # [post] light_x/y (uv)
    dye_depth=0.5,                                    # [liquid_acid] dye_depth
    dye_depth_tilt=0.28,                              # [liquid_acid] dye_depth_tilt
    dye_depth_w=0.25,                                 # [liquid_acid] dye_depth_w
    droplet_depth=0.35,                               # [liquid_acid] droplet_depth
)
# fixed aperture constant baked into the shader -- not a key at all (see
# shaders.h "CIRCLE OF CONFUSION" comment: "Fixed rather than another key --
# it is the aperture, and camera_focus, focus_band_px and droplet_depth
# already aim the plane."):
DOF_RAMP_SPAN = 0.12   # depth interval the CoC ramps over, beyond the band

L = LIVE
DEG = math.pi / 180.0
ASPECT = L["FRAME_W"] / L["FRAME_H"]

# ----------------------------------------------------------------------
# Shared geometry helpers -- transcribed 1:1 from fluid.cpp / shaders.h
# ----------------------------------------------------------------------
def to_p(u, v):
    """uv (0..1, 0..1) -> p-space (x scaled by aspect, y = frame heights)."""
    return np.array([u * ASPECT, v])

axP = to_p(L["camera_axis_x"], L["camera_axis_y"])
lampP_abs = to_p(L["light_x"], L["light_y"])
lampP = lampP_abs - axP
lampP_len = np.hypot(*lampP)
lampD = lampP / lampP_len if lampP_len > 1e-5 else np.array([1.0, 0.0])

tilt_angle_rad = L["focus_tilt_angle"] * DEG
tilt_dir = np.array([math.cos(tilt_angle_rad), math.sin(tilt_angle_rad)])

def focus_depth(qo):
    """fz = camera_focus + field_curve*|qo|^2 + focus_tilt*(qo . tilt_dir)."""
    rr2 = qo[0] ** 2 + qo[1] ** 2
    return (L["camera_focus"]
            + L["camera_field_curve"] * rr2
            + L["focus_tilt"] * float(np.dot(qo, tilt_dir)))

def focus_gradient_mag(qo):
    """gm = |focus_tilt| + 2*|field_curve|*|qo| (shaders.h `gm`)."""
    rr = math.hypot(*qo)
    return abs(L["focus_tilt"]) + 2.0 * abs(L["camera_field_curve"]) * rr

def sharp_tolerance(qo):
    """Half-width, in depth units, of the zero-blur band at this point."""
    band_frac = L["focus_band_px"] / 1440.0
    return 0.5 * band_frac * focus_gradient_mag(qo)

def dye_depth_at(qo):
    return L["dye_depth"] + L["dye_depth_tilt"] * float(np.dot(qo, lampD))

def coc_px(depth, qo):
    """Circle-of-confusion radius in px at 1440p, before the psf_px floor."""
    fz = focus_depth(qo)
    tol = sharp_tolerance(qo)
    dz = abs(depth - fz)
    x = np.clip((dz - tol) / DOF_RAMP_SPAN, 0.0, 1.0)
    return L["dof_max_px"] * x * (2.0 - x)

def blur_px(depth, qo):
    return max(coc_px(depth, qo), L["psf_px"])

# fovK: tan(theta) = fovK * r  (r in p-space / frame-heights)
if L["camera_fov"] > 0.01:
    fovK = math.tan(min(L["camera_fov"], 170.0) * 0.5 * DEG) / 0.5
    cam_distance_fh = 1.0 / fovK          # derived equivalent pinhole distance
else:
    fovK = 0.0
    cam_distance_fh = None

half_angle_deg = L["camera_fov"] / 2.0

# ----------------------------------------------------------------------
# Derived numbers used all over the figure (and printed to stdout so the
# "three most surprising numbers" can be pulled straight from a run)
# ----------------------------------------------------------------------
qo_axis = np.array([0.0, 0.0])
tol_axis = sharp_tolerance(qo_axis)
fz_axis = focus_depth(qo_axis)

# frame corners in p-space, relative to the axis (uv corners 0/1 x 0/1)
corners_uv = [(0, 0), (1, 0), (0, 1), (1, 1)]
corners_qo = [to_p(u, v) - axP for u, v in corners_uv]
fz_corners = [focus_depth(q) for q in corners_qo]
fz_min_corner, fz_max_corner = min(fz_corners), max(fz_corners)
tol_corner_max = max(sharp_tolerance(q) for q in corners_qo)

# central vertical column (x = axis_x), top edge to bottom edge
qo_top = to_p(L["camera_axis_x"], 0.0) - axP
qo_bot = to_p(L["camera_axis_x"], 1.0) - axP
fz_top, fz_bot = focus_depth(qo_top), focus_depth(qo_bot)

drop_lo = max(0.0, 0.5 - 0.5 * L["droplet_depth"])
drop_hi = min(1.0, 0.5 + 0.5 * L["droplet_depth"])

dye_top = dye_depth_at(qo_top)
dye_bot = dye_depth_at(qo_bot)

lamp_below_frame_px = (L["light_y"] - 1.0) * L["FRAME_H"] if L["light_y"] > 1.0 else None

depth_at_ramp_end = fz_axis + tol_axis + DOF_RAMP_SPAN

print("---- derived numbers ----")
print(f"fovK={fovK:.4f}  equivalent camera distance D = 1/fovK = {cam_distance_fh:.3f} frame-heights "
      f"({cam_distance_fh*L['FRAME_H']:.0f} px at 1440p), half-angle={half_angle_deg:.1f} deg")
print(f"sharp-band half-tolerance at optical axis: tol = {tol_axis:.4f} depth-units "
      f"(full band {2*tol_axis:.4f}, i.e. {2*tol_axis*100:.1f}% of the 0..1 depth range)")
print(f"sharp-band half-tolerance at the worst corner: {tol_corner_max:.4f} depth-units")
print(f"focus surface fz: axis={fz_axis:.3f}  central-column top={fz_top:.3f} bot={fz_bot:.3f}  "
      f"corner range=[{fz_min_corner:.3f},{fz_max_corner:.3f}]")
print(f"droplet_depth spread: [{drop_lo:.3f},{drop_hi:.3f}] (width {L['droplet_depth']:.2f})")
print(f"dye layer depth across frame height: top={dye_top:.3f} bot={dye_bot:.3f} "
      f"(width {abs(dye_top-dye_bot):.3f})")
print(f"depth where blur first reaches the dof_max_px clamp (axis): {depth_at_ramp_end:.3f}")
print(f"lamp: light_x={L['light_x']} light_y={L['light_y']} -> "
      f"{'off-frame, ' + format(lamp_below_frame_px, '.0f') + ' px below the bottom edge' if lamp_below_frame_px else 'on-frame'}")

# ----------------------------------------------------------------------
# ASSUMED PHYSICAL CAMERA -- used ONLY to make the FOV cone and the depth
# of field in the three mini side-views "real" (the shader itself has no
# sensor, lens or subject-distance keys at all). Every number in this
# block is a stated ASSUMPTION, not read from any ini or source file.
# Change these five constants and every mini-panel below refits itself.
# ----------------------------------------------------------------------
ASSUMED_SENSOR_WIDTH_MM = 36.0     # ASSUMPTION: full-frame sensor width
ASSUMED_FOCAL_LENGTH_MM = 100.0    # ASSUMPTION: 100 mm macro lens
ASSUMED_F_NUMBER = 2.8             # ASSUMPTION: shooting at f/2.8
ASSUMED_FOCUS_DISTANCE_MM = 300.0  # ASSUMPTION: focused at ~0.3 m
ASSUMED_COC_MM = 0.03              # ASSUMPTION: full-frame "acceptable
                                    # circle of confusion" criterion
# [post] focus_tilt's own slider ceiling (reference/FEATURES.md §5): the
# "extreme" tilt used by the two tilted mini-panels is this value, not an
# assumption -- it is the top of the key's authored 0..2 range.
FOCUS_TILT_SLIDER_MAX = 2.0

f = ASSUMED_FOCAL_LENGTH_MM
s = ASSUMED_FOCUS_DISTANCE_MM
N = ASSUMED_F_NUMBER
c = ASSUMED_COC_MM

# thin-lens magnification and the physical size the frame covers at the
# focus distance (DERIVED, not assumed): dish width from the sensor width
# and magnification, dish height from the 2560x1440 pixel aspect (the
# rendered frame is treated as a 16:9 crop of the sensor image, not the
# sensor's native 3:2 -- an explicit simplification, not a 6th assumption).
mag = f / (s - f)
dish_w_mm = ASSUMED_SENSOR_WIDTH_MM / mag
dish_h_mm = dish_w_mm / ASPECT
mm_per_p_unit = dish_h_mm   # 1 p-space unit == 1 frame height == dish_h_mm

# the lens's own angular field of view (a property of sensor+focal length
# alone, independent of subject distance) -- this is the cone half-angle
# drawn in the mini-panels, and it is NOT fitted to camera_fov=28 deg
# (that key drives a different, code-only approximation -- see panel 2).
lens_half_angle_deg = math.degrees(math.atan((ASSUMED_SENSOR_WIDTH_MM / 2.0) / f))

# thin-lens hyperfocal / near / far depth-of-field limits (mm), the
# standard formulas:
H_hyper = f * f / (N * c) + f
dof_near_mm = s * (H_hyper - f) / (H_hyper + s - 2 * f)
dof_far_mm = (s * (H_hyper - f) / (H_hyper - s)) if H_hyper > s else float("inf")
dof_near_half_mm = s - dof_near_mm
dof_far_half_mm = dof_far_mm - s
dof_avg_half_mm = 0.5 * (dof_near_half_mm + dof_far_half_mm)

# Calibration: map the CODE's synthetic depth-tolerance at the optical
# axis (tol_axis, computed above from focus_band_px/focus_tilt) onto this
# assumed lens's physical half-DOF at the same point. This is the ONE
# fitting step -- everything else in the mini-panels (droplet_depth
# spread, the two extreme tilts) is carried through this same scale
# factor, not re-fitted.
mm_per_depth_unit = dof_avg_half_mm / tol_axis

drop_half_mm = 0.5 * L["droplet_depth"] * mm_per_depth_unit
tol_axis_mm = tol_axis * mm_per_depth_unit
lamp_offset_p = L["light_y"] - 1.0   # p-units below the bottom edge (0.2 here)

print("---- assumed physical camera (mini side-views only) ----")
print(f"assumed: sensor width {ASSUMED_SENSOR_WIDTH_MM:.0f} mm (full-frame), lens {f:.0f} mm macro, "
      f"f/{N:.1f}, focus distance {s:.0f} mm, CoC {c:.3f} mm")
print(f"derived: magnification {mag:.3f}x, dish/frame {dish_w_mm:.1f} x {dish_h_mm:.1f} mm, "
      f"lens half-angle {lens_half_angle_deg:.2f} deg")
print(f"derived: thin-lens DOF at this setup: near {dof_near_mm:.3f} mm, far {dof_far_mm:.3f} mm "
      f"(total {dof_far_mm - dof_near_mm:.3f} mm, i.e. +/-{dof_avg_half_mm:.3f} mm)")
print(f"calibration: {mm_per_depth_unit:.2f} mm per synthetic depth-unit "
      f"(code's axis tolerance {tol_axis:.4f} depth-units == {tol_axis_mm:.3f} mm physical half-DOF, by construction)")
print(f"-> droplet_depth spread in mm: +/-{drop_half_mm:.2f} mm about the focal plane")
for tilt_amt, label in ((0.0, "flat"), (FOCUS_TILT_SLIDER_MAX, "extreme +"), (-FOCUS_TILT_SLIDER_MAX, "extreme -")):
    phi = math.degrees(math.atan(tilt_amt * mm_per_depth_unit / mm_per_p_unit))
    print(f"-> physical focal-plane tilt at focus_tilt={tilt_amt:+.2f} ({label}): {phi:+.2f} deg")

# ========================================================================
# FIGURE
# ========================================================================
DPI = 120
FIG_W_PX = 2400
FIG_W_IN = FIG_W_PX / DPI
FIG_H_IN = FIG_W_IN * 0.50

INK = "#1a1a1a"
GRID = "#c9c9c9"
ACCENT = "#b0402a"       # focus / sharp band
ACCENT2 = "#2a6fb0"      # droplets
ACCENT3 = "#5a8a3a"      # dye layer
LAMP_COL = "#c98a1a"

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 11,
    "text.color": INK,
    "axes.edgecolor": INK,
    "axes.labelcolor": INK,
    "xtick.color": INK,
    "ytick.color": INK,
})

fig = plt.figure(figsize=(FIG_W_IN, FIG_H_IN), dpi=DPI, facecolor="white")
gs = fig.add_gridspec(2, 4, width_ratios=[1.0, 1.0, 1.0, 1.55], height_ratios=[0.62, 1.0],
                       left=0.045, right=0.985, top=0.84, bottom=0.335,
                       hspace=0.85, wspace=0.22)

ax_mini = [fig.add_subplot(gs[0, i]) for i in range(3)]   # panel 1: 3 side views
ax_depth = fig.add_subplot(gs[1, 0:3])                    # panel 2: depth model
ax_frame = fig.add_subplot(gs[:, 3])                      # panel 3: frame view

fig.suptitle("Liquid-acid virtual camera -- to scale, from the live preset (acid-rise-12.ini)",
             fontsize=15, y=0.985, color=INK)

# ------------------------------------------------------------------
# PANEL 1 -- three side views of the SAME assumed physical camera and
# dish, sliced along the tilt-gradient direction so the full tilt
# magnitude (not just its component along one fixed screen axis) shows
# to scale: flat, and the [post] focus_tilt slider's two extremes.
# All three share one x-axis (mm, camera at 0) and one y-axis (mm, a
# position along the tilt-gradient direction through the optical axis)
# -- one scale bar covers all three.
# ------------------------------------------------------------------
mini_configs = [
    (0.0, "1a. Flat / head-on\nfocus_tilt = 0"),
    (FOCUS_TILT_SLIDER_MAX, f"1b. Tilted, one extreme\nfocus_tilt = {FOCUS_TILT_SLIDER_MAX:.0f} @ {L['focus_tilt_angle']:.0f}°"),
    (-FOCUS_TILT_SLIDER_MAX, f"1c. Tilted, other extreme\nfocus_tilt = {FOCUS_TILT_SLIDER_MAX:.0f} @ {L['focus_tilt_angle']+180:.0f}°"),
]

lens_half_ang_rad = math.radians(lens_half_angle_deg)
r_max_p = 1.02          # half-diagonal reach shown, in p-units (frame-heights)
x_max_mm = s * 1.55
y_max_mm = r_max_p * mm_per_p_unit * 1.12
# Both physical bands below (in-focus half-width ~0.5 mm, droplet spread
# ~+/-2.9 mm) are sub-millimetre-to-few-mm slivers next to a 300 mm camera
# distance -- invisible at this scale undrawn. Both are exaggerated by the
# SAME factor for a fair side-by-side comparison, and both true values are
# annotated so nothing is hidden.
BAND_EXAGGERATION = 6.0

for ax_m, (tilt_amt, title) in zip(ax_mini, mini_configs):
    ax_m.set_title(title, fontsize=9.3, loc="left", color=INK)

    # camera + its real angular FOV cone (assumed lens, not camera_fov)
    ax_m.plot(0, 0, marker="o", color=INK, ms=6, zorder=5)
    for sign in (+1, -1):
        ax_m.plot([0, x_max_mm], [0, sign * x_max_mm * math.tan(lens_half_ang_rad)],
                   color=INK, lw=1.0)

    # frame extent at the (untitled) focus distance -- dashed reference
    ax_m.plot([s, s], [-dish_h_mm / 2, dish_h_mm / 2], color="#999", lw=1.0, ls=(0, (4, 3)))

    # droplet_depth spread -- symmetric about the focus distance, untitled,
    # exaggerated (see BAND_EXAGGERATION note above)
    drop_half_shown = drop_half_mm * BAND_EXAGGERATION
    ax_m.axvspan(s - drop_half_shown, s + drop_half_shown, color=ACCENT2, alpha=0.30, lw=0.8,
                 ec=ACCENT2)

    # the tilted focal (sharp) plane through the optical axis
    y_line = np.array([-y_max_mm, y_max_mm])
    phi = math.atan(tilt_amt * mm_per_depth_unit / mm_per_p_unit)
    x_line = s + y_line * math.tan(phi)
    ax_m.plot(x_line, y_line, color=ACCENT, lw=2.0, solid_capstyle="butt", zorder=4)

    # in-focus band around it (true half-width tol_axis_mm, exaggerated by
    # the same factor as the droplet band above)
    half_shown = tol_axis_mm * BAND_EXAGGERATION
    ax_m.fill_betweenx(y_line, x_line - half_shown, x_line + half_shown,
                        color=ACCENT, alpha=0.35, lw=0, zorder=3)

    # lamp -- shown for reference just beyond the bottom edge of the frame
    # extent drawn above (its true position is (light_x, light_y) in
    # SCREEN uv; projecting that exactly onto this tilt-gradient axis is
    # skipped here for clarity, per the brief -- it is not that deep)
    lamp_y = -(dish_h_mm / 2 + lamp_offset_p * mm_per_p_unit)
    ax_m.plot(s, lamp_y, marker="*", color=LAMP_COL, ms=13, zorder=5, clip_on=False)

    ax_m.set_xlim(0, x_max_mm)
    ax_m.set_ylim(-y_max_mm, y_max_mm)
    ax_m.set_aspect("equal")
    ax_m.set_yticks([])
    if ax_m is ax_mini[0]:
        ax_m.set_ylabel("mm (along tilt\ngradient direction)", fontsize=7.5)
    ax_m.tick_params(labelsize=7.5)
    for sp in ("top", "right", "left"):
        ax_m.spines[sp].set_visible(False)
    ax_m.spines["bottom"].set_color(GRID)

# one shared scale bar + exaggeration caption, placed in FIGURE coordinates
# just under the middle mini-panel so it can never collide with the cone,
# the title or the bands regardless of the data ranges above
bar_mm = 50.0
bar_frac_of_width = bar_mm / (2 * x_max_mm)  # panel data half-width -> axes-fraction
mid_pos = ax_mini[1].get_position()
mid_cx = mid_pos.x0 + 0.5 * mid_pos.width
bar_y_fig = mid_pos.y0 - 0.028
bar_half_fig = bar_frac_of_width * mid_pos.width
fig.add_artist(plt.Line2D([mid_cx - bar_half_fig, mid_cx + bar_half_fig], [bar_y_fig, bar_y_fig],
                           color=INK, lw=1.3, solid_capstyle="butt",
                           transform=fig.transFigure))
for xoff in (-bar_half_fig, bar_half_fig):
    fig.add_artist(plt.Line2D([mid_cx + xoff, mid_cx + xoff], [bar_y_fig - 0.006, bar_y_fig + 0.006],
                               color=INK, lw=1.3, transform=fig.transFigure))
fig.text(mid_cx, bar_y_fig - 0.014, f"{bar_mm:.0f} mm  (scale shared by all 3 panels above)",
          ha="center", va="top", fontsize=7.6, color=INK, transform=fig.transFigure)
fig.text(mid_cx, bar_y_fig - 0.030,
          f"red + blue bands shown {BAND_EXAGGERATION:.0f}x true width -- actual: "
          f"focus +/-{tol_axis_mm:.2f} mm, droplets +/-{drop_half_mm:.2f} mm",
          ha="center", va="top", fontsize=6.8, color="#555", transform=fig.transFigure)

assume_text = (
    f"ASSUMED physical camera (edit the ASSUMED_* constants to change): full-frame sensor "
    f"{ASSUMED_SENSOR_WIDTH_MM:.0f} mm, {ASSUMED_FOCAL_LENGTH_MM:.0f} mm macro, f/{ASSUMED_F_NUMBER:.1f}, "
    f"focused at {ASSUMED_FOCUS_DISTANCE_MM:.0f} mm, CoC {ASSUMED_COC_MM:.3f} mm  |  "
    f"derived lens half-angle {lens_half_angle_deg:.1f}°, thin-lens DOF ±{dof_avg_half_mm:.2f} mm "
    f"(this fixes the mm-per-depth-unit scale used only to draw the tilt to scale below)"
)
fig.text(0.045, 0.928, assume_text, fontsize=7.6, color="#555", ha="left", va="top",
          bbox=dict(boxstyle="round,pad=0.35", fc="#f5f3ef", ec=GRID, lw=0.8))

# ------------------------------------------------------------------
# PANEL 1b -- the synthetic depth axis: focus surface, DOF falloff,
# droplet / dye bands
# ------------------------------------------------------------------
ax = ax_depth
ax.set_title("2. Synthetic depth axis (camera_focus / dye_depth / droplet_depth, 0..1 -- independent of panel 1's mm)",
             fontsize=10.5, loc="left", color=INK, pad=10)

depths = np.linspace(0.0, 1.0, 600)
blur_axis = [blur_px(d, qo_axis) for d in depths]

ax2 = ax.twinx()
ax2.plot(depths, blur_axis, color=INK, lw=1.6, label="blur radius at the optical axis (rr=0)")
ax2.set_ylabel("blur radius, px at 1440p", fontsize=9)
ax2.set_ylim(0, L["dof_max_px"] * 1.35)
ax2.axhline(L["psf_px"], color="#888", lw=0.8, ls=(0, (2, 2)))
ax2.text(0.995, L["psf_px"] + 0.15, f"psf_px floor = {L['psf_px']:.1f} px (never below this, even in focus)",
          fontsize=7.5, color="#666", ha="right")
ax2.axhline(L["dof_max_px"], color="#888", lw=0.8, ls=(0, (2, 2)))
ax2.text(0.995, L["dof_max_px"] + 0.15, f"dof_max_px clamp = {L['dof_max_px']:.0f} px", fontsize=7.5,
          color="#666", ha="right")

# in-focus band (shaded), at the optical axis
ax.axvspan(fz_axis - tol_axis, fz_axis + tol_axis, color=ACCENT, alpha=0.22, lw=0)
ax.axvline(fz_axis, color=ACCENT, lw=1.4)
ax.annotate(f"in-focus band (blur=0) at the axis\ncamera_focus {fz_axis:.2f} +/- {tol_axis:.3f}\n"
            f"-> only {2*tol_axis*100:.1f}% of the 0..1 depth range",
            (fz_axis, 0.86), xytext=(fz_axis + 0.09, 0.80), fontsize=8, color=ACCENT,
            arrowprops=dict(arrowstyle="-", color=ACCENT, lw=0.8))

# focus-surface sweep across the whole frame (corner to corner) as a fainter band
ax.axvspan(fz_min_corner, fz_max_corner, color=ACCENT, alpha=0.08, lw=0)
ax.annotate(f"focus surface fz sweeps [{fz_min_corner:.2f}, {fz_max_corner:.2f}]\ncorner-to-corner "
            f"(camera_field_curve {L['camera_field_curve']}, focus_tilt {L['focus_tilt']} "
            f"@ {L['focus_tilt_angle']:.0f} deg)",
            (fz_min_corner, 0.72), xytext=(0.015, 0.60), fontsize=7.8, color=ACCENT)

# droplet depth band
ax.axvspan(drop_lo, drop_hi, ymin=0.0, ymax=0.34, color=ACCENT2, alpha=0.30, lw=0)
ax.annotate(f"droplet_depth spread\n[{drop_lo:.2f}, {drop_hi:.2f}]  (width = droplet_depth = "
            f"{L['droplet_depth']:.2f})",
            (0.5 * (drop_lo + drop_hi), 0.34), xytext=(0.5 * (drop_lo + drop_hi), 0.40),
            fontsize=8, color=ACCENT2, ha="center",
            arrowprops=dict(arrowstyle="-", color=ACCENT2, lw=0.8))

# dye layer band (tilted -> shown as its top/bottom-of-frame extremes)
ax.axvspan(min(dye_top, dye_bot), max(dye_top, dye_bot), ymin=0.36, ymax=0.55,
           color=ACCENT3, alpha=0.30, lw=0)
ax.annotate(f"dye layer (dye_depth {L['dye_depth']}, tilted by dye_depth_tilt {L['dye_depth_tilt']})\n"
            f"top of frame {dye_top:.2f} .. bottom of frame {dye_bot:.2f}",
            (0.5 * (dye_top + dye_bot), 0.55), xytext=(0.5 * (dye_top + dye_bot), 0.62),
            fontsize=8, color=ACCENT3, ha="center",
            arrowprops=dict(arrowstyle="-", color=ACCENT3, lw=0.8))

ax.set_xlim(0, 1)
ax.set_ylim(0, 1)
ax.set_yticks([])
ax.set_xlabel("depth (0..1, synthetic units: camera_focus / dye_depth / droplet_depth all live on this axis)",
              fontsize=9)
ax.set_xticks(np.linspace(0, 1, 11))
for sp in ("top", "left"):
    ax.spines[sp].set_visible(False)
ax2.spines["top"].set_visible(False)

# scale bar for the depth axis
ax.annotate("", xy=(0.0, -0.16), xytext=(1.0, -0.16), annotation_clip=False,
            arrowprops=dict(arrowstyle="<->", color=INK, lw=1))
ax.text(0.5, -0.24, "full synthetic depth range (0 = camera_focus's low end .. 1 = its high end; "
        "not a physical distance)", ha="center", fontsize=7.8, color="#555", clip_on=False)

# ------------------------------------------------------------------
# PANEL 2 -- top/front view of the actual 2560x1440 frame
# ------------------------------------------------------------------
ax = ax_frame
ax.set_title("3. Frame view (2560x1440)\ntilt direction, sharp band, lamp",
             fontsize=10.2, loc="left")

W, H = L["FRAME_W"], L["FRAME_H"]
ax.add_patch(Rectangle((0, 0), W, H, fill=False, edgecolor=INK, lw=1.6))

axis_px = (L["camera_axis_x"] * W, L["camera_axis_y"] * H)
ax.plot(*axis_px, marker="+", color=INK, ms=16, mew=2, zorder=5)
ax.annotate("optical axis / lens centre\ncamera_axis_x/y = 0.5, 0.5", axis_px,
            xytext=(axis_px[0] + 60, axis_px[1] + 90), fontsize=8.5)

# tilt direction (depth changes fastest along this line)
tlen = 900
tdx, tdy = math.cos(tilt_angle_rad) * tlen, math.sin(tilt_angle_rad) * tlen
ax.plot([axis_px[0] - tdx, axis_px[0] + tdx], [axis_px[1] - tdy, axis_px[1] + tdy],
         color="#888", lw=1.2, ls=(0, (5, 3)))
ax.annotate(f"focus_tilt_angle = {L['focus_tilt_angle']:.0f} deg\n(depth changes fastest this way)",
            (axis_px[0] + tdx * 0.55, axis_px[1] + tdy * 0.55), fontsize=8, color="#666",
            xytext=(axis_px[0] + tdx * 0.55 + 40, axis_px[1] + tdy * 0.55 + 20))

# sharp band: a strip PERPENDICULAR to the tilt direction (constant depth
# along the perpendicular, per shaders.h), physical width = focus_band_px
# at 1440p, by construction of the depth tolerance from that same key.
perp = np.array([-math.sin(tilt_angle_rad), math.cos(tilt_angle_rad)])
band_half = (L["focus_band_px"] / 2.0)  # already in px at 1440p == this frame's H
along = np.array([math.cos(tilt_angle_rad), math.sin(tilt_angle_rad)])
band_len = 3200
p0 = np.array(axis_px) - perp * band_half - along * band_len / 2
p1 = np.array(axis_px) - perp * band_half + along * band_len / 2
p2 = np.array(axis_px) + perp * band_half + along * band_len / 2
p3 = np.array(axis_px) + perp * band_half - along * band_len / 2
band_poly = plt.Polygon([p0, p1, p2, p3], closed=True, color=ACCENT, alpha=0.22, lw=0, zorder=1)
ax.add_patch(band_poly)
ax.plot([p0[0], p1[0]], [p0[1], p1[1]], color=ACCENT, lw=1)
ax.plot([p3[0], p2[0]], [p3[1], p2[1]], color=ACCENT, lw=1)
mid_edge = (np.array(axis_px) + perp * band_half + along * band_len * 0.30)
ax.annotate(f"sharp band, width = focus_band_px = {L['focus_band_px']:.0f} px at 1440p\n"
            f"({L['focus_band_px']/H*100:.1f}% of frame height)", mid_edge,
            xytext=(mid_edge[0] - 950, mid_edge[1] + 120), fontsize=8, color=ACCENT)

# field-curvature contours (equal-fz rings around the axis) -- faint, for context
for rr_fh in (0.4, 0.8):
    ax.add_patch(Circle(axis_px, rr_fh * H, fill=False, edgecolor="#bbb", lw=0.8, ls=(0, (1, 3))))

# lamp -- off-frame below the bottom edge
lamp_px = (L["light_x"] * W, L["light_y"] * H)
ax.plot(*lamp_px, marker="*", color=LAMP_COL, ms=18, zorder=5, clip_on=False)
ax.annotate(f"lamp: light_x={L['light_x']}, light_y={L['light_y']}\n"
            f"({(L['light_y']-1.0)*H:.0f} px below the bottom edge)", lamp_px,
            xytext=(lamp_px[0] + 80, lamp_px[1] - 40), fontsize=8.5, color=LAMP_COL,
            annotation_clip=False)
ax.plot([axis_px[0], lamp_px[0]], [axis_px[1], lamp_px[1]], color=LAMP_COL, lw=0.8, ls=":",
        clip_on=False)

ax.set_xlim(-260, W + 260)
ax.set_ylim(H + 500, -260)  # y grows downward like screen space; extra room for the lamp
ax.set_aspect("equal")
ax.set_xlabel("px (2560 wide)", fontsize=8.5)
ax.set_ylabel("px (1440 tall)", fontsize=8.5)

# ------------------------------------------------------------------
# LEGEND / KEY BLOCK
# ------------------------------------------------------------------
WRAP_W = 148
note = textwrap.fill(
    "What is and isn't a real camera here: the shader has NO sensor, lens or focus-distance key at all -- "
    "panel 1's mm scale comes entirely from the ASSUMED macro-lens setup boxed above the mini panels, used "
    "only to make its FOV cone and depth-of-field physically real; the tilt angle, in-focus band width and "
    "droplet depth range plotted in mm there are the CODE's own numbers, carried across via one calibration "
    "(the code's axis tolerance matched to this lens's thin-lens DOF). camera_fov=28 deg is a SEPARATE, "
    "code-only approximation (fovK = tan(camera_fov/2)/0.5, tan(theta) = fovK*r) that foreshortens off-axis "
    "droplet rings only (shaders.h, the 'PERSPECTIVE' block) -- it is not drawn here and not the same lens "
    "as panel 1's assumed one. Panel 2's depth axis is a separate, synthetic 0..1 scalar (camera_focus / "
    "dye_depth / droplet_depth) driving only the depth-of-field blur radius (shaders.h, the 'CIRCLE OF "
    "CONFUSION' block); it has no distance units of its own. 'Depth' in this codebase always means one of "
    "these three unrelated things, never a single physical z.",
    width=WRAP_W,
)
legend_lines = [
    "Keys and live values (acid-rise-12.ini, cross-checked reference/FEATURES.md §5 Camera / §4 Per-droplet optics):",
    f"[post] camera_fov={L['camera_fov']:.0f} deg   camera_axis_x/y={L['camera_axis_x']},{L['camera_axis_y']}   "
    f"camera_focus={L['camera_focus']}   camera_field_curve={L['camera_field_curve']}",
    f"[post] focus_tilt={L['focus_tilt']}   focus_tilt_angle={L['focus_tilt_angle']:.0f} deg   "
    f"focus_band_px={L['focus_band_px']:.0f}   dof_max_px={L['dof_max_px']:.0f}   psf_px={L['psf_px']}",
    f"[post] light_x={L['light_x']}   light_y={L['light_y']}      "
    f"[liquid_acid] dye_depth={L['dye_depth']}   dye_depth_tilt={L['dye_depth_tilt']}   "
    f"dye_depth_w={L['dye_depth_w']}   droplet_depth={L['droplet_depth']}",
    "",
    note,
]
fig.text(0.045, 0.300, "\n".join(legend_lines), fontsize=8.3, va="top", ha="left",
          family="DejaVu Sans", color=INK,
          bbox=dict(boxstyle="round,pad=0.6", fc="#f5f3ef", ec=GRID, lw=1))

fig.text(0.045, 0.015,
         "Generated by reference/diagrams/camera-geometry.py from reference/configs/acid-rise-12.ini "
         "and the formulas in src/shaders.h + src/fluid.cpp. Regenerate after any camera-key change.",
         fontsize=7.5, color="#888")

out_svg = "reference/diagrams/camera-geometry.svg"
out_png = "reference/diagrams/camera-geometry.png"
fig.savefig(out_svg, facecolor="white")
fig.savefig(out_png, facecolor="white", dpi=DPI)
print(f"wrote {out_svg}")
print(f"wrote {out_png}")
