#!/usr/bin/env bash
# Build this port with the upstream mpc-vst-plugins toolchain.
#   vst/build.sh [extra args]
# Set MPC_VST_PLUGINS to the framework checkout if it is not the default sibling.
#
# The Docker daemon is the TrueNAS host and mounts only host-visible paths, so every path handed to
# the framework's tools is translated to the host alias first (the sibling ports do the same).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MV="${MPC_VST_PLUGINS:-/workspace/mpc-vst-plugins}"

host() {
  case "$1" in
    /workspace/*)                         echo "/mnt/SSD/Container/codex-agent${1#/workspace}" ;;
    /truenas/SSD/Container/codex-agent/*) echo "/mnt/SSD/Container/codex-agent${1#/truenas/SSD/Container/codex-agent}" ;;
    *)                                    echo "$1" ;;
  esac
}

"$(host "$MV")/tools/build_port.sh" "$(host "$HERE")/vst.json" "$@"

# named presets beside the skin build (see gen_presets.py)
MPC_VST_PLUGINS="$MV" python3 "$HERE/gen_presets.py" >/dev/null
