"""Black share of --shot frames (brief BZ BLACK-MASS, pre-flight items 13-14).

black % = share of the pixels of the 8-bit SDR <out>.png (display-referred
scRGB / sdrScale, main.cpp WriteShotPair) with max(R,G,B) < 40 -- popdetect.py's
`hole` class, ~2% of SDR white linear. It is measured on the PNG, never on the
in-process coverage readback (that one downsamples the FLUID DYE texture, not
the frame) and never from the shared %TEMP% ShotLog.

    python tools/black-share.py [--thresh 40] [--frames] [--deltas] [--json out.json] GROUP [GROUP ...]
        GROUP = label=glob | glob | directory       (a directory = every *.png in it
                                                     that is not -hdr / -pq)
      prints, per group: frames, mean, min, max (points of 100), and with --deltas
      the largest |black %| change between consecutive frames (sorted by name).
      With more than one group it also prints the mean of the group means.

    python tools/black-share.py merge <base.ini> <overlay.ini> <out.ini> [sec.key=val ...]
      compose a FULL ini from a base + a PARTIAL overlay the way
      tools/preset-identity.ps1 New-ComposedIni does (colliding sections merged,
      overlay keys first, so the profile API's first-match reads them; new
      sections appended), then apply sec.key=val overrides the same way.
      Writes UTF-8 without a BOM, CRLF.
"""
import glob
import json
import os
import re
import sys

try:
    import numpy as np
    from PIL import Image
except ImportError:  # fallback: session scratchpad copy, as ab.py
    sys.path.insert(0, r"C:/Users/abg77/AppData/Local/Temp/claude/"
                       r"C--Users-abg77-OneDrive-Desktop-wall-paper-engine-claude-code/"
                       r"8c9d0c8d-d72a-4f56-9d50-a24cc6eac2d0/scratchpad/pylibs")
    import numpy as np
    from PIL import Image


def black_pct(path, thresh=40):
    a = np.asarray(Image.open(path).convert('RGB'))
    return 100.0 * float((a.max(axis=2) < thresh).mean())


def frames_of(spec):
    if os.path.isdir(spec):
        files = glob.glob(os.path.join(spec, '*.png'))
    else:
        files = glob.glob(spec)
    files = [f for f in files if not re.search(r'-(hdr|pq)\.png$', f, re.I)]
    return sorted(files)


def measure(argv):
    thresh = 40
    show_frames = False
    deltas = False
    jout = None
    groups = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--thresh':
            thresh = int(argv[i + 1]); i += 2; continue
        if a == '--frames':
            show_frames = True; i += 1; continue
        if a == '--deltas':
            deltas = True; i += 1; continue
        if a == '--json':
            jout = argv[i + 1]; i += 2; continue
        if '=' in a and not os.path.exists(a):
            label, spec = a.split('=', 1)
        else:
            label, spec = a, a
        groups.append((label, spec))
        i += 1
    if not groups:
        print(__doc__)
        return 2
    result = []
    for label, spec in groups:
        files = frames_of(spec)
        if not files:
            print(f'{label}: NO FRAMES for {spec}')
            result.append({'label': label, 'n': 0})
            continue
        vals = [black_pct(f, thresh) for f in files]
        if show_frames:
            for f, v in zip(files, vals):
                print(f'  {os.path.basename(f)}  {v:6.2f}')
        g = {'label': label, 'n': len(vals), 'mean': sum(vals) / len(vals),
             'min': min(vals), 'max': max(vals), 'frames': vals,
             'files': [os.path.basename(f) for f in files]}
        line = (f'{label}: black% mean {g["mean"]:5.1f}  min {g["min"]:5.1f}  '
                f'max {g["max"]:5.1f}  ({len(vals)} frames, max(RGB)<{thresh})')
        if deltas and len(vals) > 1:
            d = [abs(vals[k] - vals[k - 1]) for k in range(1, len(vals))]
            k = max(range(len(d)), key=lambda j: d[j])
            g['max_delta'] = d[k]
            g['max_delta_at'] = os.path.basename(files[k + 1])
            line += f'  max |delta| {d[k]:.2f} at {g["max_delta_at"]}'
        print(line)
        result.append(g)
    good = [g for g in result if g.get('n')]
    if len(good) > 1:
        mm = [g['mean'] for g in good]
        print(f'ALL: mean of {len(mm)} means {sum(mm) / len(mm):5.1f}  '
              f'(means {", ".join(f"{m:.1f}" for m in mm)}; '
              f'spread {max(mm) - min(mm):.1f})')
    if jout:
        with open(jout, 'w', encoding='utf-8') as f:
            json.dump(result, f, indent=1)
    return 0


def parse_ini(text):
    """-> prelude lines, ordered [(name_lower, header_line, [body lines])]"""
    prelude, secs, cur = [], [], None
    for line in text.splitlines():
        m = re.match(r'^\s*\[([^\]]+)\]', line)
        if m:
            cur = (m.group(1).strip().lower(), line, [])
            secs.append(cur)
            continue
        (prelude if cur is None else cur[2]).append(line)
    return prelude, secs


def merge(base_path, over_path, out_path, sets):
    base = open(base_path, encoding='utf-8-sig').read()
    over = open(over_path, encoding='utf-8-sig').read() if over_path and over_path != '-' else ''
    # overrides are one more overlay, applied FIRST so they win over both
    extra = {}
    for s in sets:
        k, v = s.split('=', 1)
        sec, key = k.split('.', 1)
        extra.setdefault(sec.strip().lower(), []).append(f'{key.strip()} = {v.strip()}')
    o_pre, o_secs = parse_ini(over)
    over_body = {}
    order = []
    for name, hdr, body in o_secs:
        if name not in over_body:
            over_body[name] = (hdr, [])
            order.append(name)
        over_body[name][1].extend(l for l in body if l.strip())
    for name, lines in extra.items():
        if name not in over_body:
            over_body[name] = (f'[{name}]', [])
            order.append(name)
        over_body[name] = (over_body[name][0], lines + over_body[name][1])
    b_pre, b_secs = parse_ini(base)
    base_names = {n for n, _, _ in b_secs}
    out = ['; black-share composed ini: base + overlay (colliding sections merged: overlay keys first)']
    out += b_pre
    done = set()
    for name, hdr, body in b_secs:
        out.append(hdr)
        if name in over_body and name not in done:
            done.add(name)
            out += over_body[name][1]
        out += body
    out += [l for l in o_pre if l.strip()]
    for name in order:
        if name in base_names:
            continue
        out.append(over_body[name][0])
        out += over_body[name][1]
    d = os.path.dirname(out_path)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(out_path, 'w', encoding='utf-8', newline='\r\n') as f:
        f.write('\n'.join(out) + '\n')
    print(f'merged {base_path} + {over_path or "-"} ({len(sets)} overrides) -> {out_path}')
    return 0


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'merge':
        if len(sys.argv) < 5:
            print(__doc__)
            sys.exit(2)
        sys.exit(merge(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:]))
    sys.exit(measure(sys.argv[1:]))
