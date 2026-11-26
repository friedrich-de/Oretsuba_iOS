#!/usr/bin/env python3
"""Makes the Android app's icon from the Apple port's icon picture.

Android shows an icon through a mask of the launcher's choosing (a circle, a
rounded square) cut from the middle two thirds of two layers. The foreground
layer is the icon picture itself (app/Resources/Assets.xcassets/AppIcon.appiconset/
icon-1024.png, unchanged) in those middle two thirds; the background layer
carries the picture's sky on to the edges, where a launcher may show it while
the icon moves.

usage: make_icons.py
"""
import os

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE = os.path.join(ROOT, "app", "Resources", "Assets.xcassets", "AppIcon.appiconset", "icon-1024.png")
OUT = os.path.join(ROOT, "android", "app", "src", "main", "res", "drawable-xxxhdpi")

LAYER = 432  # 108 dp at xxxhdpi
INNER = 288  # the 72 dp a mask is cut from


def main():
    icon = Image.open(SOURCE).convert("RGB").resize((INNER, INNER), Image.LANCZOS)
    inset = (LAYER - INNER) // 2

    foreground = Image.new("RGBA", (LAYER, LAYER), (0, 0, 0, 0))
    foreground.paste(icon, (inset, inset))

    # The sky: each row takes the colour of the icon's left edge at that height,
    # and the rows above and below the picture those of its first and last row.
    background = Image.new("RGB", (LAYER, LAYER))
    pixels = background.load()
    for y in range(LAYER):
        colour = icon.getpixel((0, min(max(y - inset, 0), INNER - 1)))
        for x in range(LAYER):
            pixels[x, y] = colour

    os.makedirs(OUT, exist_ok=True)
    foreground.save(os.path.join(OUT, "ic_launcher_foreground.png"), optimize=True)
    background.save(os.path.join(OUT, "ic_launcher_background.png"), optimize=True)
    print("wrote the icon layers to", OUT)


if __name__ == "__main__":
    main()
