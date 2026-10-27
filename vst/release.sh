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
# every path handed to the framework's tools is mapped to /mnt/SSD/Container/... .
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"     # .../vst
MV="${MPC_VST_PLUGINS:-/workspace/mpc-vst-plugins}"
FOLDER="MPCDJ - VST - MPC DJ FX 2"
SO="mpc_djfx2.so"
VERSION="2.1.1"
ABOUT="One-slot DJ insert: 3-band isolator, DJ filter, beat repeat, delay, reverb, flanger, grain delay (the original rack's Glitch stage) and a true-peak limiter, driven by the eight macros measured from Lenny Kiser's DJ-FX rack."

host() {  # in-container path -> host-visible path (for the Docker daemon)
  case "$1" in
    /workspace/*)                         echo "/mnt/SSD/Container/codex-agent${1#/workspace}" ;;
    /truenas/SSD/Container/codex-agent/*) echo "/mnt/SSD/Container/codex-agent${1#/truenas/SSD/Container/codex-agent}" ;;
    *)                                    echo "$1" ;;
  esac
}

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
