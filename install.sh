#!/usr/bin/env bash
# Install the DROPLET app into a checkout of https://github.com/m5stack/StackChan (firmware v1.5.1, commit 1b57655).
# Usage: ./install.sh /path/to/StackChan
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DST="${1:?usage: install.sh /path/to/StackChan}"
FW="$DST/firmware"
[ -d "$FW/main/apps" ] || { echo "not a StackChan checkout: $DST"; exit 1; }
mkdir -p "$FW/main/apps/app_droplet"
cp "$HERE"/main/apps/app_droplet/*.{h,cpp} "$FW/main/apps/app_droplet/"
if git -C "$DST" apply --check "$HERE/patches/wiring.patch" 2>/dev/null; then
  git -C "$DST" apply "$HERE/patches/wiring.patch"
  echo "wiring patch applied"
else
  echo "wiring patch did not apply cleanly (already applied, or firmware version differs). See README for the 4 manual edits."
fi
touch "$FW/main/CMakeLists.txt"   # re-evaluate the source GLOB so the new files are picked up
echo "done. Next: cd $FW && idf.py build && idf.py -p <port> app-flash   (never erase-flash)"
