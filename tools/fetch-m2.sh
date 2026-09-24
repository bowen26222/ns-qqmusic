#!/bin/bash
# 下载 streamfin overlay GUI（M2 移植素材）
set -euo pipefail
cd /mnt/e/Codes/ns-qqmusic
R=tools/ref2
rm -rf "$R" && mkdir -p "$R/gui" "$R/elm" "$R/core"
B=https://raw.githubusercontent.com/dammitjeff/streamfin-switch/master/overlay/source
f() { curl -fsSL "$B/$1" -o "$R/$(basename "$1")"; }
f main.cpp
for g in gui_browser gui_error gui_keyboard gui_main gui_picker gui_playlist gui_settings; do
  f "gui/$g.cpp"
  f "gui/$g.hpp"
done
for e in elm_overlayframe elm_status_bar elm_volume; do
  if [ "$e" = "elm_volume" ]; then f "elm/$e.hpp"; else f "elm/$e.cpp"; f "elm/$e.hpp"; fi
done
f core/ovl_log.cpp
f core/ovl_log.hpp
ls -la "$R" "$R/gui" "$R/elm" "$R/core" | head -30