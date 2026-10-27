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

## Switching the interface on the device

`tools/ui-set.sh` swaps `Plugin Skins` in place (it saves the current one under `/data/djfx2-skins/` first):

```sh
scp tools/ui-set.sh "root@<device>:/sdcard/Synths/MPCDJ - VST - MPC DJ FX 2/"
ssh root@<device> 'cd "/sdcard/Synths/MPCDJ - VST - MPC DJ FX 2" && ./ui-set.sh /tmp/MPC-DJ-FX-2-3.0.1-ui-pulytek.zip'
```

An interface can only change while no instance holds it, so the script ends with the step that matters:
remove the plugin instance on the track and insert it again. `./ui-set.sh --list` shows what is saved,
`--use <name>` switches back. Installing another release zip with `install.sh` does the same thing.
