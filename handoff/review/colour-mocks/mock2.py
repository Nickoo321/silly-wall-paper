# Re-mock of the two "?" tiles: yellow/gold members allowed to stay bright (no equal-luminance
# dimming inside patches), plus the painter's-wheel triad for magenta. Rough mock, not the app.
import numpy as np, os
from PIL import Image, ImageDraw, ImageFont
import importlib.util, sys, types
src = open('mock.py', encoding='utf-8').read().split('# (tier, name')[0]   # functions only
m = types.ModuleType('m'); m.__dict__['__file__'] = os.path.abspath('mock.py'); exec(src, m.__dict__)
W, H = m.W, m.H

def tile(film, off2, off3, seed):
    w2 = np.maximum(m.field(W*0.30, H*0.78, 175), m.field(W*0.86, H*0.20, 130))
    w3 = m.field(W*0.72, H*0.70, 95); w2 = w2*(1-w3)
    hue = np.mod(film + off2*w2 + off3*w3, 360.0)
    dim = np.array([m.hue_rgb(i/2.0) for i in range(720)])
    import colorsys
    full = np.array([colorsys.hsv_to_rgb((i/2.0)/360.0, 0.88, 1.0) for i in range(720)])
    idx = (hue*2).astype(int) % 720
    img = dim[idx]
    # warm-yellow members (hue 25..110) inside a patch keep their full brightness
    yel = ((hue > 25) & (hue < 110)).astype(float) * np.clip(w2 + w3, 0, 1)
    img = img*(1-yel[...,None]) + full[idx]*0.92*yel[...,None]
    yy, xx = np.mgrid[0:H, 0:W]
    v = np.clip(((xx/W-0.5)**2 + (yy/H-0.5)**2)*2.2, 0, 1)**1.5
    img = img*(1 - v[...,None]*0.85)
    im = Image.fromarray((np.clip(img,0,1)*255).astype(np.uint8)); d = ImageDraw.Draw(im)
    rng = np.random.default_rng(seed)
    d.polygon([(0,H*.35),(W*.18,H*.30),(W*.30,H*.45),(W*.26,H*.62),(W*.10,H*.66),(0,H*.60)], fill=(4,4,6))
    for _ in range(70):
        x,y = rng.uniform(0,W), rng.uniform(0,H); r = rng.choice([2,3,4,5,7,10,14], p=[.25,.2,.18,.15,.1,.08,.04])
        d.ellipse([x-r,y-r,x+r,y+r], fill=(6,5,8))
    return im

T = [("T3 Triad, as mocked before", 325, -120, +120, "light-wheel triad 325/205/85, gold dimmed = olive"),
     ("T3a Triad, gold kept bright", 325, -120, +120, "same hues, yellow member not dimmed"),
     ("T3b Painter's triad", 330, -155, +70, "magenta + teal + gold (red-violet/blue-green/yellow-orange)"),
     ("T4b Split, yellow kept bright", 215, +150, -150, "blue + red + yellow (split of orange)")]
font = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 17); small = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", 14)
S = Image.new("RGB", (4*(W+12)+12, 50+H+56), (14,14,16)); d = ImageDraw.Draw(S)
d.text((12,10), "The two '?' tiles, fixed  - rough mock, not an app render", font=ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 26), fill=(235,235,235))
for i,(n,f,o2,o3,note) in enumerate(T):
    x = 12 + i*(W+12)
    if i == 0:
        t = m.tile(f, o2, o3, None, seed=13)   # original dimmed version
    else:
        t = tile(f, o2, o3, seed=13)
    S.paste(t, (x,50)); d.text((x,50+H+3), n, font=font, fill=(240,240,240)); d.text((x,50+H+23), note, font=small, fill=(170,170,170))
S.save("combos-question-fix.png"); print("ok")
