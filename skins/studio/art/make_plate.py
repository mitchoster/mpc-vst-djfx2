#!/usr/bin/env python3
"""Draw the page plates for the MPC DJ FX 2 skin (1280x628, one PNG per tab).

Composition (3-tab redesign, 2026-10-27): header strip with the master section, one large plot panel
that carries the curve grid, the meter rail and the two live read-outs, then a row of four parameter
cards (310 px wide, 8 px apart, 8 px margin). Everything static is baked here -- the layout
(vst/layout.conf) only places the live controls on top, and the widget artwork (vst/art/make_widgets.py)
is drawn separately: the filmstrips are the only things MPC animates.

Pages: DJ FX (performance), DJ FX 2 (space / texture / output), SETUP (macros). SETUP fills two of its
four card slots and leaves the other two deliberately empty ("RESERVED") -- the owner wants room for
later ideas, so nothing else goes there.

Rules kept: no text where MPC draws a live name or value (only titles, captions and the grid labels
are baked), control areas stay calm enough for the live text, and the curve columns are drawn from a
transparent filmstrip so this grid stays readable underneath them.

  docker run --rm -v <framework>:/mv:ro -v <port>:/w -w /w mpc-vst-html-art \
      python3 vst/art/make_plate.py -o vst
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import look as L  # noqa: E402

# ---- per tab: header tag, rail layout, and the four card slots (None = an empty, reserved slot).
# Card order is Q-Link order: the four knobs of slot 0 are Q-Link 1-4, slot 1 is 5-8, and so on.
def card(title, accent, sub, hints=(), readout=None):
    return {"title": title, "accent": accent, "sub": sub, "hints": list(hints), "readout": readout}


PAGES = {
    "dj": {
        "tag": "LIVE",
        "file": "bg_dj.png",
        "rail": "meters",
        "cards": [
            card("DJ FILTER", L.CYAN, "LPF \u00b7 HPF", ["FILTER \u2212 CUTS HIGHS, + CUTS LOWS",
                                                      "HPF + HP RESO = THE VERB'S HIGH-PASS"]),
            card("ISOLATOR", L.AMBER, "3-BAND", ["LOW X SPLITS LOW / MID",
                                                 "HI X: OUTPUT CARD, DJ FX 2"]),
            card("BEAT REPEAT", L.GREEN, "GRID", []),
            card("DELAY", L.VIOLET, "SYNC \u00b7 PONG", ["DIVISION ON THE SETUP PAGE"]),
        ],
    },
    "dj2": {
        "tag": "SPACE",
        "file": "bg_dj2.png",
        "rail": "meters",
        "cards": [
            card("REVERB", L.BLUE, "PLATE / HALL", ["PRE + VERB AMOUNT = THE THROW"]),
            card("FLANGER", L.MAGENTA, "THROUGH-ZERO", ["MIX 0 = DRY"]),
            card("GRAIN", L.TEAL, "GLITCH", ["THE ORIGINAL GLITCH STAGE",
                                             "RATE = GRAIN SIZE \u00b7 SPRAY = JITTER"]),
            card("OUTPUT", L.RED, "FILL \u00b7 MIX \u00b7 HI X", ["MIX FOLLOWS THE EIGHT MACROS"],
                 readout="mix"),
        ],
    },
    "setup": {
        "tag": "MACROS",
        "file": "bg_setup.png",
        "rail": "division",
        "cards": [
            card("MACRO \u00b7 FX", L.ORANGE, "MORPH", ["ONE KNOB EACH, AS THE ORIGINAL RACK"]),
            card("MACRO \u00b7 TONE", L.ORANGE, "EQ \u00b7 FILL", ["LOW-EQ / MID-EQ / HI-EQ",
                                                                 "AUTO-FILL DRIVES THE BEAT REPEAT"]),
            None,
            None,
        ],
    },
}

DB_TICKS = (12, 6, 0, -6, -12, -18, -24)
HZ_TICKS = ((20, "20"), (50, "50"), (100, "100"), (200, "200"), (500, "500"), (1000, "1k"),
            (2000, "2k"), (5000, "5k"), (10000, "10k"), (20000, "20k"))


def header(img, d, tag):
    d.rectangle([0, 0, L.W, L.HEADER_H], fill=(16, 19, 23))
    d.line([(0, L.HEADER_H), (L.W, L.HEADER_H)], fill=(35, 40, 48), width=1)
    # accent block + product name + page tag (MPC does not draw tab names, so the page is named here)
    d.rectangle([24, 17, 28, 39], fill=L.AMBER)
    L.tracked(d, (40, 9), "DJ FX 2", L.font("bold", 27), L.INK, 1.4)
    L.tracked(d, (186, 22), tag, L.font("semi", 11), L.AMBER, 2.6)
    # captions for the three header controls (baked; the controls themselves are placed by the layout)
    L.cap(d, (293, 24), "curve", 10, L.INK_DIM, 1.4, anchor="r")
    L.cap(d, (465, 24), "master", 10, L.INK_DIM, 1.4, anchor="r")
    L.cap(d, (892, 24), "limiter", 10, L.INK_DIM, 1.4, anchor="r")


def panel(img, d, page):
    L.plate(img, L.PANEL, 12, L.PANEL_TOP, L.PANEL_BOT, L.PANEL_EDGE, shadow=8)
    L.cap(d, (30, 74), "isolator + dj filter", 10, L.INK_FAINT, 1.8)
    L.cap(d, (L.RAIL[0] - 22, 74), "32 bands \u00b7 10 hz \u00b7 \u221224 \u2026 +12 db", 10, L.INK_FAINT,
          1.4, anchor="r")
    grid(img, d)
    rail(img, d, page["rail"])


def grid(img, d):
    gx0, gy0, gx1, gy1 = L.GRID
    # plot well: slightly darker than the panel so the columns sit "inside" the panel
    d.rounded_rectangle([gx0 - 1, gy0 - 1, gx1, gy1], radius=6, fill=(11, 13, 17))
    for hz, lab in HZ_TICKS:
        x = L.freq_x(hz)
        d.line([(x, gy0), (x, gy1)], fill=L.GRID_LINE, width=1)
        L.cap(d, (x, gy1 + 8), lab, 10, L.INK_FAINT, 0.8, anchor="m")
    for db in DB_TICKS:
        y = L.db_y(db)
        if db == 0:
            d.line([(gx0, y), (gx1, y)], fill=L.GRID_ZERO, width=1)
        else:
            d.line([(gx0, y), (gx1, y)], fill=L.GRID_LINE, width=1)
        txt = "%+d" % db if db else "0"
        L.cap(d, (gx0 - 12, y - 6), txt, 10, L.INK_DIM if db else L.INK, 0.6, anchor="r")


def trough(d, cy, h, ticks=8):
    """The recessed well a meter's filmstrip fills (the strip itself is transparent where it is empty)."""
    x0, x1 = L.RAIL
    r = h // 2
    d.rounded_rectangle([x0, cy - h // 2, x1, cy + h // 2], radius=r, fill=(10, 12, 16),
                        outline=(38, 44, 53), width=1)
    pad = 3
    for i in range(1, ticks):
        x = x0 + pad + (x1 - x0 - 2 * pad) * i / ticks
        d.line([(x, cy - h // 2 + 3), (x, cy + h // 2 - 3)], fill=(30, 35, 42), width=1)


def rule(d, y, pad=0):
    """A hairline across the rail: separates a rail footer (the delay time) from the sections above."""
    x0, x1 = L.RAIL
    d.line([(x0 + pad, y), (x1 - pad, y)], fill=(36, 42, 51), width=1)


def readout_box(d, cx, cy, w, h):
    """The recessed LCD a read-out's live number sits on."""
    d.rounded_rectangle([cx - w // 2, cy - h // 2, cx + w // 2, cy + h // 2], radius=5,
                        fill=(9, 11, 14), outline=(36, 41, 49), width=1)


def readout_plate(d, cy, h):
    x0, x1 = L.RAIL
    readout_box(d, (x0 + x1) // 2, cy, x1 - x0, h)


def rail(img, d, kind):
    x0, x1 = L.RAIL
    L.cap(d, (x0, 88), "output \u00b7 dbfs", 10, L.INK_FAINT, 1.8)
    readout_plate(d, 119, 38)
    if kind == "meters":
        # 145, not 150: at 150 the caption's last rows sat under the VU trough (which starts at 159) and
        # were cut off by it. Kept 3 px clear like the other three captions.
        L.cap(d, (x0, 145), "level", 10, L.INK_FAINT, 1.8)
        trough(d, 166, 14)
        L.cap(d, (x0, 186), "delay tail", 10, L.INK_FAINT, 1.8)
        trough(d, 204, 12)
        L.cap(d, (x0, 218), "reverb tail", 10, L.INK_FAINT, 1.8)
        trough(d, 238, 12)
        L.cap(d, (x0, 252), "repeat", 10, L.INK_FAINT, 1.8)
        trough(d, 272, 12)
        rule(d, 282)
        L.cap(d, (x0, 288), "delay time", 10, L.INK_FAINT, 1.8)
        readout_plate(d, 316, 32)
    else:                                   # setup page: the delay division selector lives here
        L.cap(d, (x0, 148), "delay division", 10, L.INK_FAINT, 1.8)
        # a faint frame around the 3 x 4 division tiles (the enum's own tiles sit inside it)
        d.rounded_rectangle([x0 + 4, 160, x1 - 4, 294], radius=6, fill=(11, 13, 17),
                            outline=(34, 40, 48), width=1)
        rule(d, 296)
        L.cap(d, (x0, 302), "delay time", 10, L.INK_FAINT, 1.8)
        readout_plate(d, 330, 28)


def cards(img, d, page):
    for i in range(L.CARD_COUNT):
        spec = page["cards"][i] if i < len(page["cards"]) else None
        x0 = L.card_x(i)
        x1 = x0 + L.CARD_W
        top = L.CARDS_Y
        bot = top + L.CARDS_H
        if spec is None:                      # an intentionally empty slot (SETUP has two)
            L.plate(img, (x0, top, x1, bot), 9, (26, 30, 36), (18, 21, 26), (34, 39, 45), shadow=6)
            L.cap(d, (x0 + L.CARD_W // 2, top + L.CARDS_H // 2 - 9), "reserved", 11, L.INK_FAINT, 3.2,
                  anchor="m")
            continue
        L.plate(img, (x0, top, x1, bot), 9, L.CARD_TOP, L.CARD_BOT, L.CARD_EDGE, shadow=6)
        # accent cap: the card's own colour, the only saturated thing on it besides its title
        d.rounded_rectangle([x0 + 2, top + 1, x1 - 3, top + 3], radius=1, fill=spec["accent"])
        # title + its spec line on one row: the space below belongs to the controls
        f_title = L.font("bold", 13)
        end = L.tracked(d, (x0 + L.CARD_PAD, L.CARD_TITLE_Y), spec["title"].upper(), f_title,
                        spec["accent"], 1.3)
        if spec["sub"]:
            L.cap(d, (end + 9, L.CARD_TITLE_Y + 2), spec["sub"], 9.5, L.INK_FAINT, 1.1)
        for n, line in enumerate(spec["hints"]):
            L.cap(d, (x0 + L.CARD_PAD, L.CARD_HINT_Y + 15 * n), line, 9, L.INK_FAINT, 1.0)
        if spec["readout"] == "mix":
            mix_readout(d, x0)


def mix_readout(d, x0):
    """The OUTPUT card's MIX read-out: a baked plate in the knob row's value slot, its caption in the
    name slot. The live number is a `readout` widget in the layout (key=mix_show) -- mix is derived
    from the eight macros, so it must not be a knob."""
    cx, cy = x0 + L.CARD_MIX_CX, L.CARD_MIX_CY
    readout_box(d, cx, cy, L.CARD_MIX_W, L.CARD_MIX_H)
    L.cap(d, (cx, L.CARD_MIX_CAP_Y), "mix", 10, L.INK_FAINT, 1.8, anchor="m")


def draw_page(name, out_dir):
    page = PAGES[name]
    img = Image.new("RGB", (L.W, L.H), L.BG)
    L.grain(img)
    L.vignette(img)
    d = ImageDraw.Draw(img)
    header(img, d, page["tag"])
    panel(img, d, page)
    cards(img, d, page)
    path = os.path.join(out_dir, page["file"])
    img.save(path, optimize=True)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="vst", help="output directory (default vst)")
    ap.add_argument("--page", default=None, help="draw one page (%s)" % ", ".join(PAGES))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    for name in ([a.page] if a.page else list(PAGES)):
        print("wrote", draw_page(name, a.out))


if __name__ == "__main__":
    main()
