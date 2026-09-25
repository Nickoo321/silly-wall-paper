#!/usr/bin/env python3
"""One-shot SEEDER for src/ui/keys.inc (UI-REHAUL phase 1a, auditor pre-flight item 6).

Reads the old SliderDef/CheckDef tables out of src/settings.cpp (default: `git show
main:src/settings.cpp`, since phase 1a deletes that file) and writes one X-macro row per key
with the keymeta columns in the SAME row: group, looks, gate, flags, special, front.

The seed rules below come from reference/FEATURES.md (sections, the "Known inert or
unverified keys" table, brief BJ's 30-key sweep) and the gates listed in the UI-REHAUL brief
(R2). After seeding, src/ui/keys.inc is the source of truth: hand-edit it, do NOT re-run this
script over it (it would drop hand fixes). tools/keymeta-check.ps1 validates the .inc.

Usage:  python tools/gen-keymeta.py [--settings <settings.cpp>] [--out src/ui/keys.inc]
"""
import argparse, re, subprocess, sys

BS = chr(92)

# --------------------------------------------------------------------------- parsing
def rows(t):
    out = []; depth = 0; cur = ''; ins = False; i = 0
    body = t[t.index('{') + 1:]
    while i < len(body):
        ch = body[i]
        if ins:
            cur += ch
            if ch == BS: cur += body[i + 1]; i += 2; continue
            if ch == '"': ins = False
        else:
            if ch == '"': ins = True; cur += ch
            elif ch == '{':
                depth += 1
                if depth == 1: cur = ''
                else: cur += ch
            elif ch == '}':
                depth -= 1
                if depth == 0: out.append(cur)
                elif depth < 0: break
                else: cur += ch
            elif ch == '/' and body[i + 1] == '/' and depth == 0:
                i = body.index('\n', i); continue
            else: cur += ch
        i += 1
    return out

def split(r):
    parts = []; cur = ''; ins = False; i = 0
    while i < len(r):
        ch = r[i]
        if ins:
            cur += ch
            if ch == BS: cur += r[i + 1]; i += 2; continue
            if ch == '"': ins = False
        elif ch == '"': ins = True; cur += ch
        elif ch == ',': parts.append(cur.strip()); cur = ''
        else: cur += ch
        i += 1
    if cur.strip(): parts.append(cur.strip())
    return parts

def narrow(x):  # L"..." -> "..." (UTF-8 narrow literal; the target compiles with /utf-8)
    if x == 'nullptr': return '""'
    assert x.startswith('L"'), x
    return x[1:]

def bare(x):
    return '' if x == 'nullptr' else x[2:-1]

# --------------------------------------------------------------------------- seed rules
def group_of(sec, key):
    if sec == 'liquid_acid':
        if key.startswith(('dye_droplet_',)) or key.startswith(('droplet', 'swarm_', 'weather', 'crust_')) \
           or key in ('diffraction', 'diffraction_px', 'depth_rise'):
            return 'G_DROPLETS'
        if key.startswith(('dye_',)) or key in ('dark_sat', 'toe_tint', 'mass_rim', 'ink_water', 'ink_shading',
                                                 'ink_levels', 'ink_soft', 'ink_mix', 'ink_hue_vary', 'ink_gain',
                                                 'seam_strength', 'seam_lo', 'speckle'):
            return 'G_MASSES'
        if key in ('hue_sweep_period', 'sweep_count', 'hue_rotate_period', 'ink_complement_lock',
                   'ink_complement_span', 'post_chroma', 'post_lift', 'oil_saturation') or key.startswith('film_hue'):
            return 'G_COLOUR'
        if key in ('grain', 'grain_scale', 'grain_shadow_weight'):
            return 'G_POST'
        if key in ('shadow_amt', 'shadow_len', 'shadow_soft', 'rise_bottom_light', 'oil_ink_blur'):
            return 'G_OPTICS'
        if key in ('cellulose_drift',):
            return 'G_MOTION'
        if key in ('blob_count', 'disc_frac', 'web_frac', 'bubble_frac', 'hole_weight', 'size_bias', 'big_bias',
                   'threshold', 'support_scale', 'flow_gain', 'curl_drift', 'repulsion', 'oil_drag', 'oil_dye_block',
                   'oil_viscosity', 'mouse_oil_mode', 'mouse_oil_radius', 'mouse_oil_gain', 'conserve_mass',
                   'spawn_grow_s', 'dissolve_s', 'accent_mode', 'accent_max_r', 'accent_frac', 'rise_respawn') \
           or key.startswith('rise_'):
            return 'G_OIL'
        return 'G_FILM'
    if sec == 'post':
        if key.startswith('lid') or key == 'glass_streaks': return 'G_LID'
        if key in ('shimmer', 'shimmer_px', 'vignette_wander', 'pixel_shift_px', 'rig_readjust',
                   'focus_tilt_period', 'focus_tilt_move_s', 'light_drift'):
            return 'G_MOTION'
        if key.startswith(('film_', 'post_blur')) or key in ('band_min', 'dither', 'artefact_lum_gate'):
            return 'G_POST'
        return 'G_OPTICS'
    if sec == 'ink': return 'G_INK'
    if sec == 'drops': return 'G_FLUID'
    if sec == 'sim': return 'G_FLUID'
    if sec == 'mirror': return 'G_OUTPUT'
    if sec == 'hdr': return 'G_OUTPUT'
    if sec == 'color':
        return 'G_COLOUR'
    if sec == 'cycle': return 'G_SYSTEM'
    if sec == 'look': return 'G_SYSTEM'
    if sec == 'general':
        return 'G_OUTPUT' if key == 'mirror_second' else 'G_SYSTEM'
    if sec == 'behavior':
        if key.startswith('hueshift') or key == 'color_cycle_period': return 'G_COLOUR'
        return 'G_FLUID'
    return 'G_SYSTEM'

def looks_of(sec, key):
    if sec == 'liquid_acid': return 'A'
    if sec == 'ink': return 'AI'            # the InkWater() block is shared with acid ink_mode=water
    if sec == 'sim' and key == 'shading': return 'FA'    # INK replaces the emboss (kDisplaySrc #ifdef INK)
    if sec == 'post' and key in ('dof_max_px', 'camera_focus', 'camera_field_curve', 'focus_tilt', 'focus_tilt_angle',
                                 'focus_band_px', 'focus_tilt_period', 'focus_tilt_move_s'):
        return 'A'                          # the CoC is written only by the LIQUID_ACID display block
    return 'FAI'

ACID_BANDS = ('ink_levels', 'ink_soft', 'ink_mix', 'ink_hue_vary', 'ink_complement_span', 'ink_gain',
              'seam_strength', 'seam_lo', 'ink_complement_lock')
POST_GATE = {
    'film_grain': 'post.film_grain>0', 'aberration': 'post.aberration>0', 'halo_px': 'post.halo>0',
    'post_glow_px': 'post.post_glow>0', 'post_glow_dark': 'post.post_glow>0', 'fog_px': 'post.fog>0',
    'fog_mass_gate': 'post.fog>0', 'bloom_px': 'post.bloom>0', 'bloom_warmth': 'post.bloom>0',
    'film_noise_size': 'post.film_noise>0', 'halation_px': 'post.halation>0',
    'halation_warmth': 'post.halation>0', 'halation_threshold': 'post.halation>0',
    'shimmer_px': 'post.shimmer>0', 'corner_warp_r': 'post.corner_warp>0',
    'film_artefact_rate': 'post.film_dust>0 || post.film_hairs>0 || post.film_scratches>0 || post.film_leak>0',
}

def gate_of(sec, key):
    if sec == 'liquid_acid':
        if key in ACID_BANDS: return 'liquid_acid.ink_water==0'
        if key.startswith('swarm_'): return 'liquid_acid.droplets==0'
        if key.startswith('droplet_racer_') and key != 'droplet_racer_frac':
            return 'liquid_acid.droplets>0 && liquid_acid.droplet_racer_frac>0'
        if key == 'droplet_coalesce_s': return 'liquid_acid.droplets>0 && liquid_acid.droplet_coalesce>0'
        if key.startswith('droplet_ring_') and key != 'droplet_ring_frac':
            return 'liquid_acid.droplets>0 && liquid_acid.droplet_ring_frac>0'
        if key.startswith(('droplet_', 'dye_droplet_', 'weather', 'crust_')): return 'liquid_acid.droplets>0'
        if key in ('dye_lum_vary', 'dye_hue_vary'): return 'liquid_acid.dye_lum>0'
        if key == 'grain': return 'post.film_grain==0'
        if key in ('grain_scale', 'grain_shadow_weight'): return 'post.film_grain==0 && liquid_acid.grain>0'
        if key.startswith('film_hue2') and key != 'film_hue2_amt': return 'liquid_acid.film_hue2_amt>0'
        if key == 'film_hue3': return 'liquid_acid.film_hue3_amt>0'
        if key == 'boundary_reflect_r': return 'liquid_acid.boundary_reflect_amt>0'
        if key in ('shadow_len', 'shadow_soft'): return 'liquid_acid.shadow_amt>0'
        if key.startswith('oil_penumbra_'): return 'liquid_acid.oil_penumbra>0'
        if key.startswith('cellulose_'): return 'liquid_acid.cellulose>0'
        if key == 'oil_fluor_reach': return 'liquid_acid.oil_fluor>0'
        if key == 'rise_respawn': return 'liquid_acid.rise_speed>0'
        if key == 'rise_parallax_dim': return 'liquid_acid.rise_parallax>0'
        if key in ('mouse_oil_radius', 'mouse_oil_gain'): return 'liquid_acid.mouse_oil_mode>0'
        if key == 'sweep_count': return 'liquid_acid.hue_sweep_period>0'
        if key == 'oil_edge_frac': return 'liquid_acid.oil_thin_edge>0'
        if key == 'meniscus_width': return 'liquid_acid.meniscus>0'
        if key == 'ink_shading': return 'sim.shading>0'
        return ''
    if sec == 'post':
        if key.startswith('lid_scratch_'): return 'post.lid>0 && post.lid_scratch>0'
        if key.startswith('lid_') or key == 'glass_streaks': return 'post.lid>0'
        if key.startswith('film_grain_'): return 'post.film_grain>0'
        if key.startswith('aberration_'): return 'post.aberration>0'
        return POST_GATE.get(key, '')
    if sec == 'ink':
        base = 'look==I || liquid_acid.ink_water>0'
        if key in ('core_knee', 'veil_floor', 'hdr_core'): return '(' + base + ') && ink.inverted>0'
        if key == 'vignette': return '(' + base + ') && ink.inverted==0'
        if key in ('edge_lo', 'edge_hi', 'edge_scale'): return '(' + base + ') && ink.edge_strength>0'
        if key == 'parallax_scale': return '(' + base + ') && ink.parallax>0'
        return base
    if sec == 'drops' and key != 'drops': return 'drops.drops>0'
    if sec == 'mirror':
        if key == 'mode': return ''
        if key == 'segments': return 'mirror.mode==4'
        return 'mirror.mode>0'
    if sec == 'cycle' and key != 'enabled': return 'cycle.enabled>0'
    if sec == 'behavior':
        if key in ('wanderer_count', 'wanderer_speed', 'wanderer_brightness', 'wanderer_scale',
                   'wanderer_resume_delay', 'wanderer_mode'):
            return 'behavior.wanderers>0'
        if key.startswith('dart_') and key != 'dart_enabled': return 'behavior.dart_enabled>0'
        if key.startswith('hueshift_') and key != 'hueshift_enabled': return 'behavior.hueshift_enabled>0'
        if key.startswith('idle_') and key != 'idle_splats': return 'behavior.idle_splats>0'
        if key == 'color_cycle_period': return 'color.colorful>0'
        return ''
    if sec == 'color':
        if key.startswith('curve_') and key != 'curve_enabled': return 'color.curve_enabled>0'
        if key == 'shadow_knee': return 'color.shadow_floor>0'
        if key in ('hue_center', 'hue_range', 'hue_linger'): return 'color.colorful>0'
        if key == 'more_colors': return 'color.colorful==0'
        return ''
    if sec == 'hdr' and key in ('knee', 'saturation', 'brightness', 'contrast'): return 'hdr.compensation>0'
    return ''

MOTION = {('post', k) for k in ('shimmer', 'shimmer_px', 'vignette_wander', 'pixel_shift_px', 'rig_readjust',
                                'focus_tilt_period', 'focus_tilt_move_s', 'light_drift')}
# gate / looks not traced (FEATURES.md "gate not traced", brief matrix "?", shared-block guesses)
UNVERIFIED = {('liquid_acid', k) for k in ('oil_ink_blur', 'oil_dye_block', 'weather', 'weather_period_s',
                                           'crust_hue_mix')} \
    | {('post', k) for k in ('film_hairs', 'film_leak', 'lid_sheen_px', 'fog_mass_gate', 'artefact_lum_gate')} \
    | {('behavior', k) for k in ('hueshift_enabled', 'hueshift_step', 'hueshift_linger', 'hueshift_glide',
                                 'hueshift_burst_steps', 'hueshift_off_time')} \
    | {('hdr', k) for k in ('knee', 'saturation', 'brightness', 'contrast', 'compensation')} \
    | {('color', k) for k in ('colorful', 'more_colors', 'hue_center', 'hue_range', 'hue_linger')}
SUPERSEDED = {('liquid_acid', 'dye_masses'), ('liquid_acid', 'dye_droplets')}
MACHINE = {('general', 'fps_limit'), ('general', 'mirror_second'), ('general', 'pause_on_fullscreen'),
           ('general', 'pause_on_maximized'), ('cycle', 'enabled'), ('cycle', 'dwell'),
           ('cycle', 'lerp'), ('cycle', 'jitter'), ('system', 'autostart'), ('hdr', 'gamut')}

ENUMS = {
    ('mirror', 'mode'): '0=Off|1=Horizontal|2=Vertical|3=Quad|4=Kaleidoscope',
    ('liquid_acid', 'oil_edge_mode'): '0=Soft film|1=Crisp',
    ('liquid_acid', 'accent_mode'): '0=Any size|1=Small only',
    ('liquid_acid', 'mouse_oil_mode'): '0=None|1=Push|2=Comb',
    ('liquid_acid', 'ink_water'): '0=Bands|1=Water',
    ('liquid_acid', 'dye_hue_follow'): '0=Fixed hue|1=Follows film hue',
}

# brief section 3B: proposed big knobs per look (phase 1b builds the front page from this column)
FRONT = {
    ('liquid_acid', 'film_level'): 'A', ('liquid_acid', 'hue_rotate_period'): 'A',
    ('liquid_acid', 'dye_hue'): 'A', ('liquid_acid', 'dye_sat'): 'A', ('liquid_acid', 'dye_lum'): 'A',
    ('liquid_acid', 'droplets'): 'A', ('liquid_acid', 'dye_droplet_hue'): 'A',
    ('liquid_acid', 'dye_droplet_sat'): 'A', ('liquid_acid', 'dye_droplet_lum'): 'A',
    ('liquid_acid', 'rise_speed'): 'A', ('post', 'bloom'): 'A', ('post', 'fog'): 'A', ('post', 'lid'): 'A',
    ('post', 'film_grain'): 'A', ('hdr', 'peak_nits'): 'FAI', ('mirror', 'mode'): 'FAI',
    ('color', 'hue_center'): 'F', ('color', 'hue_range'): 'F', ('behavior', 'color_cycle_period'): 'F',
    ('behavior', 'hueshift_enabled'): 'F', ('sim', 'splat_radius'): 'F', ('behavior', 'wanderer_count'): 'F',
    ('behavior', 'show_mouse'): 'F', ('color', 'post_saturation'): 'F',
    ('ink', 'inverted'): 'I', ('ink', 'chroma'): 'I', ('ink', 'density'): 'I', ('drops', 'drops'): 'I',
    ('drops', 'interval'): 'I', ('sim', 'gravity'): 'I', ('ink', 'pair_sweep_period'): 'I',
    ('ink', 'hdr_core'): 'I',
}

ZERO_WORDS = re.compile(r'0\s*=\s*(off|never|fixed)', re.I)

def special_of(sec, key, label):
    if (sec, key) in ENUMS: return 'enum:' + ENUMS[(sec, key)]
    if (sec, key) == ('hdr', 'peak_nits'): return 'peak'
    if key.startswith('dye_droplet_'): return 'neg:inherit'
    if (sec, key) in (('look', 'liquid_acid'), ('look', 'ink')): return 'look'
    if (sec, key) == ('system', 'autostart'): return 'autostart'
    m = ZERO_WORDS.search(label)
    if m: return 'zero:' + m.group(1).lower()
    return ''

GLOBAL = {('hdr', k) for k in ('peak_nits', 'knee', 'saturation', 'brightness', 'contrast', 'compensation', 'gamut')}     | {('mirror', k) for k in ('mode', 'segments', 'source', 'center_x', 'center_y', 'rotate_period', 'drift', 'soft')}     | {('general', k) for k in ('fps_limit', 'mirror_second', 'pause_on_fullscreen', 'pause_on_maximized')}     | {('system', 'autostart')}
# the cycle keys reach the conductor only through the src/ui/ui_cycle.h adapter
ADAPTER = {'&g_moodSettings.dwellMinutes': 'UiCycleDwellPtr()', '&g_moodSettings.transitionSec': 'UiCycleTransitionPtr()',
           '&g_moodSettings.jitter': 'UiCycleJitterPtr()', '&g_moodSettings.enabled': 'UiCycleEnabledPtr()'}

def flags_of(sec, key):
    f = []
    if (sec, key) in MOTION: f.append('KF_MOTION')
    if (sec, key) in UNVERIFIED or sec == 'ink': f.append('KF_UNVERIFIED')
    if (sec, key) in SUPERSEDED: f.append('KF_SUPERSEDED')
    if (sec, key) in MACHINE: f.append('KF_MACHINE')
    if (sec, key) in GLOBAL: f.append('KF_GLOBAL')
    return '|'.join(f) if f else '0'

# --------------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--settings', default='')
    ap.add_argument('--out', default='src/ui/keys.inc')
    a = ap.parse_args()
    if a.settings:
        src = open(a.settings, encoding='utf-8').read()
    else:
        src = subprocess.run(['git', 'show', 'main:src/settings.cpp'], capture_output=True,
                             check=True).stdout.decode('utf-8')
    i0 = src.index('s_sliders = {'); i1 = src.index('s_checks = {'); i2 = src.index('};', i1)
    S = [split(r) for r in rows(src[i0:i1])]
    C = [split(r) for r in rows(src[i1:i2])]
    out = []
    w = out.append
    w('// src/ui/keys.inc -- THE key table of the settings window: one X-macro row per ini key.')
    w('// Seeded once by tools/gen-keymeta.py from the old src/settings.cpp tables + reference/FEATURES.md;')
    w('// hand-maintained from here on (this file is the source of truth). tools/keymeta-check.ps1 validates it.')
    w('//')
    w('// KEY_SLIDER(label, min, max, step, decimals, floatPtr, intPtr, section, key, reinitWanderers, tip,')
    w('//            group, looks, gate, flags, special, front)')
    w('// KEY_CHECK (label, boolPtr, section, key, tip, group, looks, gate, flags, special, front)')
    w('//   group   G_* (ui_model.h)            looks  which look\'s render reads it: F fluid, A liquid acid, I ink')
    w('//   gate    "sec.key<op>num" joined by && / || (parens ok); pseudo-key look==F|A|I. False -> row disabled')
    w('//   flags   KF_MOTION (effect only visible over time, never hidden) | KF_UNVERIFIED (looks/gate not traced)')
    w('//           | KF_SUPERSEDED | KF_SHELL (read-only) | KF_MACHINE (shell key, never in a preset, not dirty)')
    w('//   special enum:v=Name|.. | peak | neg:<name> (value<0 shown as name) | zero:<word> | look | autostart')
    w('//   front   looks whose front page carries this knob (brief 3B proposal, used by phase 1b)')
    w('// Pointers: expressions over `c` (the FluidConfig being edited) or a listed global (ui_model.cpp asserts).')
    w('')
    for p in S:
        assert len(p) == 13, (len(p), p[:3])
        label, mn, mx, step, dec, fv, iv, sec, key, reinit, hdr, col, tip = p
        s, k = bare(sec), bare(key)
        fv = ADAPTER.get(fv, fv)
        w('KEY_SLIDER(%s, %s, %s, %s, %s, %s, %s, "%s", "%s", %s, %s,\n           %s, "%s", "%s", %s, "%s", "%s")' % (
            narrow(label), mn, mx, step, dec, fv, iv, s, k, reinit, narrow(tip),
            group_of(s, k), looks_of(s, k), gate_of(s, k), flags_of(s, k), special_of(s, k, bare(label)),
            FRONT.get((s, k), '')))
    # rows the old window drew as bespoke combos / radios; now ordinary table rows
    extra = [
        ('"Wanderer path"', '0', '2', '1', '0', 'nullptr', '&c.wandererMode', 'behavior', 'wanderer_mode', 'true',
         '"Path the wanderers follow"', 'enum:0=Random wander|1=Circle|2=Figure 8'),
        ('"Colour gamut"', '0', '2', '1', '0', 'nullptr', '&g_gamutMode', 'hdr', 'gamut', 'false',
         '"Output gamut while Windows HDR is on"', 'enum:0=sRGB|1=Display-P3|2=BT.2020 (QD-OLED)'),
        ('"Sim resolution"', '32', '512', '1', '0', 'nullptr', '&c.simRes', 'sim', 'sim_res', 'false',
         '"Fluid grid size. Read-only here: 256 is the WE grid (AGENTS.md hard constraint)"', ''),
        ('"Dye resolution"', '256', '4096', '1', '0', 'nullptr', '&c.dyeRes', 'sim', 'dye_res', 'false',
         '"Dye texture size. Read-only here: 4096 is the live value (AGENTS.md hard constraint)"', ''),
    ]
    for label, mn, mx, step, dec, fv, iv, s, k, reinit, tip, spec in extra:
        flags = flags_of(s, k)
        if k in ('sim_res', 'dye_res'):
            flags = 'KF_SHELL'
        w('KEY_SLIDER(%s, %s, %s, %s, %s, %s, %s, "%s", "%s", %s, %s,\n           %s, "%s", "%s", %s, "%s", "%s")' % (
            label, mn, mx, step, dec, fv, iv, s, k, reinit, tip, group_of(s, k), looks_of(s, k), gate_of(s, k),
            flags, spec, FRONT.get((s, k), '')))
    for p in C:
        assert len(p) == 7, p[:3]
        label, val, sec, key, hdr, col, tip = p
        s, k = bare(sec), bare(key)
        val = ADAPTER.get(val, val)
        if val == 'nullptr':  # autostart row (registry-backed)
            s, k = 'system', 'autostart'
            tip = 'L"Start the wallpaper when you sign in (HKCU Run key)"'
        w('KEY_CHECK(%s, %s, "%s", "%s", %s,\n          %s, "%s", "%s", %s, "%s", "%s")' % (
            narrow(label), val, s, k, narrow(tip), group_of(s, k), looks_of(s, k), gate_of(s, k),
            flags_of(s, k), special_of(s, k, bare(label)), FRONT.get((s, k), '')))
    open(a.out, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
    print('wrote %s: %d sliders + %d extra + %d checks' % (a.out, len(S), len(extra), len(C)))

if __name__ == '__main__':
    main()
