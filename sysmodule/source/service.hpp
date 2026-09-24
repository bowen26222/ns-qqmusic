#pragma once

#include <switch.h>

#ifndef R_ABORT_UNLESS
// 失败即 svcBreak 断言（NPDM 已授权 svcBreak）
#define R_ABORT_UNLESS(expr) \
    do { \
        Result _qqmusic_rc = (expr); \
        if (R_FAILED(_qqmusic_rc)) \
            svcBreak(_qqmusic_rc, (uintptr_t)0, 0); \
    } while (0)
#endif

namespace qqmusic {

    extern const u32 ApiVersion;

    bool GetRunning();

    Result InitializeServer();
    Result ExitServer();
    void LoopProcess();

} // namespace qqmusic