#!/usr/bin/env bash
# Offline x86 host test (ASan/UBSan) for this port; must print PASSED. No device needed.
# Uses the host gcc/g++ when present, else a Docker image. The Docker daemon runs on the host and
# mounts only host-visible paths, so translate workspace paths to the host alias first.
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

"$(host "$MV")/tools/test_port.sh" "$(host "$HERE")/vst.json" "$@"
