#include "client.h"

#include <stdio.h>
#include <string.h>
#include "cmd.h"

Service g_qqmusic;

// 定长查询字段的 UTF-8 安全拷贝：直接 snprintf 会在 64 字节处硬截断，
// 中文关键词正好被切成半个字符时，服务端会判定为非法编码而拒绝搜索。
static void CopyUtf8Truncated(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0)
        return;
    size_t len = strnlen(src, dst_size - 1);
    if (len == dst_size - 1) {
        // 截断点落在字符中间（后续字节是 10xxxxxx 续字节）时回退到字符起始处
        while (len > 0 && (((unsigned char)src[len]) & 0xC0) == 0x80)
            len--;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

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
    if (!path || type > 1)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    const size_t len = strnlen(path, FS_MAX_PATH);
    if (!len || len == FS_MAX_PATH)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_Enqueue, type,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { path, len + 1 } });
}

Result qqmusicRemove(u32 index) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_Remove, index);
}

Result qqmusicClearQueue(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_ClearQueue);
}

Result qqmusicGetMode(u32 *repeat, u32 *shuffle) {
    u32 out[2] = { 0, 0 };
    Result rc = serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetMode, out);
    if (R_SUCCEEDED(rc)) {
        *repeat  = out[0];
        *shuffle = out[1];
    }
    return rc;
}

Result qqmusicGetTitleVolume(float *out) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetTitleVolume, *out);
}

Result qqmusicSetTitleVolume(float volume) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetTitleVolume, volume);
}

Result qqmusicGetDefaultTitleVolume(float *out) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetDefaultTitleVolume, *out);
}

Result qqmusicSetDefaultTitleVolume(float volume) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetDefaultTitleVolume, volume);
}
Result qqmusicGetQueueSize(u32 *count) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetQueueSize, *count);
}
Result qqmusicGetQueueItem(u32 index, char *out_path, u32 out_size) {
    if (!out_path || !out_size)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    out_path[0] = '\0';
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_GetQueueItem, index,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_path, out_size } });
}

Result qqmusicGetCurrentTrack(QqMusicCurrentTrack *out) {
    if (!out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    memset(out, 0, sizeof(*out));
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_GetCurrentTrack,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out, sizeof(*out) } });
}

Result qqmusicSetRepeatMode(u32 mode) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetRepeat, mode);
}

Result qqmusicSetShuffleMode(u32 on) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetShuffle, on);
}

Result qqmusicSetTitleEnabled(u64 tid, u32 enabled) {
    struct {
        u64 tid;
        u32 enabled;
    } in = { tid, enabled };
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetTitleEnabled, in);
}

Result qqmusicSetTitleEnabledDefault(u32 enabled) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetTitleEnabledDefault, enabled);
}

Result qqmusicGetActiveApp(u32 *valid, u64 *tid, char *name, u32 name_size) {
    struct {
        u32 valid;
        u64 tid;
        char name[64];
    } out = {};
    Result rc = serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetActiveApp, out);
    if (R_SUCCEEDED(rc)) {
        if (valid)
            *valid = out.valid;
        if (tid)
            *tid = out.tid;
        if (name && name_size)
            snprintf(name, name_size, "%s", out.name);
    }
    return rc;
}

Result qqmusicGetTitleEnabled(u64 tid, u32 *has, u32 *enabled) {
    struct {
        u64 tid;
    } in = { tid };
    struct {
        u32 has;
        u32 enabled;
    } out = {};
    Result rc = serviceDispatchInOut(&g_qqmusic, QqMusicIpcCmd_GetTitleEnabled, in, out);
    if (R_SUCCEEDED(rc)) {
        if (has)
            *has = out.has;
        if (enabled)
            *enabled = out.enabled;
    }
    return rc;
}

Result qqmusicGetTitleEnabledDefault(u32 *enabled) {
    u32 out = 0;
    Result rc = serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetTitleEnabledDefault, out);
    if (R_SUCCEEDED(rc) && enabled)
        *enabled = out;
    return rc;
}

Result qqmusicGetRadioMode(u32 *out) {
    if (!out) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetRadioMode, *out);
}

Result qqmusicSetRadioMode(u32 enabled) {
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_SetRadioMode, enabled);
}

Result qqmusicGetTrackMeta(u32 index, QqMusicTrackMeta *out) {
    if (!out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    memset(out, 0, sizeof(*out));
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_GetTrackMeta, index,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out, sizeof(*out) } });
}

Result qqmusicGetPathMeta(const char *path, QqMusicTrackMeta *out) {
    if (!path || !out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    const size_t len = strnlen(path, FS_MAX_PATH);
    if (!len || len == FS_MAX_PATH)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    memset(out, 0, sizeof(*out));
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_GetPathMeta,
        .buffer_attrs = {
            SfBufferAttr_In | SfBufferAttr_HipcMapAlias,
            SfBufferAttr_Out | SfBufferAttr_HipcMapAlias
        },
        .buffers = {
            { path, len + 1 },
            { out, sizeof(*out) }
        });
}

Result qqmusicEnsureCover(const char *path, char *out_name, u32 out_name_size) {
    if (!path || !path[0] || !out_name || !out_name_size)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    out_name[0] = '\0';

    u32 ok = 0;
    const u32 in = 0;
    Result rc = serviceDispatchInOut(&g_qqmusic, QqMusicIpcCmd_EnsureCover, in, ok,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias,
                          SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { path, strlen(path) + 1 }, { out_name, out_name_size } });
    if (R_FAILED(rc) || !ok)
        out_name[0] = '\0';
    return rc;
}

Result qqmusicEnsureLyric(const char *path, char *out_name, u32 out_name_size) {
    if (!path || !path[0] || !out_name || !out_name_size)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    out_name[0] = '\0';

    u32 ok = 0;
    const u32 in = 0;
    Result rc = serviceDispatchInOut(&g_qqmusic, QqMusicIpcCmd_EnsureLyric, in, ok,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias,
                          SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { path, strlen(path) + 1 }, { out_name, out_name_size } });
    if (R_FAILED(rc) || !ok)
        out_name[0] = '\0';
    return rc;
}

Result qqmusicOnlineGetStatus(QqMusicOnlineStatus *out) {
    if (!out)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_OnlineGetStatus, *out);
}

Result qqmusicOnlineStartQrLogin(u8 *out_png, u32 max_png_size, u32 *out_png_size) {
    if (!out_png || !max_png_size || !out_png_size)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    *out_png_size = 0;
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_OnlineStartQrLogin, *out_png_size,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_png, max_png_size } });
}

Result qqmusicOnlinePollQrLogin(u32 *out_status_code, char *out_msg, u32 max_msg_size) {
    struct {
        u32 status_code;
        char msg[64];
    } out = {};
    Result rc = serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_OnlinePollQrLogin, out);
    if (R_SUCCEEDED(rc)) {
        if (out_status_code)
            *out_status_code = out.status_code;
        if (out_msg && max_msg_size)
            snprintf(out_msg, max_msg_size, "%s", out.msg);
    }
    return rc;
}

Result qqmusicOnlineLogout(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_OnlineLogout);
}

Result qqmusicOnlineSearch(const char *query, u32 page, u32 page_size, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total) {
    if (!query || !out_songs || !max_songs || !out_count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    struct {
        u32 page;
        u32 page_size;
        char query[64];
    } in = {};
    in.page = page;
    in.page_size = page_size;
    CopyUtf8Truncated(in.query, sizeof(in.query), query);

    struct {
        u32 count;
        u32 total;
    } out = {};

    Result rc = serviceDispatchInOut(&g_qqmusic, QqMusicIpcCmd_OnlineSearch, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_songs, max_songs * sizeof(QqMusicOnlineSong) } });
    if (R_SUCCEEDED(rc)) {
        *out_count = out.count;
        if (out_total)
            *out_total = out.total;
    }
    return rc;
}

Result qqmusicOnlineGetFavorites(u32 offset, u32 count, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total) {
    if (!out_songs || !max_songs || !out_count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    struct {
        u32 offset;
        u32 count;
    } in = { offset, count };

    struct {
        u32 count;
        u32 total;
    } out = {};

    Result rc = serviceDispatchInOut(&g_qqmusic, QqMusicIpcCmd_OnlineGetFavorites, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_songs, max_songs * sizeof(QqMusicOnlineSong) } });
    if (R_SUCCEEDED(rc)) {
        *out_count = out.count;
        if (out_total)
            *out_total = out.total;
    }
    return rc;
}

Result qqmusicOnlineGetRadarPage(u32 cmd, u32 start_page, u32 pages, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_next_page) {
    if (!out_songs || !max_songs || !out_count || !pages)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    struct {
        u32 start_page;
        u32 pages;
    } in = { start_page, pages };
    struct {
        u32 count;
        u32 next_page;
    } out = {};
    Result rc = serviceDispatchInOut(&g_qqmusic, cmd, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_songs, max_songs * sizeof(QqMusicOnlineSong) } });
    if (R_SUCCEEDED(rc)) {
        *out_count = out.count;
        if (out_next_page)
            *out_next_page = out.next_page;
    }
    return rc;
}

Result qqmusicOnlineGetGuessRecommend(u32 start_page, u32 pages, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_next_page) {
    return qqmusicOnlineGetRadarPage(QqMusicIpcCmd_OnlineGetGuessRecommend, start_page, pages,
                                     out_songs, max_songs, out_count, out_next_page);
}

Result qqmusicOnlineGetTopChart(u32 top_id, u32 offset, u32 count, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count) {
    if (!out_songs || !max_songs || !out_count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    struct {
        u32 top_id;
        u32 offset;
        u32 count;
    } in = { top_id, offset, count };

    u32 actual_count = 0;
    Result rc = serviceDispatchInOut(&g_qqmusic, QqMusicIpcCmd_OnlineGetTopChart, in, actual_count,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_songs, max_songs * sizeof(QqMusicOnlineSong) } });
    if (R_SUCCEEDED(rc)) {
        *out_count = actual_count;
    }
    return rc;
}
Result qqmusicOnlinePlaySong(const QqMusicOnlineSong *song, u32 play_now) {
    if (!song)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_OnlinePlaySong, play_now,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { song, sizeof(*song) } });
}

Result qqmusicOnlineEnqueueBatch(const QqMusicOnlineSong *songs, u32 count, u32 play_now) {
    if (!songs || !count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return serviceDispatchIn(&g_qqmusic, QqMusicIpcCmd_OnlineEnqueueBatch, play_now,
        .buffer_attrs = { SfBufferAttr_In | SfBufferAttr_HipcMapAlias },
        .buffers = { { songs, count * sizeof(QqMusicOnlineSong) } });
}

// ---- 在线集合（专辑 / 歌手 / 歌单）----

Result qqmusicOnlineSearchCollections(const char *query, u32 kind, u32 page, u32 page_size,
                                      QqMusicOnlineCollection *out_items, u32 max_items,
                                      u32 *out_count, u32 *out_total) {
    if (!query || !out_items || !max_items || !out_count || !kind)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    struct {
        u32 page;
        u32 page_size;
        u32 kind;
        char query[64];
    } in = {};
    in.page = page;
    in.page_size = page_size;
    in.kind = kind;
    CopyUtf8Truncated(in.query, sizeof(in.query), query);

    struct {
        u32 count;
        u32 total;
    } out = {};

    Result rc = serviceDispatchInOut(&g_qqmusic, QqMusicIpcCmd_OnlineSearchCollections, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_items, max_items * sizeof(QqMusicOnlineCollection) } });
    if (R_SUCCEEDED(rc)) {
        *out_count = out.count;
        if (out_total)
            *out_total = out.total;
    }
    return rc;
}

// 收藏的专辑 / 歌单共用：in { offset, count } → out { count, total }
static Result OnlineGetCollectionPage(u32 cmd, u32 offset, u32 count,
                                      QqMusicOnlineCollection *out_items, u32 max_items,
                                      u32 *out_count, u32 *out_total) {
    if (!out_items || !max_items || !out_count)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    struct {
        u32 offset;
        u32 count;
    } in = { offset, count };

    struct {
        u32 count;
        u32 total;
    } out = {};

    Result rc = serviceDispatchInOut(&g_qqmusic, cmd, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_items, max_items * sizeof(QqMusicOnlineCollection) } });
    if (R_SUCCEEDED(rc)) {
        *out_count = out.count;
        if (out_total)
            *out_total = out.total;
    }
    return rc;
}

Result qqmusicOnlineGetFavAlbums(u32 offset, u32 count, QqMusicOnlineCollection *out_items, u32 max_items, u32 *out_count, u32 *out_total) {
    return OnlineGetCollectionPage(QqMusicIpcCmd_OnlineGetFavAlbums, offset, count,
                                   out_items, max_items, out_count, out_total);
}

Result qqmusicOnlineGetFavPlaylists(u32 offset, u32 count, QqMusicOnlineCollection *out_items, u32 max_items, u32 *out_count, u32 *out_total) {
    return OnlineGetCollectionPage(QqMusicIpcCmd_OnlineGetFavPlaylists, offset, count,
                                   out_items, max_items, out_count, out_total);
}

// 专辑 / 歌手 / 歌单的曲目：三者入参形状不同（mid / mid / disstid），各自成函数。
static Result OnlineGetScopedSongs(u32 cmd, u32 begin, u32 num, const char *mid, u64 disstid,
                                   QqMusicOnlineSong *out_songs, u32 max_songs,
                                   u32 *out_count, u32 *out_total) {
    if (!out_songs || !max_songs || !out_count || !num)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    struct {
        u32 begin;
        u32 num;
        u64 disstid;
        char mid[64];
    } in = {};
    in.begin = begin;
    in.num = num;
    in.disstid = disstid;
    if (mid)
        snprintf(in.mid, sizeof(in.mid), "%s", mid);

    struct {
        u32 count;
        u32 total;
    } out = {};

    Result rc = serviceDispatchInOut(&g_qqmusic, cmd, in, out,
        .buffer_attrs = { SfBufferAttr_Out | SfBufferAttr_HipcMapAlias },
        .buffers = { { out_songs, max_songs * sizeof(QqMusicOnlineSong) } });
    if (R_SUCCEEDED(rc)) {
        *out_count = out.count;
        if (out_total)
            *out_total = out.total;
    }
    return rc;
}

Result qqmusicOnlineGetAlbumSongs(const char *albummid, u32 begin, u32 num, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total) {
    if (!albummid || !albummid[0])
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return OnlineGetScopedSongs(QqMusicIpcCmd_OnlineGetAlbumSongs, begin, num, albummid, 0,
                                out_songs, max_songs, out_count, out_total);
}

Result qqmusicOnlineGetSingerSongs(const char *singermid, u32 begin, u32 num, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total) {
    if (!singermid || !singermid[0])
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return OnlineGetScopedSongs(QqMusicIpcCmd_OnlineGetSingerSongs, begin, num, singermid, 0,
                                out_songs, max_songs, out_count, out_total);
}

Result qqmusicOnlineGetPlaylistSongs(u64 disstid, u32 begin, u32 num, QqMusicOnlineSong *out_songs, u32 max_songs, u32 *out_count, u32 *out_total) {
    if (!disstid)
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    return OnlineGetScopedSongs(QqMusicIpcCmd_OnlineGetPlaylistSongs, begin, num, NULL, disstid,
                                out_songs, max_songs, out_count, out_total);
}

Result qqmusicQuit(void) {
    return serviceDispatch(&g_qqmusic, QqMusicIpcCmd_QuitServer);
}

Result qqmusicGetApiVersion(u32 *version) {
    return serviceDispatchOut(&g_qqmusic, QqMusicIpcCmd_GetApiVersion, *version);
}