#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
"""Makes gd01's icons from the Guitar Dream Engine badge (res/icon_full.jpg at
the repo root): the round badge cut out of its black square with smooth,
transparent edges, in every size and format the builds use. Run it again
after changing the badge (needs Pillow); the results are committed, so the
builds themselves need nothing extra.

    python3 gd-game-01/tools/make_icons.py

Writes:
    engine/res/gd01_icon.png       256 px: built into the program (window icon,
                                   first screen) and shipped as gd01.png on Linux
    engine/res/gd01_icon_512.png   512 px: shipped with the Linux / Steam Deck downloads
    engine/res/gd01.ico            Windows: the icon of gd01.exe (16 to 256 px)
    engine/res/gd01.icns           macOS: the icon of gd01.app
    engine/android/app/src/main/res/mipmap-*/ic_launcher*.png and
    mipmap-anydpi-v26/ic_launcher*.xml   Android: launcher icons (adaptive on 8+)
"""
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.dirname(HERE)
REPO = os.path.dirname(GAME)
SRC = os.path.join(REPO, "res", "icon_full.jpg")
RES = os.path.join(GAME, "engine", "res")
ANDROID_RES = os.path.join(GAME, "engine", "android", "app", "src", "main", "res")

# The badge in res/icon_full.jpg (2048 x 2048): its circle's centre and radius.
CX, CY, R = 1023, 1030, 992

# Android launcher sizes (48 dp) and adaptive-icon layer sizes (108 dp).
DENSITIES = {"mdpi": 1, "hdpi": 1.5, "xhdpi": 2, "xxhdpi": 3, "xxxhdpi": 4}


def badge(size):
    """The round badge, size x size, transparent outside the circle."""
    src = Image.open(SRC).convert("RGB")
    crop = src.crop((CX - R, CY - R, CX + R, CY + R))
    big = 2048  # mask drawn large, then scaled: a smooth edge
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, big - 1, big - 1), fill=255)
    out = crop.resize((big, big), Image.LANCZOS).convert("RGBA")
    out.putalpha(mask)
    return out.resize((size, size), Image.LANCZOS)


def padded(size, scale):
    """The badge at `scale` of a transparent size x size square, centred."""
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    inner = max(1, round(size * scale))
    b = badge(inner)
    canvas.paste(b, ((size - inner) // 2, (size - inner) // 2), b)
    return canvas


def main():
    if not os.path.isfile(SRC):
        sys.exit(f"make_icons.py: {SRC} not found")
    os.makedirs(RES, exist_ok=True)
    master = badge(1024)
    master.resize((256, 256), Image.LANCZOS).save(os.path.join(RES, "gd01_icon.png"), optimize=True)
    master.resize((512, 512), Image.LANCZOS).save(os.path.join(RES, "gd01_icon_512.png"), optimize=True)
    master.save(os.path.join(RES, "gd01.ico"), sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    master.save(os.path.join(RES, "gd01.icns"))

    for name, k in DENSITIES.items():
        d = os.path.join(ANDROID_RES, f"mipmap-{name}")
        os.makedirs(d, exist_ok=True)
        legacy = padded(round(48 * k), 44 / 48)  # the classic icon: a little margin around the circle
        legacy.save(os.path.join(d, "ic_launcher.png"), optimize=True)
        legacy.save(os.path.join(d, "ic_launcher_round.png"), optimize=True)
        # Adaptive icon foreground (108 dp): the launcher's mask shows the
        # inner 72 dp; the badge fills the 66 dp safe circle.
        padded(round(108 * k), 66 / 108).save(os.path.join(d, "ic_launcher_foreground.png"), optimize=True)
    anydpi = os.path.join(ANDROID_RES, "mipmap-anydpi-v26")
    os.makedirs(anydpi, exist_ok=True)
    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<!-- Made by gd-game-01/tools/make_icons.py: the badge on black. -->\n'
           '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
           '    <background android:drawable="@color/ic_launcher_background"/>\n'
           '    <foreground android:drawable="@mipmap/ic_launcher_foreground"/>\n'
           '</adaptive-icon>\n')
    for f in ("ic_launcher.xml", "ic_launcher_round.xml"):
        with open(os.path.join(anydpi, f), "w") as out:
            out.write(xml)
    print("icons written to", RES, "and", ANDROID_RES)


if __name__ == "__main__":
    main()
