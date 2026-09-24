#pragma once

// IPC 命令号：overlay -> sysmodule（named port "qqmusic"）
// 编号规则：0-9 状态查询，10-19 音量，20-39 播放/队列，40-49 游戏开关/元数据，50 生命周期，5000+ 版本
enum QqMusicIpcCmd {
    QqMusicIpcCmd_GetStatus     = 0,    // out: u32 flags（bit0: 播放中, bit1: 有当前曲目, bit2: 音频输出不可用, bit3: 黑名单游戏占用中（自动暂停））
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
    QqMusicIpcCmd_Enqueue       = 26,   // in: u32 type（0=队尾/1=队首）; send buffer: UTF-8 路径含 NUL
    QqMusicIpcCmd_Remove        = 27,   // in: u32 索引
    QqMusicIpcCmd_ClearQueue    = 28,
    QqMusicIpcCmd_GetQueueSize  = 29,   // out: u32
    QqMusicIpcCmd_GetQueueItem  = 30,   // in: u32 索引; receive buffer: UTF-8 路径含 NUL
    QqMusicIpcCmd_GetCurrentTrack = 31, // receive buffer: QqMusicCurrentTrack（空闲时 index=UINT32_MAX）
    QqMusicIpcCmd_SetRepeat     = 32,   // in: u32 0=关闭 1=单曲 2=列表
    QqMusicIpcCmd_SetShuffle    = 33,   // in: u32 0/1
    QqMusicIpcCmd_GetMode       = 34,   // out: u32 repeat, u32 shuffle
    QqMusicIpcCmd_GetTitleVolume = 35,  // out: float 当前游戏音量
    QqMusicIpcCmd_SetTitleVolume = 36,  // in: float
    QqMusicIpcCmd_GetDefaultTitleVolume = 37, // out: float 默认游戏音量
    QqMusicIpcCmd_SetDefaultTitleVolume = 38, // in: float

    // 游戏黑名单 / 按游戏开关（M2）：sysmodule 侧写入 config 并即时生效
    QqMusicIpcCmd_SetTitleEnabled = 40,   // in: u64 tid + u32 enabled
    QqMusicIpcCmd_SetTitleEnabledDefault = 41, // in: u32 enabled
    QqMusicIpcCmd_GetActiveApp   = 42,   // out: { u32 valid; u64 tid; char name[64]; }（无前台游戏 valid=0）
    QqMusicIpcCmd_GetTitleEnabled = 43,  // in: u64 tid; out: { u32 has; u32 enabled; }（has=0 表示未设每游戏开关）

    // 元数据 / 封面（M2，懒加载：仅当前曲目按需解析）
    QqMusicIpcCmd_GetTrackMeta   = 44,   // in: u32 index（0xFFFFFFFF=当前）; receive buffer: QqMusicTrackMeta
    QqMusicIpcCmd_EnsureCover   = 45,   // send_buffer: 曲目路径; out: u32 ok; recv_buffer: 封面缓存文件名
    QqMusicIpcCmd_GetTitleEnabledDefault = 46, // out: u32 enabled 默认游戏开关
    QqMusicIpcCmd_GetPathMeta    = 47,   // send_buffer: path; receive_buffer: QqMusicTrackMeta
    QqMusicIpcCmd_GetRadioMode   = 48,   // out: u32 (0/1)
    QqMusicIpcCmd_SetRadioMode   = 49,   // in: u32 (0/1)
    QqMusicIpcCmd_EnsureLyric   = 52,   // send_buffer: 曲目路径; out: u32 ok; recv_buffer: 歌词缓存文件名

    // 在线音乐（M3）
    QqMusicIpcCmd_OnlineGetStatus    = 60, // out: { u32 logged_in; char uin[32]; char nickname[64]; }
    QqMusicIpcCmd_OnlineStartQrLogin = 61, // out: u32 png_size; recv_buffer: PNG 数据
    QqMusicIpcCmd_OnlinePollQrLogin  = 62, // out: { u32 status_code; char msg[64]; }
    QqMusicIpcCmd_OnlineLogout       = 63,
    QqMusicIpcCmd_OnlineSearch       = 64, // in: { u32 page; u32 page_size; char query[64]; }; out: { u32 count; u32 total; }; recv_buffer: QqMusicOnlineSong[]
    QqMusicIpcCmd_OnlineGetTopChart  = 65, // in: { u32 top_id; u32 offset; u32 count; }; out: { u32 count; }; recv_buffer: QqMusicOnlineSong[]
    QqMusicIpcCmd_OnlineGetFavorites = 67, // in: { u32 offset; u32 count; }; out: { u32 count; u32 total; }; recv_buffer: QqMusicOnlineSong[]
    QqMusicIpcCmd_OnlineGetGuessRecommend = 69, // in: { u32 start_page; u32 pages; }; out: { u32 count; u32 next_page; }; recv_buffer: QqMusicOnlineSong[]
    QqMusicIpcCmd_OnlinePlaySong     = 66, // in: u32 play_now; send_buffer: QqMusicOnlineSong
    QqMusicIpcCmd_OnlineEnqueueBatch = 70, // in: u32 play_now; send_buffer: QqMusicOnlineSong[]

    // 在线集合（专辑 / 歌手 / 歌单）：列表条目与歌曲不同，单独用 QqMusicOnlineCollection
    QqMusicIpcCmd_OnlineSearchCollections = 71, // in: { u32 page; u32 page_size; u32 kind; char query[64]; }
                                                //   kind: 1=歌手 2=专辑 3=歌单
                                                //   out: { u32 count; u32 total; }; recv_buffer: QqMusicOnlineCollection[]
    QqMusicIpcCmd_OnlineGetFavAlbums  = 72, // in: { u32 offset; u32 count; }; out: { u32 count; u32 total; }; recv_buffer: QqMusicOnlineCollection[]
    QqMusicIpcCmd_OnlineGetFavPlaylists = 73, // in: { u32 offset; u32 count; }; out: { u32 count; u32 total; }; recv_buffer: QqMusicOnlineCollection[]
    QqMusicIpcCmd_OnlineGetAlbumSongs = 74, // in: { u32 begin; u32 num; char albummid[64]; }; out: { u32 count; u32 total; }; recv_buffer: QqMusicOnlineSong[]
    QqMusicIpcCmd_OnlineGetSingerSongs = 75, // in: { u32 begin; u32 num; char singermid[64]; }; out: { u32 count; u32 total; }; recv_buffer: QqMusicOnlineSong[]
    QqMusicIpcCmd_OnlineGetPlaylistSongs = 76, // in: { u32 begin; u32 num; u64 disstid; }; out: { u32 count; u32 total; }; recv_buffer: QqMusicOnlineSong[]

    QqMusicIpcCmd_QuitServer    = 50,   // 退出 sysmodule IPC 循环

    QqMusicIpcCmd_GetApiVersion = 5000, // out: u32 API 版本
};