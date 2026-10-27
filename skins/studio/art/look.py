#!/usr/bin/env python3
"""Shared look for the MPC DJ FX 2 skin: palette, geometry and text helpers.

One place for the numbers that the page artwork (make_plate.py) and the widget filmstrips
(make_widgets.py) must agree on, plus the same values written into vst/layout.conf. If a coordinate
moves here, move it there too -- the art is baked, the controls are placed by the layout, and only these
two files know where a card or the rail sits.

Design (2026-10-27, redesign brief): dark charcoal plate, a large plot with a light grid on top, one
accent hue per card below (3dthc's Shapey 5-Band EQ as the composition reference -- header strip,
hero plot, row of parameter cards -- with our own artwork and our own type scale).

Needs Pillow. Fonts come from the framework's html_art image (Titillium Web, the same face MPC uses
for its live text, so baked captions and device-drawn values sit on one grid).
"""
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1280, 628           # the plugin area (device y 86..714 -> art y 0..628)
Y_OFF = 86                 # layout.conf is written in DEVICE coordinates: add this to every art y

# ---------------------------------------------------------------- geometry (mirrors vst/layout.conf)
HEADER_H = 56
PANEL = (12, 62, 1268, 356)                 # x0, y0, x1, y1 of the plot panel
GRID = (78, 96, 1058, 302)                  # plot grid: x0, y0, x1, y1 (bars live inside)
RAIL = (1072, 1256)                         # right-hand meter rail inside the panel
COLS = 32                                   # curve columns (eq_00..eq_31)
COL_W = 29                                  # column width; step = (GRID[2]-GRID[0]) / COLS
COL_H = GRID[3] - GRID[1]                   # 206
COL_CY = GRID[1] + COL_H // 2               # 199
DB_MIN, DB_MAX = -24.0, 12.0                # the engine's curve range (EQ_DB_MIN/MAX)

CARDS_Y = 366                               # card row: y 366..614
CARDS_H = 248
CARD_COUNT = 4                             # cards per row = Q-Link cards per tab (4 knobs each)
CARD_W = 310                                # (1280 - 2*8 - 3*8) / 4: four cards fill the row
CARD_GAP = 8
CARD_X0 = 8
CARD_PAD = 14
CARD_TITLE_Y = 378                          # title text top (the subtitle sits on the same line)
CARD_CTRL_Y = 400                           # enum zone: an enum_h is centred on its cy, rows= go down
CARD_HINT_Y = 442                           # first hint line under the controls
CARD_KNOB_CY = 512                          # knob row centre (same on every card)
CARD_CELL = 68                              # knob cell width; 4 cells = 272 inside a 310 card
KNOB_R = 22                                 # card knob radius (the header master uses the same)
# the OUTPUT card's MIX read-out (a display-only read-out, not a knob: the engine derives mix from the
# macros). Plate and caption sit in the knob's name/value rows, the HI X knob beside them; offsets are
# from the card's left edge so the pair stays centred on the 310 px card.
CARD_MIX_CX = 108                           # read-out plate centre (card centre - 47)
CARD_MIX_CY = 576                           # plate centre = the knob row's value-text centre
CARD_MIX_W = 130
CARD_MIX_H = 30
CARD_MIX_CAP_Y = 536                        # "mix" caption top = the knob row's name text top
CARD_HIX_CX = 235                           # HI X knob centre (card centre + 80)


def card_x(i):
    """Left edge of card i (0..3) in art pixels."""
    return CARD_X0 + i * (CARD_W + CARD_GAP)


def knob_x(i, k):
    """Centre x of knob k (0..3) on card i: symmetric on the card, CARD_CELL apart (the layout.conf
    lines must use these numbers -- they are what check_qlinks.py reads back out of the built skin)."""
    return card_x(i) + CARD_W // 2 + round((k - 1.5) * CARD_CELL)

# ---------------------------------------------------------------- palette
BG = (10, 12, 15)            # page
BG_HI = (16, 19, 23)         # vignette centre
PANEL_TOP = (19, 23, 28)
PANEL_BOT = (14, 17, 21)
PANEL_EDGE = (40, 45, 53)
CARD_TOP = (31, 36, 43)
CARD_BOT = (22, 26, 32)
CARD_EDGE = (42, 48, 56)
CARD_INNER = (12, 14, 18)    # hairline inside the card's top edge

INK = (233, 236, 240)
INK_DIM = (143, 152, 162)
INK_FAINT = (92, 100, 110)
GRID_LINE = (36, 42, 51)
GRID_STRONG = (58, 66, 78)
GRID_ZERO = (86, 96, 110)
WHITE = (255, 255, 255)

# one accent hue per card (bar + title); the curve and the knob arcs stay one cool/warm pair
AMBER = (224, 163, 58)
CYAN = (79, 184, 217)
GREEN = (92, 192, 122)
VIOLET = (155, 134, 232)
BLUE = (94, 143, 232)
MAGENTA = (217, 107, 176)
TEAL = (69, 196, 176)
ORANGE = (232, 118, 58)
RED = (224, 82, 82)

CURVE = (126, 178, 232)      # curve fill/edge (cool, so it never fights a card accent)
CURVE_CAP = (232, 243, 255)
ARC = (224, 163, 58)         # knob value arc (one hardware part across all cards)

FONT_DIRS = ("/mv/tools/html_art/fonts", os.path.join(os.path.dirname(__file__), "fonts"),
             "/usr/share/fonts/truetype/dejavu")
FONTS = {"regular": "TitilliumWeb-Regular.ttf", "semi": "TitilliumWeb-SemiBold.ttf",
         "bold": "TitilliumWeb-Bold.ttf"}
FALLBACK = {"regular": "DejaVuSans.ttf", "semi": "DejaVuSans-Bold.ttf", "bold": "DejaVuSans-Bold.ttf"}


def font(kind, size):
    for d in FONT_DIRS:
        for name in (FONTS[kind], FALLBACK[kind]):
            p = os.path.join(d, name)
            if os.path.exists(p):
                try:
                    return ImageFont.truetype(p, size)
                except Exception:
                    pass
    return ImageFont.load_default()


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(len(a)))


def rgba(c, a=255):
    return (c[0], c[1], c[2], a)


# ---------------------------------------------------------------- drawing helpers
def vgrad(size, top, bot):
    """A vertical gradient image."""
    w, h = size
    strip = Image.new("RGB", (1, max(2, h)))
    for y in range(strip.size[1]):
        strip.putpixel((0, y), lerp(top, bot, y / (strip.size[1] - 1)))
    return strip.resize((w, h))


def plate(img, box, radius, top, bot, edge, width=1, shadow=0):
    """A rounded panel with a vertical gradient, a 1 px edge and an optional soft drop shadow."""
    x0, y0, x1, y1 = box
    w, h = x1 - x0, y1 - y0
    if shadow:
        sh = Image.new("RGBA", (w + 2 * shadow, h + 2 * shadow), (0, 0, 0, 0))
        ImageDraw.Draw(sh).rounded_rectangle([shadow, shadow, shadow + w, shadow + h], radius=radius,
                                             fill=(0, 0, 0, 110))
        sh = sh.filter(ImageFilter.GaussianBlur(shadow * 0.7))
        img.paste(sh, (x0 - shadow, y0 - shadow + 2), sh)
    mask = Image.new("L", (w, h), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, w - 1, h - 1], radius=radius, fill=255)
    img.paste(vgrad((w, h), top, bot), (x0, y0), mask)
    if edge:
        ImageDraw.Draw(img).rounded_rectangle([x0, y0, x1 - 1, y1 - 1], radius=radius, outline=edge, width=width)
    return mask


def tracked(d, xy, s, fnt, fill, tracking=0.0, anchor="l"):
    """Draw text with letter spacing; anchor l/m/r around x."""
    x, y = xy
    if anchor != "l":
        total = sum(d.textlength(c, font=fnt) + tracking for c in s) - tracking
        x -= total if anchor == "r" else total / 2.0
    for c in s:
        d.text((x, y), c, font=fnt, fill=fill)
        x += d.textlength(c, font=fnt) + tracking
    return x


def cap(d, xy, s, size=10, fill=INK_DIM, tracking=1.6, weight="semi", anchor="l"):
    """A small letterspaced caption (the skin's caption style)."""
    tracked(d, xy, s.upper(), font(weight, size), fill, tracking, anchor)


def grain(img, amount=5, seed=17):
    """Faint sensor-style noise so large dark areas don't band; deterministic."""
    noise = Image.effect_noise((W, H), amount).convert("L")
    noise = noise.filter(ImageFilter.GaussianBlur(0.4))
    img.paste(Image.blend(img, Image.merge("RGB", (noise, noise, noise)), 0.05))


def vignette(img, strength=40):
    m = Image.new("L", (W, H), 0)
    ImageDraw.Draw(m).ellipse([-W * 0.30, -H * 0.55, W * 1.30, H * 1.55], fill=255)
    m = m.filter(ImageFilter.GaussianBlur(90))
    dark = Image.new("RGB", (W, H), (0, 0, 0))
    img.paste(Image.composite(img, dark, m))


def freq_x(hz):
    """Log-frequency position of hz inside the grid (20 Hz .. 20 kHz)."""
    import math
    t = math.log10(hz / 20.0) / 3.0
    return GRID[0] + t * (GRID[2] - GRID[0])


def db_y(db):
    return GRID[1] + (DB_MAX - db) / (DB_MAX - DB_MIN) * COL_H


def col_cx(k):
    step = (GRID[2] - GRID[0]) / float(COLS)
    return GRID[0] + step * k + step / 2.0
