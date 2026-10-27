#!/usr/bin/env python3
"""MPC DJ FX 2 skin touch-ups on the generated TUI.json (vst.json "skin_post": gen_vst.py runs this
after the skin is built).

  1. Live text hierarchy: MPC draws every control's name and value itself, in the only two families that
     resolve on the device (Titillium Web, Roboto). The baked captions are Titillium, so the names switch
     to Titillium SemiBold upper-case and the values to Titillium SemiBold in a brighter ink -- quiet
     names, loud numbers, which is what the card layout is built around. Heights come from the layout
     (knob `ns=`/`vs=`), so nothing is resized here.
  2. Read-outs keep what the layout gave them (`tsize=`/`tcolor=`/`tweight=`), so the panel's dBFS and
     delay-time numbers stay larger and accented.
  3. Toggles: any generated pill toggle gets "Mouse Down -> Toggle Switch" instead of focus-only, so a
     tap flips it.

Colours only ever touch the knob-style components, never a readout. Defensive by design: a missing key
skips that touch-up instead of failing the build.
Run: python3 vst/skin_post.py <skin folder>
"""
import json
import sys
from pathlib import Path

NAMES = "ff8f98a2"      # quiet grey: control names
VALUES = "ffe9ecf0"     # bright ink: the live numbers
FAMILY = "Titillium Web"
KNOB_PREFIX = ("shKnob", "shSlider", "shToggle", "shRow", "shPadTint")


def main():
    skin = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if not skin:
        raise SystemExit("usage: skin_post.py <skin folder>")
    tui = skin / "Plugin Skins" / "TUI.json"
    d = json.loads(tui.read_text())

    fonts = 0
    defs = d.get("pageData", {}).get("componentDefinitions", {}).get("localComponentDefinitions", [])
    for definition in defs:
        if not str(definition.get("key", "")).startswith(KNOB_PREFIX):
            continue
        for sub in definition.get("value", {}).get("componentsData", []):
            data = (sub.get("componentData") or {}).get("data") or {}
            if (sub.get("componentData") or {}).get("type") != "Label" or not isinstance(data.get("textStyle"), dict):
                continue
            style = data["textStyle"]
            kind = data.get("type")
            if kind not in ("Name", "Value"):
                continue
            font = style.get("font")
            if not isinstance(font, dict):
                continue
            font["name"] = FAMILY
            font["style"] = "SemiBold"
            style["colour"] = NAMES if kind == "Name" else VALUES
            if kind == "Name":
                style["case"] = "Upper"
            fonts += 1

    toggles = 0
    for definition in defs:
        if not str(definition.get("key", "")).startswith("shToggle"):
            continue
        for action in definition.get("value", {}).get("actions", []):
            if action.get("onAction") == "Mouse Down":
                action["handler"] = "Toggle Switch"
                toggles += 1

    tui.write_text(json.dumps(d, indent=2) + "\n")
    print("skin_post: %d live text styles -> Titillium, %d toggle handlers -> Toggle Switch" % (fonts, toggles))


if __name__ == "__main__":
    main()
