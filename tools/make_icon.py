"""PULSE's icon (icons/pulse.png, 512x512): a bedside monitor's black glass and phosphor grid, a bright ECG trace with
one tall beat, and a red heart. Drawn at 4x and scaled down for clean edges.
Usage: python -I tools/make_icon.py   (from the pulse folder)"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter

S = 4
N = 512 * S
GREEN = (43, 255, 136)
RED = (255, 59, 78)
here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.join(here, "icons", "pulse.png")

img = Image.new("RGBA", (N, N), (4, 9, 7, 255))
d = ImageDraw.Draw(img)
# the phosphor grid
step = 64 * S
for i in range(0, N + 1, step):
    d.line([(i, 0), (i, N)], fill=(43, 255, 136, 26), width=S)
    d.line([(0, i), (N, i)], fill=(43, 255, 136, 26), width=S)
for i in range(0, N + 1, step // 4):
    d.line([(i, 0), (i, N)], fill=(43, 255, 136, 9), width=S)
    d.line([(0, i), (N, i)], fill=(43, 255, 136, 9), width=S)


def ecg(p):
    def g(x, c, w):
        return math.exp(-((x - c) / w) ** 2)
    return 0.12 * g(p, -0.2, 0.04) - 0.14 * g(p, -0.05, 0.018) + g(p, 0, 0.026) - 0.28 * g(p, 0.055, 0.02) + 0.22 * g(p, 0.27, 0.065)


# the trace: flat, one big beat in the middle, a smaller one either side
base = N * 0.62
pts = []
for i in range(0, N + 1, S):
    x = i / N
    v = ecg((x - 0.52) * 1.15) * 1.0 + ecg((x - 0.12) * 1.15) * 0.0 + ecg((x - 0.92) * 1.15) * 0.0
    pts.append((i, base - v * N * 0.42))
glow = Image.new("RGBA", (N, N), (0, 0, 0, 0))
gd = ImageDraw.Draw(glow)
gd.line(pts, fill=GREEN + (200,), width=26 * S, joint="curve")
glow = glow.filter(ImageFilter.GaussianBlur(14 * S))
img = Image.alpha_composite(img, glow)
d = ImageDraw.Draw(img)
d.line(pts, fill=GREEN + (255,), width=10 * S, joint="curve")
# the head of the sweep
hx = int(N * 0.86)
hy = base
d.ellipse([hx - 13 * S, hy - 13 * S, hx + 13 * S, hy + 13 * S], fill=(225, 255, 238, 255))
# the heart, top left
cx, cy, r = N * 0.2, N * 0.22, N * 0.085
heart = Image.new("RGBA", (N, N), (0, 0, 0, 0))
hd = ImageDraw.Draw(heart)
hd.ellipse([cx - r * 1.0, cy - r * 0.75, cx + r * 0.05, cy + r * 0.3], fill=RED + (255,))
hd.ellipse([cx - r * 0.05, cy - r * 0.75, cx + r * 1.0, cy + r * 0.3], fill=RED + (255,))
hd.polygon([(cx - r * 0.98, cy - r * 0.1), (cx + r * 0.98, cy - r * 0.1), (cx, cy + r * 1.05)], fill=RED + (255,))
hglow = heart.filter(ImageFilter.GaussianBlur(10 * S))
img = Image.alpha_composite(img, hglow)
img = Image.alpha_composite(img, heart)
# the bezel
d = ImageDraw.Draw(img)
d.rectangle([S * 6, S * 6, N - S * 6, N - S * 6], outline=(43, 255, 136, 90), width=4 * S)
os.makedirs(os.path.dirname(out), exist_ok=True)
img.resize((512, 512), Image.LANCZOS).save(out)
print("ok", out)
