# Rough mocks (NOT app renders): (A) new colour pairs from the backlog, (B) oil layouts.
# Oil = metaballs with a compact kernel, always black. Black % is measured per tile.
import colorsys, math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H = 480, 270
OUT = os.path.dirname(os.path.abspath(__file__))
YY, XX = np.mgrid[0:H, 0:W].astype(np.float64)

def lin(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

def hue_rgb(h, s=0.88, target=0.26):
    r, g, b = colorsys.hsv_to_rgb((h % 360) / 360, s, 1.0)
    Y = 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)
    return np.array([r, g, b]) * min(1.0, (target / max(float(Y), 1e-4)) ** (1 / 2.2))

def oil(blobs, stretch=1.0):
    """blobs = [(x, y, r)]; compact kernel (1-(d/2r)^2)^3, iso-surface at d=r for a lone blob."""
    F = np.zeros((H, W))
    for x, y, r in blobs:
        R = 2.0 * r
        x0, x1 = int(max(0, x - R)), int(min(W, x + R + 1))
        y0, y1 = int(max(0, y - R * stretch)), int(min(H, y + R * stretch + 1))
        if x0 >= x1 or y0 >= y1:
            continue
        dx = XX[y0:y1, x0:x1] - x; dy = (YY[y0:y1, x0:x1] - y) / stretch
        q = np.clip(1 - (dx * dx + dy * dy) / (R * R), 0, 1)
        F[y0:y1, x0:x1] += q ** 3
    F /= 0.421875
    t = np.clip((F - 0.93) / 0.14, 0, 1)
    return t * t * (3 - 2 * t)  # alpha of black

def base(film, s=0.88, target=0.26, shadow=None, grey_corner=False, grain=0.0, seed=0):
    img = np.broadcast_to(hue_rgb(film, s, target), (H, W, 3)).copy()
    if grey_corner:  # lamp grey: Y-flat desaturation toward the far (bottom-right) corner, keeps 10% chroma
        m = np.clip(((XX / W) * 0.6 + (YY / H) * 0.4 - 0.45) / 0.45, 0, 1) ** 1.3
        L = lin(img); Y = (L @ np.array([0.2126, 0.7152, 0.0722]))[..., None]
        L = Y + (L - Y) * (1 - 0.9 * m[..., None])
        img = np.where(L <= 0.0031308, L * 12.92, 1.055 * np.power(L, 1 / 2.4) - 0.055)
    v = np.clip(((XX / W - 0.5) ** 2 + (YY / H - 0.5) ** 2) * 2.2, 0, 1) ** 1.5
    dark = hue_rgb(shadow, 0.8) * 0.45 if shadow is not None else np.zeros(3)
    img = img * (1 - v[..., None] * 0.85) + dark * v[..., None] * 0.85
    if grain:
        img = img + np.random.default_rng(seed).normal(0, grain, (H, W, 1))
    return img

def compose(img, a):
    img = img * (1 - a[..., None]) + np.array([0.015, 0.015, 0.02]) * a[..., None]
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))

# ---------- layouts ----------
def L_today(rng):  # like the app today: one medium mass + scattered droplets (<10% black)
    b = [(70, 140, 38), (105, 125, 30), (60, 175, 26)]
    b += [(rng.uniform(0, W), rng.uniform(0, H), rng.choice([2, 3, 4, 5, 7], p=[.3, .25, .2, .15, .1])) for _ in range(80)]
    return oil(b)

def L_giants(rng):  # few giants: 3 huge lobed masses, some off-frame, sparse droplets
    b = []
    for cx, cy, R in [(35, 55, 78), (410, 255, 88), (300, 25, 42)]:
        for _ in range(7):
            b.append((cx + rng.normal(0, R * 0.45), cy + rng.normal(0, R * 0.35), R * rng.uniform(0.4, 0.62)))
    b += [(rng.uniform(0, W), rng.uniform(0, H), rng.choice([2, 3, 5])) for _ in range(25)]
    return oil(b)

def L_starfield(rng):  # dense small droplets, no big mass
    b = [(rng.uniform(0, W), rng.uniform(0, H), rng.choice([1.5, 2, 3, 4, 6], p=[.3, .3, .2, .13, .07])) for _ in range(650)]
    return oil(b)

def L_rafts(rng):  # droplets gathered in ring clusters (rafts) with a small core
    b = []
    for cx, cy, rr in [(90, 80, 42), (250, 170, 55), (400, 70, 38), (380, 215, 30), (150, 225, 28)]:
        n = int(rr * 0.55)
        for k in range(n):
            a = 2 * math.pi * k / n + rng.normal(0, 0.05)
            d = rr + rng.normal(0, 3)
            b.append((cx + d * math.cos(a), cy + d * math.sin(a), rng.uniform(3.5, 6.5)))
        b.append((cx, cy, rr * 0.22))
    b += [(rng.uniform(0, W), rng.uniform(0, H), rng.choice([2, 3])) for _ in range(30)]
    return oil(b)

def L_lava(rng):  # lava rise: tall blobs at different heights, some pinching off a bubble above
    b = []
    for cx in [45, 125, 210, 290, 370, 445]:
        cy = rng.uniform(40, 240); r = rng.uniform(18, 30)
        b += [(cx, cy, r), (cx + rng.normal(0, 4), cy + r * 1.4, r * 0.85)]
        if rng.random() < 0.6:
            b.append((cx + rng.normal(0, 3), cy - r * 2.9, r * 0.5))
    return oil(b, stretch=1.9)

LAYOUTS = [("Today (reference)", L_today, "like the app now"),
           ("Few giants", L_giants, "2-3 huge masses, some off-frame"),
           ("Starfield", L_starfield, "dense small droplets only"),
           ("Rafts", L_rafts, "droplets gather in rings"),
           ("Lava rise", L_lava, "tall blobs, bubbles pinching off")]

# ---------- new colour pairs (all on one moderate layout so only colour changes) ----------
PAIRS = [("Sodium / Blue", dict(film=30, shadow=225), "sodium-lamp orange, deep blue shadows"),
         ("Darkroom Red", dict(film=358, s=0.95, target=0.09, grain=0.035), "very dark red, heavy grain (night)"),
         ("Ultramarine / Grey", dict(film=232, grey_corner=True), "far corner goes grey (lamp grey)"),
         ("Cyan / Maroon", dict(film=186, shadow=345), "breaks 'cool shadows only' - ask"),
         ("Parchment", dict(film=38, s=0.22, target=0.16), "pale warm paper, dim for ABL"),
         ("Moonlight", dict(film=215, s=0.35, target=0.14, grey_corner=True), "pale steel blue, dim, grey corner")]

try:
    font = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 17)
    small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 14)
    big = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 24)
except OSError:
    font = small = big = ImageFont.load_default()

def sheet(tiles, title, fname, cols=3):
    rows = math.ceil(len(tiles) / cols); pad, lab = 12, 44
    S = Image.new("RGB", (cols * (W + pad) + pad, 48 + rows * (H + lab + pad)), (14, 14, 16))
    d = ImageDraw.Draw(S); d.text((pad, 10), title, font=big, fill=(235, 235, 235))
    for i, (im, name, note) in enumerate(tiles):
        x = pad + (i % cols) * (W + pad); y = 48 + (i // cols) * (H + lab + pad)
        S.paste(im, (x, y))
        d.text((x, y + H + 3), name, font=font, fill=(240, 240, 240))
        d.text((x, y + H + 23), note, font=small, fill=(170, 170, 170))
    S.save(os.path.join(OUT, fname))

mid = L_today(np.random.default_rng(5))  # moderate reference layout
pair_tiles = [(compose(base(**kw, seed=i), mid), n, note) for i, (n, kw, note) in enumerate(PAIRS)]
sheet(pair_tiles, "NEW COLOUR PAIRS (backlog, not approved)  - rough mock, not an app render", "pairs-new.png")

lay_tiles = []
for i, (n, fn, note) in enumerate(LAYOUTS):
    a = fn(np.random.default_rng(20 + i))
    lay_tiles.append((compose(base(325), a), f"{n}  -  {a.mean() * 100:.0f}% black", note))
sheet(lay_tiles, "OIL LAYOUTS on Return to Form (proven photos: 20-30% black)  - rough mock, not an app render", "layouts.png")
print("ok", [t[1] for t in lay_tiles])
