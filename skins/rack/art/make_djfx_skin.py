#!/usr/bin/env python3
"""Faceplate, filmstrips and tiles for the MPC DJ FX 2 skin (PulyTek-style hardware panel).

One page = one anodised graphite panel with engraved lettering, a single warm amber LED colour and
one real cream-dial VU. Everything is drawn procedurally with Pillow, so the build is deterministic and
needs nothing but this file, the framework's Titillium Web TTFs and Python 3 + Pillow:

    docker run --rm -v <framework>:/mv -v <port>:/w -w /w mpc-vst-html-art \
        python3 vst/art/make_djfx_skin.py

Output (written next to vst/):
    bg_perform.png  bg_fx.png  bg_tone.png   1280x628 page backgrounds (texture, masthead, chain strip)
    vu_strip.png      252x150 x 81   the VU dial, scale baked in (key=vu)
    eq_strip.png      24x250  x 41   one EQ column, filled from the bottom (key=eq_00..eq_31)
    led_strip.png     200x14  x 41   stage-activity bar (rep_act / dly_tail / verb_tail)
    seg_off/on.png    54x25          option tile (GRID, DIVISION)
    panic_off/on.png  170x80         the PANIC button

Two things are read back out of vst/layout.conf so the artwork can never drift from the layout:
the `readout` widgets (each gets a recessed plate) and the eq_* `meter` columns (the plot grid).

Nothing live is baked: MPC draws every parameter name and value on top of this art.
"""
import os
import os
import random
import shlex
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1280, 628
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)                      # vst/
LAYOUT = os.path.join(OUT, "layout.conf")

# ---------------------------------------------------------------- palette
PANEL_TOP = (31, 36, 41)      # anodised graphite, lit from above
PANEL_BOT = (17, 20, 23)
PANEL_MID = (24, 28, 32)
RULE = (63, 71, 78)
RULE_HI = (96, 106, 113)
RULE_DIM = (44, 51, 57)
BONE = (214, 205, 185)        # engraved labels
BONE_HI = (241, 233, 214)     # values / masthead
DIM = (138, 145, 150)
AMBER = (229, 168, 65)
AMBER_HI = (247, 199, 112)
HOT = (214, 92, 52)
CREAM = (238, 229, 204)       # VU dial
INK = (26, 27, 25)            # VU scale ink
BEZEL = (11, 13, 15)

FONT_DIRS = [os.environ.get("DJFX_FONT_DIR", ""), "/mv/tools/html_art/fonts",
             os.environ.get("MPC_VST_FONTS", "/usr/share/fonts/truetype"),
             os.path.join(HERE, "fonts")]
_FONTS = {}


def font(weight, size):
    key = (weight, size)
    if key in _FONTS:
        return _FONTS[key]
    name = {"bold": "TitilliumWeb-Bold.ttf", "semi": "TitilliumWeb-SemiBold.ttf",
            "reg": "TitilliumWeb-Regular.ttf"}[weight]
    for d in FONT_DIRS:
        if d and os.path.isfile(os.path.join(d, name)):
            f = ImageFont.truetype(os.path.join(d, name), size)
            _FONTS[key] = f
            return f
    sys.exit("make_djfx_skin: %s not found (set DJFX_FONT_DIR)" % name)


# ---------------------------------------------------------------- small helpers
def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def over(base, layer):
    return Image.alpha_composite(base.convert("RGBA"), layer)


def layer():
    return Image.new("RGBA", (W, H), (0, 0, 0, 0))


def th_len(text, f, tracking):
    return sum(f.getlength(c) for c in text) + tracking * max(0, len(text) - 1)


def tracked(d, xy, text, f, fill, tracking=0.0, anchor="left", shadow=None, sh_off=(0, 1)):
    """Letterspaced text, vertically centred on xy[1]; shadow = (r,g,b,a) drawn sh_off px lower."""
    x, y = xy
    total = th_len(text, f, tracking)
    if anchor == "center":
        x -= total / 2
    elif anchor == "right":
        x -= total
    for col, off in ((shadow, sh_off), (fill, (0, 0))):
        if col is None:
            continue
        xx = x + off[0]
        for ch in text:
            d.text((xx, y + off[1]), ch, font=f, fill=col, anchor="lm")
            xx += f.getlength(ch) + tracking
    return total


def panel_base():
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):
        d.line([(0, y), (W, y)], fill=lerp(PANEL_TOP, PANEL_BOT, (y / (H - 1)) ** 0.8))

    # brushed anodising: fine horizontal streaks, then a soft light wash from the top-left
    rnd = random.Random(4242)
    streaks = Image.new("L", (W, H), 0)
    sd = ImageDraw.Draw(streaks)
    for _ in range(2600):
        y = rnd.randrange(H)
        x0 = rnd.randrange(-200, W // 2)
        sd.line([(x0, y), (x0 + rnd.randrange(300, W), y)], fill=rnd.randint(1, 6))
    streaks = streaks.filter(ImageFilter.GaussianBlur(0.7))
    wash = Image.new("RGBA", (W, H), (126, 140, 152, 0))
    wash.putalpha(streaks.point(lambda v: int(v * 1.7)))
    img = over(img, wash)
    # a whisper of fine grain so the large areas are not flat
    grain = Image.effect_noise((W, H), 7).filter(ImageFilter.GaussianBlur(0.4))
    g = Image.new("RGBA", (W, H), (255, 255, 255, 0))
    g.putalpha(grain.point(lambda v: int(max(0, v - 120) * 0.16)))
    img = over(img, g)

    grad = Image.new("L", (W, H), 0)
    gd = ImageDraw.Draw(grad)
    for i in range(90):
        a = int(52 * (1 - i / 90.0))
        gd.line([(0, i), (W, i)], fill=a)
    hi = Image.new("RGBA", (W, H), (255, 250, 236, 0))
    hi.putalpha(grad)
    return over(img, hi)


def vignette(img, strength=0.55):
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).ellipse([-W * 0.25, -H * 0.55, W * 1.25, H * 1.55], fill=255)
    m = m.filter(ImageFilter.GaussianBlur(150))
    dark = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    dark.putalpha(m.point(lambda v: int((255 - v) * strength)))
    return over(img, dark)


def rule(img, x0, y0, x1, y1=None, col=RULE, a=110, w=1):
    """An engraved hairline: dark cut, light lip underneath (one px each)."""
    lay = layer()
    d = ImageDraw.Draw(lay)
    if y1 is None:
        d.line([(x0, y0), (x1, y0)], fill=col + (a,), width=w)
        d.line([(x0, y0 + 1), (x1, y0 + 1)], fill=(0, 0, 0, int(a * 0.8)), width=1)
    else:
        d.line([(x0, y0), (x0, y1)], fill=col + (a,), width=w)
        d.line([(x0 + 1, y0), (x0 + 1, y1)], fill=(0, 0, 0, int(a * 0.8)), width=1)
    return over(img, lay)


def recess(img, box, radius=7, rim=True):
    """A milled-out window: dark fill, a lit top lip and a shadow along the bottom."""
    x0, y0, x1, y1 = box
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.rounded_rectangle([x0 - 1, y0 - 1, x1 + 1, y1 + 1], radius=radius + 1, fill=(64, 72, 79, 190))
    d.rounded_rectangle([x0, y0, x1, y1], radius=radius, fill=(9, 12, 14, 252))
    if rim:
        d.rounded_rectangle([x0, y0, x1, y1], radius=radius, outline=(0, 0, 0, 200), width=2)
        d.line([(x0 + radius, y0 + 1), (x1 - radius, y0 + 1)], fill=(105, 116, 124, 120), width=1)
        d.line([(x0 + radius, y1 - 1), (x1 - radius, y1 - 1)], fill=(0, 0, 0, 210), width=1)
    # inner top shade, so the window looks sunk
    shade = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shade)
    for i in range(7):
        sd.line([(x0 + radius, y0 + 2 + i), (x1 - radius, y0 + 2 + i)], fill=(0, 0, 0, int(70 * (1 - i / 7.0))))
    lay = over(lay, shade)
    return over(img, lay)


def screw(img, cx, cy, r=7):
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(16, 19, 22, 255), outline=(6, 7, 9, 255), width=1)
    d.ellipse([cx - r + 2, cy - r + 2, cx + r - 2, cy + r - 2], fill=(38, 43, 48, 255))
    d.arc([cx - r + 1, cy - r + 1, cx + r - 1, cy + r - 1], 200, 340, fill=(122, 132, 140, 190), width=1)
    d.line([(cx - r + 3, cy + r - 3), (cx + r - 3, cy - r + 3)], fill=(9, 11, 13, 235), width=2)
    d.line([(cx - r + 3, cy + r - 2), (cx + r - 2, cy - r + 2)], fill=(96, 104, 111, 90), width=1)
    return over(img, lay)


def frame_edges(img):
    """The panel's own double hairline: outer edge, then a finer inner rule."""
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.rounded_rectangle([6, 6, W - 7, H - 7], radius=12, outline=(5, 6, 8, 255), width=1)
    d.rounded_rectangle([8, 8, W - 9, H - 9], radius=11, outline=(74, 83, 90, 210), width=1)
    d.rounded_rectangle([13, 13, W - 14, H - 14], radius=8, outline=(38, 44, 49, 190), width=1)
    for cx, cy in ((22, 22), (W - 23, 22), (22, H - 23), (W - 23, H - 23)):
        pass
    img = over(img, lay)
    for cx, cy in ((22, 22), (W - 23, 22), (22, H - 23), (W - 23, H - 23)):
        img = screw(img, cx, cy, 7)
    return img


def masthead(img, title, sub, page):
    lay = layer()
    d = ImageDraw.Draw(lay)
    # wordmark
    f_title = font("bold", 31)
    tracked(d, (36, 40), title, f_title, BONE_HI, tracking=5.0, shadow=(6, 8, 10, 210), sh_off=(0, 1))
    tw = th_len(title, f_title, 5.0)
    d.rectangle([36, 57, 36 + tw - 4, 58], fill=AMBER + (170,))
    f_sub = font("semi", 10)
    tracked(d, (38, 72), sub, f_sub, DIM + (235,), tracking=2.4, shadow=(6, 8, 10, 160))

    # page indicator: three engraved pips with the current one lit
    f_pip = font("semi", 9)
    x = 1252
    for i in (2, 1, 0):
        label = ("PERFORM", "FX", "TONE")[i]
        on = i == page
        wlab = th_len(label, f_pip, 2.2)
        x -= wlab
        tracked(d, (x, 34), label, f_pip, AMBER + (255,) if on else DIM + (170,),
                tracking=2.2, shadow=None if on else (6, 8, 10, 140))
        if on:
            d.rectangle([x, 43, x + wlab - 2, 45], fill=AMBER + (230,))
        x -= 26
    f_brand = font("semi", 8)
    tracked(d, (1252, 58), "LarryDaimond  \u00b7  DJ FX 2", f_brand, (120, 128, 134, 210), tracking=2.6, anchor="right")
    img = over(img, lay)
    img = rule(img, 28, 88, W - 28, col=RULE, a=120)
    return rule(img, 28, 91, W - 28, col=RULE_DIM, a=120)


def footer(img, page):
    lay = layer()
    d = ImageDraw.Draw(lay)
    f = font("semi", 8)
    tracked(d, (32, 614), "LarryDaimond  \u00b7  DJ FX 2  \u00b7  V2", f, (108, 115, 121, 190), tracking=2.4)
    tracked(d, (W - 32, 614), "PAGE %d / 3" % (page + 1), f, (108, 115, 121, 190), tracking=2.4, anchor="right")
    img = over(img, lay)
    return rule(img, 28, 604, W - 28, col=RULE_DIM, a=90)


# ---------------------------------------------------------------- layout read-back
def read_layout():
    """{tab name: [widget dict]} - enough to place recessed plates and the EQ plot grid."""
    tabs, cur = {}, None
    for raw in open(LAYOUT):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[tab "):
            cur = line[5:].rstrip("]").strip()
            tabs[cur] = []
            continue
        if cur is None or "=" not in line or line.startswith("qlinks"):
            continue
        try:
            toks = shlex.split(line)
        except ValueError:
            continue
        w = {"kind": toks[0]}
        for t in toks[1:]:
            k, _, v = t.partition("=")
            w[k] = v
        for k in ("cx", "cy", "x", "y", "w", "h", "r", "sw", "sh", "rows", "bw", "ns", "vs"):
            if k in w:
                try:
                    w[k] = int(w[k])
                except ValueError:
                    pass
        tabs[cur].append(w)
    return tabs


def plates(img, widgets, screen_y_offset=86):
    for w in widgets:
        if w["kind"] != "readout" or str(w.get("box", "1")) != "0":
            continue
        cx, cy = w["cx"], w["cy"] - screen_y_offset
        rw, rh = w["w"], w["h"]
        img = recess(img, (cx - rw // 2 - 5, cy - rh // 2 - 5, cx + rw // 2 + 5, cy + rh // 2 + 5), radius=6)
    return img


# ---------------------------------------------------------------- page 1: PERFORM
def chain_strip(img):
    """The signal path, engraved: the one place the panel explains itself."""
    y = 376
    lay = layer()
    d = ImageDraw.Draw(lay)
    d.rounded_rectangle([30, y - 17, W - 30, y + 17], radius=8, fill=(9, 11, 13, 120))
    d.line([(38, y - 17), (W - 38, y - 17)], fill=(96, 106, 113, 60))
    d.line([(38, y + 17), (W - 38, y + 17)], fill=(0, 0, 0, 170))
    stages = ["ISOLATOR", "DJ FILTER", "REPEAT", "DELAY", "REVERB", "FLANGER", "GRAIN", "LIMITER", "OUTPUT"]
    f = font("semi", 9)
    track = 2.6
    gap = 20
    total = sum(th_len(s, f, track) for s in stages) + gap * (len(stages) - 1)
    x = (W - total) / 2
    for i, s in enumerate(stages):
        tracked(d, (x, y), s, f, BONE + (185,), tracking=track, shadow=(0, 0, 0, 150))
        x += th_len(s, f, track)
        if i < len(stages) - 1:
            # small chevron
            cx = x + gap / 2
            d.line([(cx - 3, y - 3), (cx + 1, y), (cx - 3, y + 3)], fill=AMBER + (150,), width=2 if False else 1)
            x += gap
    img = over(img, lay)
    f2 = font("semi", 8)
    lay = layer()
    d = ImageDraw.Draw(lay)
    tracked(d, (42, y), "SIGNAL PATH", f2, (128, 136, 142, 190), tracking=2.6)
    tracked(d, (W - 42, y), "ALL EIGHT MACROS ACT ON THIS CHAIN", f2, (128, 136, 142, 190),
            tracking=2.0, anchor="right")
    return over(img, lay)


def page_perform(tabs):
    img = panel_base()
    img = vignette(img)
    img = frame_edges(img)
    img = masthead(img, "PERFORM", "MACRO MORPH  \u00b7  ISOLATOR  \u00b7  DJ FILTER  \u00b7  MASTER", 0)
    # three machined dividers between the four blocks of the top row
    for x in (396, 762, 1052):
        img = rule(img, x, 118, x, 344, col=RULE_DIM, a=120)
    img = chain_strip(img)
    # hairline above the macro row, with the row caption
    img = rule(img, 30, 408, W - 30, col=RULE, a=110)
    lay = layer()
    d = ImageDraw.Draw(lay)
    f = font("semi", 10)
    tracked(d, (36, 424), "MACRO MORPH", f, BONE + (230,), tracking=3.6, shadow=(0, 1, 0, 190))
    tracked(d, (W - 36, 424), "ONE KNOB PER GESTURE \u2014 THEY DRIVE THE ISO, FILTER, DELAY AND WET BUS",
            font("semi", 8), (120, 128, 134, 180), tracking=1.8, anchor="right")
    img = over(img, lay)
    img = plates(img, tabs.get("PERFORM", []))
    img = footer(img, 0)
    return img


# ---------------------------------------------------------------- page 2: FX
BAYS = {"REPEAT": (24, 260), "DELAY": (290, 260), "REVERB": (556, 250),
        "FLANGE": (812, 220), "GRAIN": (1038, 218)}


def flanger_wave(img):
    """A slow through-zero sweep engraved under the flanger knobs."""
    import math
    lay = layer()
    d = ImageDraw.Draw(lay)
    x0, x1, cy = 830, 1014, 470
    pts = []
    for i in range(0, x1 - x0, 2):
        t = i / float(x1 - x0)
        pts.append((x0 + i, cy + 16 * math.sin(t * 6.283185 * 1.75) * (0.45 + 0.55 * t)))
    d.line([(x0, cy), (x1, cy)], fill=(96, 106, 113, 70))
    d.line(pts, fill=(229, 168, 65, 150), width=2, joint="curve")
    f = font("semi", 8)
    tracked(d, ((x0 + x1) / 2, cy + 34), "THROUGH-ZERO SWEEP", f, (128, 136, 142, 175), tracking=2.4, anchor="center")
    return over(img, lay)


def grain_dots(img):
    """A grain cloud: dense in the middle, thinning to the edges, a few grains lit."""
    lay = layer()
    d = ImageDraw.Draw(lay)
    rnd = random.Random(77)
    cx, cy = 1147, 474
    for _ in range(260):
        x = cx + rnd.gauss(0, 34)
        y = cy + rnd.gauss(0, 20)
        if not (1052 < x < 1242 and 444 < y < 506):
            continue
        r = rnd.choice((0.7, 1.0, 1.0, 1.6, 2.2))
        a = rnd.randint(30, 130)
        lit = rnd.random() < 0.16
        d.ellipse([x - r, y - r, x + r, y + r],
                  fill=((247, 199, 112, 230) if lit else (229, 168, 65, a)))
    f = font("semi", 8)
    tracked(d, (cx, cy + 34), "GRAIN CLOUD", f, (128, 136, 142, 175), tracking=2.4, anchor="center")
    return over(img, lay)


def page_fx(tabs):
    img = panel_base()
    img = vignette(img)
    img = frame_edges(img)
    img = masthead(img, "FX", "BEAT REPEAT  \u00b7  DELAY  \u00b7  REVERB  \u00b7  FLANGER  \u00b7  GRAIN", 1)
    img = flanger_wave(img)
    img = grain_dots(img)
    img = plates(img, tabs.get("FX", []))
    img = footer(img, 1)
    return img


# ---------------------------------------------------------------- page 3: TONE
def db_of_step(step, steps=40):
    """The eq_nn columns carry 0..40 steps over the isolator's -24..+6 dB."""
    return -24.0 + 30.0 * step / steps


def eq_plot(img, widgets):
    bars = [w for w in widgets if w["kind"] == "meter" and str(w["key"]).startswith("eq_")]
    if not bars:
        return img
    bx0 = min(w["cx"] for w in bars) - bars[0]["w"] // 2
    bx1 = max(w["cx"] for w in bars) + bars[0]["w"] // 2
    y0 = bars[0]["cy"] - bars[0]["h"] // 2 - 86
    y1 = bars[0]["cy"] + bars[0]["h"] // 2 - 86
    img = recess(img, (bx0 - 6, y0 - 12, bx1 + 6, y1 + 26), radius=8)

    lay = layer()
    d = ImageDraw.Draw(lay)
    for i in range(int(y1 - y0)):
        a = int(18 * (1 - i / max(1.0, y1 - y0)))
        d.line([(bx0, y0 + i), (bx1, y0 + i)], fill=(64, 84, 96, a))
    for db in (12, 6, 0, -6, -12, -18, -24):
        y = y1 - (db + 24) / 36.0 * (y1 - y0)
        zero = db == 0
        d.line([(bx0, y), (bx1, y)], fill=(124, 138, 146, 80 if zero else 34), width=1)
    for hz in (20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000):
        x = bx0 + __import__("math").log10(hz / 20.0) / 3.0 * (bx1 - bx0)
        d.line([(x, y0), (x, y1)], fill=(124, 138, 146, 26), width=1)
    f = font("semi", 9)
    for db in (12, 6, 0, -6, -12, -18, -24):
        y = y1 - (db + 24) / 36.0 * (y1 - y0)
        tracked(d, (bx0 - 14, y), "%+d" % db, f, (162, 170, 176, 200) if db else (208, 216, 222, 235),
                tracking=1.2, anchor="right")
    for hz, lab in ((20, "20"), (50, "50"), (100, "100"), (200, "200"), (500, "500"), (1000, "1k"),
                    (2000, "2k"), (5000, "5k"), (10000, "10k"), (20000, "20k")):
        x = bx0 + __import__("math").log10(hz / 20.0) / 3.0 * (bx1 - bx0)
        tracked(d, (x, y1 + 15), lab, f, (162, 170, 176, 200), tracking=1.2, anchor="center")
    tracked(d, (bx0 - 14, y0 - 22), "dB", f, (140, 148, 154, 210), tracking=1.6, anchor="right")
    return over(img, lay)


def page_tone(tabs):
    img = panel_base()
    img = vignette(img)
    img = frame_edges(img)
    img = masthead(img, "TONE", "3-BAND ISOLATOR  \u00b7  CROSSOVERS  \u00b7  32-BAND CURVE", 2)
    img = eq_plot(img, tabs.get("TONE", []))
    lay = layer()
    d = ImageDraw.Draw(lay)
    f = font("semi", 9)
    tracked(d, (350, 442), "3-BAND ISOLATOR", f, (150, 158, 164, 205), tracking=2.6, anchor="center")
    tracked(d, (852, 442), "CROSSOVER", f, (150, 158, 164, 205), tracking=2.6, anchor="center")
    img = over(img, lay)
    img = footer(img, 2)
    return img


# ---------------------------------------------------------------- filmstrips
def vu_dial(value):
    """One VU frame: value 0..1 (-48 dBFS at the left stop .. 0 dBFS at the right)."""
    import math
    w, h = 252, 150
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # bezel: a machined frame with a lit top lip and a shadow along the bottom
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=9, fill=BEZEL + (255,))
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=9, outline=(62, 70, 77, 255), width=1)
    d.line([(10, 1), (w - 11, 1)], fill=(128, 140, 148, 110), width=1)
    d.line([(10, h - 2), (w - 11, h - 2)], fill=(0, 0, 0, 220), width=1)
    d.rounded_rectangle([4, 4, w - 5, h - 5], radius=6, fill=(26, 24, 21, 255))
    # dial face, warm cream with a soft top-to-bottom falloff
    fw, fh = w - 16, h - 16
    face = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
    fd = ImageDraw.Draw(face)
    for y in range(fh):
        t = y / (fh - 1)
        fd.line([(0, y), (fw, y)], fill=lerp((246, 239, 219), (223, 212, 185), t ** 1.2) + (255,))
    mask = Image.new("L", (fw, fh), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, fw - 1, fh - 1], radius=4, fill=255)
    img.paste(face, (8, 8), mask)
    d = ImageDraw.Draw(img)

    px, py = w / 2, 152.0            # needle pivot, just below the window
    sweep = 88.0
    R_arc, R_tick, R_lab, R_needle = 136.0, 124.0, 104.0, 120.0

    def at(frac, rad):
        a = math.radians(-sweep / 2 + sweep * frac)
        return px + rad * math.sin(a), py - rad * math.cos(a)

    # reference arc
    arc = [at(i / 120.0, R_arc) for i in range(121)]
    d.line(arc, fill=(74, 66, 50, 210), width=1)
    # -9..0 dBFS in red, the rest in ink
    for db in range(-48, 1, 2):
        frac = (db + 48) / 48.0
        major = db % 6 == 0
        hot = db > -9
        x0, y0 = at(frac, R_tick)
        x1, y1 = at(frac, R_tick + (11 if major else 6))
        d.line([(x0, y0), (x1, y1)], fill=((176, 58, 38) if hot else (62, 56, 42)) + (255,),
               width=2 if major else 1)
    for db in (-48, -36, -24, -12, 0):
        frac = (db + 48) / 48.0
        x, y = at(frac, R_lab)
        hot = db > -9
        tracked(d, (x, y), "0" if db == 0 else "%d" % db, font("bold", 14),
                (156, 48, 32) if hot else INK, tracking=0.4, anchor="center")
    tracked(d, (w / 2, 94), "VU", font("bold", 22), (44, 42, 34), tracking=3.4, anchor="center")
    tracked(d, (w / 2, 110), "PEAK METER  \u00b7  0 dBFS", font("semi", 7), (104, 95, 76),
            tracking=1.2, anchor="center")
    tracked(d, (w / 2, 128), "LarryDaimond", font("semi", 6), (126, 117, 96), tracking=1.8, anchor="center")

    # needle
    a = math.radians(-sweep / 2 + sweep * value)
    nx, ny = px + R_needle * math.sin(a), py - R_needle * math.cos(a)
    # tapered needle: 2.6 px at the pivot, a hair at the tip
    ox, oy = math.cos(a) * 1.6, math.sin(a) * 1.6
    d.polygon([(px - ox, py - oy), (px + ox, py + oy), (nx, ny)], fill=(164, 48, 30, 255))
    d.line([(px + ox * 0.4, py + oy * 0.4), (nx, ny)], fill=(206, 96, 68, 110), width=1)
    d.ellipse([px - 6, py - 6, px + 6, py + 6], fill=(30, 27, 23, 255))
    d.ellipse([px - 3, py - 3, px + 3, py + 3], fill=(158, 146, 124, 255))
    # glass: a diagonal reflection across the top-left
    gl = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(gl)
    gd.polygon([(9, 9), (w - 9, 9), (w - 9, 30), (9, 66)], fill=(255, 255, 255, 14))
    gd.rounded_rectangle([8, 8, w - 9, h - 9], radius=4, outline=(255, 255, 255, 22), width=1)
    return over(img, gl)


def eq_column(value, w=24, h=230):
    """One EQ column: a bar filled from the bottom, brighter at the top, with a lit cap."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    fill = max(0, int(round(value * h)))
    if fill <= 0:
        return img
    x0, x1 = 5, w - 6
    top = h - fill
    for y in range(top, h):
        t = (y - top) / max(1, fill - 1)          # 0 at the bar top, 1 at the floor
        c = lerp(AMBER_HI, (150, 96, 34), t ** 0.7)
        d.line([(x0, y), (x1, y)], fill=c + (255,))
    d.rectangle([x0, max(top, 0), x1, min(h - 1, top + 1)], fill=(252, 226, 168, 255))
    d.rectangle([x0, h - 2, x1, h - 1], fill=(120, 76, 28, 255))
    return img


def led_bar(value):
    """Stage-activity bar: 20 segments, amber, red at the top of the scale."""
    w, h = 200, 14
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=4, fill=(9, 11, 13, 255), outline=(56, 64, 70, 255))
    n = 20
    pad, gap = 4, 2
    seg = (w - 2 * pad - (n - 1) * gap) / n
    lit = int(round(value * n))
    for i in range(n):
        x0 = pad + i * (seg + gap)
        on = i < lit
        col = (229, 168, 65) if i < n - 4 else (214, 92, 52)
        if on:
            d.rounded_rectangle([x0, pad, x0 + seg - 1, h - pad - 1], radius=1, fill=col + (255,))
        else:
            d.rectangle([x0, pad, x0 + seg - 1, h - pad - 1], fill=(34, 38, 42, 255))
    return img


def write_strip(path, frames):
    fw, fh = frames[0].size
    strip = Image.new("RGBA", (fw, fh * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        strip.paste(f, (0, i * fh))
    strip.save(path, optimize=True)
    print("wrote %s (%dx%d, %d frames)" % (path, fw, fh * len(frames), len(frames)))


# ---------------------------------------------------------------- tiles and plates
def tiles():
    w, h = 54, 25
    off = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(off)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=4, fill=(32, 37, 42, 255))
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=4, outline=(5, 6, 8, 255), width=1)
    d.line([(4, 1), (w - 5, 1)], fill=(96, 106, 113, 120), width=1)
    d.line([(4, h - 2), (w - 5, h - 2)], fill=(0, 0, 0, 190), width=1)
    on = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(on)
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=4, fill=(232, 223, 198, 255))
    d.rounded_rectangle([0, 0, w - 1, h - 1], radius=4, outline=(229, 168, 65, 210), width=1)
    d.line([(4, 1), (w - 5, 1)], fill=(255, 252, 240, 200), width=1)
    d.line([(4, h - 2), (w - 5, h - 2)], fill=(176, 166, 142, 220), width=1)
    off.save(os.path.join(OUT, "art", "seg_off.png"))
    on.save(os.path.join(OUT, "art", "seg_on.png"))
    print("wrote art/seg_off.png art/seg_on.png (%dx%d)" % (w, h))


def toggle_plate():
    """A mini bat-lever toggle in a machined base plate, with an amber indicator that lights when on."""
    w, h = 34, 50
    cx = w / 2
    out = []
    for on in (0, 1):
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([1, 1, w - 2, h - 2], radius=6, fill=(30, 36, 41, 255))
        d.rounded_rectangle([1, 1, w - 2, h - 2], radius=6, outline=(6, 8, 10, 255), width=1)
        d.line([(6, 2), (w - 7, 2)], fill=(112, 122, 129, 130), width=1)
        d.line([(6, h - 3), (w - 7, h - 3)], fill=(0, 0, 0, 210), width=1)

        # lever, drawn first so the nut ring covers its foot
        base_y, tip_y = 23.0, (9.0 if on else 36.0)
        d.polygon([(cx - 2.6, base_y), (cx + 2.6, base_y), (cx + 1.5, tip_y), (cx - 1.5, tip_y)],
                  fill=(196, 189, 170, 255))
        d.line([(cx - 1.9, base_y), (cx - 1.0, tip_y)], fill=(246, 241, 226, 190), width=1)
        d.line([(cx + 2.0, base_y), (cx + 1.2, tip_y)], fill=(96, 92, 82, 190), width=1)
        d.ellipse([cx - 4.2, tip_y - 4.2, cx + 4.2, tip_y + 4.2], fill=(222, 214, 194, 255),
                  outline=(112, 106, 92, 220))

        # pivot nut
        d.ellipse([cx - 9, 23 - 9, cx + 9, 23 + 9], fill=(46, 53, 59, 255), outline=(7, 9, 11, 255))
        d.arc([cx - 9, 23 - 9, cx + 9, 23 + 9], 185, 355, fill=(128, 140, 148, 160), width=1)
        d.ellipse([cx - 4, 23 - 4, cx + 4, 23 + 4], fill=(24, 28, 32, 255))

        if on:
            glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            gd = ImageDraw.Draw(glow)
            gd.ellipse([cx - 7, 38, cx + 7, 52], fill=(240, 176, 74, 95))
            img = over(img, glow)
            d = ImageDraw.Draw(img)
        d.ellipse([cx - 2.6, 42.4, cx + 2.6, 47.6],
                  fill=(244, 182, 84, 255) if on else (40, 39, 34, 255))
        p = os.path.join(OUT, "art", "switch_%s.png" % ("on" if on else "off"))
        img.save(p)
        out.append(p)
    print("wrote %s" % " ".join(out))


def panic_plate():
    w, h = 170, 80
    out = []
    for on in (0, 1):
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        base = (150, 52, 32) if on else (74, 33, 24)
        d.rounded_rectangle([0, 0, w - 1, h - 1], radius=8, fill=base + (255,))
        d.rounded_rectangle([0, 0, w - 1, h - 1], radius=8, outline=(5, 6, 8, 255), width=2)
        d.line([(10, 2), (w - 11, 2)], fill=(255, 178, 148, 90 if not on else 190), width=1)
        d.line([(10, h - 3), (w - 11, h - 3)], fill=(0, 0, 0, 200), width=2)
        if on:
            glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            gd = ImageDraw.Draw(glow)
            gd.rounded_rectangle([2, 2, w - 3, h - 3], radius=7, fill=(240, 120, 70, 70))
            img = over(img, glow)
            d = ImageDraw.Draw(img)
        f = font("bold", 21)
        tracked(d, (w / 2, h / 2 - 6), "PANIC", f, (255, 236, 220) if on else (214, 190, 178),
                tracking=4.4, anchor="center", shadow=(0, 1, 0, 200))
        f2 = font("semi", 7)
        tracked(d, (w / 2, h / 2 + 18), "FLUSH ALL BUFFERS", f2, (255, 220, 200, 190) if on else (168, 130, 116),
                tracking=1.8, anchor="center")
        p = os.path.join(OUT, "art", "panic_%s.png" % ("on" if on else "off"))
        img.save(p)
        out.append(p)
    print("wrote %s" % " ".join(out))


# ---------------------------------------------------------------- main
def main():
    tabs = read_layout()
    for name, fn in (("perform", page_perform), ("fx", page_fx), ("tone", page_tone)):
        img = fn(tabs)
        p = os.path.join(OUT, "bg_%s.png" % name)
        img.convert("RGB").save(p, optimize=True)
        print("wrote", p)

    write_strip(os.path.join(OUT, "art", "vu_strip.png"), [vu_dial(i / 80.0) for i in range(81)])
    bars = [w for w in tabs.get("TONE", []) if w["kind"] == "meter" and str(w["key"]).startswith("eq_")]
    bw, bh = (bars[0]["w"], bars[0]["h"]) if bars else (24, 230)
    write_strip(os.path.join(OUT, "art", "eq_strip.png"),
                [eq_column(i / 40.0, bw, bh) for i in range(41)])
    write_strip(os.path.join(OUT, "art", "led_strip.png"), [led_bar(i / 40.0) for i in range(41)])
    tiles()
    toggle_plate()
    panic_plate()


if __name__ == "__main__":
    main()
