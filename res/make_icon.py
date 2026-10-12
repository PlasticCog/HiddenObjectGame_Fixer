"""Generates src/launcher/hog.ico: a cartoon hog app icon, drawn supersampled then
downscaled to the standard icon sizes. Run: python res/make_icon.py"""
import os
from PIL import Image, ImageDraw

S = 1024  # supersample canvas


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def draw():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # Diagonal blue->purple gradient background.
    top = (90, 150, 245)
    bot = (124, 70, 240)
    grad = Image.new("RGB", (S, S))
    gd = grad.load()
    for y in range(S):
        row = lerp(top, bot, y / (S - 1))
        for x in range(S):
            # a touch of diagonal shimmer
            t = 0.12 * (x / S)
            gd[x, y] = lerp(row, (255, 255, 255), t * 0.15)
    img.paste(grad, (0, 0), rounded_mask(S, int(S * 0.22)))

    d = ImageDraw.Draw(img)

    cx = S // 2
    pink = (244, 162, 196)
    pink_d = (226, 125, 170)
    pink_dd = (196, 96, 142)
    snout = (248, 180, 208)

    def ellipse(cx, cy, rx, ry, fill, outline=None, w=0):
        d.ellipse([cx - rx, cy - ry, cx + rx, cy + ry], fill=fill, outline=outline, width=w)

    # Ears (behind the head): rounded triangles.
    for sgn in (-1, 1):
        ex = cx + sgn * int(S * 0.26)
        ey = int(S * 0.30)
        ear = [
            (ex - sgn * int(S * 0.02), ey - int(S * 0.16)),
            (ex + sgn * int(S * 0.14), ey - int(S * 0.02)),
            (ex - sgn * int(S * 0.10), ey + int(S * 0.08)),
        ]
        d.polygon(ear, fill=pink_d)
        inner = [
            (ex, ey - int(S * 0.10)),
            (ex + sgn * int(S * 0.07), ey),
            (ex - sgn * int(S * 0.04), ey + int(S * 0.03)),
        ]
        d.polygon(inner, fill=pink_dd)

    # Head.
    hy = int(S * 0.52)
    ellipse(cx, hy, int(S * 0.34), int(S * 0.30), pink)
    # Soft shading at the bottom of the head.
    ellipse(cx, hy + int(S * 0.08), int(S * 0.33), int(S * 0.24), None, outline=None)

    # Cheeks (blush).
    for sgn in (-1, 1):
        ellipse(cx + sgn * int(S * 0.21), hy + int(S * 0.10), int(S * 0.055), int(S * 0.04),
                (255, 150, 180))

    # Eyes.
    for sgn in (-1, 1):
        ex = cx + sgn * int(S * 0.135)
        ey = hy - int(S * 0.07)
        ellipse(ex, ey, int(S * 0.062), int(S * 0.072), (255, 255, 255))
        ellipse(ex, ey + int(S * 0.012), int(S * 0.034), int(S * 0.040), (40, 40, 55))
        ellipse(ex - int(S * 0.012), ey - int(S * 0.014), int(S * 0.013), int(S * 0.015),
                (255, 255, 255))
        # little eyebrow for a cheeky look
        d.arc([ex - int(S * 0.06), ey - int(S * 0.13), ex + int(S * 0.06), ey - int(S * 0.03)],
              start=200, end=340, fill=pink_dd, width=int(S * 0.012))

    # Snout.
    sy = hy + int(S * 0.11)
    ellipse(cx, sy, int(S * 0.145), int(S * 0.095), snout, outline=pink_d, w=int(S * 0.008))
    for sgn in (-1, 1):
        ellipse(cx + sgn * int(S * 0.055), sy, int(S * 0.028), int(S * 0.040), (150, 80, 110))

    # Smile.
    d.arc([cx - int(S * 0.10), sy + int(S * 0.02), cx + int(S * 0.10), sy + int(S * 0.14)],
          start=20, end=160, fill=pink_dd, width=int(S * 0.012))

    return img


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "..", "src", "launcher", "hog.ico")
    big = draw()
    sizes = [16, 24, 32, 48, 64, 128, 256]
    base = big.resize((256, 256), Image.LANCZOS)
    base.save(out, format="ICO", sizes=[(s, s) for s in sizes])
    # Also a PNG preview.
    big.resize((256, 256), Image.LANCZOS).save(os.path.join(here, "hog_preview.png"))
    print("wrote", os.path.abspath(out))


if __name__ == "__main__":
    main()
