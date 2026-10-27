#!/usr/bin/env python3
"""MPC DJ FX 2 skin touch-ups on the generated TUI.json (vst.json "skin_post": gen_vst.py runs this after the
skin is built). Pattern and rationale from MagicStino/pulytek's tools/skin_post.py:

  1. Live text: only Titillium Web and Roboto resolve on the device (docs/NOTES.md). The layout decides every
     SIZE (ns=/vs=/tsize=), so this only sets the family, the weight and the case -- names in SemiBold upper
     case like engraved panel print, values in Bold, exactly the weight a PulyTek value has.
  2. Colours come from the layout's theme_ink / theme_ink_dim / theme_accent lines (names, values, readout
     text); nothing is overridden here, so a control's own ink=/ink_dim= or a readout's tcolor= keeps working.
  3. Switches: the generated toggles focus on tap by default -- make every generated toggle toggle on tap
     (`Mouse Down` -> `Toggle Switch`), which is what a switch should do.

Defensive by design: a missing key skips that touch-up instead of failing the build.
Run: python3 vst/skin_post.py <skin folder>
"""
import json
import sys
from pathlib import Path


def main():
    skin = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    if not skin:
        raise SystemExit("usage: skin_post.py <skin folder>")
    tui = skin / "Plugin Skins" / "TUI.json"
    d = json.loads(tui.read_text())

    named = valued = toggles = 0

    def walk(v):
        nonlocal named, valued
        if isinstance(v, dict):
            if v.get("type") == "Label" and isinstance(v.get("data"), dict):
                data = v["data"]
                tstyle = data.get("textStyle")
                style = (tstyle or {}).get("font")
                if style:
                    if data.get("type") == "Value":
                        style["name"] = "Titillium Web"
                        style["style"] = "Bold"
                        style["height"] = max(int(style.get("height", 26)), 20)
                        tstyle["case"] = "Original"
                        valued += 1
                    elif data.get("type") == "Name":
                        style["name"] = "Titillium Web"
                        style["style"] = "SemiBold"
                        style["height"] = max(int(style.get("height", 20)), 17)
                        tstyle["case"] = "Upper"
                        named += 1
            for child in v.values():
                walk(child)
        elif isinstance(v, list):
            for child in v:
                walk(child)

    walk(d)

    page = d.get("pageData", {})
    defs = page.get("componentDefinitions", {}).get("localComponentDefinitions", [])
    for definition in defs:
        if not str(definition.get("key", "")).startswith("shToggle"):
            continue
        value = definition.get("value", {})
        for action in value.get("actions", []):
            if action.get("onAction") == "Mouse Down":
                action["handler"] = "Toggle Switch"
                toggles += 1

    tui.write_text(json.dumps(d, indent=2) + "\n")
    print("skin_post: %d names + %d values -> Titillium Web, %d toggle handlers -> Toggle Switch"
          % (named, valued, toggles))


if __name__ == "__main__":
    main()
