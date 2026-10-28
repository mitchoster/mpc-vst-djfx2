#!/usr/bin/env python3
"""Draw the sample-value mock-ups of the built skin (docs/ui-preview/page_N_values.png).

`studio.py preview` leaves every live value as an outline placeholder (MPC draws those numbers itself),
so the pages cannot be judged at a glance. This paints them in: one table of sample values per parameter,
then per page it pastes the matching knob/meter filmstrip frame, swaps the chosen option tile to its "on"
image, and writes the value text in Titillium Web on top of each placeholder.

It is a MOCK-UP, not a render: the numbers are chosen to show the type scale and the density, and the
curve columns are computed from a simple 3-band model of the isolator (below LOW X = LOW gain, between
the crossovers = 0 dB, above HI X = HI gain) rather than from the engine. Nothing here ships in the skin.

  docker run --rm -v <framework>:/mv:ro -v <port>:/w -w /w mpc-vst-html-art \\
      python3 vst/art/make_values.py
"""
import argparse
import json
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import look as L  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SKIN = os.path.join(HERE, "..", "build", "skin", "LarryDaimond - VST - MPC DJ FX 2", "Plugin Skins")

# parameter -> (0..1 position on the knob scale, the text MPC would print)
SAMPLES = {
    "dj_filter": (0.42, "LPF 780"),
    "dj_reso": (0.35, "35 %"),
    "hp_hz": (26, "26 Hz"),
    "hp_reso": (0.5, "Q 1.50"),
    "iso_lo": (-6, "\u22126 dB"),
    "iso_mid": (0, "0 dB"),
    "iso_hi": (2, "+2 dB"),
    "iso_lo_x": (250, "250 Hz"),
    "iso_hi_x": (2500, "2500 Hz"),
    "rep_gate": (0.25, "25 %"),
    "rep_chance": (1.0, "100 %"),
    "rep_count": (4, "4"),
    "rep_mix": (1.0, "100 %"),
    "dly_time": (375, "375 ms"),
    "dly_fbk": (0.55, "55 %"),
    "dly_tone": (0.4, "40 %"),
    "dly_mix": (1.0, "100 %"),
    "verb_amount": (0.3, "30 %"),
    "verb_decay": (0.5, "50 %"),
    "verb_damp": (0.5, "50 %"),
    "verb_predelay": (20, "20 ms"),
    "fl_rate": (0.3, "30 %"),
    "fl_depth": (0.5, "50 %"),
    "fl_fbk": (0.5, "50 %"),
    "fl_mix": (0.5, "50 %"),
    "gr_rate": (20, "20 Hz"),
    "gr_time": (20, "20 ms"),
    "gr_spray": (0.35, "35 %"),
    "gr_mix": (0.0, "0 %"),
    "mac_glitch": (0.0, "0 %"),
    "mac_verb": (0.0, "0 %"),
    "mac_fade": (0.0, "0 %"),
    "mac_lpf": (1.0, "100 %"),
    "mac_lo": (1.0, "100 %"),
    "mac_mid": (1.0, "100 %"),
    "mac_hi": (1.0, "100 %"),
    "mac_fill": (0.0, "0 %"),
    "output_gain": (0, "+0 dB"),
    "vu": (51, ""),          # meters are a frame index, not a 0..1 fraction: 51 of 80 VU steps
    "dly_tail": (18, ""),
    "verb_tail": (22, ""),
    "rep_act": (12, ""),
}
READOUTS = {"out_show": ("\u22128.4 dBFS", "e9ecf0"), "dly_show": ("1/8 (375 ms)", "e0a33a"),
            "mix_show": ("62 %", "e0a33a")}
ENUMS = {"eq_on": "On", "limiter": "On", "rep_mode": "1/8", "dly_sync": "Sync", "dly_pingpong": "Off",
         "verb_mode": "Plate", "fill_mode": "Fill", "dly_div": "1/8"}
# the curve mock-up: the isolator at the samples above (LOW X splits low/mid, HI X splits mid/hi)
ISO_LO, ISO_HI, LO_X, HI_X = -6.0, 2.0, 250.0, 2500.0


def frac(key, params):
    p = next((p for p in params if p["key"] == key), None)
    v = SAMPLES.get(key, (0, ""))[0]
    if p is None or "min" not in p or p["max"] == p["min"]:
        return 0.5
    return max(0.0, min(1.0, (v - p["min"]) / float(p["max"] - p["min"])))


def curve_frac(k, n=32):
    """Band k of the mocked response, as a 0..1 filmstrip position."""
    import math
    hz = 20.0 * (10.0 ** (3.0 * k / (n - 1)))
    if hz < LO_X:
        db = ISO_LO
    elif hz > HI_X:
        db = ISO_HI
    else:
        db = 0.0
    return max(0.0, min(1.0, (db - L.DB_MIN) / (L.DB_MAX - L.DB_MIN))), math.isnan(db)


def frame_of(strip, num_frames, f):
    """The filmstrip frame for position f (0..1). Knobs ship one frame more than numFrames."""
    n_img = num_frames + 1 if strip.size[1] == (num_frames + 1) * strip.size[0] else num_frames
    fh = strip.size[1] // n_img
    k = min(n_img - 1, round(f * (n_img - 1)))
    return strip.crop((0, k * fh, strip.size[0], (k + 1) * fh))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skin", default=SKIN)
    ap.add_argument("--pages", default=os.path.join(HERE, "..", "..", "docs", "ui-preview",
                                                   "page_%d.png"))
    ap.add_argument("--out", default=None, help="output pattern (default: <page>_values.png)")
    a = ap.parse_args()
    params = json.load(open(os.path.join(HERE, "..", "params.json")))["params"]
    pkey = [p["key"] for p in params]
    tui = json.load(open(os.path.join(a.skin, "TUI.json")))["pageData"]
    defs = {d["key"]: d["value"] for d in tui["componentDefinitions"]["localComponentDefinitions"]}

    for n, tab in enumerate(tui["tabs"]):
        src = a.pages % n
        out = a.out % n if a.out else src.replace(".png", "_values.png")
        im = Image.open(src).convert("RGB")
        dr = ImageDraw.Draw(im)
        # the page's own baked background: a meter's resting frame in the plain preview would otherwise
        # show through the transparent part of the frame we paste (the curve columns are a known case)
        bgc = next(c for c in defs[tab["componentName"]]["componentsData"]
                   if c["componentData"]["type"] == "Image")
        bg = Image.open(os.path.join(a.skin, bgc["componentData"]["data"]["image"])).convert("RGB")
        for c in defs[tab["componentName"]]["componentsData"]:
            cd = c["componentData"]
            idx = None
            for m in c.get("handle remapping", {}).get("map", []):
                if m.get("key") == "Data":
                    idx = int(str(m["value"]).split()[1]) if m.get("value", "").startswith("Parameter") else None
            x, y, w, h = [int(float(v)) for v in c["bounds"]["bounds"].split()]
            key = pkey[idx] if idx is not None else None
            for s in (defs.get(cd["type"]) or {}).get("componentsData", []):
                sd = s["componentData"]
                sx, sy, sw, sh = [int(float(v)) for v in s["bounds"]["bounds"].split()]
                data = sd.get("data") or {}
                if sd["type"] == "Knob" and key:
                    f = frac(key, params)
                    if key.startswith("eq_"):
                        f = curve_frac(int(key[3:]))[0]
                    im.paste(bg.crop((x + sx, y + sy, x + sx + sw, y + sy + sh)), (x + sx, y + sy))
                    strip = Image.open(os.path.join(a.skin, data["filmStrip"])).convert("RGBA")
                    fr = frame_of(strip, int(data.get("numFrames") or 127), f)
                    im.paste(fr, (x + sx, y + sy), fr)
                elif sd["type"] == "Button" and key and key in ENUMS:
                    opts = next((p.get("options") or [] for p in params if p["key"] == key), [])
                    on = data.get("onImage") if ENUMS[key] in opts and \
                        opts.index(ENUMS[key]) == data.get("buttonId") else data.get("offImage")
                    if on:   # the resting image is already there; the chosen one may need the on face
                        img = Image.open(os.path.join(a.skin, on)).convert("RGBA")
                        im.paste(img, (x + sx, y + sy), img)
                elif sd["type"] == "Label" and data.get("type") == "Value":
                    ts = sd["data"]["textStyle"]
                    if key in READOUTS:
                        text, col = READOUTS[key]
                    elif key in SAMPLES and SAMPLES[key][1]:
                        text, col = SAMPLES[key][1], ts["colour"][2:]
                    else:
                        continue
                    under = im.getpixel((min(x + sx + sw - 4, x + sx + 4),
                                         min(y + sy + sh - 4, y + sy + 4)))
                    dr.rectangle([x + sx, y + sy, x + sx + sw - 1, y + sy + sh - 1], fill=under)
                    fnt = L.font("semi", max(1, round(float(ts["font"]["height"]))))
                    rgb = tuple(int(col[i:i + 2], 16) for i in (0, 2, 4))
                    bx = dr.textbbox((0, 0), text, font=fnt)
                    dr.text((x + sx + (sw - (bx[2] - bx[0])) / 2 - bx[0],
                             y + sy + (sh - (bx[3] - bx[1])) / 2 - bx[1]), text, font=fnt, fill=rgb)
        im.save(out, optimize=True)
        print("wrote", out)


if __name__ == "__main__":
    main()
