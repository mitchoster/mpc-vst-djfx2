#!/usr/bin/env bash
# Offline A/B over the macro rides: render our engine on the dry reference with the same macro ride the
# original got, then print both transfer curves.
#
#   vst/tests/ab_run.sh <input-dir>        # input-dir holds dry_ref.wav + ride_*.json + <macro>.wav
#
# The original recordings come from Ableton: one track records the master (Resampling) while a script
# steps one macro from its neutral value to the far end and back, one bar per step; every step's wall
# clock time is in the ride JSON. The dry reference is the same loop with every macro at neutral.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"     # .../vst/tests
VST="$(cd "$HERE/.." && pwd)"             # .../vst
MV="${MPC_VST_PLUGINS:-/workspace/mpc-vst-plugins}"
IN="${1:?usage: ab_run.sh <input-dir>}"
WORK="$VST/build/ab"
mkdir -p "$WORK"

host() { case "$1" in /workspace/*) echo "/mnt/SSD/Container/codex-agent${1#/workspace}" ;; *) echo "$1" ;; esac }

cp -f "$IN/dry_ref.wav" "$WORK/"
for f in "$IN"/ride_*.json; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .json)"; macro="${base#ride_}"
  cp -f "$f" "$WORK/"
  cp -f "$IN/$macro.wav" "$WORK/" 2>/dev/null || true
  # ride csv: seconds from the first step, then the Ableton macro value
  python3 - "$f" "$WORK/$base.csv" <<'PY'
import json, sys
ride = json.load(open(sys.argv[1]))
t0 = ride["log"][0]["wall"]
with open(sys.argv[2], "w") as fh:
    for e in ride["log"]:
        fh.write("%.4f,%d\n" % (e["wall"] - t0, e["value"]))
PY
done

# build the renderer once (arm/x86 host build via the framework's docker pattern)
docker run --rm -v "$(host "$VST")":/w -v "$(host "$MV")":/mv -w /w gcc:12 \
  sh -c 'gcc -O2 -std=gnu11 -Iinclude -Ibuild -I/mv/wrapper -o build/render tests/render.c src/*.c -lm'

KEY_lo=mac_lo; KEY_mid=mac_mid; KEY_hi=mac_hi; KEY_lpf=mac_lpf
KEY_glitch=mac_glitch; KEY_verb=mac_verb; KEY_fade=mac_fade; KEY_fill=mac_fill

for f in "$WORK"/ride_*.json; do
  base="$(basename "$f" .json)"; macro="${base#ride_}"
  case "$macro" in
    LOW-EQ) key=$KEY_lo ;; MID-EQ) key=$KEY_mid ;; HI-EQ) key=$KEY_hi ;; LPF) key=$KEY_lpf ;;
    Glitch) key=$KEY_glitch ;; VERB) key=$KEY_verb ;; Fade_to_Grey) key=$KEY_fade ;;
    Auto-Fill) key=$KEY_fill ;; *) echo "skip $macro" >&2; continue ;;
  esac
  echo "== render $macro ($key)"
  docker run --rm -v "$(host "$VST")":/w -w /w gcc:12 \
    sh -c "build/render build/ab/dry_ref.wav build/ab/ours_$macro.wav $key build/ab/$base.csv"
done

echo
for f in "$WORK"/ride_*.json; do
  base="$(basename "$f" .json)"; macro="${base#ride_}"
  uv run --quiet --with numpy python3 "$HERE/ab_analyze.py" "$f" --dry "$WORK/dry_ref.wav" --orig "$WORK/$macro.wav" --ours "$WORK/ours_$macro.wav"
  echo
done
