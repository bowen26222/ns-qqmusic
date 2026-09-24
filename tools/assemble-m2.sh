#!/bin/bash
# M2: 装配 streamfin GUI 到 overlay（gui/elm/core）
set -euo pipefail
cd /mnt/e/Codes/ns-qqmusic
R=tools/ref2
mkdir -p overlay/source/gui overlay/source/elm overlay/source/core

cp "$R/gui_main.cpp" "$R/gui_main.hpp" "$R/gui_browser.cpp" "$R/gui_browser.hpp" \
   "$R/gui_playlist.cpp" "$R/gui_playlist.hpp" "$R/gui_keyboard.cpp" "$R/gui_keyboard.hpp" \
   "$R/gui_error.cpp" "$R/gui_error.hpp" overlay/source/gui/
cp "$R/elm_overlayframe.hpp" "$R/elm_volume.hpp" overlay/source/elm/
cp "$R/ovl_log.cpp" "$R/ovl_log.hpp" overlay/source/core/

# 机械改名
find overlay/source/gui overlay/source/elm overlay/source/core -type f -print0 | xargs -0 sed -i \
  -e 's/StreamfinOverlayFrame/QqMusicOverlayFrame/g' \
  -e 's/drawString("Streamfin"/drawString("QQMusic"/g' \
  -e 's/#include "tune.h"/#include "client.h"/g' \
  -e 's/TUNE_API_VERSION/QQMUSIC_API_VERSION/g' \
  -e 's/tuneInitialize/qqmusicInitialize/g' \
  -e 's/tuneExit/qqmusicExit/g' \
  -e 's/tuneGetStatus/qqmusicGetStatus/g' \
  -e 's/tunePlay/qqmusicPlay/g' \
  -e 's/tunePause/qqmusicPause/g' \
  -e 's/tuneNext/qqmusicNext/g' \
  -e 's/tunePrev/qqmusicPrev/g' \
  -e 's/tuneSeek/qqmusicSeek/g' \
  -e 's/tuneSelect/qqmusicSelect/g' \
  -e 's/tuneEnqueue/qqmusicEnqueue/g' \
  -e 's/tuneRemove/qqmusicRemove/g' \
  -e 's/tuneClearQueue/qqmusicClearQueue/g' \
  -e 's/tuneGetPlaylistSize/qqmusicGetQueueSize/g' \
  -e 's/tuneGetPlaylistItem/qqmusicGetQueueItem/g' \
  -e 's/tuneGetVolume/qqmusicGetVolume/g' \
  -e 's/tuneSetVolume/qqmusicSetVolume/g' \
  -e 's/tuneGetTitleVolume/qqmusicGetTitleVolume/g' \
  -e 's/tuneSetTitleVolume/qqmusicSetTitleVolume/g' \
  -e 's/tuneGetDefaultTitleVolume/qqmusicGetDefaultTitleVolume/g' \
  -e 's/tuneSetDefaultTitleVolume/qqmusicSetDefaultTitleVolume/g' \
  -e 's/tuneQuit/qqmusicQuit/g' \
  -e 's/tuneGetApiVersion/qqmusicGetApiVersion/g' \
  -e 's/TuneEnqueueType_Back/0/g' \
  -e 's/TuneEnqueueType_Front/1/g'
grep -rn 'tune[A-Z]\|Tune[A-Z]' overlay/source/gui overlay/source/elm overlay/source/core | head -20