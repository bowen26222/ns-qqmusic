#!/bin/bash
# 把 streamfin 参考文件装配进 ns-qqmusic 目标布局（M1）
set -euo pipefail
cd /mnt/e/Codes/ns-qqmusic
R=tools/ref
mkdir -p common/sdmc common/pm common/config common/minIni
mkdir -p sysmodule/source/impl/resamplers

cp "$R/sdmc.cpp" "$R/sdmc.hpp" common/sdmc/
cp "$R/pm.cpp" "$R/pm.hpp" common/pm/
cp "$R/config.cpp" "$R/config.hpp" common/config/
cp "$R/minIni.c" "$R/minIni.h" "$R/minGlue.c" "$R/minGlue.h" common/minIni/

cp "$R/source.cpp" sysmodule/source/impl/source.cpp
cp "$R/source.hpp" sysmodule/source/impl/source.hpp
cp "$R/music_player.cpp" sysmodule/source/impl/player.cpp
cp "$R/music_player.hpp" sysmodule/source/impl/player.hpp
cp "$R/aud_wrapper.h" sysmodule/source/impl/aud_wrapper.h
cp "$R/SDL_audioEX.c" "$R/SDL_audioEX.h" sysmodule/source/impl/resamplers/
cp "$R/dr_flac.h" "$R/dr_mp3.h" "$R/dr_wav.h" sysmodule/source/impl/
cp "$R/tune_result.hpp" sysmodule/source/result.hpp
cp "$R/tune_types.hpp" sysmodule/source/types.hpp

# audout.c 从 streamfin 拉一份
curl -fsSL https://raw.githubusercontent.com/dammitjeff/streamfin-switch/master/sys-tune/source/impl/audout.c -o sysmodule/source/impl/audout.c
curl -fsSL https://raw.githubusercontent.com/dammitjeff/streamfin-switch/master/sys-tune/source/impl/audout.h -o sysmodule/source/impl/audout.h

# 全局改名: tune -> qqmusic
find common/sdmc common/pm common/config common/minIni sysmodule/source/impl sysmodule/source -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.c' -o -name '*.h' \) -print0 | xargs -0 sed -i \
  -e 's/namespace tune::impl/namespace qqmusic::impl/g' \
  -e 's/namespace tune {/namespace qqmusic {/g' \
  -e 's/\btune::impl\b/qqmusic::impl/g' \
  -e 's/\btune::/qqmusic::/g' \
  -e 's/tune_result\.hpp/result.hpp/g' \
  -e 's/tune_types\.hpp/types.hpp/g' \
  -e 's|/config/streamfin|/config/qqmusic|g'

# 验证残留
grep -rn 'tune' common/sdmc common/pm common/config sysmodule/source --include='*.hpp' --include='*.cpp' -l || echo CLEAN