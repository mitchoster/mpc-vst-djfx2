#!/usr/bin/env bash
# Offline x86 host test (ASan/UBSan) for this port; must print PASSED. No device needed.
# Uses the host gcc/g++ when present, else a Docker image. The Docker daemon runs on the host and
# mounts only host-visible paths, so translate workspace paths to the host alias first.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST_PLUGINS:-$(cd "$(dirname "$0")/../../mpc-vst-plugins" 2>/dev/null && pwd || echo "")}"
[ -n "$MV" ] || { echo "set MPC_VST_PLUGINS to the mpc-vst-plugins checkout" >&2; exit 2; }
# Translate a path under the workspace to the prefix the Docker daemon sees: set MPC_VST_WORKSPACE to the
# directory holding this port and the framework, MPC_VST_HOST_ROOT to how the daemon sees it (both empty
# when docker runs on this machine).
WORKSPACE="${MPC_VST_WORKSPACE:-$(cd "$MV/.." 2>/dev/null && pwd)}"
HOST_ROOT="${MPC_VST_HOST_ROOT:-}"
host() { case "$1" in "$WORKSPACE"/*) printf '%s%s' "$HOST_ROOT" "${1#"$WORKSPACE"}" ;; *) printf '%s' "$1" ;; esac; }


MPC_VST_PLUGINS="$(host "$MV")" "$(host "$MV")/tools/test_port.sh" "$(host "$HERE")/vst.json" "$@"

echo "== companion feed (HTTP /state + the page) =="
MPC_VST_PLUGINS="$MV" "$HERE/tests/companion.sh"

echo "== companion lifetime across dlclose (the 2026-11-01 device crash) =="
"$HERE/tests/dlclose.sh"

echo "== companion page logic (headless) =="
"$HERE/tests/page.sh"
