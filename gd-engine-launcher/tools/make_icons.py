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
"""
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = os.path.dirname(HERE)
REPO = os.path.dirname(LAUNCHER)
SRC = os.path.join(REPO, "res", "icon_full.jpg")
RES = os.path.join(LAUNCHER, "res")
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
    Image.open(SRC).convert("RGB").resize((1024, 1024), Image.LANCZOS).save(
        os.path.join(RES, "gdul_backdrop.jpg"), quality=82, optimize=True)
    print("images written to", RES)


if __name__ == "__main__":
    main()
