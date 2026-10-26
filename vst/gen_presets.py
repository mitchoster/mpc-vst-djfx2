#!/usr/bin/env python3
"""Ship named MPC presets (.xpl) + the VST-program list for MPC DJ FX 2.

Two routes, same names:
  * `.xpl` files in MPC's effect-preset folder (`MPC Documents/Plugin Presets/Insert Effects/
    <vendor> - VST - <name>/`), read with the folder icon next to the PRESET field (the skin's own
    `Presets/` is the instruments path and does nothing for an effect);
  * `presets.json` next to vst.json (vst.json `"presets"`), which gen_vst compiles into params.h so
    MPC's PRESET field lists the same names.

The engine state is the same `key=value\n` line set DubStation uses, so a preset is a partial override
of vst/params.json; every other parameter keeps its default.  Values are validated against params.json
(an option may be given by label or index).

    vst/gen_presets.py                                   # into the built skin's Presets/ + presets.json
    vst/gen_presets.py --out-dir <dir> --so-path <abs>   # into a device effect-preset folder
"""
import argparse
import importlib.util
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))          # .../vst
MV = os.environ.get("MPC_VST_PLUGINS", "/workspace/mpc-vst-plugins")

# name -> parameter overrides (everything else stays at params.json's default)
PRESETS = [
    ("Init", {}),
    ("DJ Classic", {            # the everyday macro setting
        "mac_lpf": 0.30, "mac_verb": 0.35, "mac_fade": 0.25,
        "mac_lo": 0.85, "mac_mid": 0.80, "mac_hi": 0.75,
    }),
    ("Filter Ride", {"mac_lpf": 0.55}),          # the LPF macro alone
    ("Stutter Fill", {"mac_fill": 1.00}),        # the Auto-Fill macro at the top
    ("Grain Wash", {             # v2: the Glitch macro = the grain delay (rate 150 Hz, time 2 ms)
        "mac_glitch": 0.70, "gr_spray": 0.60, "gr_time": 6.0, "mac_verb": 0.30,
    }),
    ("Glitch Madness", {"mac_glitch": 1.00, "mac_fill": 0.80}),
    ("Echo Throw", {"mac_fade": 1.00}),          # v2: Fade to Grey = the echo throw plus the mid dip
    ("Dub Wash", {"mac_verb": 1.00, "mac_fade": 0.45}),
    ("Siren Dub", {"mac_mid": 0.00, "mac_hi": 0.00}),   # kill the mid + high bands
    ("Telephone", {              # the sound v1's Fade to Grey wrongly produced - now a manual choice
        "iso_lo": -24.0, "iso_hi": -24.0, "iso_lo_x": 250.0, "iso_hi_x": 2500.0, "iso_mid": 0.0,
        "mac_fade": 0.0,
    }),
    ("Flanger Sweep", {          # the flanger's own controls (manual since v2: the Glitch macro owns
        "fl_rate": 0.25, "fl_depth": 0.60, "fl_fbk": 0.60, "fl_mix": 0.50,   # the grain now)
    }),
]


def load_xpl():
    path = os.path.join(MV, "tools", "xpl.py")
    spec = importlib.util.spec_from_file_location("xpl", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def numstr(v):
    return "%g" % v if isinstance(v, float) else str(v)


def resolve(p, raw):
    """params.json default/override -> the engine's state string for one key."""
    key = p["key"]
    opts = p.get("options")
    if opts:
        if isinstance(raw, str) and raw in opts:
            return str(opts.index(raw))
        try:
            i = int(raw)
        except (TypeError, ValueError):
            raise SystemExit("preset %s: %r is not an option of %s (=%s)" % (key, raw, key, opts))
        if not 0 <= i < len(opts):
            raise SystemExit("preset %s: option %d out of range 0..%d" % (key, i, len(opts) - 1))
        return str(i)
    lo, hi = p.get("min"), p.get("max")
    if not isinstance(raw, (int, float)):
        raise SystemExit("preset %s: %r is not a number" % (key, raw))
    if lo is not None and raw < lo:
        raise SystemExit("preset %s: %s below min %s" % (key, raw, lo))
    if hi is not None and raw > hi:
        raise SystemExit("preset %s: %s above max %s" % (key, raw, hi))
    return numstr(raw)


def state_string(plist, overrides):
    unknown = set(overrides) - {p["key"] for p in plist}
    if unknown:
        raise SystemExit("preset uses unknown parameter(s): %s" % ", ".join(sorted(unknown)))
    return "".join("%s=%s\n" % (p["key"], resolve(p, overrides.get(p["key"], p.get("default"))))
                   for p in plist)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out-dir", help="write the .xpl files here instead of the built skin's Presets/. "
                                      "Use the device effect-preset folder for an insert effect.")
    ap.add_argument("--so-path", help="the <PLUGIN file=...> value; default '%%payload-path%%/<skin>/<so>'.")
    a = ap.parse_args()

    cfg = json.load(open(os.path.join(HERE, "vst.json"), encoding="utf-8"))
    plist = json.load(open(os.path.join(HERE, cfg.get("params", "params.json")), encoding="utf-8"))["params"]

    skin_name = "%s - VST - %s" % (cfg["vendor"], cfg["name"])
    so_path = a.so_path or ("%%payload-path%%/%s/%s" % (skin_name, cfg["so"]))
    uid = int.from_bytes(cfg["uid"].encode(), "big")

    if a.out_dir:
        presets_dir = a.out_dir
        fname = lambda i, name: "%s.xpl" % name                        # noqa: E731
    else:
        skin = os.path.join(HERE, "build", "skin", skin_name)
        if not os.path.isdir(skin):
            raise SystemExit("no built skin at %s - run vst/preview.sh or vst/build.sh first" % skin)
        presets_dir = os.path.join(skin, "Presets")
        fname = lambda i, name: "%04d-%s.xpl" % (i, name)              # noqa: E731
    os.makedirs(presets_dir, exist_ok=True)

    xpl = load_xpl()
    for i, (name, ov) in enumerate(PRESETS, start=1):
        chunk = state_string(plist, ov).encode("utf-8") + b"\0"
        text = xpl.xpl(cfg["name"], cfg["vendor"], so_path, uid,
                       cfg.get("version", 1000), chunk, preset=name, effect=bool(cfg.get("effect")))
        dst = os.path.join(presets_dir, fname(i, name))
        with open(dst, "w", encoding="utf-8") as f:
            f.write(text)
        print("preset:", dst)

    # the VST-program list (vst.json "presets") so MPC's PRESET field shows the same names.
    # gen_vst needs at least one value per program, so an override-free preset (Init) is skipped here;
    # its .xpl still ships (an all-defaults state) and the module's own defaults are program 0.
    programs = [{"name": name, "values": dict(ov)} for name, ov in PRESETS if ov]
    out_json = os.path.join(HERE, "presets.json")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump({"presets": programs}, f, indent=2)
        f.write("\n")
    print("programs:", out_json)


if __name__ == "__main__":
    main()
