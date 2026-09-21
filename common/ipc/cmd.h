#pragma once

// IPC 命令号：overlay -> sysmodule（named port "qqmusic"）
// 编号规则：0-9 状态查询，10-19 音量，20-29 播放控制（M1+），50 生命周期，5000+ 版本
enum QqMusicIpcCmd {
    QqMusicIpcCmd_GetStatus     = 0,    // out: u32 flags（bit0: 播放中, bit1: 有当前曲目）
    QqMusicIpcCmd_GetTick       = 1,    // out: u64 心跳计数（每次 +1，验证双向通路）

    QqMusicIpcCmd_SetVolume     = 10,   // in: float 线性音量系数 0.0-1.0
    QqMusicIpcCmd_GetVolume     = 11,   // out: float

    // 播放控制（M1）
    QqMusicIpcCmd_Play          = 20,   // 继续/开始
    QqMusicIpcCmd_Pause         = 21,   // 暂停
    QqMusicIpcCmd_Next          = 22,   // 下一曲
    QqMusicIpcCmd_Prev          = 23,   // 上一曲
    QqMusicIpcCmd_Seek          = 24,   // in: u32 采样帧序号
    QqMusicIpcCmd_Select        = 25,   // in: u32 队列索引（立即播放）

    // 队列管理（M1）
    QqMusicIpcCmd_Enqueue       = 26,   // in: u32 type + char path[256]（sdmc:/ 绝对路径）
    QqMusicIpcCmd_Remove        = 27,   // in: u32 索引
    QqMusicIpcCmd_ClearQueue    = 28,
    QqMusicIpcCmd_GetQueueSize  = 29,   // out: u32
    QqMusicIpcCmd_GetQueueItem  = 30,   // in: u32 索引; out: char path[256]
    QqMusicIpcCmd_GetCurrentTrack = 31, // out: { u32 index; u32 sample_rate; u32 current_frame; u32 total_frames; char path[256]; }
    QqMusicIpcCmd_SetRepeat     = 32,   // in: u32 0=关闭 1=单曲 2=列表
    QqMusicIpcCmd_SetShuffle    = 33,   // in: u32 0/1
    QqMusicIpcCmd_GetMode       = 34,   // out: u32 repeat, u32 shuffle

    QqMusicIpcCmd_QuitServer    = 50,   // 退出 sysmodule IPC 循环

    QqMusicIpcCmd_GetApiVersion = 5000, // out: u32 API 版本
};