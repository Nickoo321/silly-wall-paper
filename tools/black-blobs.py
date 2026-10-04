"""Count the BIGGER black blobs in --shot frames (sibling of tools/black-share.py).

A black pixel is black-share.py's: max(R,G,B) < 40 on the 8-bit SDR <out>.png.
Black pixels are grouped into 4-connected regions on a 2x-downsampled mask (a
region = one black shape on screen: a mass, a bubble, a droplet, or several that
touch). Per frame it prints the black share and how many regions are at least
0.1 % ("medium", a disc ~3.5 % of the frame height across), 0.5 % ("big", ~8 %)
and 2 % ("giant", ~16 %) of the frame, plus the largest region. A region whose
centre lies in the outer 10 % of the frame is NOT counted (it is still in the
black share): that is the lens vignette darkening a corner or an edge, not a blob.

    python tools/black-blobs.py [--thresh 40] [--json out.json] GROUP [GROUP ...]
        GROUP = label=glob | glob | directory   (as black-share.py; -hdr / -pq pngs skipped)

A frame that is more than 90 % black (a fade, a warm-up) is listed but left out
of the group means. No scipy: plain numpy + a stack flood fill.
"""
import glob
import json
import os
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError:  # fallback: session scratchpad copy, as black-share.py
    sys.path.insert(0, r"C:/Users/abg77/AppData/Local/Temp/claude/"
                       r"C--Users-abg77-OneDrive-Desktop-wall-paper-engine-claude-code/"
                       r"8c9d0c8d-d72a-4f56-9d50-a24cc6eac2d0/scratchpad/pylibs")
    import numpy as np
    from PIL import Image

SIZES = (0.1, 0.5, 2.0)          # region area, % of the frame


def regions(mask):
    """(area px, centre y, centre x) of the 4-connected True regions of a 2-D bool array."""
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    out = []
    ys, xs = np.nonzero(mask)
    for y0, x0 in zip(ys.tolist(), xs.tolist()):
        if seen[y0, x0]:
            continue
        seen[y0, x0] = True
        stack = [(y0, x0)]
        n = sy = sx = 0
        while stack:
            y, x = stack.pop()
            n += 1; sy += y; sx += x
            if y > 0 and mask[y - 1, x] and not seen[y - 1, x]:
                seen[y - 1, x] = True; stack.append((y - 1, x))
            if y + 1 < h and mask[y + 1, x] and not seen[y + 1, x]:
                seen[y + 1, x] = True; stack.append((y + 1, x))
            if x > 0 and mask[y, x - 1] and not seen[y, x - 1]:
                seen[y, x - 1] = True; stack.append((y, x - 1))
            if x + 1 < w and mask[y, x + 1] and not seen[y, x + 1]:
                seen[y, x + 1] = True; stack.append((y, x + 1))
        out.append((n, sy / n, sx / n))
    return out


def measure(path, thresh=40):
    a = np.asarray(Image.open(path).convert("RGB"))
    black = a.max(axis=2) < thresh
    share = 100.0 * float(black.mean())
    small = black[::2, ::2]
    total = small.size
    h, w = small.shape
    areas = [100.0 * n / total for n, cy, cx in regions(small)
             if 0.10 * h <= cy <= 0.90 * h and 0.10 * w <= cx <= 0.90 * w]
    return {"file": os.path.basename(path), "black": share,
            "counts": [sum(1 for v in areas if v >= s) for s in SIZES],
            "largest": max(areas) if areas else 0.0}


def expand(group):
    label, pat = (group.split("=", 1) if "=" in group and not os.path.exists(group) else (None, group))
    if os.path.isdir(pat):
        files = sorted(glob.glob(os.path.join(pat, "*.png")))
        label = label or os.path.basename(os.path.normpath(pat))
    else:
        files = sorted(glob.glob(pat))
        label = label or pat
    return label, [f for f in files if not f.endswith(("-hdr.png", "-pq.png"))]


def main(argv):
    thresh, jpath, groups = 40, None, []
    i = 0
    while i < len(argv):
        if argv[i] == "--thresh":
            thresh = int(argv[i + 1]); i += 2
        elif argv[i] == "--json":
            jpath = argv[i + 1]; i += 2
        else:
            groups.append(argv[i]); i += 1
    if not groups:
        print(__doc__)
        return 1
    report = {}
    for g in groups:
        label, files = expand(g)
        rows = [measure(f, thresh) for f in files]
        report[label] = rows
        print("%s  (%d frames)" % (label, len(rows)))
        print("  %-28s %7s  %6s %6s %6s  %8s" % ("frame", "black%", ">=0.1%", ">=0.5%", ">=2%", "largest%"))
        for r in rows:
            print("  %-28s %7.2f  %6d %6d %6d  %8.2f%s" % (r["file"], r["black"], r["counts"][0], r["counts"][1],
                                                         r["counts"][2], r["largest"],
                                                         "   (dark frame, not in the mean)" if r["black"] > 90 else ""))
        lit = [r for r in rows if r["black"] <= 90]
        if lit:
            n = float(len(lit))
            print("  %-28s %7.2f  %6.1f %6.1f %6.1f  %8.2f" % ("MEAN", sum(r["black"] for r in lit) / n,
                  sum(r["counts"][0] for r in lit) / n, sum(r["counts"][1] for r in lit) / n,
                  sum(r["counts"][2] for r in lit) / n, sum(r["largest"] for r in lit) / n))
    if jpath:
        with open(jpath, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
