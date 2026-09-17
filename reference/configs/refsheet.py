"""Reference-vs-port sheet: each row = one Liquid Acid reference crop next to a port frame.
usage: python refsheet.py out.png "label=frame.png=ref.jpg" ...
Refs are phone screenshots (1440x3120); the clean band y=300..1900 is cropped (skips the
Instagram chrome and the Download card). Port frames are shown at 960x540.
"""
import sys, os
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, r"C:/Users/abg77/AppData/Local/Temp/claude/C--Users-abg77-OneDrive-Desktop-wall-paper-engine-claude-code/8c9d0c8d-d72a-4f56-9d50-a24cc6eac2d0/scratchpad/pylibs")
from PIL import Image, ImageDraw, ImageFont
out = sys.argv[1]
rows = [a.split('=', 2) for a in sys.argv[2:]]
W, H, pad, head = 960, 540, 8, 30
RW = int(1440 * H / 1600)  # ref crop 1440x1600 scaled to height H
sheet = Image.new('RGB', (pad * 3 + RW + W, len(rows) * (H + head + pad) + pad), (18, 18, 18))
d = ImageDraw.Draw(sheet)
try: font = ImageFont.truetype('arial.ttf', 20)
except Exception: font = ImageFont.load_default()
for ri, (label, frame, ref) in enumerate(rows):
    y = pad + ri * (H + head + pad)
    r = Image.open(ref).convert('RGB').crop((0, 300, 1440, 1900)).resize((RW, H), Image.LANCZOS)
    f = Image.open(frame).convert('RGB').resize((W, H), Image.LANCZOS)
    d.text((pad, y + 4), 'REF', fill=(200, 200, 200), font=font)
    d.text((pad * 2 + RW, y + 4), label, fill=(240, 240, 240), font=font)
    sheet.paste(r, (pad, y + head)); sheet.paste(f, (pad * 2 + RW, y + head))
sheet.save(out, optimize=True); print('wrote', out, sheet.size)
