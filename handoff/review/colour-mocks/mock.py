# Rough colour-combo mock tiles (NOT renders of the app): film + hue2/hue3 patches whose
# hue is ROTATED from the film hue along a signed path (so the seam shows the in-between
# hues, like the app's hue2 field), black oil blobs on top, shadow tint in the dark corners.
import colorsys, math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H = 480, 270
OUT = os.path.dirname(os.path.abspath(__file__))

def lin(c):  # sRGB -> linear
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def hue_rgb(h_deg, s=0.88):
    """Full-value HSV colour, dimmed so every hue carries about the same luminance
    (the 'yellow must not be blinding' rule): target Y ~= today's magenta."""
    h = (h_deg % 360.0) / 360.0
    r, g, b = colorsys.hsv_to_rgb(h, s, 1.0)
    Y = 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)
    target = 0.26
    k = min(1.0, (target / max(Y, 1e-4)) ** (1 / 2.2))
    return np.array([r, g, b]) * k

def field(cx, cy, rad):
    yy, xx = np.mgrid[0:H, 0:W]
    d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2) / rad
    # flat interior, narrow seam band at the edge (like the proven panel photos)
    t = np.clip((1.0 - d) / 0.22, 0, 1)
    return t * t * (3 - 2 * t)  # smoothstep

def tile(film, off2=None, off3=None, shadow=None, seed=1):
    rng = np.random.default_rng(seed)
    w2 = np.zeros((H, W)); w3 = np.zeros((H, W))
    if off2 is not None:
        # proven looks: second colour at LARGE size (two big patches)
        w2 = np.maximum(field(W * 0.30, H * 0.78, 175), field(W * 0.86, H * 0.20, 130))
    if off3 is not None:
        w3 = field(W * 0.72, H * 0.70, 95)
        w2 = w2 * (1 - w3)
    hue = film + (off2 or 0) * w2 + (off3 or 0) * w3
    # per-pixel hue -> rgb (vectorised via lookup of 720 steps)
    lut = np.array([hue_rgb(i / 2.0) for i in range(720)])
    idx = (np.mod(hue, 360.0) * 2).astype(int) % 720
    img = lut[idx]
    # shadow tint: dark vignette toward the corners, tinted with the shadow hue
    yy, xx = np.mgrid[0:H, 0:W]
    v = np.clip(((xx / W - 0.5) ** 2 + (yy / H - 0.5) ** 2) * 2.2, 0, 1) ** 1.5
    dark = np.array([0.0, 0.0, 0.0])
    if shadow is not None:
        dark = hue_rgb(shadow, 0.8) * 0.45
    img = img * (1 - v[..., None] * 0.85) + dark * v[..., None] * 0.85
    im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    # black oil: one big mass + droplets (black stays black in every tier)
    d = ImageDraw.Draw(im)
    mass = [(0, H * 0.35), (W * 0.18, H * 0.30), (W * 0.30, H * 0.45), (W * 0.26, H * 0.62),
            (W * 0.10, H * 0.66), (0, H * 0.60)]
    d.polygon(mass, fill=(4, 4, 6))
    for _ in range(70):
        x, y = rng.uniform(0, W), rng.uniform(0, H)
        r = rng.choice([2, 3, 4, 5, 7, 10, 14], p=[.25, .2, .18, .15, .1, .08, .04])
        d.ellipse([x - r, y - r, x + r, y + r], fill=(6, 5, 8))
    return im.filter(ImageFilter.GaussianBlur(0.8))

# (tier, name, film, off2, off3, shadow, note)
COMBOS = [
    ("HOME x7", "1 Return to Form", 325, None, None, None, "magenta + black"),
    ("HOME x7", "2 Lightroom", 325, None, None, 185, "magenta, teal in the darks"),
    ("HOME x7", "3 Magenta / Mint", 325, +180, None, None, "seam warm: red-orange-yellow"),
    ("HOME x7", "4 Magenta / Cyan", 325, -140, None, None, "seam cool: violet-blue"),
    ("HOME x7", "5 Blue / Coral", 215, +160, None, None, "seam violet-magenta"),
    ("HOME x7", "6 Red / Sky", 355, -140, None, None, "seam magenta-violet"),
    ("HOME x7", "7 Violet / Amber", 275, +125, None, None, "seam magenta-red"),
    ("HOME x7", "8 Teal / Orange", 190, -160, None, None, "seam green-yellow"),
    ("3-COLOUR x2", "T1 Neon Demon", 325, -140, +30, None, "turquoise + red, no yellow"),
    ("3-COLOUR x2", "T2 Warm Arc", 10, +30, -40, None, "red, amber, magenta"),
    ("3-COLOUR x2", "T3 Triad", 325, -120, +120, None, "azure + gold on magenta"),
    ("3-COLOUR x2", "T4 Split", 215, +150, -150, None, "blue, red, amber"),
    ("3-COLOUR x2", "T5 Euphoria", 275, +125, -45, None, "violet, amber, blue"),
    ("4-COLOUR x1", "Q1 Square+", 325, +180, +90, 235, "mint, amber, blue darks"),
    ("4-COLOUR x1", "Q2 Synthwave", 325, -145, +55, 275, "cyan, orange, violet darks"),
    ("4-COLOUR x1", "Q3 Microscope", 230, -105, +140, 275, "green, red, violet darks"),
]

try:
    font = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 17)
    small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 14)
    big = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 26)
except OSError:
    font = small = big = ImageFont.load_default()

def sheet(items, title, fname, cols=4):
    rows = math.ceil(len(items) / cols)
    pad, lab = 12, 44
    S = Image.new("RGB", (cols * (W + pad) + pad, 50 + rows * (H + lab + pad)), (14, 14, 16))
    d = ImageDraw.Draw(S)
    d.text((pad, 10), title, font=big, fill=(235, 235, 235))
    for i, c in enumerate(items):
        tier, name, film, o2, o3, sh, note = c
        x = pad + (i % cols) * (W + pad); y = 50 + (i // cols) * (H + lab + pad)
        S.paste(tile(film, o2, o3, sh, seed=i + 3), (x, y))
        d.text((x, y + H + 3), f"{name}  [{tier}]", font=font, fill=(240, 240, 240))
        d.text((x, y + H + 23), note, font=small, fill=(170, 170, 170))
    S.save(os.path.join(OUT, fname))

sheet([c for c in COMBOS if c[0] == "HOME x7"],
      "HOME (weight 7): one colour, or a primary/secondary pair  - rough mock, not an app render",
      "combos-home.png")
sheet([c for c in COMBOS if c[0] != "HOME x7"],
      "MIX-UPS: 3-colour (weight 2) and 4-colour (weight 1)  - rough mock, not an app render",
      "combos-mixups.png")
print("ok")
