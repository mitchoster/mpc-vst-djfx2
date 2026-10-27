#!/usr/bin/env bash
# Does the MPC run the build that is on disk? (READ-ONLY - no device writes, no UI)
#   vst/device_loaded.sh [device-ip]
#
# A loaded plugin instance keeps the .so file it was loaded from. After a deploy the process still maps
# the OLD inode, which the kernel marks "(deleted)" - and our instance only picks up a new .so when it is
# removed and re-inserted (see the mpc-vst-plugin skill). So this compares the md5/inode on disk with the
# inode the MPC process actually has mapped: they differ exactly when the running code is stale.
# Run it BEFORE judging any change by ear on the device.
set -euo pipefail

DEVICE="${1:-${DEVICE:?set DEVICE to the MPC IP address (or pass it as the first argument)}}"
SO="/sdcard/Synths/MPCDJ - VST - MPC DJ FX 2/mpc_djfx2.so"

ssh -o BatchMode=yes -o StrictHostKeyChecking=no "root@$DEVICE" '
SO="'"$SO"'"
echo -n "disk    : "; md5sum "$SO" | cut -d" " -f1
echo -n "          inode "; ls -i "$SO" | awk "{print \$1}"
line=""
for d in /proc/[0-9]*; do
  [ -r "$d/maps" ] || continue
  l=$(grep -m1 mpc_djfx2 "$d/maps" 2>/dev/null) || continue
  [ -n "$l" ] && line="$l"
done
if [ -z "$line" ]; then
  echo "loaded  : (no MPC DJ FX 2 instance in memory)"
  exit 0
fi
inode=$(echo "$line" | awk "{print \$5}")
case "$line" in
  *"(deleted)"*) echo "loaded  : inode $inode   <-- STALE: remove EVERY instance of the plugin (all tracks), then insert it again" ;;
  *)             echo "loaded  : inode $inode   ok (the running build is the one on disk)" ;;
esac
'