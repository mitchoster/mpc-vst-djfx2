#!/usr/bin/env python3
"""Draw the "paper / ink" faceplate for the MPC DJ FX 2 HTML skin (1280x628, one PNG per page).

Design brief (2026-10-26, owner): reference is MagicStino's Magic Tape Echo (ivory chassis, deep-ink
header band, hairline rules, letterspaced caps) plus Ableton's built-in devices (flat surfaces, one
accent colour, small value plates, no chrome).

Rules kept from the mpc-vst-plugin skill: no text where MPC draws live labels/values (only the header
title, the section captions and the footer mark are baked), control areas stay calm so the live text
reads, and every page gets its own file so sections can differ per tab.

Needs Pillow. Run inside the browser-art image (build_port.sh builds `mpc-vst-html-art` when vst.json
has "art":"html"):

  docker run --rm -v <framework>:/mv:ro -v <port>:/w -w /w mpc-vst-html-art \
      python3 vst/art/make_faceplate.py --all -o vst
"""
import argparse
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1280, 628
# The layout (layout.conf) is written in DEVICE coordinates: the plugin area is y 86..714, while this artwork is
# the 1280x628 plugin area itself. A control at layout cy=300 therefore sits at art y=214 (verified 2026-10-26
# against a rendered page: knob centre cy=300 showed up at 213). Section boxes are given in layout coordinates
# and shifted here, so layout.conf and this file can be read side by side.
Y_OFF = 86

# ---- palette (the CSS in vst/skin.css uses the same values through layout theme_* lines)
PAPER = (239, 232, 216)
PAPER_EDGE = (226, 218, 198)
INK = (25, 21, 17)
INK_SOFT = (74, 66, 56)
RULE = (176, 166, 146)
ACCENT = (217, 83, 30)
CREAM = (246, 241, 229)

FONT_DIR = "/mv/tools/html_art/fonts"
FONTS = {
    "bold": os.path.join(FONT_DIR, "TitilliumWeb-Bold.ttf"),
    "semi": os.path.join(FONT_DIR, "TitilliumWeb-SemiBold.ttf"),
    "reg": os.path.join(FONT_DIR, "TitilliumWeb-Regular.ttf"),
}


def font(kind, size):
    try:
        return ImageFont.truetype(FONTS[kind], size)
    except Exception:
        return ImageFont.load_default()


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def paper_field(img):
    d = ImageDraw.Draw(img)
    for y in range(H):
        d.line([(0, y), (W, y)], fill=lerp(PAPER, PAPER_EDGE, 0.35 + 0.65 * (y / (H - 1)) ** 2))


def grain(img, amount=4, seed=11):
    """Fine paper grain: cheap value noise, barely visible, keeps large areas from looking like flat fill."""
    rnd = random.Random(seed)
    sw, sh = W // 2, H // 2
    small = Image.new("L", (sw, sh))
    # "L" holds 0..255 only: centre the noise on 128, so a negative offset survives the round trip
    small.putdata([128 + rnd.randint(-amount, amount) for _ in range(sw * sh)])
    noise = small.resize((W, H), Image.BICUBIC).filter(ImageFilter.GaussianBlur(0.8))
    px, npx = img.load(), noise.load()
    for y in range(H):
        for x in range(W):
            n = npx[x, y] - 128
            if n:
                r, g, b = px[x, y]
                px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n)), max(0, min(255, b + n)))


def vignette(img, strength=26):
    mask = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(mask)
    d.ellipse([-W * 0.25, -H * 0.45, W * 1.25, H * 1.45], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(120))
    edge = Image.new("RGB", (W, H), lerp(PAPER_EDGE, INK, 0.06))
    img.paste(edge, (0, 0), mask.point(lambda v: int((255 - v) * strength / 255)))


def header(img, d, title, sub):
    """Deep-ink masthead with letterspaced caps, like the Tape Echo's plate."""
    band_h = 96
    d.rectangle([0, 0, W, band_h], fill=INK)
    # thin accent hairline under the band
    d.rectangle([0, band_h, W, band_h + 3], fill=ACCENT)
    f_title = font("bold", 40)
    f_sub = font("semi", 15)

    def tracked(draw, xy, text, fnt, fill, tracking=3.2):
        x, y = xy
        for ch in text:
            draw.text((x, y), ch, font=fnt, fill=fill)
            x += draw.textlength(ch, font=fnt) + tracking
        return x

    tracked(d, (46, 20), title, f_title, CREAM, 3.6)
    tracked(d, (48, 68), sub, f_sub, (168, 176, 168), 2.6)
    # right-hand mark
    f_mark = font("semi", 14)
    label = "MPCDJ"
    wlab = sum(d.textlength(c, font=f_mark) + 2.2 for c in label) - 2.2
    tracked(d, (W - 46 - wlab, 40), label, f_mark, (168, 176, 168), 2.2)


def section(d, box, caption, radius=10):
    """Ableton-quiet group: one hairline rule with the caption sitting on it (no boxed chrome).

    A rounded frame was the first idea, but every control row starts close to the frame's top edge and the
    live knob names/plates then collide with it; a rule above the row never does."""
    x0, y0, x1, y1 = box[0], box[1] - Y_OFF, box[2], box[3] - Y_OFF
    d.line([(x0, y0), (x1, y0)], fill=RULE, width=3)
    if caption:
        f = font("bold", 16)
        d.text((x0 + 14, y0 - 22), caption.upper(), font=f, fill=INK_SOFT)
        d.rectangle([x0, y0 - 20, x0 + 3, y0 - 6], fill=ACCENT)


def vu_window(img, d, box):
    """Recessed dark window for the live VU (the meter is engine-driven, see vst/art/make_vu.py)."""
    x0, y0, x1, y1 = box[0], box[1] - Y_OFF, box[2], box[3] - Y_OFF
    d.rounded_rectangle([x0, y0, x1, y1], radius=8, fill=lerp(INK, PAPER, 0.04), outline=RULE, width=2)
    f = font("semi", 12)
    d.text((x0 + 12, y0 + 6), "LEVEL", font=f, fill=(150, 158, 150))


def eq_grid(d, box):
    """Plot grid for the EQ page: dB lines, log frequency lines, both labelled (baked)."""
    x0, y0, x1, y1 = box[0], box[1] - Y_OFF, box[2], box[3] - Y_OFF
    f = font("reg", 11)
    for db in (12, 6, 0, -6, -12, -18, -24):
        y = y0 + (12.0 - db) / 36.0 * (y1 - y0)
        strong = db == 0
        d.line([(x0, y), (x1, y)], fill=(RULE if strong else lerp(RULE, PAPER, 0.45)),
               width=2 if strong else 1)
        d.text((x0 - 34, y - 7), "%+d" % db, font=f, fill=INK_SOFT)
    for hz, label in ((20, "20"), (50, "50"), (100, "100"), (200, "200"), (500, "500"),
                      (1000, "1k"), (2000, "2k"), (5000, "5k"), (10000, "10k"), (20000, "20k")):
        t = math.log10(hz / 20.0) / 3.0
        x = x0 + t * (x1 - x0)
        d.line([(x, y0), (x, y1)], fill=lerp(RULE, PAPER, 0.55), width=1)
        wlab = d.textlength(label, font=f)
        d.text((x - wlab / 2, y1 + 8), label, font=f, fill=INK_SOFT)


def footer(d, page):
    f = font("reg", 12)
    d.text((46, H - 20), "MPC DJ FX 2", font=f, fill=INK_SOFT)
    txt = page.upper()
    wlab = d.textlength(txt, font=f)
    d.text((W - 46 - wlab, H - 20), txt, font=f, fill=INK_SOFT)


# ---- per page: (title, subtitle, sections, vu box or None)
PAGES = {
    # boxes are (x0, y0, x1, y1) in LAYOUT/device coordinates; they follow the control rows in layout.conf
    "macros": ("DJ FX 2", "MACRO MORPH  /  ISOLATOR  /  SPACE", [
        ((46, 236, 1234, 430), "macro morph"),
        ((46, 442, 1234, 700), "isolator  \u00b7  space"),
    ], None),
    "dj": ("DJ", "FILTER  /  3-BAND ISOLATOR  /  MASTER", [
        ((46, 226, 1010, 430), "dj filter  \u00b7  isolator"),
        ((46, 442, 700, 700), "master"),
    ], (866, 540, 1226, 656)),
    "repeat": ("BEAT REPEAT", "GRID  /  GATE  /  CHANCE", [
        ((46, 236, 1234, 420), "grid  \u00b7  mode"),
        ((46, 442, 1234, 700), "gate  \u00b7  chance  \u00b7  repeats"),
    ], None),
    "delay": ("DELAY", "TIME  /  FEEDBACK  /  TONE", [
        ((46, 226, 1234, 420), "time  \u00b7  sync  \u00b7  division"),
        ((46, 442, 1234, 700), "feedback  \u00b7  tone  \u00b7  mix"),
    ], None),
    "verb": ("REVERB", "MODE  /  DECAY  /  DAMPING", [
        ((46, 226, 1234, 420), "mode  \u00b7  amount  \u00b7  decay"),
        ((46, 442, 1234, 700), "predelay  \u00b7  high-pass"),
    ], None),
    "flange": ("FLANGER + GRAIN", "MANUAL FLANGER  /  GRAIN DELAY", [
        ((46, 250, 1234, 424), "flanger"),
        ((46, 470, 1234, 700), "grain  \u00b7  the original glitch stage"),
    ], None),
    "eq": ("EQ", "ISOLATOR + DJ FILTER  /  32 BANDS  /  10 HZ", [
        ((46, 226, 1234, 466), ""),
        ((46, 512, 1234, 700), "isolator  \u00b7  dj filter"),
    ], None),
    "echo": ("ECHO", "DELAY HISTORY  /  16 COLUMNS  /  5 HZ", [
        ((46, 236, 1234, 300), "delay history"),
    ], None),
    "setup": ("SETUP", "CROSSOVERS  /  LIMITER  /  SYNC", [
        ((46, 242, 1234, 430), "isolator crossovers  \u00b7  limiter"),
        ((46, 442, 1234, 700), "delay sync"),
    ], None),
}


def draw_page(page, out_dir):
    title, sub, sections, vu = PAGES[page]
    img = Image.new("RGB", (W, H), PAPER)
    paper_field(img)
    grain(img)
    vignette(img)
    d = ImageDraw.Draw(img)
    header(img, d, title, sub)
    for box, cap in sections:
        section(d, box, cap)
    if vu:
        vu_window(img, d, vu)
    if page == "eq":
        eq_grid(d, (86, 236, 1254, 460))
    footer(d, page)
    path = os.path.join(out_dir, "bg_%s.png" % page)
    img.save(path, optimize=True)
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", default="vst", help="output directory (default vst)")
    ap.add_argument("--all", action="store_true", help="write every page")
    ap.add_argument("--page", default=None, help="write one page by name")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    pages = list(PAGES) if a.all or not a.page else [a.page]
    for p in pages:
        print("wrote", draw_page(p, a.out))


if __name__ == "__main__":
    main()
