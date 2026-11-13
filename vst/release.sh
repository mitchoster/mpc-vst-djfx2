#!/usr/bin/env bash
# MPC DJ FX 2 - build the two interfaces + .so and package the release zip (install.sh / uninstall.sh).
#
#   vst/release.sh
#
# The zip's plugin folder holds ONE skin set: Plugin Skins/{classic,rack}/ plus the flat switch files and
# ui.txt (see vst/make_skinset.sh). install.sh registers the plugin on the device; updates to an already
# registered plugin go through vst/redeploy.sh instead.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"     # .../vst
ROOT="$(cd "$HERE/.." && pwd)"
MV="${MPC_VST_PLUGINS:?set MPC_VST_PLUGINS to the mpc-vst-plugins checkout}"
FOLDER="LarryDaimond - VST - MPC DJ FX 2"
SO="mpc_djfx2.so"
VERSION="3.2.1"
ABOUT="One-slot DJ insert: 3-band isolator, DJ filter, beat repeat, delay, reverb, flanger, grain delay (the original rack's Glitch stage) and a true-peak limiter, driven by the eight macros measured from Lenny Kiser's DJ-FX rack. Two interfaces: Classic and Rack."

echo "== 1/2 build the interface set =="
"$HERE/make_skinset.sh"
SKIN="$ROOT/dist-local/$FOLDER"
python3 "$HERE/gen_presets.py" \
  --out-dir "$SKIN/Plugin Skins/Presets" --so-path "/sdcard/Synths/$FOLDER/$SO" >/dev/null

echo "== 2/2 package =="
python3 "$MV/tools/release.py" \
  --so "$HERE/build/$SO" \
  --skin "$SKIN" \
  --entry "$HERE/build/pluginlist-entry.xml" \
  --version "$VERSION" \
  ${BENCH:+--bench "$BENCH"} \
  --about "$ABOUT" \
  --id mpc-dj-fx-2 \
  --license MIT \
  --repo mitchoster/mpc-vst-djfx2 \
  -o "$HERE/build/dist"
