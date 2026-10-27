#!/usr/bin/env bash
# Build this port with the upstream mpc-vst-plugins toolchain.
#   vst/build.sh [extra args]
# Set MPC_VST_PLUGINS to the framework checkout if it is not the default sibling.
#
# The Docker daemon is the TrueNAS host and mounts only host-visible paths, so every path handed to
# the framework's tools is translated to the host alias first (the sibling ports do the same).
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


"$(host "$MV")/tools/build_port.sh" "$(host "$HERE")/vst.json" "$@"

# named presets beside the skin build (see gen_presets.py)
MPC_VST_PLUGINS="$MV" python3 "$HERE/gen_presets.py" >/dev/null
