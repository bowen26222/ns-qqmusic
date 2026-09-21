#include "service.hpp"

#include <nxExt.h>

#include "client.h" // QQMUSIC_API_VERSION
#include "cmd.h"

namespace qqmusic {

    namespace {

        IpcServer g_server;
        bool g_running = true;

        // M0 占位状态；M1 起由播放内核驱动（加锁）
        u64 g_tick = 0;
        float g_volume = 0.8f;

        Result ServiceHandlerFunc(void *, const IpcServerRequest *r, u8 *out_data, size_t *out_dataSize) {
            switch (r->data.cmdId) {
                case QqMusicIpcCmd_GetStatus:
                    *out_dataSize = sizeof(u32);
                    *(u32 *)out_data = 0;
                    return 0;

                case QqMusicIpcCmd_GetTick:
                    g_tick++;
                    *out_dataSize = sizeof(u64);
                    *(u64 *)out_data = g_tick;
                    return 0;

                case QqMusicIpcCmd_SetVolume:
                    if (r->data.size >= sizeof(float)) {
                        g_volume = *(float *)r->data.ptr;
                        return 0;
                    }
                    break;

                case QqMusicIpcCmd_GetVolume:
                    *out_dataSize = sizeof(float);
                    *(float *)out_data = g_volume;
                    return 0;

                case QqMusicIpcCmd_QuitServer:
                    g_running = false;
                    return 0;

                case QqMusicIpcCmd_GetApiVersion:
                    *out_dataSize = sizeof(u32);
                    *(u32 *)out_data = ApiVersion;
                    return 0;
            }
            // 未知命令：LibnxError_BadInput
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

    } // namespace

    const u32 ApiVersion = QQMUSIC_API_VERSION;

    u32 GetStatusFlags() {
        return 0;
    }

    u64 GetTick() {
        return g_tick;
    }

    float GetVolume() {
        return g_volume;
    }

    void SetVolume(float v) {
        g_volume = v;
    }

    bool GetRunning() {
        return g_running;
    }

    Result InitializeServer() {
        return ipcServerInit(&g_server, "qqmusic", 2);
    }

    Result ExitServer() {
        return ipcServerExit(&g_server);
    }

    void LoopProcess() {
        while (g_running) {
            if (ipcServerProcess(&g_server, ServiceHandlerFunc, nullptr) == KERNELRESULT(Cancelled))
                break;
        }
    }

} // namespace qqmusic