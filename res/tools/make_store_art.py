#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Renders the store / launcher-listing art in res/ from our own sources only
(res/icon_full.jpg and the bundled Metal Mania font, SIL OFL 1.1):

    python3 res/tools/make_store_art.py

Writes res/logo*.png (transparent badge + "GUITAR DREAM ENGINE" wordmark,
horizontal and stacked), res/badge.png and res/backdrop_1920x1080*.png.
Needs Python 3 with Pillow and numpy. Deterministic (fixed random seed).
"""
import os

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
RES = os.path.join(ROOT, "res")
FONT = os.path.join(ROOT, "gd-engine-launcher", "res", "fonts", "MetalMania-Regular.ttf")
FIRE = [(0.0, (255, 211, 77)), (0.45, (255, 138, 31)), (1.0, (232, 52, 58))]  # docs/index.html --fire
FRETS = [(47, 211, 90), (232, 52, 58), (245, 197, 24), (47, 140, 240), (255, 138, 31)]


# ---------------------------------------------------------------- badge cut-out
def badge_cutout():
    """The circular badge from icon_full.jpg on a transparent background. The
    alpha is the disc (anti-aliased) plus the letters' spikes that poke out of
    it, so nothing of the black square around it survives."""
    img = Image.open(os.path.join(RES, "icon_full.jpg")).convert("RGB")
    a = np.asarray(img).astype(np.float32)
    luma = a.mean(axis=2)
    h, w = luma.shape
    # Fit the disc edge: walk outwards along 360 rays and find where it turns black.
    cx0, cy0 = w / 2, h / 2
    pts = []
    for t in np.linspace(0, 2 * np.pi, 360, endpoint=False):
        dx, dy = np.cos(t), np.sin(t)
        for r in range(int(w * 0.5) - 2, int(w * 0.40), -1):
            x, y = int(cx0 + dx * r), int(cy0 + dy * r)
            if luma[max(0, y - 1):y + 2, max(0, x - 1):x + 2].mean() > 6:
                pts.append((x, y))
                break
    p = np.array(pts, np.float64)
    # Least-squares circle, then drop rays that hit the spikes and refit.
    for _ in range(3):
        A = np.c_[2 * p[:, 0], 2 * p[:, 1], np.ones(len(p))]
        b = (p ** 2).sum(1)
        cx, cy, c = np.linalg.lstsq(A, b, rcond=None)[0]
        r = np.sqrt(c + cx * cx + cy * cy)
        d = np.hypot(p[:, 0] - cx, p[:, 1] - cy)
        p = p[np.abs(d - np.median(d)) < 6]
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    dist = np.hypot(xx - cx, yy - cy)
    disc = np.clip(r - 1.0 - dist + 0.5, 0, 1)  # 1 px anti-aliased edge, 1 px inset
    # Parts outside the disc (the GDE spikes): bright content, grown by the dark
    # outline drawn around the letters, then feathered.
    content = Image.fromarray(((luma > 14) * 255).astype(np.uint8))
    content = content.filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(1.6))
    outside = np.asarray(content).astype(np.float32) / 255.0
    outside = np.clip((outside - 0.15) / 0.7, 0, 1)
    alpha = np.maximum(disc, outside * (dist > r - 4))
    rgba = np.dstack([a, alpha * 255]).astype(np.uint8)
    out = Image.fromarray(rgba, "RGBA")
    return out.crop(out.getbbox())


# ---------------------------------------------------------------- wordmark
def gradient_fill(size, stops):
    w, h = size
    t = np.linspace(0, 1, h)[:, None]
    col = np.zeros((h, 3))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        m = (t >= t0) & (t <= t1)
        k = np.clip((t - t0) / (t1 - t0), 0, 1)
        col = np.where(m, np.array(c0) * (1 - k) + np.array(c1) * k, col)
    return Image.fromarray(np.repeat(col[:, None, :], w, axis=1).astype(np.uint8), "RGB")


def wordmark(lines, px, align="left", line_gap=0.02):
    """Fire-gradient Metal Mania text, dark outline, hard drop shadow and a warm
    glow, like the launcher header and the site's h1."""
    font = ImageFont.truetype(FONT, px)
    stroke = max(2, px // 22)
    sh = max(3, px // 24)  # drop shadow offset (h1: 0 4px 0 #000 at ~112px)
    pad = px // 2
    boxes = [font.getbbox(s, stroke_width=stroke) for s in lines]
    lw = [b[2] - b[0] for b in boxes]
    lh = [b[3] - b[1] for b in boxes]
    gap = int(px * line_gap)
    W = max(lw) + 2 * pad
    H = sum(lh) + gap * (len(lines) - 1) + 2 * pad + sh
    text = Image.new("L", (W, H), 0)   # glyph fill mask
    outl = Image.new("L", (W, H), 0)   # glyph + outline mask
    d_t, d_o = ImageDraw.Draw(text), ImageDraw.Draw(outl)
    y = pad
    fills = Image.new("RGB", (W, H))
    for s, b, w_, h_ in zip(lines, boxes, lw, lh):
        x = pad + (0 if align == "left" else (max(lw) - w_) // 2)
        d_t.text((x - b[0], y - b[1]), s, font=font, fill=255)
        d_o.text((x - b[0], y - b[1]), s, font=font, fill=255, stroke_width=stroke, stroke_fill=255)
        # Gradient per line, so each line runs yellow -> red.
        fills.paste(gradient_fill((W, h_), FIRE), (0, y))
        y += h_ + gap
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    glow = outl.filter(ImageFilter.GaussianBlur(px / 7))
    out.paste(Image.new("RGBA", (W, H), (255, 120, 0, 255)), (0, 0), glow.point(lambda v: int(v * 0.45)))
    shadow = ImageChops.offset(outl, 0, sh)
    out.paste(Image.new("RGBA", (W, H), (0, 0, 0, 255)), (0, 0), shadow)
    out.paste(Image.new("RGBA", (W, H), (20, 6, 4, 255)), (0, 0), outl)
    # Fill, with a thin bright bevel along the top of each glyph.
    out.paste(fills.convert("RGBA"), (0, 0), text)
    hi = ImageChops.subtract(text, ImageChops.offset(text, 0, max(1, px // 60)))
    out.paste(Image.new("RGBA", (W, H), (255, 245, 200, 255)), (0, 0), hi.point(lambda v: int(v * 0.55)))
    return out.crop(out.getbbox())


def compose(badge, mark, layout, badge_h):
    b = badge.resize((int(badge.width * badge_h / badge.height), badge_h), Image.LANCZOS)
    if layout == "horizontal":
        gap = badge_h // 10
        W, H = b.width + gap + mark.width, max(b.height, mark.height)
        out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        out.alpha_composite(b, (0, (H - b.height) // 2))
        out.alpha_composite(mark, (b.width + gap, (H - mark.height) // 2))
    else:
        gap = badge_h // 16
        W, H = max(b.width, mark.width), b.height + gap + mark.height
        out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        out.alpha_composite(b, ((W - b.width) // 2, 0))
        out.alpha_composite(mark, ((W - mark.width) // 2, b.height + gap))
    return out


def save_scaled(img, path, width):
    img.resize((width, round(img.height * width / img.width)), Image.LANCZOS).save(path, optimize=True)


# ---------------------------------------------------------------- backdrop
def noise(h, w, rng, octaves=((6, 1.0), (12, 0.5), (24, 0.25), (48, 0.12))):
    acc = np.zeros((h, w), np.float32)
    for cells, amp in octaves:
        small = rng.random((cells, int(cells * w / h) + 1)).astype(np.float32)
        acc += amp * np.asarray(Image.fromarray(small).resize((w, h), Image.BICUBIC))
    acc -= acc.min()
    return acc / acc.max()


def blur(arr, radius):
    chans = [np.asarray(Image.fromarray(arr[..., i]).filter(ImageFilter.GaussianBlur(radius))) for i in range(arr.shape[2])]
    return np.dstack(chans)


def backdrop(W=1920, H=1080, seed=7):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    img = np.zeros((H, W, 3), np.float32)
    # Deep background: near-black with a warm stage glow behind the highway.
    vx, yh = W * 0.64, H * 0.30            # vanishing point
    img += np.array([10, 8, 12]) / 255.0
    g = np.exp(-(((xx - vx) / (W * 0.30)) ** 2 + ((yy - yh) / (H * 0.22)) ** 2))
    img += g[..., None] * np.array([0.55, 0.20, 0.05])
    g2 = np.exp(-(((xx - vx) / (W * 0.10)) ** 2 + ((yy - yh) / (H * 0.07)) ** 2))
    img += g2[..., None] * np.array([0.9, 0.55, 0.2])

    # Stage-light beams from the rig above, with smoke inside them.
    smoke = noise(H, W, rng)
    beams = [(0.30, 0.00, 0.10, (1.0, 0.45, 0.10)), (0.47, -0.03, 0.08, (0.95, 0.25, 0.15)),
             (0.62, -0.02, 0.07, (1.0, 0.75, 0.30)), (0.80, -0.04, 0.09, (0.25, 0.45, 1.0)),
             (0.96, 0.00, 0.10, (0.85, 0.30, 0.95)), (0.12, -0.03, 0.08, (0.25, 0.50, 1.0))]
    for bx, by, spread, col in beams:
        ox, oy = bx * W, by * H
        tx, ty = vx + (bx - 0.64) * W * 0.35, H * 0.95  # aim at the stage floor
        ang_axis = np.arctan2(tx - ox, ty - oy)
        ang = np.arctan2(xx - ox, yy - oy)
        dist = np.hypot(xx - ox, yy - oy) + 1
        cone = np.exp(-((ang - ang_axis) / spread) ** 2)
        core = np.exp(-((ang - ang_axis) / (spread * 0.25)) ** 2)
        fall = np.exp(-dist / (H * 0.9))
        lit = (0.55 * cone + 0.35 * core) * fall * (0.35 + 0.9 * smoke)
        img += lit[..., None] * np.array(col) * 0.55
        # Lamp flare at the source.
        f = np.exp(-(((xx - ox) ** 2 + (yy - max(oy, 4)) ** 2) / (2 * (W * 0.012) ** 2)))
        img += f[..., None] * (np.array(col) * 0.6 + 0.6)

    # Note highway: perspective board, d = depth (1 at the bottom edge).
    lanepx = 190.0
    below = yy > yh + 2
    d = np.where(below, (H - yh) / np.maximum(yy - yh, 1e-3), 1e6)
    X = (xx - vx) * d / lanepx                      # lane units, lanes span [-2.5, 2.5]
    fog = np.exp(-(d - 1) * 0.16)
    endf = np.clip((20 - d) / 9, 0, 1) ** 1.5      # board fades out into the stage glow
    on = ((np.abs(X) < 2.5) & below) * endf
    board = np.zeros_like(img)
    grain = noise(H, W, rng, ((40, 1.0), (160, 0.5)))
    base = 0.05 + 0.03 * grain
    board += (on * base)[..., None] * np.array([1.0, 0.85, 0.8])
    pxw = d / lanepx                                # lane units per pixel at this row
    def line(center, width):
        return np.exp(-((X - center) / np.maximum(width, pxw * 0.8)) ** 2)
    for s in (-1.5, -0.5, 0.5, 1.5):                # strings
        board += (on * line(s, 0.02) * 0.28)[..., None] * np.array([0.8, 0.8, 0.9])
    rails = (line(-2.5, 0.06) + line(2.5, 0.06)) * below * endf
    board += rails[..., None] * np.array([1.0, 0.55, 0.15]) * 0.9
    # Beat lines (frets) moving away from the strikeline.
    phase = (d * 1.2) % 1.0
    fret = np.exp(-((phase - 0.5) / np.maximum(0.012, 1.2 * d / (yy - yh + 1e-3) * 0.6)) ** 2)
    board += (on * fret * 0.35)[..., None] * np.array([0.9, 0.9, 1.0])
    cover = on * (0.55 + 0.4 * np.clip(1.6 - d * 0.08, 0, 1))  # board hides the beams behind it
    img = img * (1 - cover[..., None]) + board * fog[..., None]
    # The strikeline.
    d_hit = 1.22
    y_hit = yh + (H - yh) / d_hit
    hit = np.exp(-((yy - y_hit) / 3.0) ** 2) * (np.abs(X) < 2.55)
    img += hit[..., None] * np.array([1.0, 0.95, 0.85]) * 0.9

    # Gems and glow, drawn with Pillow on a supersampled layer.
    S = 2
    layer = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
    glow = Image.new("RGB", (W * S, H * S), (0, 0, 0))
    dl, dg = ImageDraw.Draw(layer), ImageDraw.Draw(glow)

    def proj(lane, depth):
        return vx + (lane - 2) * lanepx / depth, yh + (H - yh) / depth

    def gem(lane, depth, strength=1.0, ring=False):
        cx, cy = proj(lane, depth)
        rx = 0.36 * lanepx / depth
        ry = rx * 0.55
        c = np.array(FRETS[lane])
        f = np.exp(-(depth - 1) * 0.13)
        cs = tuple(int(v) for v in c * f)
        R = lambda k, sx=1.0, sy=1.0, oy=0.0: [S * (cx - rx * k * sx), S * (cy - ry * k * sy + oy),
                                               S * (cx + rx * k * sx), S * (cy + ry * k * sy + oy)]
        dg.ellipse(R(1.9), fill=tuple(int(v * strength * f) for v in c))
        if ring:  # strikeline target
            dl.ellipse(R(1.05), outline=cs + (255,), width=int(S * max(2, rx * 0.16)))
            dl.ellipse(R(0.62), fill=(15, 12, 14, 230))
            return
        dl.ellipse(R(1.0, oy=ry * 0.35), fill=(0, 0, 0, 200))                 # side/shadow
        dl.ellipse(R(1.0), fill=(235, 235, 235, 255))                          # silver rim
        dl.ellipse(R(0.84), fill=cs + (255,))
        hl = tuple(int(min(255, v * 0.45 + 150 * f)) for v in c)
        dl.ellipse(R(0.5, 0.8, 0.45, -ry * 0.3), fill=hl + (255,))            # top highlight
        dl.ellipse(R(0.22, 1, 1, -ry * 0.05), fill=(255, 255, 255, 230))      # stud

    for lane in range(5):
        gem(lane, d_hit, 0.9, ring=True)
    # Sustain on yellow.
    for k in np.linspace(1.5, 6.0, 900):
        cx, cy = proj(2, k)
        w = 0.07 * lanepx / k
        dg.rectangle([S * (cx - w * 2), S * cy, S * (cx + w * 2), S * (cy + 4)], fill=(150, 115, 10))
        dl.rectangle([S * (cx - w), S * cy, S * (cx + w), S * (cy + 4)], fill=(255, 220, 90, 255))
    notes = [(0, 1.40), (1, 1.40), (2, 1.48), (3, 2.0), (4, 2.0), (1, 2.6), (2, 3.2), (0, 3.2),
             (3, 4.0), (4, 4.6), (1, 5.4), (2, 6.4), (0, 7.4), (3, 8.6), (4, 10.0), (2, 11.5)]
    for lane, depth in sorted(notes, key=lambda n: -n[1]):
        gem(lane, depth, 1.0)
    # Hit flames at the strikeline for the chord just played.
    for lane in (0, 1):
        cx, cy = proj(lane, d_hit)
        for i in range(10):
            r = (0.65 - i * 0.05) * lanepx
            dg.ellipse([S * (cx - r * 0.5), S * (cy - r * 1.4), S * (cx + r * 0.5), S * (cy + r * 0.2)],
                       fill=tuple(int(v * 0.12) for v in (255, 160, 60)))
    layer = layer.resize((W, H), Image.LANCZOS)
    glow = glow.resize((W, H), Image.LANCZOS)
    gl = np.asarray(glow).astype(np.float32) / 255.0
    img += blur((gl * 255).astype(np.uint8), 18).astype(np.float32) / 255.0 * 1.2
    img += blur((gl * 255).astype(np.uint8), 60).astype(np.float32) / 255.0 * 0.9
    la = np.asarray(layer).astype(np.float32) / 255.0
    img = img * (1 - la[..., 3:]) + la[..., :3] * la[..., 3:]

    # Haze over everything, sparks, vignette, a darker left side for store text.
    haze = noise(H, W, rng, ((3, 1.0), (7, 0.6), (15, 0.3)))
    img += (haze * 0.07)[..., None] * np.array([0.9, 0.6, 0.5])
    for _ in range(140):
        sx, sy = rng.normal(vx, W * 0.18), rng.normal(H * 0.55, H * 0.25)
        r = rng.uniform(0.6, 2.2)
        s = np.exp(-(((xx - sx) ** 2 + (yy - sy) ** 2) / (2 * r * r)))
        img += s[..., None] * np.array([1.0, 0.6, 0.2]) * rng.uniform(0.3, 1.0)
    vig = np.hypot((xx - W * 0.6) / (W * 0.75), (yy - H * 0.45) / (H * 0.85))
    img *= np.clip(1.15 - vig ** 2 * 0.85, 0.08, 1)[..., None]
    left = np.clip((xx / W - 0.05) / 0.40, 0, 1) ** 1.5
    img *= (0.35 + 0.65 * left)[..., None]
    img = img / (1 + img * 0.35)                    # soft tone map
    img = np.clip(img ** (1 / 1.05), 0, 1)
    img += (rng.random((H, W, 1)) - 0.5) / 255.0    # dither against banding
    return Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB")


def main():
    badge = badge_cutout()
    save_scaled(badge, os.path.join(RES, "badge.png"), 1024)
    one = wordmark(["GUITAR DREAM ENGINE"], 360)
    two = wordmark(["GUITAR DREAM", "ENGINE"], 300, align="center", line_gap=0.0)
    horiz = compose(badge, one, "horizontal", 900)
    stack = compose(badge, two, "stacked", 1100)
    save_scaled(horiz, os.path.join(RES, "logo@2x.png"), 2400)
    save_scaled(horiz, os.path.join(RES, "logo.png"), 1200)
    save_scaled(stack, os.path.join(RES, "logo_stacked@2x.png"), 1600)
    save_scaled(stack, os.path.join(RES, "logo_stacked.png"), 800)

    bd = backdrop()
    bd.save(os.path.join(RES, "backdrop_1920x1080.png"), optimize=True)
    lg = bd.convert("RGBA")
    logo = horiz.resize((860, round(horiz.height * 860 / horiz.width)), Image.LANCZOS)
    lg.alpha_composite(logo, (80, 1080 * 2 // 5 - logo.height // 2))
    lg.convert("RGB").save(os.path.join(RES, "backdrop_1920x1080_logo.png"), optimize=True)
    for f in sorted(os.listdir(RES)):
        p = os.path.join(RES, f)
        if f.endswith(".png"):
            with Image.open(p) as im:
                print(f"{f:32s} {im.width}x{im.height} {os.path.getsize(p) // 1024} KB")


if __name__ == "__main__":
    main()
