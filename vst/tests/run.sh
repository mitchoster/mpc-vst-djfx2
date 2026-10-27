#!/usr/bin/env bash
# Offline x86 unit tests for MPC DJ FX 2 (no device). Uses the host gcc when present, else the
# gcc:12 Docker image (the Docker daemon is the TrueNAS host, so paths are translated first).
# The module tests are self-contained; the macro test links the whole engine and needs the framework
# wrapper (engine.h) and the generated build/params.h - it is skipped until vst/test.sh has run once.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"      # .../vst/tests
VST="$(cd "$HERE/.." && pwd)"              # .../vst
MV="${MPC_VST_PLUGINS:-$(cd "$(dirname "$0")/../../mpc-vst-plugins" 2>/dev/null && pwd || echo "")}"
[ -n "$MV" ] || { echo "set MPC_VST_PLUGINS to the mpc-vst-plugins checkout" >&2; exit 2; }
# Translate a path under the workspace to the prefix the Docker daemon sees: set MPC_VST_WORKSPACE to the
# directory holding this port and the framework, MPC_VST_HOST_ROOT to how the daemon sees it (both empty
# when docker runs on this machine).
WORKSPACE="${MPC_VST_WORKSPACE:-$(cd "$MV/.." 2>/dev/null && pwd)}"
HOST_ROOT="${MPC_VST_HOST_ROOT:-}"
host() { case "$1" in "$WORKSPACE"/*) printf '%s%s' "$HOST_ROOT" "${1#"$WORKSPACE"}" ;; *) printf '%s' "$1" ;; esac; }
CC="${CC:-gcc}"


MODS="iso repeat flanger limiter grain fadeband dubfilter"

if command -v "$CC" >/dev/null 2>&1; then
  echo "== module tests (host $CC) =="
  for t in $MODS; do
    case "$t" in                               # source list per test (some have no own module)
      grain)    mods="$VST/src/grain.c $VST/src/repeat.c" ;;                  # vs. the shipped repeat
      fadeband) mods="$VST/src/iso.c" ;;                                     # moving EQ crossovers
      dubfilter) mods="$VST/src/dub_filter.c" ;;                             # the shared filter engine
      *)        mods="$VST/src/${t}.c" ;;
    esac
    $CC -O2 -std=gnu11 -Wall -Wextra -I"$VST/include" -o "$HERE/${t}_test" "$HERE/${t}_test.c" $mods -lm
    "$HERE/${t}_test"; echo
  done
  if [ -f "$VST/build/params.h" ]; then
    echo "== macro test =="
    $CC -O2 -std=gnu11 -Wall -Wextra -I"$VST/include" -I"$MV/wrapper" -I"$VST/build" \
        -o "$HERE/macro_test" "$HERE/macro_test.c" "$VST"/src/*.c -lm
    "$HERE/macro_test"
    echo "== wet-path high-pass test =="
    $CC -O2 -std=gnu11 -Wall -Wextra -I"$VST/include" -I"$MV/wrapper" -I"$VST/build" \
        -o "$HERE/verbhp_test" "$HERE/verbhp_test.c" "$VST"/src/*.c -lm
    "$HERE/verbhp_test"
    echo "== macro audio test =="
    $CC -O2 -std=gnu11 -Wall -Wextra -I"$VST/include" -I"$MV/wrapper" -I"$VST/build" \
        -o "$HERE/macro_audio_test" "$HERE/macro_audio_test.c" "$VST"/src/*.c -lm
    "$HERE/macro_audio_test"
  else
    echo "(macro_test skipped: run vst/test.sh once to generate build/params.h)"
  fi
else
  echo "== module tests + macro test (docker gcc:12) =="
  docker run --rm -v "$(host "$VST")":/w -v "$(host "$MV")":/mv -w /w gcc:12 sh -c '
    set -e
    for t in '"$MODS"'; do
      case "$t" in
        grain)    mods="src/grain.c src/repeat.c" ;;
        fadeband) mods="src/iso.c" ;;
        dubfilter) mods="src/dub_filter.c" ;;
        *)        mods="src/${t}.c" ;;
      esac
      gcc -O2 -std=gnu11 -Wall -Wextra -Iinclude -o tests/${t}_test tests/${t}_test.c $mods -lm
      ./tests/${t}_test; echo
    done
    if [ -f build/params.h ]; then
      gcc -O2 -std=gnu11 -Wall -Wextra -Iinclude -I/mv/wrapper -Ibuild -o tests/macro_test tests/macro_test.c src/*.c -lm
      ./tests/macro_test
      gcc -O2 -std=gnu11 -Wall -Wextra -Iinclude -I/mv/wrapper -Ibuild -o tests/verbhp_test tests/verbhp_test.c src/*.c -lm
      ./tests/verbhp_test
      echo "== macro audio test =="
      gcc -O2 -std=gnu11 -Wall -Wextra -Iinclude -I/mv/wrapper -Ibuild -o tests/macro_audio_test tests/macro_audio_test.c src/*.c -lm
      ./tests/macro_audio_test
    else
      echo "(macro_test skipped: run vst/test.sh once to generate build/params.h)"
    fi
  '
fi
