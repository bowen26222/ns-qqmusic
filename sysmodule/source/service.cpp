#include "service.hpp"
#include <nxExt.h>
#include <cstring>

// newlib 的 <cstring> 在 C++ 模式（无 __STDC_VERSION__）下不暴露 strnlen，显式声明。
#include "impl/player.hpp"

extern "C" size_t strnlen(const char *s, size_t maxlen);
#include "types.hpp"

#include "client.h" // QQMUSIC_API_VERSION
#include "cmd.h"

namespace qqmusic {

    namespace {

        IpcServer g_server;
        bool g_running = true;


        // M0 心跳（验证 overlay<->sysmodule 双向通路），与播放状态无关。
        u64 g_tick = 0;

        Result ServiceHandlerFunc(void *, const IpcServerRequest *r, u8 *out_data, size_t *out_dataSize) {
            switch (r->data.cmdId) {
                case QqMusicIpcCmd_GetStatus: {
                    u32 flags = 0;
                    if (impl::GetStatus())
                        flags |= 1;
                    CurrentStats stats = {};
                    char path[256] = {};
                    if (R_SUCCEEDED(impl::GetCurrentQueueItem(&stats, path, sizeof(path))))
                        flags |= 2;
                    *out_dataSize = sizeof(u32);
                    *(u32 *)out_data = flags;
                    return 0;
                }

                case QqMusicIpcCmd_GetTick:
                    g_tick++;
                    *out_dataSize = sizeof(u64);
                    *(u64 *)out_data = g_tick;
                    return 0;

                case QqMusicIpcCmd_SetVolume:
                    if (r->data.size >= sizeof(float))
                        impl::SetVolume(*(float *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_GetVolume:
                    *out_dataSize = sizeof(float);
                    *(float *)out_data = impl::GetVolume();
                    return 0;

                case QqMusicIpcCmd_Play:
                    impl::Play();
                    return 0;

                case QqMusicIpcCmd_Pause:
                    impl::Pause();
                    return 0;

                case QqMusicIpcCmd_Next:
                    impl::Next();
                    return 0;

                case QqMusicIpcCmd_Prev:
                    impl::Prev();
                    return 0;

                case QqMusicIpcCmd_Seek:
                    if (r->data.size >= sizeof(u32))
                        impl::Seek(*(u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_Select:
                    if (r->data.size >= sizeof(u32))
                        impl::Select(*(u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_Enqueue: {
                    // in: u32 type + char path[256]
                    if (r->data.size < sizeof(u32) + 1)
                        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
                    const u32 type = *(const u32 *)r->data.ptr;
                    const char *path = (const char *)r->data.ptr + sizeof(u32);
                    const size_t len = strnlen(path, r->data.size - sizeof(u32));
                    return R_SUCCEEDED(impl::Enqueue(path, len, (EnqueueType)type)) ? 0 : MAKERESULT(Module_Libnx, LibnxError_BadInput);
                }

                case QqMusicIpcCmd_Remove:
                    if (r->data.size >= sizeof(u32))
                        impl::Remove(*(u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_ClearQueue:
                    impl::ClearQueue();
                    return 0;

                case QqMusicIpcCmd_GetQueueSize:
                    *out_dataSize = sizeof(u32);
                    *(u32 *)out_data = impl::GetPlaylistSize();
                    return 0;

                case QqMusicIpcCmd_GetQueueItem: {
                    if (r->data.size < sizeof(u32) || *out_dataSize < 256)
                        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
                    const u32 index = *(const u32 *)r->data.ptr;
                    char path[256] = {};
                    const u32 rc = impl::GetPlaylistItem(index, path, sizeof(path));
                    *out_dataSize = sizeof(path);
                    memcpy(out_data, path, sizeof(path));
                    return R_SUCCEEDED(rc) ? 0 : rc;
                }

                case QqMusicIpcCmd_GetCurrentTrack: {
                    QqMusicCurrentTrack track = {};
                    CurrentStats stats = {};
                    const Result rc = impl::GetCurrentQueueItem(&stats, track.path, sizeof(track.path));
                    if (R_SUCCEEDED(rc)) {
                        track.sample_rate = stats.sample_rate;
                        track.current_frame = stats.current_frame;
                        track.total_frames = stats.total_frames;
                    }
                    *out_dataSize = sizeof(track);
                    if (R_SUCCEEDED(rc))
                        memcpy(out_data, &track, sizeof(track));
                    return rc;
                }

                case QqMusicIpcCmd_SetRepeat:
                    if (r->data.size >= sizeof(u32))
                        impl::SetRepeatMode((RepeatMode)*(const u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_SetShuffle:
                    if (r->data.size >= sizeof(u32))
                        impl::SetShuffleMode((ShuffleMode)*(const u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_GetMode: {
                    u32 out[2] = { (u32)impl::GetRepeatMode(), (u32)impl::GetShuffleMode() };
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, out, sizeof(out));
                    return 0;
                }

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