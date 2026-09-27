# brief BY PHOTO-STAGE proof images (pre-flight 31): every case at its NATIVE
# fit size for a 2560x1440 output, so no scaler is in the numbers -- except
# `offsize` (1600x1200 -> Fant-scaled to 1920x1440) and the fill case, which
# reuses bars-4x3 with fit=fill (reference/configs/photo-test/fill-4x3.ini).
#
#   python tools\photo-stage-images.py            # (re)write reference\photos-stage\<case>\*.png
#   python tools\photo-stage-images.py --check    # also print the layout the proof measures
#   python tools\photo-stage-images.py --big <dir>  # proof-time only, NOT committed:
#                                                   #   <dir>\big-30mb.jpg (noise, ~30 MB)
#                                                   #   <dir>\notjpeg.jpg  (a text file)
#
# Layout of every "bars" image (the proof script tools\photo-stage-proof.py
# measures the same layout): the top 60 % holds 8 vertical colour bars
# (white, yellow, cyan, green, magenta, red, blue, a 50 % grey), the bottom 40 %
# a 16-step grey ramp, step k = round(k * 255 / 15) (0 .. 255, white = 255).
# grey16 = a horizontal 16-bit ramp 0..65535 over the width; grey8 = the same
# ramp in 8 bits (the banding reference).
import os, sys
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'reference', 'photos-stage')

BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (128, 128, 128)]
RAMP = [round(k * 255 / 15) for k in range(16)]
SPLIT = 0.6                     # bars above, ramp below (fraction of the height)


def bars(w, h):
    a = np.zeros((h, w, 3), np.uint8)
    hb = int(round(h * SPLIT))
    for k, c in enumerate(BARS):
        x0, x1 = (k * w) // 8, ((k + 1) * w) // 8
        a[:hb, x0:x1] = c
    for k, v in enumerate(RAMP):
        x0, x1 = (k * w) // 16, ((k + 1) * w) // 16
        a[hb:, x0:x1] = v
    return a


def save_png(arr, case, name):
    d = os.path.join(OUT, case)
    os.makedirs(d, exist_ok=True)
    p = os.path.join(d, name)
    if arr.dtype == np.uint16:
        import cv2                  # PIL's 16-bit PNG writer is grey-only and fussy; cv2 is exact
        cv2.imwrite(p, arr, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    else:
        Image.fromarray(arr).save(p, optimize=True)
    print('%-48s %6d KB  %s' % (os.path.relpath(p, ROOT), os.path.getsize(p) // 1024, arr.shape))
    return p


def main():
    if '--big' in sys.argv:
        d = sys.argv[sys.argv.index('--big') + 1]
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, 'notjpeg.jpg'), 'w') as f:
            f.write('this is not a JPEG: a text file with a .jpg name (brief BY proof 5)\n')
        rng = np.random.default_rng(1234)
        w, h = 6000, 4000                         # ~30 MB of incompressible noise at q95
        a = rng.integers(0, 256, (h, w, 3), dtype=np.uint8)
        p = os.path.join(d, 'big-30mb.jpg')
        Image.fromarray(a).save(p, quality=95, subsampling=0)
        print(p, os.path.getsize(p) // (1024 * 1024), 'MB', w, 'x', h)
        return
    save_png(bars(2560, 1440), 'bars-16x9', 'bars-16x9.png')
    save_png(bars(1920, 1440), 'bars-4x3', 'bars-4x3.png')      # pillarbox 320 each side
    save_png(bars(2560, 1098), 'bars-21x9', 'bars-21x9.png')    # letterbox 171 top and bottom
    save_png(bars(1600, 1200), 'offsize', 'offsize-1600x1200.png')   # Fant 1.2x -> 1920x1440
    x = np.arange(2560, dtype=np.float64) / 2559.0
    g16 = np.round(x * 65535).astype(np.uint16)
    g8 = np.round(x * 255).astype(np.uint8)
    save_png(np.repeat(g16[None, :], 1440, 0), 'grey16', 'grey16.png')   # 16-bit greyscale PNG
    save_png(np.repeat(g8[None, :], 1440, 0), 'grey8', 'grey8.png')      # 8-bit greyscale PNG
    if '--check' in sys.argv:
        print('bars', BARS)
        print('ramp', RAMP)


if __name__ == '__main__':
    main()
