#
# preset-identity manifest: one row per ini the project ships under
# reference\configs, reference\presets and reference\moods (the moods folder
# does not exist yet; any ini that lands there must get a row here, and
# `tools\preset-identity.ps1 -DryRun` lists it as UNCLASSIFIED until it does).
#
# Columns
#   Path   ini path relative to the repo root (the baseline key)
#   Look   fluid | acid | ink | overlay | test
#   Base   PARTIAL overlays only: the full ini the overlay is composed onto
#          (base text + overlay text concatenated into a BOM-less temp ini).
#          Empty for full inis. An ini without a [look] style key is NOT an
#          overlay by itself: --ini loads onto built-in defaults, where the
#          style defaults to fluid, so the parity-*/eyes-* configs are full.
#   Delay  --shot-delay seconds (60 default, 15 for test inis)
#   Tier   fast | full | skip.  -Tier fast = fast rows; -Tier full = fast +
#          full rows; -Tier all = every row including skip.
#   Note   why the row is in its tier / what to watch for
#
# PowerShell 5.1 readable: Import-PowerShellDataFile.
@{
    Rows = @(
        # ---------------- reference\configs ----------------
        @{ Path = 'reference\configs\we-look-live.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'FLUID PARITY: rendered first by the parity check (md5 10E36EBF...), never re-rendered as a row' }
        @{ Path = 'reference\configs\acid-rise-12.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'LIVE preset; ink_mode=water; post bloom 0.30' }
        @{ Path = 'reference\configs\acid-rise-2hue.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'v1 default list; ink_mode=water' }
        @{ Path = 'reference\configs\acid-rise-8020.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'v1 default list; ink_mode=water' }
        @{ Path = 'reference\configs\acid-rise-rotate.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'v1 default list; ink_mode=water' }
        @{ Path = 'reference\configs\eyes-diff015.ini'; Look = 'test'; Base = ''; Delay = 15; Tier = 'skip'
           Note = 'eye A/B test: we-look-live with one knob changed' }
        @{ Path = 'reference\configs\eyes-diff03.ini'; Look = 'test'; Base = ''; Delay = 15; Tier = 'skip'
           Note = 'eye A/B test: we-look-live with one knob changed' }
        @{ Path = 'reference\configs\eyes-diff04.ini'; Look = 'test'; Base = ''; Delay = 15; Tier = 'skip'
           Note = 'eye A/B test: we-look-live with one knob changed' }
        @{ Path = 'reference\configs\eyes-peak0.ini'; Look = 'test'; Base = ''; Delay = 15; Tier = 'skip'
           Note = 'eye A/B test: we-look-live with one knob changed' }
        @{ Path = 'reference\configs\eyes-shadow0.ini'; Look = 'test'; Base = ''; Delay = 15; Tier = 'skip'
           Note = 'eye A/B test: we-look-live with one knob changed' }
        @{ Path = 'reference\configs\ink-auto-bursts-drops.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink + [drops] emitter' }
        @{ Path = 'reference\configs\ink-auto-bursts.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'same keys as presets\Ink - auto bursts (md5 should match it)' }
        @{ Path = 'reference\configs\ink-auto-drops.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink + [drops] emitter' }
        @{ Path = 'reference\configs\ink-duo-bursts-yellow-magenta.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'same keys as presets\Ink - duo bursts yellow' }
        @{ Path = 'reference\configs\ink-duo-pour-teal-vermillion.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'same keys as presets\Ink - duo pour teal' }
        @{ Path = 'reference\configs\ink-duo-pour-yellow-magenta.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'same keys as presets\Ink - duo pour yellow' }
        @{ Path = 'reference\configs\ink-inverted.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink INVERTED mode; same keys as presets\Ink - inverted (fast)' }
        @{ Path = 'reference\configs\ink-paper.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'ink PAPER mode (inverted=0): the only paper-mode ini' }
        @{ Path = 'reference\configs\ink-pour.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'same keys as presets\Ink - paint pour (fast)' }
        @{ Path = 'reference\configs\lapd-look-candidate.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'LAPD look candidate r8; ink_mode=water' }
        @{ Path = 'reference\configs\lapd-look-user-0924.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'user live-tuned post 0924; ink_mode=water' }
        @{ Path = 'reference\configs\layering1-09-real.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'explore.py variant 09/12 of liquid-acid-a; ink_mode=water' }
        @{ Path = 'reference\configs\liquid-acid-a-glass.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink_mode=bands (default)' }
        @{ Path = 'reference\configs\liquid-acid-a-real.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink_mode=bands (default)' }
        @{ Path = 'reference\configs\liquid-acid-a-sweep.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink_mode=bands (default)' }
        @{ Path = 'reference\configs\liquid-acid-a.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink_mode=bands (default); presets\Liquid Acid A is the fast row' }
        @{ Path = 'reference\configs\liquid-acid-b.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink_mode=bands (default)' }
        @{ Path = 'reference\configs\liquid-acid-c.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink_mode=bands (default)' }
        @{ Path = 'reference\configs\liquid-acid-water-glass.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink_mode=water + glass film' }
        @{ Path = 'reference\configs\liquid-acid-water.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'ink_mode=water' }
        @{ Path = 'reference\configs\mirror-quad.ini'; Look = 'overlay'; Base = 'reference\configs\we-look-live.ini'; Delay = 60; Tier = 'full'
           Note = 'PARTIAL [mirror] overlay on the fluid look (the presets\Mirror rows cover acid)' }
        @{ Path = 'reference\configs\monotone-post-0924.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'monotone look with the user 0924 post; ink_mode=water' }
        @{ Path = 'reference\configs\parity-hdr700.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'real WE-parity variant: parity.ini + peak_nits 700 / knee 0.70' }
        @{ Path = 'reference\configs\parity-it40.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'real WE-parity variant: parity.ini + pressure_iterations 40' }
        @{ Path = 'reference\configs\parity-plus.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'real WE-parity variant: parity.ini + 700 nits + sim_res 256' }
        @{ Path = 'reference\configs\parity-shadow.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'real WE-parity variant: parity.ini + shadow_floor 0.10' }
        @{ Path = 'reference\configs\parity-sim256.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'real WE-parity variant: parity.ini + sim_res 256 / vorticity 48' }
        @{ Path = 'reference\configs\parity-v48.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'real WE-parity variant: parity.ini + vorticity 48' }
        @{ Path = 'reference\configs\parity.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'the original WE-parity config (sim_res 512, vorticity 24)' }
        @{ Path = 'reference\configs\we-original-preset.ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'UTF-16LE WITHOUT a BOM: the profile API may read it as ANSI (= built-in defaults); still deterministic' }

        # ---------------- reference\presets (tray) ----------------
        @{ Path = 'reference\presets\Ink - auto bursts.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'tray ink preset (inverted, bursts)' }
        @{ Path = 'reference\presets\Ink - duo bursts yellow.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray ink preset' }
        @{ Path = 'reference\presets\Ink - duo pour teal.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray ink preset' }
        @{ Path = 'reference\presets\Ink - duo pour yellow.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray ink preset' }
        @{ Path = 'reference\presets\Ink - inverted.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'tray ink preset, INVERTED mode' }
        @{ Path = 'reference\presets\Ink - paint pour.ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'tray ink preset (pour)' }
        @{ Path = 'reference\presets\Liquid Acid - magenta on black (real oil).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'tray acid preset; ink_mode=water, no [post]' }
        @{ Path = 'reference\presets\Liquid Acid - oil on ink water (glass film).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray acid preset; ink_mode=water' }
        @{ Path = 'reference\presets\Liquid Acid - oil on ink water.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'tray acid preset; ink_mode=water + [drops]' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (80-20 complement, mouse push).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (80-20, NO lens effects).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (camera medium).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant; was the v1 default tray row' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (camera strong).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (camera subtle).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (cellulose medium).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (cellulose strong).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (cellulose subtle).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (crisp edge).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (film overlay medium).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant (full ini despite the name)' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (film overlay strong).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant (full ini despite the name)' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (film overlay subtle).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant (full ini despite the name)' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (lid medium).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (lid strong).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (lid subtle).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (light medium).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (light strong).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (light subtle).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (mouse comb).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant (headless: no mouse input)' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (mouse push).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant (headless: no mouse input)' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours (two hues, mouse push).ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray rising-colours variant (headless: no mouse input)' }
        @{ Path = 'reference\presets\Liquid Acid - rising colours.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'tray acid preset, the rising-colours family head; ink_mode=water + [post]' }
        @{ Path = 'reference\presets\Liquid Acid - rising hue rotation.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray acid preset' }
        @{ Path = 'reference\presets\Liquid Acid A - glass film.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray acid preset; ink_mode=bands (default)' }
        @{ Path = 'reference\presets\Liquid Acid A - palette sweep.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray acid preset; ink_mode=bands (default)' }
        @{ Path = 'reference\presets\Liquid Acid A - real oil.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'tray acid preset; ink_mode=bands (default)' }
        @{ Path = 'reference\presets\Liquid Acid A.ini'; Look = 'acid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'tray acid preset; the ink_mode=bands (default) representative' }
        @{ Path = 'reference\presets\Mirror - kaleidoscope 6 (overlay).ini'; Look = 'overlay'; Base = 'reference\configs\acid-rise-12.ini'; Delay = 60; Tier = 'fast'
           Note = 'PARTIAL [mirror] overlay (mode 4) on the live acid preset' }
        @{ Path = 'reference\presets\Mirror - off (overlay).ini'; Look = 'overlay'; Base = 'reference\configs\acid-rise-12.ini'; Delay = 60; Tier = 'full'
           Note = 'PARTIAL [mirror] mode=0: md5 must equal acid-rise-12 (mode 0 is bit-identical)' }
        @{ Path = 'reference\presets\Mirror - quad (overlay).ini'; Look = 'overlay'; Base = 'reference\configs\acid-rise-12.ini'; Delay = 60; Tier = 'fast'
           Note = 'PARTIAL [mirror] overlay (mode 3) on the live acid preset' }
        @{ Path = 'reference\presets\User - Preset 5 (2026-09-17).ini'; Look = 'ink'; Base = ''; Delay = 60; Tier = 'full'
           Note = 'user-saved ink preset; no [moods]/[cycle]/[general] (built-in defaults); ink_mode=bands is inert under style=ink' }
        @{ Path = 'reference\presets\WE parity (fluid).ini'; Look = 'fluid'; Base = ''; Delay = 60; Tier = 'fast'
           Note = 'tray fluid preset; same keys as we-look-live, md5 must equal the parity md5' }
    )
}
