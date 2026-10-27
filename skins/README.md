# Alternative skins

The default skin lives in `vst/` (Classic). These two are the other interfaces from the 3.0.0 release; each
folder holds the layout, the stylesheet and the PIL art generators — no generated PNGs.

To build one:

1. copy this folder's `layout.conf`, `skin.css`, `skin_post.py` and `art/` over `vst/` (keep `vst/params.json`,
   `vst/vst.json` and `vst/src/`),
2. `python3 vst/art/<generator>.py` (Studio: `make_plate.py --page all` + `make_widgets.py`; Rack:
   `make_djfx_skin.py`),
3. `vst/build.sh` and `vst/preview.sh`.

Both were built and rendered that way; see the release assets `-ui-classic.zip`, `-mpc-armv7.zip` (Studio) and
`-ui-pulytek.zip` (Rack).
