#pragma once

#include <switch.h>

// libnx 4.x 无 R_ABORT_UNLESS；失败即 svcBreak 断言（NPDM 已授权 svcBreak）
#define R_ABORT_UNLESS(expr) \
    do { \
        Result _qqmusic_rc = (expr); \
        if (R_FAILED(_qqmusic_rc)) \
            svcBreak(_qqmusic_rc, (uintptr_t)0, 0); \
    } while (0)

namespace qqmusic {

    extern const u32 ApiVersion;

    // 状态（M0：占位；M1 起由播放内核驱动）
    u32 GetStatusFlags();
    u64 GetTick();
    float GetVolume();
    void SetVolume(float v);

    bool GetRunning();

    Result InitializeServer();
    Result ExitServer();
    void LoopProcess();

} // namespace qqmusic