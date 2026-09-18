"""Pop detector for the Liquid Acid droplet sim.

The user, on the live panel: "individual dots" popping, "usually the blur
around the dark spots". This turns that into a number. Feed it a --shot series
taken one FRAME apart (see --shot-series with a sub-tenth interval, which names
frames in milliseconds) and it classifies every pixel as

    oil  : R > 150      hole : max(R,G,B) < 40

then finds the connected components of each class and counts the ones that
appear in frame N with NO overlap at all in frame N-1 (and, the other way
round, vanish with no overlap in frame N). Those are pops: a droplet that was
not there and now is, or the reverse, with no continuity between the two.
A droplet that is growing in, shrinking away or sliding has overlap and is
correctly NOT counted.

    python tools/popdetect.py build2/shots/pop/before-*.png

Prints a per-transition table and the totals; --min N ignores components
smaller than N pixels (default 4, which drops single-pixel AA noise).
"""
import sys, glob, os

sys.path.insert(0, r"C:/Users/abg77/AppData/Local/Temp/claude/"
                   r"C--Users-abg77-OneDrive-Desktop-wall-paper-engine-claude-code/"
                   r"8c9d0c8d-d72a-4f56-9d50-a24cc6eac2d0/scratchpad/pylibs")
from PIL import Image


def band_stats(a, b, w, h):
    """The user's flicker is not in the oil and not in the hole: it is 'the blur
    around the dark spots', i.e. the wide soft thin-edge band between them,
    which the oil/hole classifier above skips entirely. Measure that band
    directly: how many of its pixels swing hard between two consecutive frames,
    and by how much on average."""
    pa, pb = a.load(), b.load()
    n = 0; swing = 0; tot = 0
    for y in range(h):
        for x in range(w):
            r1, g1, b1 = pa[x, y]
            r2, g2, b2 = pb[x, y]
            mid1 = 40 <= max(r1, g1, b1) and r1 <= 150
            mid2 = 40 <= max(r2, g2, b2) and r2 <= 150
            if not (mid1 or mid2):
                continue
            tot += 1
            d = abs(r1 - r2) + abs(g1 - g2) + abs(b1 - b2)
            swing += d
            if d > 60:
                n += 1
    return tot, n, (swing / tot if tot else 0.0)


def masks(path):
    """Strict masks AND loose ones, for hysteresis.

    The strict thresholds alone lie. Measured on the real frames: every single
    "appeared hole" component at 1440p was a pixel going from brightness ~49 to
    ~39 -- a 3% drift in a near-black gradient that happens to straddle the
    hard cut at 40. It is invisible, it is not a droplet, and there were ~37 of
    them per frame pair purely because 1440p has seven times as many pixels
    sitting within a few levels of the boundary (which is also why the count
    looked resolution-dependent, and why that looked like evidence for tiny
    droplets). So a component only counts as new if it does not overlap the
    LOOSE mask of the previous frame -- i.e. those pixels were not even close
    to being a hole before. That is the difference between measuring the sim
    and measuring the instrument.
    """
    im = Image.open(path).convert('RGB')
    w, h = im.size
    px = im.load()
    oil = bytearray(w * h)
    hole = bytearray(w * h)
    oil_loose = bytearray(w * h)
    hole_loose = bytearray(w * h)
    for y in range(h):
        base = y * w
        for x in range(w):
            r, g, b = px[x, y]
            m = max(r, g, b)
            if r > 150:
                oil[base + x] = 1
            if r > 120:
                oil_loose[base + x] = 1
            if m < 40:
                hole[base + x] = 1
            if m < 70:
                hole_loose[base + x] = 1
    return w, h, oil, hole, oil_loose, hole_loose


def components(mask, w, h, minpx):
    """Iterative 4-connected labelling; returns a list of pixel-index sets."""
    seen = bytearray(len(mask))
    out = []
    for start in range(len(mask)):
        if not mask[start] or seen[start]:
            continue
        stack = [start]
        seen[start] = 1
        comp = []
        while stack:
            i = stack.pop()
            comp.append(i)
            x = i % w
            if x > 0 and mask[i - 1] and not seen[i - 1]:
                seen[i - 1] = 1; stack.append(i - 1)
            if x < w - 1 and mask[i + 1] and not seen[i + 1]:
                seen[i + 1] = 1; stack.append(i + 1)
            if i >= w and mask[i - w] and not seen[i - w]:
                seen[i - w] = 1; stack.append(i - w)
            if i + w < len(mask) and mask[i + w] and not seen[i + w]:
                seen[i + w] = 1; stack.append(i + w)
        if len(comp) >= minpx:
            out.append(comp)
    return out


def pops(curComps, prevMask):
    """Components of `cur` with NO pixel overlapping `prev` at all."""
    n = 0
    px = 0
    for c in curComps:
        if not any(prevMask[i] for i in c):
            n += 1
            px += len(c)
    return n, px


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    minpx = 4
    if '--min' in sys.argv:
        minpx = int(sys.argv[sys.argv.index('--min') + 1])
    files = []
    for a in args:
        files.extend(sorted(glob.glob(a)))
    files = [f for f in files if '-hdr' not in os.path.basename(f)]
    if len(files) < 2:
        print('need at least two frames; got', files)
        return 1
    print('%d frames, min component %d px' % (len(files), minpx))
    prev = None
    prevf = None
    tot_app = tot_van = tot_px = tot_band = 0
    for f in files:
        cur = masks(f)
        if prev is not None:
            w, h, oil, hole, oilL, holeL = cur
            _, _, poil, phole, poilL, pholeL = prev
            ao, apo = pops(components(oil, w, h, minpx), poilL)
            ah, aph = pops(components(hole, w, h, minpx), pholeL)
            vo, vpo = pops(components(poil, w, h, minpx), oilL)
            vh, vph = pops(components(phole, w, h, minpx), holeL)
            bt, bn, bm = band_stats(Image.open(prevf).convert('RGB'),
                                    Image.open(f).convert('RGB'), w, h)
            print('  %-22s pops oil %2d/%2d hole %2d/%2d | band %5d px, %4d jumpy, mean d %.1f'
                  % (os.path.basename(f), ao, vo, ah, vh, bt, bn, bm))
            tot_band += bn
            tot_app += ao + ah
            tot_van += vo + vh
            tot_px += apo + aph + vpo + vph
        prev = cur
        prevf = f
    print('TOTAL pops: %d appeared + %d vanished = %d (%d px); jumpy band px: %d'
          % (tot_app, tot_van, tot_app + tot_van, tot_px, tot_band))
    return 0


if __name__ == '__main__':
    sys.exit(main())
