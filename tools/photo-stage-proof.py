# brief BY PHOTO-STAGE proof measurements (pre-flight 33) on a cycle-photo-test.ini series:
#   FluidWallpaper.exe --shot <dir>\p.png --ini reference\configs\cycle-photo-test.ini --hdr on|off
#       --shot-delay 19 --shot-series 7:14 --shot-size 2560x1440 --seed 1234 > <dir>\p.log
#   python tools\photo-stage-proof.py <dir> <stem> <hdr|sdr> <log> <sheet.png> [<numbers.txt>]
# Series order = cycle-photo-test's photo stages: bars-16x9, bars-4x3, bars-21x9, grey16, grey8,
# offsize (1600x1200 -> 1920x1440), fill-4x3 (bars-4x3 with fit=fill).
#  (a) the [shot] line: max_scRGB == sdrScale (3.000 at 240 nits HDR on / 1.000 off) and
#      above_sdr_white == 0.00 %  (+ the [state] line: phase=dwell fade=1.0000)
#  (b,c,d) the 8-bit <stem>.png (= /sdrScale): bar / ramp centres == source bytes +-1;
#      pillarbox / letterbox bytes == 0; fill = the centred crop (bars/ramp seam at row 912)
#  (e) the 16-bit -pq.png: grey16 shows more distinct levels than grey8 along a row
import os, re, sys
import numpy as np
from PIL import Image, ImageDraw
import cv2

BARS = [(255, 255, 255), (255, 255, 0), (0, 255, 255), (0, 255, 0),
        (255, 0, 255), (255, 0, 0), (0, 0, 255), (128, 128, 128)]
RAMP = [round(k * 255 / 15) for k in range(16)]
W, H = 2560, 1440
TIMES = [19, 33, 47, 61, 75, 89, 103]
CASES = [  # name, rect (x0, y0, w, h) of the photo on the 2560x1440 output
    ('bars-16x9', (0, 0, 2560, 1440)),
    ('bars-4x3', (320, 0, 1920, 1440)),
    ('bars-21x9', (0, 171, 2560, 1098)),
    ('grey16', (0, 0, 2560, 1440)),
    ('grey8', (0, 0, 2560, 1440)),
    ('offsize', (320, 0, 1920, 1440)),
    ('fill-4x3', (0, -240, 2560, 1920)),   # the COVER rect; the screen shows its centre
]


def patch(img, x, y, r=4):
    p = img[y - r:y + r + 1, x - r:x + r + 1].reshape(-1, 3)
    return np.median(p, axis=0)


def main():
    d, stem, mode, logp, sheet = sys.argv[1:6]
    nums = sys.argv[6] if len(sys.argv) > 6 else None
    sdr = 3.0 if mode == 'hdr' else 1.0
    log = open(logp, encoding='utf-8', errors='replace').read()
    shot = re.findall(r'\[shot\] t=([\d.]+)s \S+ .*?above_sdr_white=([\d.]+)%\s+max_scRGB=([\d.]+)', log)
    state = re.findall(r'\[state\] t=([\d.]+) stage=(\d+)\(([^,]+),(\w+)\) phase=(\w+) fade=([\d.]+)', log)
    out = []
    ok_all = True

    def say(s):
        out.append(s)
        print(s)

    say('photo-stage proof, mode=%s (sdrScale %.3f), dir %s' % (mode, sdr, d))
    thumbs = []
    for k, ((name, (x0, y0, rw, rh)), t) in enumerate(zip(CASES, TIMES)):
        base = os.path.join(d, '%s-%03d' % (stem, t))
        img = np.asarray(Image.open(base + '.png').convert('RGB')).astype(np.int32)
        fails = []
        # (a)
        a = shot[k] if k < len(shot) else None
        st = state[k] if k < len(state) else None
        if a:
            mx, above = float(a[2]), float(a[1])
            if abs(mx - sdr) > 0.0005 or above != 0.0:
                fails.append('max_scRGB %.3f above %.2f%%' % (mx, above))
        else:
            fails.append('no [shot] line')
        if not st or st[4] != 'dwell' or abs(float(st[5]) - 1.0) > 1e-4 or st[3] != 'photo':
            fails.append('state %s' % (st,))
        worst = 0
        # (b) bars + ramp (bars cases; fill = bars at the cover's scale, centre crop)
        if name.startswith('bars') or name in ('offsize', 'fill-4x3'):
            hb = round(rh * 0.6) if name != 'fill-4x3' else 1152
            for i, c in enumerate(BARS):
                x = x0 + int((i + 0.5) * rw / 8)
                y = y0 + hb // 2 if name != 'fill-4x3' else 456
                v = patch(img, x, y)
                worst = max(worst, int(np.max(np.abs(v - np.array(c)))))
            for i, g in enumerate(RAMP):
                x = x0 + int((i + 0.5) * rw / 16)
                y = y0 + (hb + rh) // 2 if name != 'fill-4x3' else 1176
                v = patch(img, x, y)
                worst = max(worst, int(np.max(np.abs(v - g))))
            if worst > 1:
                fails.append('bar/ramp centre off by %d' % worst)
        # (c) letterbox / pillarbox
        box = None
        if x0 > 0:
            box = max(int(img[:, :x0].max()), int(img[:, x0 + rw:].max()))
        if y0 > 0:
            box = max(int(img[:y0].max()), int(img[y0 + rh:].max()))
        if box is not None and box != 0:
            fails.append('letterbox/pillarbox max byte %d' % box)
        # (d) fill: the seam row in column 100 (white bar above, ramp step 0 below)
        seam = None
        if name == 'fill-4x3':
            col = img[:, 100, 0]
            seam = int(np.argmax(col < 200))
            if abs(seam - 912) > 1:
                fails.append('fill seam at row %d, expected 912 (centred crop)' % seam)
            if int(img[0].max()) == 0 or int(img[-1].max()) == 0:
                fails.append('fill has black edges')
        # grey ramps: the 8-bit png vs the source ramp
        levels = None
        if name in ('grey16', 'grey8'):
            xs = np.arange(W)
            src = np.round(xs / 2559.0 * 65535) / 65535.0 if name == 'grey16' else np.round(xs / 2559.0 * 255) / 255.0
            exp = np.round(src * 255)
            row = img[720, :, 0]
            worst = int(np.max(np.abs(row - exp)))
            if worst > 1:
                fails.append('grey ramp off by %d' % worst)
            pq = cv2.imread(base + '-pq.png', cv2.IMREAD_UNCHANGED)
            levels = len(np.unique(pq[720, :, 2]))   # cv2 = BGR: channel 2 = R
        ok = not fails
        ok_all &= ok
        say('%-10s t=%3d  %s  max_scRGB=%s above_sdr_white=%s%%  centre/ramp worst |d|=%d  box=%s%s%s  %s' % (
            name, t, 'PASS' if ok else 'FAIL', a[2] if a else '?', a[1] if a else '?', worst,
            'n/a' if box is None else box, '' if seam is None else '  seam row %d' % seam,
            '' if levels is None else '  pq levels %d' % levels,
            ('state %s %s fade %s' % (st[4], st[3], st[5])) if st else 'state ?') +
            ('' if ok else '  <- ' + '; '.join(fails)))
        thumbs.append((name, t, ok, Image.open(base + '.png').convert('RGB').resize((640, 360), Image.NEAREST)))
    # (e)
    lv = {}
    for l in out:
        m = re.match(r'(grey16|grey8)\s.*pq levels (\d+)', l)
        if m:
            lv[m.group(1)] = int(m.group(2))
    if 'grey16' in lv and 'grey8' in lv:
        e = lv['grey16'] > lv['grey8']
        ok_all &= e
        say('(e) -pq.png distinct levels along row 720: grey16 %d vs grey8 %d -> %s' % (
            lv['grey16'], lv['grey8'], 'PASS' if e else 'FAIL'))
    say('RESULT %s' % ('PASS' if ok_all else 'FAIL'))
    # sheet: 7 thumbnails (nearest-neighbour), 3 + 4 per row, labelled
    cols = 4
    rows = (len(thumbs) + cols - 1) // cols
    sh = Image.new('RGB', (cols * 650 + 10, rows * 400 + 40), (40, 40, 40))
    dr = ImageDraw.Draw(sh)
    dr.text((10, 10), 'brief BY photo stage, cycle-photo-test.ini, HDR %s (8-bit /sdrScale view), --seed 1234' %
            ('ON sdr-white 240' if mode == 'hdr' else 'OFF'), fill=(230, 230, 230))
    for i, (name, t, ok, th) in enumerate(thumbs):
        x, y = 10 + (i % cols) * 650, 40 + (i // cols) * 400
        sh.paste(th, (x, y))
        dr.text((x, y + 364), '%s  t=%d s  %s' % (name, t, 'PASS' if ok else 'FAIL'),
                fill=(120, 230, 120) if ok else (255, 90, 90))
    sh.save(sheet)
    if nums:
        with open(nums, 'a', encoding='utf-8') as f:
            f.write('\n'.join(out) + '\n\n')
    sys.exit(0 if ok_all else 1)


if __name__ == '__main__':
    main()
