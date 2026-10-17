# -*- coding: utf-8 -*-
# 生成 KrakenAV 图标（深色圆角底 + 蓝色盾牌 + 章鱼眼）
from PIL import Image, ImageDraw

def draw_icon(size):
    S = 4  # 超采样抗锯齿
    n = size * S
    img = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(n * 0.22)
    d.rounded_rectangle([0, 0, n - 1, n - 1], radius=r, fill=(27, 36, 49, 255))
    cx, cy = n / 2, n * 0.46
    w, h = n * 0.5, n * 0.5
    pts = [
        (cx, cy - h / 2),
        (cx + w / 2, cy - h / 2 + h * 0.18),
        (cx + w * 0.48, cy - h / 2 + h * 0.55),
        (cx, cy + h / 2),
        (cx - w * 0.48, cy - h / 2 + h * 0.55),
        (cx - w / 2, cy - h / 2 + h * 0.18),
    ]
    d.polygon(pts, fill=(61, 155, 233, 255), outline=(91, 184, 245, 255), width=max(1, n // 64))
    er = n * 0.085
    d.ellipse([cx - er, cy - er * 0.9, cx + er, cy + er * 0.9], fill=(27, 36, 49, 255))
    pr = er * 0.45
    d.ellipse([cx - pr, cy - pr * 1.2, cx + pr, cy + pr * 1.2], fill=(232, 237, 244, 255))
    lw = max(2, n // 40)
    for sgn in (-1, 1):
        x0 = cx + sgn * w * 0.26
        y0 = cy + h * 0.28
        pts_b = [(x0, y0), (x0 + sgn * n * 0.05, y0 + n * 0.12),
                 (x0 - sgn * n * 0.04, y0 + n * 0.17), (x0 + sgn * n * 0.03, y0 + n * 0.25)]
        d.line(pts_b, fill=(91, 184, 245, 255), width=lw, joint='curve')
    return img.resize((size, size), Image.LANCZOS)

if __name__ == '__main__':
    import sys, os
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    imgs = [draw_icon(s) for s in (16, 24, 32, 48, 64, 128, 256)]
    ico = os.path.join(out, 'krakenav.ico')
    imgs[6].save(ico, sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print('icon written:', ico)
