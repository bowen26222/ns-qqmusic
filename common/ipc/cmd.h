#pragma once

// IPC 命令号：overlay -> sysmodule（named port "qqmusic"）
// 编号规则：0-9 状态查询，10-19 音量，20-29 播放控制（M1+），50 生命周期，5000+ 版本
enum QqMusicIpcCmd {
    QqMusicIpcCmd_GetStatus     = 0,    // out: u32 flags（bit0: 音频活跃）
    QqMusicIpcCmd_GetTick       = 1,    // out: u64 心跳计数（每次 +1，验证双向通路）

    QqMusicIpcCmd_SetVolume     = 10,   // in: float 线性音量系数 0.0-1.0
    QqMusicIpcCmd_GetVolume     = 11,   // out: float

    QqMusicIpcCmd_QuitServer    = 50,   // 退出 sysmodule IPC 循环

    QqMusicIpcCmd_GetApiVersion = 5000, // out: u32 API 版本
};