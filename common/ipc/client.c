#include "client.h"

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

Result qqmusicQuit(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_QuitServer);
}

Result qqmusicGetApiVersion(u32 *version) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetApiVersion, *version);
}