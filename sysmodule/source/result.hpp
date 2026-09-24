#pragma once

#include <switch.h>

namespace qqmusic {

    constexpr const u32 Module = 420;

    constexpr const Result InvalidArgument  = MAKERESULT(Module, 1);
    constexpr const Result InvalidPath      = MAKERESULT(Module, 2);
    constexpr const Result FileNotFound     = MAKERESULT(Module, 3);
    constexpr const Result QueueEmpty       = MAKERESULT(Module, 10);
    constexpr const Result NotPlaying       = MAKERESULT(Module, 11);
    constexpr const Result OutOfRange       = MAKERESULT(Module, 12);
    constexpr const Result FileOpenFailure  = MAKERESULT(Module, 20);
    constexpr const Result VoiceInitFailure = MAKERESULT(Module, 21);
    constexpr const Result OutOfMemory      = MAKERESULT(Module, 30);
    constexpr const Result AudioUnavailable = MAKERESULT(Module, 22);
    // 在线接口：把「服务端没答上来（网络/拒绝）」与「答了但这一页为空」区分开，
    // IPC 层据此返回错误而不是 0 条成功——否则界面只能显示「没有更多内容」。
    constexpr const Result OnlineRequestFailed = MAKERESULT(Module, 40);

}

#define R_UNLESS(bool_expr, res) \
    ({                           \
        if (!(bool_expr)) {      \
            return res;          \
        }                        \
    })

#define R_TRY(res_expr)                   \
    ({                                    \
        const auto temp_res = (res_expr); \
        if (R_FAILED(temp_res))           \
            return temp_res;              \
    })

#ifndef R_ABORT_UNLESS
#define R_ABORT_UNLESS(res_expr)   \
    ({                             \
        auto tmp_res = (res_expr); \
        if (R_FAILED(tmp_res))     \
            diagAbortWithResult(tmp_res);   \
    })
#endif
