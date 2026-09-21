#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <switch.h>

#define QQMUSIC_API_VERSION 1

Result qqmusicInitialize(void);
void qqmusicExit(void);

Result qqmusicGetStatus(u32 *flags);
Result qqmusicGetTick(u64 *tick);
Result qqmusicSetVolume(float volume);
Result qqmusicGetVolume(float *out);
Result qqmusicQuit(void);
Result qqmusicGetApiVersion(u32 *version);

#ifdef __cplusplus
}
#endif