#include "client.h"

#include <string.h>
#include "cmd.h"

Service g_qqmusic;

Result qqmusicInitialize(void) {
    Handle handle = 0;
    Result rc = svcConnectToNamedPort(&handle, "qqmusic");
    if (R_SUCCEEDED(rc))
        serviceCreate(&g_qqmusic, handle);
    return rc;
}

void qqmusicExit(void) {
    serviceClose(&g_qqmusic);
}

Result qqmusicGetStatus(u32 *flags) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetStatus, *flags);
}

Result qqmusicGetTick(u64 *tick) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetTick, *tick);
}

Result qqmusicSetVolume(float volume) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetVolume, volume);
}

Result qqmusicGetVolume(float *out) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetVolume, *out);
}

Result qqmusicPlay(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_Play);
}

Result qqmusicPause(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_Pause);
}

Result qqmusicNext(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_Next);
}

Result qqmusicPrev(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_Prev);
}

Result qqmusicSeek(u32 frame) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_Seek, frame);
}

Result qqmusicSelect(u32 index) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_Select, index);
}

Result qqmusicEnqueue(const char *path, u32 type) {
    const u32 path_max = 256;
    u8 in_buf[sizeof(u32) + path_max] = {};
    *(u32 *)in_buf = type;
    if (path) {
        const size_t len = strnlen(path, path_max - 1);
        memcpy(in_buf + sizeof(u32), path, len);
    }
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_Enqueue, in_buf);
}

Result qqmusicRemove(u32 index) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_Remove, index);
}

Result qqmusicClearQueue(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_ClearQueue);
}

Result qqmusicGetQueueSize(u32 *count) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetQueueSize, *count);
}
Result qqmusicGetQueueItem(u32 index, char *path) {
    u32 in = index;
    char path_buf[256] = {0};
    Result rc = serviceDispatchInOut(&g_qqmusic, QqMusicIpcCmd_GetQueueItem, in, path_buf);
    if (R_SUCCEEDED(rc))
        memcpy(path, path_buf, sizeof(path_buf));
    return rc;
}

Result qqmusicGetCurrentTrack(QqMusicCurrentTrack *out) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetCurrentTrack, *out);
}

Result qqmusicSetRepeatMode(u32 mode) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetRepeat, mode);
}

Result qqmusicSetShuffleMode(u32 on) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetShuffle, on);
}

Result qqmusicGetMode(u32 *repeat, u32 *shuffle) {
    u32 out[2] = { 0, 0 };
    Result rc = serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetMode, out);
    if (R_SUCCEEDED(rc)) {
        *repeat = out[0];
        *shuffle = out[1];
    }
    return rc;
}

Result qqmusicQuit(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_QuitServer);
}

Result qqmusicGetApiVersion(u32 *version) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetApiVersion, *version);
}