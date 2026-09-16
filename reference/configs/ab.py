"""Build an A/B sheet: rows = frames (by elapsed seconds), cols = variants.
usage: python ab.py out.png "Label A=prefixA" "Label B=prefixB" ...
prefix is e.g. build/shots/s0-live  -> uses prefix-120.png, -180.png, -240.png
"""
import sys, os, glob
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, r"C:/Users/abg77/AppData/Local/Temp/claude/C--Users-abg77-OneDrive-Desktop-wall-paper-engine-claude-code/8c9d0c8d-d72a-4f56-9d50-a24cc6eac2d0/scratchpad/pylibs")
from PIL import Image, ImageDraw, ImageFont
out = sys.argv[1]
cols = [a.split('=', 1) for a in sys.argv[2:]]
W = 960; H = 540; pad = 8; head = 34  # thumbnails; pad = 8; head = 34
times = sorted({os.path.basename(f).rsplit('-', 1)[1][:-4] for _, p in cols for f in glob.glob(p + '-[0-9][0-9][0-9].png')})
sheet = Image.new('RGB', (pad + len(cols) * (W + pad), head + len(times) * (H + pad)), (18, 18, 18))
d = ImageDraw.Draw(sheet)
try: font = ImageFont.truetype('arial.ttf', 22)
except Exception: font = ImageFont.load_default()
for ci, (label, p) in enumerate(cols):
    d.text((pad + ci * (W + pad) + 6, 6), label, fill=(240, 240, 240), font=font)
    for ri, t in enumerate(times):
        f = f'{p}-{t}.png'
        if not os.path.exists(f): continue
        im = Image.open(f).convert('RGB').resize((W, H), Image.LANCZOS)
        x = pad + ci * (W + pad); y = head + ri * (H + pad)
        sheet.paste(im, (x, y))
        d.text((x + 8, y + 6), f't={int(t)}s', fill=(255, 255, 255), font=font)
sheet.save(out, optimize=True)
print('wrote', out, sheet.size)
