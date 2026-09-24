#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <switch.h>

#ifndef QQMUSIC_API_VERSION
#define QQMUSIC_API_VERSION 3
#endif

// 当前曲目状态（GetCurrentTrack 输出）
typedef struct {
    u32 index;         // 队列索引
    u32 sample_rate;   // 输出采样率（Hz）
    u32 current_frame; // 当前采样帧
    u32 total_frames;  // 总采样帧
    char path[FS_MAX_PATH]; // UTF-8 SD 路径（含结尾 NUL）
} QqMusicCurrentTrack;

// 在线曲目元数据
typedef struct {
    char songmid[36];
    char media_mid[36];
    char title[96];
    char artist[96];
    char album[96];
    u32 duration_sec;
    char album_mid[36];
    u32 publish_date; // 发行日期 YYYYMMDD（0 = 未知），用于「最新」排序
} QqMusicOnlineSong;

// 在线集合条目（专辑 / 歌手 / 歌单）：搜索与「收藏」列表共用
typedef struct {
    u32 kind;          // 1=歌手 2=专辑 3=歌单
    char id[64];       // singerMID / albumMID / disstid（十进制字符串）
    char title[96];    // 歌手名 / 专辑名 / 歌单名
    char subtitle[96]; // 歌手名 / 歌单创建者 / 发行时间
    char extra[64];    // 摘要（歌曲数等，已格式化）
} QqMusicOnlineCollection;

typedef struct {
    u32 logged_in;
    char uin[32];
    char nickname[64];
} QqMusicOnlineStatus;

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
Result qqmusicGetQueueItem(u32 index, char *out_path, u32 out_size);
Result qqmusicSetTitleEnabled(u64 tid, u32 enabled);
Result qqmusicSetTitleEnabledDefault(u32 enabled);
// out: valid=0 表示当前无前台游戏；name = 标题名（NACP，失败时为 TID 十六进制）
Result qqmusicGetActiveApp(u32 *valid, u64 *tid, char *name, u32 name_size);
// out: has=0 表示未设每游戏开关（跟随默认）
Result qqmusicGetTitleEnabled(u64 tid, u32 *has, u32 *enabled);
Result qqmusicGetTitleEnabledDefault(u32 *enabled);
Result qqmusicGetRadioMode(u32 *out);
Result qqmusicSetRadioMode(u32 enabled);

// 元数据 / 封面（M2）
typedef struct {
    u32 valid;
    char title[96];
    char artist[96];
    char album[96];
} QqMusicTrackMeta;

#define QQMUSIC_TRACKMETA_CURRENT 0xFFFFFFFFu
Result qqmusicGetTrackMeta(u32 index, QqMusicTrackMeta *out);
Result qqmusicGetPathMeta(const char *path, QqMusicTrackMeta *out);
// 确保曲目封面已落盘到 /qqmusic-cache/<out_name>；返回后 out_name 为空串表示暂无封面。
Result qqmusicEnsureCover(const char *path, char *out_name, u32 out_name_size);
Result qqmusicGetCurrentTrack(QqMusicCurrentTrack *out);
Result qqmusicSetRepeatMode(u32 mode); // 0=关闭 1=单曲 2=列表
Result qqmusicSetShuffleMode(u32 on);
Result qqmusicGetMode(u32 *repeat, u32 *shuffle);
Result qqmusicGetTitleVolume(float *out);
Result qqmusicSetTitleVolume(float volume);
Result qqmusicGetDefaultTitleVolume(float *out);
Result qqmusicSetDefaultTitleVolume(float volume);


// 在线功能（M3）
Result qqmusicOnlineGetStatus(QqMusicOnlineStatus *out);
Result qqmusicOnlineStartQrLogin(u8 *out_png, u32 max_png_size, u32 *out_png_size);
Result qqmusicOnlinePollQrLogin(u32 *out_status_code, char *out_msg, u32 max_msg_size);
Result qqmusicOnlineLogout(void);
Result qqmusicOnlineSearch(const char *query, u32 page, u32 page_size, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total);
Result qqmusicOnlineGetFavorites(u32 offset, u32 count, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total);
Result qqmusicOnlineGetGuessRecommend(u32 start_page, u32 pages, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_next_page);
Result qqmusicOnlineGetTopChart(u32 top_id, u32 offset, u32 count, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count);
// 集合搜索：kind 1=歌手 2=专辑 3=歌单（歌曲搜索仍走 qqmusicOnlineSearch）
Result qqmusicOnlineSearchCollections(const char *query, u32 kind, u32 page, u32 page_size,
                                      QqMusicOnlineCollection *out_items, u32 max_items,
                                      u32 *out_count, u32 *out_total);
Result qqmusicOnlineGetFavAlbums(u32 offset, u32 count, QqMusicOnlineCollection *out_items, u32 max_items, u32 *out_count, u32 *out_total);
Result qqmusicOnlineGetFavPlaylists(u32 offset, u32 count, QqMusicOnlineCollection *out_items, u32 max_items, u32 *out_count, u32 *out_total);
Result qqmusicOnlineGetAlbumSongs(const char *albummid, u32 begin, u32 num, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total);
Result qqmusicOnlineGetSingerSongs(const char *singermid, u32 begin, u32 num, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total);
Result qqmusicOnlineGetPlaylistSongs(u64 disstid, u32 begin, u32 num, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total);
Result qqmusicOnlinePlaySong(const QqMusicOnlineSong *song, u32 play_now);
Result qqmusicOnlineEnqueueBatch(const QqMusicOnlineSong *songs, u32 count, u32 play_now);
Result qqmusicQuit(void);
Result qqmusicGetApiVersion(u32 *version);

#ifdef __cplusplus
}
#endif