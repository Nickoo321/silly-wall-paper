# Stand-in OilTank dump (DUMP-SCHEMA v1) from a live FluidWallpaper frame: the dark oil areas of the
# screenshot become a smooth signed-distance-like height ramp (16-bit), 0 outside.
import numpy as np, cv2, sys, json, os
src, outdir, thr = sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 0.42
W, H = 2560, 1440
os.makedirs(outdir, exist_ok=True)
im = cv2.imread(src, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32) / 255.0
im = cv2.resize(im, (W, H), interpolation=cv2.INTER_CUBIC)
mx = im.max(axis=2)
mx = cv2.GaussianBlur(mx, (0, 0), 3.0)                      # kill the film grain before thresholding
# the screenshot's own vignette darkens the corners to near black: divide it out (radial, corner = 1)
yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
rn = np.sqrt(((xx - W / 2) / (W / 2)) ** 2 + ((yy - H / 2) / (H / 2)) ** 2) / np.sqrt(2.0)
t = np.clip((rn - 0.80) / 0.25, 0, 1); vig = np.maximum(1.0 - t * t * (3 - 2 * t), 0.05)
ratio = mx / vig
inside = (ratio < thr).astype(np.uint8)
cv2.imwrite(os.path.join(outdir, 'ratio-preview.png'), cv2.resize((np.clip(ratio,0,1)*255).astype(np.uint8), (1280, 720), interpolation=cv2.INTER_AREA))
cv2.imwrite(os.path.join(outdir, 'ratio-preview.png'), cv2.resize((np.clip(ratio,0,1)*255).astype(np.uint8), (1280, 720), interpolation=cv2.INTER_AREA))
# drop specks smaller than ~3 px radius (grain clumps) via an opening
inside = cv2.morphologyEx(inside, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))
din = cv2.distanceTransform(inside, cv2.DIST_L2, 5)
dout = cv2.distanceTransform(1 - inside, cv2.DIST_L2, 5)
sdf = din - dout                                             # px, >0 inside
# sub-pixel edge: the blurred brightness itself crosses thr smoothly; blend the EDT with it near the edge
sdf = cv2.GaussianBlur(sdf.astype(np.float32), (0, 0), 2.0) # smooth iso-lines (no EDT quantisation steps)
R = 40.0                                                     # px: ramp width to full thickness
x = np.clip(sdf / R, 0.0, 1.0)
h = 1.0 - (1.0 - x) ** 2                                     # sessile-cap-like: steep at the edge, flat top
h[sdf <= 0] = 0.0
# guarantee the iso value 0.02 sits at sdf ~ 0.4 px: h(0.4px) = 1-(1-0.01)^2 ~ 0.02
h16 = np.round(h * 65535.0).astype(np.uint16)
cv2.imwrite(os.path.join(outdir, 'height.png'), h16)
prev = (h * 255).astype(np.uint8)
cv2.imwrite(os.path.join(outdir, 'height-preview.png'), cv2.resize(prev, (1280, 720), interpolation=cv2.INTER_AREA))
print('oil cover %.3f  h>0.02 %.3f' % (inside.mean(), (h > 0.02).mean()))
dump = {
  "version": 1,
  "t": 0.0,
  "width": W, "height": H,
  "_source": "STAND-IN: dark oil areas of handoff/review/cycle-final/stage-06-Scheme_Magenta_Cyan.png (1280x720, upscaled), "
             "thr %.2f on the 3px-blurred max channel with the screenshot's radial vignette divided out, EDT ramp R=%dpx; film_rgb = the measured magenta film (linear), accent = the cyan film" % (thr, R),
  "film_rgb": [1.0, 0.0048, 0.227],
  "accent_rgb": [0.0048, 0.275, 0.371],
  "height_png": "height.png",
  "film_png": "",
  "droplets": []
}
json.dump(dump, open(os.path.join(outdir, 'dump.json'), 'w'), indent=2)
