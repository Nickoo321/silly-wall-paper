"""Random-variation experiment tool: mutate a base ini per a JSON knob space,
render each variant headless (GPU-locked, one at a time), and build a labeled
contact sheet.

usage:
  python tools/explore.py --space tools/explore-space-layering.json --seed 7 \
      --count 12 --out build2/shots/explore/batch1 [--drop 480,90,20] \
      [--base <ini>] [--time 75] [--width 960] [--height 540] [--hdr on]

Space JSON shape (see tools/explore-space-layering.json):
  { "bases": [ { "name": ..., "ini": "reference/configs/X.ini",
                 "keys": {"section.key": [choices...] | {"min":.., "max":..}},
                 "groups": {"group_name": [ {"label": "...", "values": {"section.key": val, ...}}, ... ] } },
               ... ] }
  (single-base mode: omit "bases" and put "keys"/"groups" at the top level,
  then pass --base <ini> on the command line.)

For each variant i in 1..count: pick a base (cycling the bases list), draw a
random value per key (uniform choice from a list, or random.uniform for a
{"min","max"} range) and one option per group, write
<out>-NN.ini (header comment lists what was mutated), render it, and after
all variants write <out>-sheet.png (a labeled contact sheet) and <out>.json
(machine-readable record of every choice). Deterministic given --seed.

GPU lock: only one render (any --shot run, anywhere) runs at a time. This
script waits for build2/shots/gpu.lock to be absent, creates it with its own
name+time, renders, and always removes it (finally-block) -- per AGENTS.md.
Never touches the user's build\\FluidWallpaper.exe or Wallpaper Engine.
"""
import argparse
import configparser
import json
import os
import random
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
LOCK = os.path.join(ROOT, 'build2', 'shots', 'gpu.lock')
PYLIBS = r"C:/Users/abg77/AppData/Local/Temp/claude/C--Users-abg77-OneDrive-Desktop-wall-paper-engine-claude-code/8c9d0c8d-d72a-4f56-9d50-a24cc6eac2d0/scratchpad/pylibs"


def wait_for_lock_free(poll_s=5, timeout_s=1800):
    waited = 0
    while os.path.exists(LOCK):
        if waited >= timeout_s:
            raise RuntimeError(f"gpu.lock held for over {timeout_s}s, giving up: {LOCK}")
        time.sleep(poll_s)
        waited += poll_s


def acquire_lock(tag):
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    with open(LOCK, 'w') as f:
        f.write(f"explore.py {tag} {time.strftime('%Y-%m-%d %H:%M:%S')}\n")


def release_lock():
    try:
        if os.path.exists(LOCK):
            os.remove(LOCK)
    except OSError:
        pass


def render_locked(exe, ini_path, out_png, width, height, hdr, delay, yield_ms, drop, tag):
    wait_for_lock_free()
    acquire_lock(tag)
    try:
        cmd = [exe, '--shot', out_png, '--ini', ini_path,
               '--hdr', hdr, '--shot-delay', str(delay),
               '--shot-size', f'{width}x{height}', '--shot-yield', str(yield_ms)]
        if drop:
            cmd += ['--shot-drop', drop]
        proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=600)
        return proc.returncode, proc.stdout, proc.stderr
    finally:
        release_lock()


def load_space(path):
    with open(path) as f:
        return json.load(f)


def pick_value(rng, spec):
    if isinstance(spec, dict) and 'min' in spec:
        return round(rng.uniform(spec['min'], spec['max']), 4)
    return rng.choice(spec)


def fmt_value(v):
    if isinstance(v, float):
        s = f'{v:.4f}'.rstrip('0').rstrip('.')
        return s if s else '0'
    return str(v)


def apply_overrides(base_ini_path, overrides):
    cfg = configparser.RawConfigParser()
    cfg.optionxform = str
    with open(base_ini_path, 'r', encoding='utf-8') as f:
        cfg.read_file(f)
    for dotted, value in overrides.items():
        section, key = dotted.split('.', 1)
        if not cfg.has_section(section):
            cfg.add_section(section)
        cfg.set(section, key, fmt_value(value))
    return cfg


def next_from_queue(rng, queues, qkey, options):
    """Shuffle-without-replacement per (base, group): full coverage of a small
    option list before any repeat, instead of pure random.choice() which can
    skip options and duplicate others when slots-per-base ~= len(options)."""
    q = queues.get(qkey)
    if not q:
        q = list(range(len(options)))
        rng.shuffle(q)
        queues[qkey] = q
    idx = q.pop()
    return options[idx]


def build_variant(rng, base, queues):
    """Return (overrides dict, list of (source, label_or_kv) for the sheet)."""
    overrides = {}
    mutated = []
    for key in sorted(base.get('keys', {})):
        val = pick_value(rng, base['keys'][key])
        overrides[key] = val
        mutated.append(('key', f'{key.split(".", 1)[1]} {fmt_value(val)}'))
    for gname in sorted(base.get('groups', {})):
        opts = base['groups'][gname]
        opt = next_from_queue(rng, queues, (base['name'], gname), opts)
        for k, v in opt['values'].items():
            overrides[k] = v
        mutated.append(('group', opt.get('label', gname)))
    return overrides, mutated


def pick_labels(mutated, n=3):
    """Prefer group labels (curated, descriptive); top up with key labels."""
    groups = [m[1] for m in mutated if m[0] == 'group']
    keys = [m[1] for m in mutated if m[0] == 'key']
    labels = (groups + keys)[:n]
    return [l.replace('=', ':') for l in labels]


def build_sheet(out_prefix, variants, cols=4):
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:  # fallback: session scratchpad copy
        sys.path.insert(0, PYLIBS)
        from PIL import Image, ImageDraw, ImageFont
    tw, th, pad, head = 320, 180, 6, 46
    rows = (len(variants) + cols - 1) // cols
    sheet = Image.new('RGB', (pad + cols * (tw + pad), pad + rows * (th + head + pad)), (18, 18, 18))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype('arial.ttf', 13)
    except Exception:
        font = ImageFont.load_default()
    for i, v in enumerate(variants):
        r, c = divmod(i, cols)
        x = pad + c * (tw + pad)
        y = pad + r * (th + head + pad)
        png = v['png']
        if os.path.exists(png):
            im = Image.open(png).convert('RGB').resize((tw, th), Image.LANCZOS)
            sheet.paste(im, (x, y))
        else:
            d.rectangle([x, y, x + tw, y + th], outline=(180, 40, 40), width=2)
            d.text((x + 6, y + th // 2), 'MISSING', fill=(255, 80, 80), font=font)
        title = f"{v['index']:02d} [{v['base']}]"
        d.text((x + 2, y + th + 2), title, fill=(255, 220, 140), font=font)
        for li, lab in enumerate(v['labels']):
            d.text((x + 2, y + th + 16 + li * 12), lab, fill=(230, 230, 230), font=font)
    out = out_prefix + '-sheet.png'
    sheet.save(out, optimize=True)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--space', required=True)
    ap.add_argument('--base', default=None, help='fallback base ini for single-base-mode space files')
    ap.add_argument('--seed', type=int, required=True)
    ap.add_argument('--count', type=int, required=True)
    ap.add_argument('--out', required=True, help='output path prefix, e.g. build2/shots/explore/batch1')
    ap.add_argument('--exe', default=os.path.join('build2', 'FluidWallpaper.exe'))
    ap.add_argument('--width', type=int, default=960)
    ap.add_argument('--height', type=int, default=540)
    ap.add_argument('--time', type=float, default=75)
    ap.add_argument('--hdr', default='on')
    ap.add_argument('--shot-yield', dest='shot_yield', type=int, default=2)
    ap.add_argument('--drop', default=None, help='X,Y,T scripted ink drop applied to every variant')
    args = ap.parse_args()

    space = load_space(args.space)
    if 'bases' in space:
        bases = space['bases']
    else:
        name = os.path.splitext(os.path.basename(args.base))[0] if args.base else 'base'
        bases = [{'name': name, 'ini': args.base, 'keys': space.get('keys', {}), 'groups': space.get('groups', {})}]
        if not args.base:
            print('ERROR: space has no "bases"; --base <ini> is required in single-base mode', file=sys.stderr)
            sys.exit(2)

    out_dir = os.path.dirname(args.out) or '.'
    os.makedirs(out_dir, exist_ok=True)

    rng = random.Random(args.seed)
    exe_abs = os.path.join(ROOT, args.exe) if not os.path.isabs(args.exe) else args.exe
    queues = {}

    record = []
    for i in range(1, args.count + 1):
        base = bases[(i - 1) % len(bases)]
        base_ini = base['ini'] if os.path.isabs(base['ini']) else os.path.join(ROOT, base['ini'])
        overrides, mutated = build_variant(rng, base, queues)
        cfg = apply_overrides(base_ini, overrides)

        ini_out = f'{args.out}-{i:02d}.ini'
        ini_out_abs = ini_out if os.path.isabs(ini_out) else os.path.join(ROOT, ini_out)
        header_lines = [
            f'; explore.py variant {i:02d}/{args.count} seed={args.seed} base={base["name"]} ({base["ini"]})',
            '; mutated: ' + ', '.join(
                f'{k}={fmt_value(v)}' for k, v in sorted(overrides.items())
            ),
        ]
        with open(ini_out_abs, 'w', encoding='utf-8') as f:
            for hl in header_lines:
                f.write(hl + '\n')
            cfg.write(f)

        png_out = f'{args.out}-{i:02d}.png'
        png_out_abs = png_out if os.path.isabs(png_out) else os.path.join(ROOT, png_out)
        print(f'[{i:02d}/{args.count}] base={base["name"]} rendering -> {png_out}', flush=True)
        rc, out, err = render_locked(
            exe_abs, ini_out_abs, png_out_abs, args.width, args.height,
            args.hdr, args.time, args.shot_yield, args.drop,
            tag=f'explore.py variant {i:02d}')
        ok = (rc == 0) and os.path.exists(png_out_abs)
        if not ok:
            print(f'  WARNING: render failed rc={rc}\nSTDOUT:\n{out}\nSTDERR:\n{err}', file=sys.stderr)
            if i == 1:
                print('ABORT: first variant failed to render -- likely a CLI/flag bug, '
                      'not a content dud. Stopping before burning the render budget.', file=sys.stderr)
                sys.exit(3)

        labels = pick_labels(mutated)
        record.append({
            'index': i,
            'base': base['name'],
            'base_ini': base['ini'],
            'ini': ini_out,
            'png': png_out,
            'ok': ok,
            'overrides': {k: (v if isinstance(v, str) else v) for k, v in overrides.items()},
            'labels': labels,
        })

    sheet_path = build_sheet(os.path.join(ROOT, args.out), [
        {**r, 'png': os.path.join(ROOT, r['png'])} for r in record
    ])

    json_path = f'{args.out}.json'
    json_path_abs = json_path if os.path.isabs(json_path) else os.path.join(ROOT, json_path)
    with open(json_path_abs, 'w', encoding='utf-8') as f:
        json.dump({'seed': args.seed, 'count': args.count, 'space': args.space, 'variants': record}, f, indent=2)

    print(f'wrote {sheet_path}')
    print(f'wrote {json_path_abs}')


if __name__ == '__main__':
    main()
