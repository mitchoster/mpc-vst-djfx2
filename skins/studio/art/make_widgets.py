#!/usr/bin/env python3
"""Draw the widget artwork for the MPC DJ FX 2 skin: the animated filmstrips and the PANIC button pair.

MPC has no drawing API: everything that moves is a parameter-driven filmstrip, so each of these files is
a stack of frames and the wrapper picks one by the engine's parameter value.

  knob_strip.png   127 frames 50x50   the knob: track ring, value arc, dome, pointer (all knobs share it)
  curve_strip.png   41 frames 29x206  one EQ column: -24..+12 dB area fill with a bright cap, transparent
                                      everywhere else so the baked plot grid stays readable
  vu_strip.png      81 frames 184x14  master level (VU_FLOOR_DB..0 dB, 81 steps in the engine)
  met_dly.png       41 frames 184x12  delay tail
  met_verb.png      41 frames 184x12  reverb tail
  met_rep.png       41 frames 184x12  beat-repeat activity
  btn_panic_off/on  136x40            the trigger button's resting and pressed plates (label drawn on top)

Frame counts must match the engine (vst/src/djfx_engine.c: EQ_STEPS, VU_STEPS, MET_STEPS): 41 curve and
stage-meter values, 81 VU steps, and 127 rotation steps for a Knob (MPC's knob filmstrips read one frame
fewer than their length, docs/NOTES.md).

  docker run --rm -v <port>:/w -w /w mpc-vst-html-art python3 vst/art/make_widgets.py -o vst/art
"""
import argparse
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import look as L  # noqa: E402

KNOB_R = L.KNOB_R           # layout r= (all knobs, card and master)
KNOB_S = 2 * KNOB_R + 10    # the frame size shadow_skin.py emits for a knob of that radius
KNOB_FRAMES = 127           # shadow_skin.ROT_FRAMES

CURVE_FRAMES = 41           # EQ_STEPS
VU_FRAMES = 81              # VU_STEPS
MET_FRAMES = 41             # MET_STEPS


def _arc_pts(cx, cy, rad, a0, a1, n=48):
    return [(cx + rad * math.sin(math.radians(a0 + (a1 - a0) * i / n)),
             cy - rad * math.cos(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def _ring(d, cx, cy, rad, a0, a1, fill, width):
    """An arc with round caps (PIL's arc has square ends)."""
    if abs(a1 - a0) < 0.4:
        return
    d.line(_arc_pts(cx, cy, rad, a0, a1), fill=fill, width=width, joint="curve")
    for a in (a0, a1):
        x = cx + rad * math.sin(math.radians(a))
        y = cy - rad * math.cos(math.radians(a))
        d.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=fill)


def _dome(size, r):
    """The knob body: a radial gradient from a light top-left to a dark bottom-right, plus a rim."""
    s = size
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    grad = Image.new("RGB", (s, s), (0, 0, 0))
    gd = ImageDraw.Draw(grad)
    cx, cy = s / 2, s / 2
    for i in range(int(r) + 1, -1, -1):
        t = i / r
        # t=0 at the centre (light), 1 at the rim (dark): a soft dome, not a flat disc
        col = L.lerp((80, 88, 99), (23, 27, 32), min(1.0, t ** 0.72))
        dx, dy = -r * 0.35 * (1 - t), -r * 0.4 * (1 - t)
        gd.ellipse([cx - i + dx, cy - i + dy, cx + i + dx, cy + i + dy], fill=col)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(8, 9, 12, 255), width=1)          # contact edge
    hi = [(cx + (r - 1.2) * math.sin(math.radians(a)), cy - (r - 1.2) * math.cos(math.radians(a)))
          for a in range(-150, -40, 4)]
    d.line(hi, fill=(96, 104, 116, 90), width=1, joint="curve")                             # top rim light
    # specular blob, blurred
    spec = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(spec).ellipse([cx - r * 0.62, cy - r * 0.78, cx + r * 0.12, cy - r * 0.24],
                                fill=(255, 255, 255, 26))
    img.alpha_composite(spec.filter(ImageFilter.GaussianBlur(2.2)))
    return img


def make_knob(path, frames=KNOB_FRAMES):
    s = KNOB_S
    c = s / 2.0
    ring_r = KNOB_R + 2.5
    dome = _dome(s, KNOB_R - 2.5)
    strip = Image.new("RGBA", (s, s * frames), (0, 0, 0, 0))
    for k in range(frames):
        fr = Image.new("RGBA", (s, s), (0, 0, 0, 0))
        d = ImageDraw.Draw(fr)
        a = -135.0 + 270.0 * k / (frames - 1)
        _ring(d, c, c, ring_r, -135, 135, (46, 52, 61, 255), 3)                  # the unlit track
        if k > 0:
            glow = Image.new("RGBA", (s, s), (0, 0, 0, 0))
            _ring(ImageDraw.Draw(glow), c, c, ring_r, -135, a, L.rgba(L.ARC, 70), 7)
            fr.alpha_composite(glow.filter(ImageFilter.GaussianBlur(2.0)))
            _ring(d, c, c, ring_r, -135, a, L.rgba(L.ARC), 3)                    # the value arc
        fr.alpha_composite(dome)
        # pointer: a bright bar from the middle out to the rim, with a dark shadow under it
        p0, p1 = KNOB_R * 0.30, KNOB_R * 0.80
        for dx, dy, col in ((0.8, 1.0, (0, 0, 0, 150)), (0, 0, (244, 247, 250, 255))):
            x0 = c + p0 * math.sin(math.radians(a)) + dx
            y0 = c - p0 * math.cos(math.radians(a)) + dy
            x1 = c + p1 * math.sin(math.radians(a)) + dx
            y1 = c - p1 * math.cos(math.radians(a)) + dy
            d.line([(x0, y0), (x1, y1)], fill=col, width=3)
        strip.paste(fr, (0, k * s))
    strip.save(path, optimize=True)
    return path


def make_curve(path, w=L.COL_W, h=L.COL_H, frames=CURVE_FRAMES):
    """One column: the area under the curve, from the value down to -24 dB, with a bright stepped cap."""
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for k in range(frames):
        db = L.DB_MIN + (L.DB_MAX - L.DB_MIN) * k / (frames - 1)
        y = int(round((L.DB_MAX - db) / (L.DB_MAX - L.DB_MIN) * h))
        y = max(0, min(h, y))
        fr = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        if y < h:
            body_h = h - y
            body = Image.new("RGBA", (w, body_h), (0, 0, 0, 0))
            bd = ImageDraw.Draw(body)
            for i in range(body_h):
                t = i / max(1, body_h - 1)
                bd.line([(0, i), (w, i)], fill=L.rgba(L.lerp(L.CURVE, (34, 58, 92), t ** 1.1),
                                                      round(150 - 118 * t)))
            fr.alpha_composite(body, (0, y))
            # cap: the curve's own line, plus a hairline of glow above it
            cap = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            cd = ImageDraw.Draw(cap)
            cd.rectangle([0, max(0, y - 1), w - 1, min(h - 1, y + 2)], fill=L.rgba(L.CURVE_CAP, 235))
            if y >= 3:
                cd.rectangle([0, y - 3, w - 1, y - 1], fill=L.rgba(L.CURVE, 90))
            fr.alpha_composite(cap.filter(ImageFilter.GaussianBlur(0.4)))
        strip.paste(fr, (0, k * h))
    strip.save(path, optimize=True)
    return path


def _bar(w, h, frac, lo, hi, hot=None):
    """A horizontal meter frame: rounded fill from the left, a bright head, nothing where it is empty."""
    fr = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    inner = w - 2
    fill_w = int(round(frac * inner))
    if fill_w > 1:
        col = L.lerp(lo, hi, min(1.0, frac / 0.85))
        if hot and frac > 0.85:
            col = L.lerp(hi, hot, (frac - 0.85) / 0.15)
        r = (h - 2) / 2.0
        d = ImageDraw.Draw(fr)
        d.rounded_rectangle([1, 1, 1 + fill_w, h - 2], radius=r, fill=L.rgba(col, 240))
        d.rounded_rectangle([1, 1, 1 + max(2, fill_w - 2), h - 3], radius=r,
                            fill=L.rgba(L.lerp(col, (255, 255, 255), 0.16), 255))
        # head: a bright 2 px edge so the value reads without a needle
        hx = 1 + fill_w
        d.rectangle([max(2, hx - 2), 2, min(w - 2, hx), h - 3], fill=L.rgba(L.lerp(col, L.WHITE, 0.75), 255))
    return fr


def make_meter(path, w, h, frames, lo, hi, hot=None):
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    for k in range(frames):
        strip.paste(_bar(w, h, k / (frames - 1), lo, hi, hot), (0, k * h))
    strip.save(path, optimize=True)
    return path


def make_panic(path_off, path_on, w=136, h=40):
    """The PANIC trigger's two plates. The label is drawn by the renderer on top (button-tx), so these
    carry no text -- a big hardware-looking trigger next to the master, dark at rest, gold while held."""
    for path, fill, edge, glow in ((path_off, (42, 18, 22), (124, 58, 63), (120, 20, 26)),
                                  (path_on, (224, 163, 58), (255, 214, 150), (255, 190, 90))):
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        mask = Image.new("L", (w, h), 0)
        ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], radius=7, fill=255)
        grad = L.vgrad((w, h), L.lerp(fill, (255, 255, 255), 0.10), L.lerp(fill, (0, 0, 0), 0.25))
        img.paste(grad, (0, 0), mask)
        d.rounded_rectangle([0, 0, w - 1, h - 1], radius=7, outline=edge, width=1)
        d.line([(8, 2), (w - 9, 2)], fill=L.rgba(glow, 90), width=1)
        img.save(path, optimize=True)
    return path_off


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="vst/art", help="output directory (default vst/art)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    j = lambda name: os.path.join(a.out, name)
    print("wrote", make_knob(j("knob_strip.png")))
    print("wrote", make_curve(j("curve_strip.png")))
    print("wrote", make_meter(j("vu_strip.png"), 184, 14, VU_FRAMES, (58, 140, 186), L.AMBER, L.RED))
    print("wrote", make_meter(j("met_dly.png"), 184, 12, MET_FRAMES, (86, 72, 148), L.VIOLET))
    print("wrote", make_meter(j("met_verb.png"), 184, 12, MET_FRAMES, (46, 76, 140), L.BLUE))
    print("wrote", make_meter(j("met_rep.png"), 184, 12, MET_FRAMES, (44, 108, 68), L.GREEN))
    print("wrote", make_panic(j("btn_panic_off.png"), j("btn_panic_on.png")))


if __name__ == "__main__":
    main()
