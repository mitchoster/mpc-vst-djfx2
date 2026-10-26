#!/usr/bin/env python3
"""Draw the VU meter for the HTML skin: a filmstrip plus one PNG per step of the "vu" param.

The layout binds the engine-driven step with `meter ... key=vu strip=art/vu_strip.png frames=N`
(a display-only filmstrip Knob, which MPC redraws live when the wrapper reports the value --
`HAS_DISPLAY_REV`; verified live on an MPC One, docs/SKIN_STUDIO.md "Meter"). The per-step PNGs are
kept for reference/preview. Count must equal the "vu" param's options in vst.json (VU_STEPS in
adapters/schwung_fx/schwung_fx_engine.c).

Needs Pillow; run it in the browser-art image:

  docker run --rm -u "$(id -u):$(id -g)" -v <host project>:/w -w /w mpc-vst-html-art \
      python3 vst/art/make_vu.py -o vst/art/vu -s vst/art/vu_strip.png
"""
import argparse
import os

from PIL import Image, ImageDraw

W, H = 304, 40
STEPS = 81
BEZEL = 3
TROUGH = (223, 216, 200, 255)   # paper trough (skin: ivory + ink, 2026-10-26)
FRAME = (30, 42, 38, 255)       # ink bezel
TICK = (120, 112, 96, 255)
LOW = (31, 122, 61)       # Braun signal-green
HIGH = (217, 83, 30)      # Braun orange near the top
ALPHA = 255


def blend(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def frame(step):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=6, fill=TROUGH, outline=FRAME, width=2)
    inner_w = W - 2 * BEZEL
    frac = step / (STEPS - 1)
    fill_w = round(frac * inner_w)
    if fill_w > 0:
        c = blend(LOW, HIGH, max(0.0, (frac - 0.75) / 0.25))
        d.rounded_rectangle([BEZEL, BEZEL, BEZEL + fill_w, H - 1 - BEZEL], radius=4,
                            fill=(c[0], c[1], c[2], ALPHA))
    # segment ticks over the trough so the steps read
    for i in range(1, 8):
        x = BEZEL + inner_w * i / 8
        d.line([(x, H - 1 - BEZEL - 6), (x, H - 1 - BEZEL)], fill=TICK, width=2)
    return img


def main():
    global W, H, STEPS
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="vst/art/vu")
    ap.add_argument("--steps", type=int, default=STEPS)
    ap.add_argument("--w", type=int, default=W)
    ap.add_argument("--h", type=int, default=H)
    ap.add_argument("--prefix", default="vu")
    ap.add_argument("-s", "--strip", default="vst/art/vu_strip.png",
                    help="filmstrip output: STEPS frames of WxH stacked down, minimum first")
    a = ap.parse_args()
    STEPS, W, H = a.steps, a.w, a.h
    os.makedirs(a.out, exist_ok=True)
    frames = [frame(s) for s in range(STEPS)]
    for step, img in enumerate(frames):
        img.save(os.path.join(a.out, "%s_%03d.png" % (a.prefix, step)))
    strip = Image.new("RGBA", (W, H * STEPS), (0, 0, 0, 0))
    for step, img in enumerate(frames):
        strip.paste(img, (0, step * H))
    strip.save(a.strip)
    print("wrote %d PNGs to %s and %s (%dx%d)" % (STEPS, a.out, a.strip, W, H * STEPS))


if __name__ == "__main__":
    main()
