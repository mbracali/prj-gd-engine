#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Makes the launcher's images from the Guitar Dream Engine badge
(res/icon_full.jpg at the repo root), the same cut as gd-game-02's
tools/make_icons.py. The results are committed; run it again (needs Pillow)
after changing the badge:

    python3 gd-engine-launcher/tools/make_icons.py

Writes, in gd-engine-launcher/res/:
    gdul_icon.png       256 px: built in (window icon, the list's header badge)
    gdul_icon_512.png   512 px: shipped next to the program
    gdul.ico            Windows: the .exe's icon (16 to 256 px)
    gdul_backdrop.jpg   1024 px: the dimmed badge behind the game list
    gdul.icns           macOS: the icon of "Guitar Dream Universal Launcher.app"
and in gd-engine-launcher/android/app/src/main/res/:
    mipmap-*/ic_launcher*.png and mipmap-anydpi-v26/ic_launcher*.xml
                        Android: the APK's launcher icons (adaptive on 8+)
"""
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = os.path.dirname(HERE)
REPO = os.path.dirname(LAUNCHER)
SRC = os.path.join(REPO, "res", "icon_full.jpg")
RES = os.path.join(LAUNCHER, "res")
ANDROID_RES = os.path.join(LAUNCHER, "android", "app", "src", "main", "res")
DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}
CX, CY, R = 1023, 1030, 992  # the badge's circle in icon_full.jpg
SMALL = 0.78                  # small icons show the badge's centre (GDE letters)


def badge(size, frac=1.0):
    src = Image.open(SRC).convert("RGB")
    r = round(R * frac)
    crop = src.crop((CX - r, CY - r, CX + r, CY + r))
    big = 2048
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, big - 1, big - 1), fill=255)
    out = crop.resize((big, big), Image.LANCZOS).convert("RGBA")
    out.putalpha(mask)
    return out.resize((size, size), Image.LANCZOS)


def padded(size, scale, frac=1.0):
    """The badge at `scale` of a transparent size x size square, centred."""
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    inner = max(1, round(size * scale))
    b = badge(inner, frac)
    canvas.paste(b, ((size - inner) // 2, (size - inner) // 2), b)
    return canvas


def android_icons():
    """The same icons as gd-game-02's APK: classic and adaptive (on black)."""
    for name, k in DENSITIES.items():
        d = os.path.join(ANDROID_RES, f"mipmap-{name}")
        os.makedirs(d, exist_ok=True)
        legacy = padded(round(48 * k), 46 / 48, SMALL)
        legacy.save(os.path.join(d, "ic_launcher.png"), optimize=True)
        legacy.save(os.path.join(d, "ic_launcher_round.png"), optimize=True)
        padded(round(108 * k), 72 / 108, 0.9).save(os.path.join(d, "ic_launcher_foreground.png"), optimize=True)
    anydpi = os.path.join(ANDROID_RES, "mipmap-anydpi-v26")
    os.makedirs(anydpi, exist_ok=True)
    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<!-- Made by gd-engine-launcher/tools/make_icons.py: the badge on black. -->\n'
           '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
           '    <background android:drawable="@color/ic_launcher_background"/>\n'
           '    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>\n'
           '</adaptive-icon>\n')
    for f in ("ic_launcher.xml", "ic_launcher_round.xml"):
        with open(os.path.join(anydpi, f), "w") as out:
            out.write(xml)


def main():
    if not os.path.isfile(SRC):
        sys.exit(f"make_icons.py: {SRC} not found")
    master, small = badge(1024), badge(1024, SMALL)
    master.resize((256, 256), Image.LANCZOS).save(os.path.join(RES, "gdul_icon.png"), optimize=True)
    master.resize((512, 512), Image.LANCZOS).save(os.path.join(RES, "gdul_icon_512.png"), optimize=True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [(small if s <= 48 else master).resize((s, s), Image.LANCZOS) for s in sizes]
    images[-1].save(os.path.join(RES, "gdul.ico"), format="ICO", sizes=[(s, s) for s in sizes],
                    append_images=images[:-1], bitmap_format="bmp")
    icns = [(small if s <= 48 else master).resize((s, s), Image.LANCZOS) for s in (16, 32, 64, 128, 256, 512)]
    master.save(os.path.join(RES, "gdul.icns"), append_images=icns)
    android_icons()
    Image.open(SRC).convert("RGB").resize((1024, 1024), Image.LANCZOS).save(
        os.path.join(RES, "gdul_backdrop.jpg"), quality=82, optimize=True)
    print("images written to", RES, "and", ANDROID_RES)


if __name__ == "__main__":
    main()
