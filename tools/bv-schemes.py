# bv-schemes.py -- brief BV: writes reference\presets\Scheme - *.ini (partial
# overlays on reference\configs\monotone-post-0924.ini) and, with --render-dir,
# full render inis (base + overlay + the anchor's start palette) for the sheet.
# The presets are OFFSET PATTERNS: they never set a film hue; the rotation
# (hue_rotate_period, warped by hue_anchor_weight) carries the whole pattern.
# The render inis add oil_color_1..4 turned to the scheme's anchor hue only so
# a headless frame at t=40 s shows the named look (start phase, not a preset key).
import colorsys, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.join(ROOT, 'reference', 'configs', 'monotone-post-0924.ini')
PRESETS = os.path.join(ROOT, 'reference', 'presets')

EQ, SHARE3 = 0.75, 0.75   # A/B: equal_load 0.5 -> +29%, 1 -> -37% frame meanY vs RtF on a yellow film; 0.75 interpolates to ~RtF

# (file name, hue2, hue3, shadow hue or None, grade-only, [(named scheme, tier, anchor, note)])
PATTERNS = [
 ('Return to Form', None, None, None, False,
  [('Return to Form', 'HOME 1', 325, 'film only (proven-1)')]),
 ('Lightroom', None, None, 185, True,
  [('Lightroom', 'HOME 2', 325, 'magenta film + teal shadows; a split-tone GRADE, not a patch scheme')]),
 ('Magenta Mint', 180, None, None, False,
  [('Magenta / Mint', 'HOME 3', 325, 'seam red-orange-yellow (proven-2)')]),
 ('Magenta Cyan', -140, None, None, False,
  [('Magenta / Cyan', 'HOME 4', 325, 'seam violet-blue (proven-3)'),
   ('Red / Sky', 'HOME 6', 355, 'seam magenta-violet')]),
 ('Blue Coral', 160, None, None, False,
  [('Blue / Coral', 'HOME 5', 215, 'seam violet-magenta (proven-4)')]),
 ('Violet Amber', 125, None, None, False,
  [('Violet / Amber', 'HOME 7', 275, 'seam magenta-red; amber secondary stays bright (patches are not equal-loaded)')]),
 ('Teal Orange', -160, None, None, False,
  [('Teal / Orange', 'HOME 8', 190, 'seam green-yellow; the teal FILM is equal-loaded')]),
 ('Neon Demon', -140, 30, None, False,
  [('Neon Demon', 'T1 3-COLOUR', 325, 'turquoise + red, no yellow anywhere (best)')]),
 ('Euphoria', -45, 125, None, False,
  [('Euphoria', 'T5 3-COLOUR', 275, 'listed +125/-45; SWAPPED so the amber (yellow-band) member is the smaller third (user rule)')]),
 ('Warm Arc', -40, 30, None, False,
  [('Warm Arc', 'T2 3-COLOUR', 10, 'listed +30/-40; SWAPPED so the amber member is the smaller third (user rule)')]),
 ('Bright Triad', -120, 120, None, False,
  [('Bright Triad', 'T3a 3-COLOUR', 325, 'azure + lime; the lime third stays BRIGHT (patches are not equal-loaded)')]),
 ('Bright Split', 150, -150, None, False,
  [('Bright Split', 'T4b 3-COLOUR', 215, 'red + yellow; the yellow third stays BRIGHT (patches are not equal-loaded)')]),
 ('Lightroom Triad', None, None, 180, True,
  [('Lightroom Triad', 'T3b 3-COLOUR', 325, 'magenta film + teal shadows + gold highlights: a split-tone GRADE, not three patch colours')]),
 ('Synthwave', -145, 55, 275, False,
  [('Synthwave', 'Q2 4-COLOUR', 325, 'cyan + orange, violet darks (best)')]),
 ('Microscope', -105, 140, 275, False,
  [('Microscope', 'Q3 4-COLOUR', 230, 'green + red, violet darks')]),
 ('Square Plus', 180, 90, 235, False,
  [('Square+', 'Q1 4-COLOUR', 325, 'mint + gold, blue darks; the gold third stays BRIGHT')]),
]


def preset_text(fname, h2, h3, sh, grade, names):
    L = []
    L.append('; Scheme - %s.ini -- brief BV colour scheme: an OFFSET PATTERN, a partial overlay' % fname)
    L.append('; on reference\\configs\\monotone-post-0924.ini. It never sets a film hue: the pattern')
    L.append('; rides the hue rotation (hue_rotate_period) and hue_anchor_weight 1 makes the')
    L.append('; rotation linger at the proven anchors (magenta 325 most, blue 215, red 355,')
    L.append('; violet 275). The named look is what the pattern shows at its ANCHOR hint.')
    pat = 'mono (film only)' if h2 is None else ('hue2 %+d' % h2) + ('' if h3 is None else ', hue3 %+d' % h3)
    L.append('; Pattern: %s%s.' % (pat, '' if sh is None else ', shadow tint %d (BU)' % sh))
    L.append('; Named schemes on this pattern (tier, anchor = film hue of the named look):')
    for n, tier, anc, note in names:
        L.append(';   %-16s %-12s anchor %3d -- %s' % (n, tier, anc, note))
    if grade:
        L.append('; NOTE: needs BU-b highlights/balance (highlight_tone_*, tone_balance): this is a')
        L.append('; Lightroom-style split-tone GRADE, not patch colours. BU (shadow_tone*, merged)')
        L.append('; gives the teal shadows now; the gold highlights + balance arrive with BU-b.')
    elif sh is not None:
        L.append('; The 4th colour is the BU split tone on the dark tones at a FIXED hue')
        L.append('; (shadow_tone_hue), always on (shadow_tone_period 0), rule 7: cool/violet only.')
    L.append('; Rules (brief BV): oil + droplets black (dye_lum 0, dye_droplet_lum -1); FILMS')
    L.append('; equal-loaded (film_equal_load, dims only); PATCHES are never dimmed, so a')
    L.append('; yellow/gold/lime member stays bright; third colour <= ~15%% of the frame')
    L.append('; (film_hue3_share %.2f ~ 13%%).' % SHARE3)
    L.append('[liquid_acid]')
    kv = [('hue_sweep_period', '0'), ('hue_anchor_weight', '1'),
          ('dye_lum', '0'), ('dye_droplet_lum', '-1'),
          ('film_equal_load', '%g' % EQ)]
    if h2 is None:
        kv += [('film_hue2_amt', '0'), ('film_hue3_amt', '0')]
    else:
        kv += [('film_hue2', '%d' % h2), ('film_hue2_amt', '1'), ('film_hue2_scale', '0.40')]
        if h3 is None:
            kv += [('film_hue3_amt', '0')]
        else:
            kv += [('film_hue3', '%d' % h3), ('film_hue3_amt', '1'),
                   ('film_hue3_share', '%g' % SHARE3)]
    if sh is None:
        kv += [('shadow_tone', '0')]
    else:
        kv += [('shadow_tone', '0.6' if grade else '1'), ('shadow_tone_hue', '%d' % sh),
               ('shadow_tone_period', '0')]
        if grade:
            kv += [('shadow_tone_lift', '0.03')]
    for k, v in kv:
        L.append('%s = %s' % (k, v))
    return '\r\n'.join(L) + '\r\n'


def merge(base_text, overlay):
    """overlay: {section: [(k, v)]}; replace in place, else insert after the header."""
    lines = base_text.splitlines()
    for sec, kvs in overlay.items():
        for k, v in kvs:
            cur, done = None, False
            for i, ln in enumerate(lines):
                m = re.match(r'^\s*\[(.+?)\]', ln)
                if m:
                    cur = m.group(1)
                    continue
                if cur == sec and re.match(r'^\s*%s\s*=' % re.escape(k), ln):
                    lines[i] = '%s = %s' % (k, v)
                    done = True
                    break
            if not done:
                for i, ln in enumerate(lines):
                    if re.match(r'^\s*\[%s\]' % re.escape(sec), ln):
                        lines.insert(i + 1, '%s = %s' % (k, v))
                        break
    return '\r\n'.join(lines) + '\r\n'


def parse_overlay(text):
    ov, cur = {}, None
    for ln in text.splitlines():
        ln = ln.split(';')[0].strip()
        if not ln:
            continue
        m = re.match(r'^\[(.+)\]$', ln)
        if m:
            cur = m.group(1)
            ov.setdefault(cur, [])
            continue
        k, v = [x.strip() for x in ln.split('=', 1)]
        ov[cur].append((k, v))
    return ov


def palette_at(base_text, anchor):
    cols = {}
    for n in range(1, 5):
        m = re.search(r'(?m)^oil_color_%d\s*=\s*([\d.]+)\s+([\d.]+)\s+([\d.]+)' % n, base_text)
        cols[n] = tuple(float(x) for x in m.groups())
    h0 = colorsys.rgb_to_hsv(*cols[1])[0] * 360.0
    out = []
    for n in range(1, 5):
        h, s, v = colorsys.rgb_to_hsv(*cols[n])
        h = ((h * 360.0 + anchor - h0) % 360.0) / 360.0
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        out.append(('oil_color_%d' % n, '%.4f %.4f %.4f' % (r, g, b)))
    return out


if __name__ == '__main__':
    base = open(BASE, encoding='utf-8').read()
    rdir = sys.argv[sys.argv.index('--render-dir') + 1] if '--render-dir' in sys.argv else None
    for fname, h2, h3, sh, grade, names in PATTERNS:
        txt = preset_text(fname, h2, h3, sh, grade, names)
        p = os.path.join(PRESETS, 'Scheme - %s.ini' % fname)
        open(p, 'w', encoding='utf-8', newline='').write(txt)
        if rdir and (not grade or fname == 'Lightroom'):
            os.makedirs(rdir, exist_ok=True)
            ov = parse_overlay(txt)
            for n, tier, anc, note in names:
                ov2 = dict(ov)
                ov2['liquid_acid'] = ov['liquid_acid'] + palette_at(base, anc)
                slug = re.sub(r'[^A-Za-z0-9]+', '-', n).strip('-').lower()
                open(os.path.join(rdir, 'scheme-%s.ini' % slug), 'w', encoding='utf-8',
                     newline='').write(merge(base, ov2))
                print('%s\t%s\t%d\t%s' % (slug, n, anc, tier))
