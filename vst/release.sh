#!/usr/bin/env bash
# MPC DJ FX 2 - rebuild the skin + .so and package the release zip (with install.sh / uninstall.sh).
#
#   vst/release.sh
#
# The zip unpacks to a folder with install.sh, which on the device stops MPC, backs up MPC.settings,
# adds the plugin-list entry and restarts MPC. That is the first-install/registration path; updates to an
# already-registered plugin go through vst/redeploy.sh instead.
#
# Environment: the Docker daemon here is the TrueNAS host daemon and mounts only host-visible paths, so
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"     # .../vst
MV="${MPC_VST_PLUGINS:-$(cd "$(dirname "$0")/../../mpc-vst-plugins" 2>/dev/null && pwd || echo "")}"
[ -n "$MV" ] || { echo "set MPC_VST_PLUGINS to the mpc-vst-plugins checkout" >&2; exit 2; }
# Translate a path under the workspace to the prefix the Docker daemon sees: set MPC_VST_WORKSPACE to the
# directory holding this port and the framework, MPC_VST_HOST_ROOT to how the daemon sees it (both empty
# when docker runs on this machine).
WORKSPACE="${MPC_VST_WORKSPACE:-$(cd "$MV/.." 2>/dev/null && pwd)}"
HOST_ROOT="${MPC_VST_HOST_ROOT:-}"
host() { case "$1" in "$WORKSPACE"/*) printf '%s%s' "$HOST_ROOT" "${1#"$WORKSPACE"}" ;; *) printf '%s' "$1" ;; esac; }
FOLDER="LarryDaimond - VST - MPC DJ FX 2"
SO="mpc_djfx2.so"
VERSION="3.1.2"
ABOUT="One-slot DJ insert: 3-band isolator, DJ filter, beat repeat, delay, reverb, flanger, grain delay (the original rack's Glitch stage) and a true-peak limiter, driven by the eight macros measured from Lenny Kiser's DJ-FX rack."


echo "== 1/2 build skin + .so =="
"$(host "$MV")/tools/build_port.sh" "$(host "$HERE")/vst.json"
MPC_VST_PLUGINS="$MV" python3 "$HERE/gen_presets.py"

SKIN="$HERE/build/skin/$FOLDER"
[ -d "$SKIN" ] || { echo "skin folder not found: $SKIN" >&2; exit 1; }
cp "$HERE/build/$SO" "$SKIN/$SO"

echo "== 2/2 package =="
MPC_VST_PLUGINS="$MV" python3 "$MV/tools/release.py" \
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
