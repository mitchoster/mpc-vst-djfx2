#!/usr/bin/env bash
# Build both interfaces into ONE skin folder, ready to deploy or package.
#
#   vst/make_skinset.sh                 -> dist-local/<folder>/
#   ACTIVE=rack vst/make_skinset.sh     -> Rack is the interface MPC shows first
#
# Classic is this build (vst/layout.conf + art/ + skin.css), so a layout edit always lands in the set.
# Rack is built from its own source in skins/rack/ in a scratch lab (.build-rack/, gitignored), because
# the two interfaces have different layouts, art generators and stylesheets. tools/make_skinset.py then
# merges both: every art reference is rewritten into its interface subfolder and the flat switch files
# (TUI.json, the Q-Link banks, ui.txt) are written - those are what MPC reads at insert. Only JSON moves
# when the user switches interfaces; no image is copied.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"     # .../vst
ROOT="$(cd "$HERE/.." && pwd)"            # repo root
MV="${MPC_VST_PLUGINS:?set MPC_VST_PLUGINS to the mpc-vst-plugins checkout}"
FOLDER="LarryDaimond - VST - MPC DJ FX 2"
SO="mpc_djfx2.so"
ACTIVE="${ACTIVE:-classic}"

echo "== 1/3 classic: build vst/ =="
"$MV/tools/build_port.sh" "$HERE/vst.json" >/dev/null
CLASSIC="$HERE/build/skin/$FOLDER/Plugin Skins"
[ -d "$CLASSIC" ] || { echo "no built skin at $CLASSIC" >&2; exit 1; }

echo "== 2/3 rack: build skins/rack in a lab =="
LAB="$ROOT/.build-rack"
rm -rf "$LAB"; mkdir -p "$LAB"
cp -a "$HERE/." "$LAB/"
rm -rf "$LAB/build"
cp -a "$ROOT/skins/rack/layout.conf" "$ROOT/skins/rack/skin.css" "$ROOT/skins/rack/skin_post.py" "$LAB/"
mkdir -p "$LAB/art"
cp -a "$ROOT/skins/rack/art/." "$LAB/art/"
# The interface's own art generator (it reads the layout it sits next to).
docker run --rm -e HOME=/tmp -v "$LAB":/w -v "$MV":/mv:ro -w /w mpc-vst-html-art \
  python3 art/make_djfx_skin.py >/dev/null
docker run --rm -e HOME=/tmp -v "$LAB":/w -v "$MV":/mv:ro -w /w mpc-vst-html-art \
  python3 /mv/tools/gen_vst.py vst.json >/dev/null
RACK="$LAB/build/skin/$FOLDER/Plugin Skins"
[ -d "$RACK" ] || { echo "no rack skin at $RACK" >&2; exit 1; }

echo "== 3/3 merge =="
STAGE="$ROOT/dist-local/$FOLDER"
rm -rf "$STAGE"; mkdir -p "$STAGE"
cp -a "$HERE/build/skin/$FOLDER/." "$STAGE/"
python3 "$ROOT/tools/make_skinset.py" -o "$STAGE/Plugin Skins" \
  classic="$CLASSIC" rack="$RACK" --active "$ACTIVE"
# The flat TUI points into its interface subfolder; the build's own top-level art copies are dead weight.
rm -f "$STAGE/Plugin Skins"/*.png
cp "$HERE/build/$SO" "$STAGE/$SO"
ls -1 "$STAGE/Plugin Skins"
