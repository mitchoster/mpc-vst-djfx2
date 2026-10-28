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
scp tools/ui-set.sh "root@<device>:/sdcard/Synths/LarryDaimond - VST - MPC DJ FX 2/"
ssh root@<device> 'cd "/sdcard/Synths/LarryDaimond - VST - MPC DJ FX 2" && ./ui-set.sh /tmp/MPC-DJ-FX-2-3.0.1-ui-pulytek.zip'
```

An interface can only change while no instance holds it, so the script ends with the step that matters:
remove the plugin instance on the track and insert it again. `./ui-set.sh --list` shows what is saved,
`--use <name>` switches back. Installing another release zip with `install.sh` does the same thing.

## Adding a fourth interface

1. Copy `vst/` (or one of the `skins/*` folders) into a scratch lab, keep `vst.json`, `params.json`/the module
   JSON and `vst/src/**` untouched, and change only `layout.conf`, `skin.css`, `skin_post.py` and the art
   generators.
2. Build it: `python3 tools/gen_vst.py <lab>/vst/vst.json`, then `studio.py preview` and look at every page.
3. Extend the switch: add `<name>` to the engine's `SKIN_DIRS`/`SKIN_TXT` list and to the `ui` options in the
   parameter source, then rebuild the plugin.
4. Merge: `python3 tools/make_skinset.py -o "<portable>/<folder>/Plugin Skins" studio=… classic=… rack=… <name>=…
   --active studio` (tools/make_skinset.py, also in sd88me/mpc-vst-plugins).
5. Package with `vst/release.sh` / `tools/release.py`, check with `catalog_check.py <zip> --catalog`, and test on
   the device: tap the control, `cmp` `Plugin Skins/TUI.json` against `Plugin Skins/<name>/TUI.json`, then
   remove+insert the instance and look.

Cost to expect: a new interface is layout + art work only — the switch, the packaging and the release flow are
already there.
