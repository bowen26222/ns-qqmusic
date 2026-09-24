#!/bin/bash
set -euo pipefail
cd /mnt/e/Codes/ns-qqmusic/tools/ref2
B=https://raw.githubusercontent.com/dammitjeff/streamfin-switch/master/overlay/source
for f in elm/elm_overlayframe.hpp elm/elm_status_bar.cpp elm/elm_status_bar.hpp elm/elm_volume.hpp core/ovl_log.cpp core/ovl_log.hpp; do
  curl -fsSL "$B/$f" -o "$(basename "$f")"
done
ls