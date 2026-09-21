#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <switch.h>

#define QQMUSIC_API_VERSION 1

// 当前曲目状态（GetCurrentTrack 输出）
typedef struct {
    u32 index;         // 队列索引
    u32 sample_rate;   // 输出采样率（Hz）
    u32 current_frame; // 当前采样帧
    u32 total_frames;  // 总采样帧
    char path[256];    // sdmc:/ 路径
} QqMusicCurrentTrack;

Result qqmusicInitialize(void);
void qqmusicExit(void);

Result qqmusicGetStatus(u32 *flags);
Result qqmusicGetTick(u64 *tick);
Result qqmusicSetVolume(float volume);
Result qqmusicGetVolume(float *out);

// 播放控制（M1）
Result qqmusicPlay(void);
Result qqmusicPause(void);
Result qqmusicNext(void);
Result qqmusicPrev(void);
Result qqmusicSeek(u32 frame);
Result qqmusicSelect(u32 index);

// 队列管理（M1）
Result qqmusicEnqueue(const char *path, u32 type); // type: 0=队尾 1=队首
Result qqmusicRemove(u32 index);
Result qqmusicClearQueue(void);
Result qqmusicGetQueueSize(u32 *count);
Result qqmusicGetQueueItem(u32 index, char *path); // path 缓冲区须 256 字节
Result qqmusicGetCurrentTrack(QqMusicCurrentTrack *out);
Result qqmusicSetRepeatMode(u32 mode); // 0=关闭 1=单曲 2=列表
Result qqmusicSetShuffleMode(u32 on);
Result qqmusicGetMode(u32 *repeat, u32 *shuffle);

Result qqmusicQuit(void);
Result qqmusicGetApiVersion(u32 *version);

#ifdef __cplusplus
}
#endif