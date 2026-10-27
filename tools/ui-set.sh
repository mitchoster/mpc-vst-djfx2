#!/bin/sh
# ui-set.sh — switch MPC DJ FX 2's interface on the device, without reinstalling the plugin.
#
# Run it ON the MPC (root shell):
#
#   sh ui-set.sh <release-zip>        # a MPC-DJ-FX-2-*-ui-*.zip or the default zip
#   sh ui-set.sh <folder>             # a folder holding a "Plugin Skins" directory
#   sh ui-set.sh --list               # show what is saved on the device
#   sh ui-set.sh --use <name>         # switch to a saved interface
#
# The plugin folder is found next to this script, so copy the script into the plugin folder
# ("/sdcard/Synths/MPCDJ - VST - MPC DJ FX 2") or set PLUGIN_DIR.
#
# What it does: saves the current "Plugin Skins" under /data/djfx2-skins/<name>, then replaces
# it. The .so and the parameter set are not touched, so the plugin keeps working across skins.
#
# After the switch: remove the plugin instance on the track and insert it again. MPC keeps the
# skin in memory while an instance is loaded, so a running instance still shows the old one.

set -e

PLUGIN_DIR="${PLUGIN_DIR:-$(cd "$(dirname "$0")" && pwd)}"
STORE="/data/djfx2-skins"
SKINS="$PLUGIN_DIR/Plugin Skins"

die() { echo "ui-set: $*" >&2; exit 2; }

[ -d "$PLUGIN_DIR" ] || die "plugin folder not found: $PLUGIN_DIR"
mkdir -p "$STORE"

case "$1" in
  ""|-h|--help)
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  --list)
    echo "plugin: $PLUGIN_DIR"
    if [ -d "$SKINS" ]; then echo "active: $(cat "$STORE/active" 2>/dev/null || echo unknown)"; else echo "active: (none)"; fi
    echo "saved:"
    for d in "$STORE"/*/; do [ -d "$d" ] && echo "  $(basename "$d")"; done
    exit 0
    ;;
  --use)
    [ -n "$2" ] || die "--use needs a name (see --list)"
    [ -d "$STORE/$2/Plugin Skins" ] || die "no saved interface named '$2'"
    ;;
  *)
    SRC="$1"
    [ -e "$SRC" ] || die "no such file: $SRC"
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    case "$SRC" in
      *.zip)
        unzip -q "$SRC" -d "$WORK"
        FOUND="$(find "$WORK" -type d -name "Plugin Skins" | head -n 1)"
        [ -n "$FOUND" ] || die "no 'Plugin Skins' folder inside $SRC"
        NEW="$(dirname "$FOUND")"
        NAME="$(basename "$(dirname "$FOUND")")"
        ;;
      *)
        [ -d "$SRC/Plugin Skins" ] || die "$SRC does not hold a 'Plugin Skins' folder"
        NEW="$SRC"; NAME="$(basename "$SRC")"
        ;;
    esac
    # tag the saved copy with the new interface's name when the caller did not pass one
    NAME="${2:-$NAME}"
    ;;
esac

if [ "${NEW:-}" = "" ]; then
  NEW="$STORE/$2"
  NAME="$2"
fi

# save the current one before overwriting it
if [ -d "$SKINS" ]; then
  BACKUP="$STORE/$(date +%Y%m%d-%H%M%S)-previous"
  cp -a "$SKINS" "$BACKUP"
  echo "saved the current interface to $BACKUP"
fi

rm -rf "$SKINS"
cp -a "$NEW/Plugin Skins" "$SKINS"
printf '%s\n' "$NAME" > "$STORE/active"
echo "interface '$NAME' is in place: $SKINS"
echo "now remove the plugin instance on the track and insert it again (or restart MPC)."
