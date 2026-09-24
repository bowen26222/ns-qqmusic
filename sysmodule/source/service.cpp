#include "service.hpp"
#include <nxExt.h>

// newlib 的 <cstring> 在 C++ 模式（无 __STDC_VERSION__）下不暴露 strnlen，显式声明。
#include "impl/player.hpp"
#include "result.hpp"
#include "impl/meta.hpp"
#include "sdmc/sdmc.hpp"
#include "pm/pm.hpp"
#include "config/config.hpp"
#include "net/qqmusic_api.hpp"

#include <cstdio>
#include <cstring>

extern "C" size_t strnlen(const char *s, size_t maxlen);
extern "C" void sysLog(const char *s);
#include "types.hpp"

#include "client.h" // QQMUSIC_API_VERSION
#include "cmd.h"

namespace qqmusic {

    namespace {

        IpcServer g_server;
        bool g_running = true;
        u64 g_tick = 0;
        qqmusic::api::QrCodeSession g_qr_session{};

        // 读取指定 title 的名称：sdmc:/atmosphere/contents/<tid>/control.nacp
        // （ApplicationTitle 英语名在文件偏移 0，UTF-8；NAND 安装或读取失败时退回 TID 十六进制）。
        void ReadActiveAppName(u64 tid, char *name, size_t name_size) {
            char hex[17];
            snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)tid);
            snprintf(name, name_size, "%s", hex);

            char path[FS_MAX_PATH + 16];
            snprintf(path, sizeof(path), "/atmosphere/contents/%s/control.nacp", hex);
            FsFile f;
            if (R_SUCCEEDED(sdmc::OpenFile(&f, path, FsOpenMode_Read))) {
                u8 buf[0x200] = {};
                u64 got = 0;
                fsFileRead(&f, 0, buf, sizeof(buf), 0, &got);
                fsFileClose(&f);
                if (got >= 0x200) {
                    size_t n = 0;
                    while (n < 0x1FF && buf[n])
                        n++;
                    if (n)
                        snprintf(name, name_size, "%.*s", (int)n, (const char *)buf);
                }
            }
        }


        Result ServiceHandlerFunc(void *, const IpcServerRequest *r, u8 *out_data, size_t *out_dataSize) {
            constexpr Result badInput = MAKERESULT(Module_Libnx, LibnxError_BadInput);
            switch (r->data.cmdId) {
                case QqMusicIpcCmd_GetStatus: {
                    u32 flags = 0;
                    if (impl::GetStatus())
                        flags |= 1;
                    CurrentStats stats = {};
                    char path[FS_MAX_PATH] = {};
                    if (R_SUCCEEDED(impl::GetCurrentQueueItem(&stats, path, sizeof(path))))
                        flags |= 2;
                    if (impl::GetAudioUnavailable())
                        flags |= 4;
                    if (impl::GetBlacklistPaused())
                        flags |= 8;
                    if (impl::GetLastPlayError())
                        flags |= 16;
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
                    if (r->data.size < sizeof(float))
                        return badInput;
                    impl::SetVolume(*(const float *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_GetVolume:
                    *out_dataSize = sizeof(float);
                    *(float *)out_data = impl::GetVolume();
                    return 0;

                case QqMusicIpcCmd_Play:
                    if (!impl::GetPlaylistSize())
                        return QueueEmpty;
                    impl::Play();
                    return 0;

                case QqMusicIpcCmd_Pause:
                    impl::Pause();
                    return 0;

                case QqMusicIpcCmd_Next:
                    if (!impl::GetPlaylistSize())
                        return QueueEmpty;
                    impl::Next();
                    return 0;

                case QqMusicIpcCmd_Prev:
                    if (!impl::GetPlaylistSize())
                        return QueueEmpty;
                    impl::Prev();
                    return 0;

                case QqMusicIpcCmd_Seek:
                    if (r->data.size < sizeof(u32))
                        return badInput;
                    impl::Seek(*(const u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_Select:
                    if (r->data.size < sizeof(u32))
                        return badInput;
                    if (*(const u32 *)r->data.ptr >= impl::GetPlaylistSize())
                        return OutOfRange;
                    impl::Select(*(const u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_Enqueue: {
                    if (r->data.size < sizeof(u32) || !r->send_buffer.ptr ||
                        !r->send_buffer.size || r->send_buffer.size > FS_MAX_PATH)
                        return badInput;
                    const u32 type = *(const u32 *)r->data.ptr;
                    if (type > 1)
                        return badInput;
                    // Copy the mapped path once before filesystem IPC; never truncate UTF-8.
                    char path[FS_MAX_PATH];
                    memcpy(path, r->send_buffer.ptr, r->send_buffer.size);
                    const size_t len = strnlen(path, r->send_buffer.size);
                    if (!len || len + 1 != r->send_buffer.size)
                        return badInput;
                    return impl::Enqueue(path, len, type == 0 ? EnqueueType::Back : EnqueueType::Front);
                }

                case QqMusicIpcCmd_Remove:
                    if (r->data.size < sizeof(u32))
                        return badInput;
                    return impl::Remove(*(const u32 *)r->data.ptr);

                case QqMusicIpcCmd_ClearQueue:
                    impl::ClearQueue();
                    return 0;

                case QqMusicIpcCmd_GetQueueSize:
                    *out_dataSize = sizeof(u32);
                    *(u32 *)out_data = impl::GetPlaylistSize();
                    return 0;

                case QqMusicIpcCmd_GetQueueItem: {
                    if (r->data.size < sizeof(u32) || !r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;
                    const u32 index = *(const u32 *)r->data.ptr;
                    return impl::GetPlaylistItem(index, (char *)r->recv_buffer.ptr, r->recv_buffer.size);
                }

                case QqMusicIpcCmd_GetCurrentTrack: {
                    if (!r->recv_buffer.ptr || r->recv_buffer.size != sizeof(QqMusicCurrentTrack))
                        return badInput;
                    QqMusicCurrentTrack track = {};
                    track.index = UINT32_MAX;
                    CurrentStats stats = {};
                    const Result rc = impl::GetCurrentQueueItem(&stats, track.path, sizeof(track.path));
                    if (R_SUCCEEDED(rc)) {
                        track.index = stats.index;
                        track.sample_rate = stats.sample_rate;
                        track.current_frame = stats.current_frame;
                        track.total_frames = stats.total_frames;
                    } else if (rc != NotPlaying) {
                        return rc;
                    }
                    memcpy(r->recv_buffer.ptr, &track, sizeof(track));
                    return 0;
                }
                case QqMusicIpcCmd_GetTitleVolume:
                    *out_dataSize = sizeof(float);
                    *(float *)out_data = impl::GetTitleVolume();
                    return 0;

                case QqMusicIpcCmd_SetTitleVolume:
                    if (r->data.size < sizeof(float))
                        return badInput;
                    impl::SetTitleVolume(*(const float *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_GetDefaultTitleVolume:
                    *out_dataSize = sizeof(float);
                    *(float *)out_data = impl::GetDefaultTitleVolume();
                    return 0;

                case QqMusicIpcCmd_SetDefaultTitleVolume:
                    if (r->data.size < sizeof(float))
                        return badInput;
                    impl::SetDefaultTitleVolume(*(const float *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_SetRepeat:
                    if (r->data.size < sizeof(u32) || *(const u32 *)r->data.ptr > 2)
                        return badInput;
                    impl::SetRepeatMode((RepeatMode)*(const u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_SetShuffle:
                    if (r->data.size < sizeof(u32) || *(const u32 *)r->data.ptr > 1)
                        return badInput;
                    impl::SetShuffleMode((ShuffleMode)*(const u32 *)r->data.ptr);
                    return 0;

                case QqMusicIpcCmd_GetMode: {
                    u32 out[2] = { (u32)impl::GetRepeatMode(), (u32)impl::GetShuffleMode() };
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, out, sizeof(out));
                    return 0;
                }

                case QqMusicIpcCmd_GetActiveApp: {
                    struct {
                        u32 valid;
                        u64 tid;
                        char name[64];
                    } a = {};
                    u64 pid = 0, tid = 0;
                    if (pm::GetActiveApp(&pid, &tid)) {
                        a.valid = 1;
                        a.tid = tid;
                        ReadActiveAppName(tid, a.name, sizeof(a.name));
                    }
                    *out_dataSize = sizeof(a);
                    memcpy(out_data, &a, sizeof(a));
                    return 0;
                }

                case QqMusicIpcCmd_GetTitleEnabled: {
                    if (r->data.size < sizeof(u64))
                        return badInput;
                    struct {
                        u32 has;
                        u32 enabled;
                    } e = {};
                    if (r->data.size >= sizeof(u64)) {
                        const u64 tid = *(const u64 *)r->data.ptr;
                        e.has = config::has_title_enabled(tid);
                        e.enabled = e.has ? config::get_title_enabled(tid) : config::get_title_enabled_default();
                    }
                    *out_dataSize = sizeof(e);
                    memcpy(out_data, &e, sizeof(e));
                    return 0;
                }

                case QqMusicIpcCmd_GetTitleEnabledDefault:
                    *out_dataSize = sizeof(u32);
                    *(u32 *)out_data = config::get_title_enabled_default();
                    return 0;

                case QqMusicIpcCmd_SetTitleEnabled: {
                    if (r->data.size < sizeof(u64) + sizeof(u32))
                        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
                    impl::SetTitleEnabled(*(const u64 *)r->data.ptr, *(const u32 *)((const char *)r->data.ptr + sizeof(u64)) != 0);
                    return 0;
                }

                case QqMusicIpcCmd_SetTitleEnabledDefault:
                    if (r->data.size < sizeof(u32))
                        return badInput;
                    impl::SetTitleEnabledDefault(*(const u32 *)r->data.ptr != 0);
                    return 0;

                case QqMusicIpcCmd_GetRadioMode: {
                    u32 mode = impl::GetRadioMode() ? 1 : 0;
                    *out_dataSize = sizeof(mode);
                    memcpy(out_data, &mode, sizeof(mode));
                    return 0;
                }

                case QqMusicIpcCmd_SetRadioMode: {
                    if (r->data.size < sizeof(u32))
                        return badInput;
                    const u32 mode = *(const u32 *)r->data.ptr;
                    impl::SetRadioMode(mode != 0);
                    return 0;
                }

                case QqMusicIpcCmd_GetTrackMeta: {
                    if (r->data.size < sizeof(u32) || !r->recv_buffer.ptr ||
                        r->recv_buffer.size != sizeof(QqMusicTrackMeta))
                        return badInput;
                    QqMusicTrackMeta m = {};
                    auto fill = [&](const char *path) {
                        qqmusic::impl::TrackMeta meta;
                        if (R_SUCCEEDED(qqmusic::impl::GetTrackMeta(path, &meta))) {
                            m.valid = 1;
                            snprintf(m.title, sizeof(m.title), "%s", meta.title);
                            snprintf(m.artist, sizeof(m.artist), "%s", meta.artist);
                            snprintf(m.album, sizeof(m.album), "%s", meta.album);
                        }
                    };
                    if (r->data.size >= sizeof(u32)) {
                        char path[FS_MAX_PATH] = {};
                        const u32 index = *(const u32 *)r->data.ptr;
                        if (index == 0xFFFFFFFFu) {
                            CurrentStats stats = {};
                            if (R_SUCCEEDED(impl::GetCurrentQueueItem(&stats, path, sizeof(path))))
                                fill(path);
                        } else if (R_SUCCEEDED(impl::GetPlaylistItem(index, path, sizeof(path)))) {
                            fill(path);
                        }
                    }
                    memcpy(r->recv_buffer.ptr, &m, sizeof(m));
                    return 0;
                }

                case QqMusicIpcCmd_GetPathMeta: {
                    if (!r->send_buffer.ptr || !r->send_buffer.size ||
                        !r->recv_buffer.ptr || r->recv_buffer.size != sizeof(QqMusicTrackMeta))
                        return badInput;
                    char path[FS_MAX_PATH] = {};
                    const size_t len = std::min<size_t>(r->send_buffer.size, sizeof(path) - 1);
                    memcpy(path, r->send_buffer.ptr, len);
                    path[len] = 0;

                    QqMusicTrackMeta m = {};
                    qqmusic::impl::TrackMeta meta;
                    if (R_SUCCEEDED(qqmusic::impl::GetTrackMeta(path, &meta))) {
                        m.valid = 1;
                        snprintf(m.title, sizeof(m.title), "%s", meta.title);
                        snprintf(m.artist, sizeof(m.artist), "%s", meta.artist);
                        snprintf(m.album, sizeof(m.album), "%s", meta.album);
                    }
                    memcpy(r->recv_buffer.ptr, &m, sizeof(m));
                    return 0;
                }

                case QqMusicIpcCmd_EnsureCover: {
                    // send_buffer: 曲目路径（含 NUL）；recv_buffer: 封面缓存文件名；out: u32 ok
                    if (!r->send_buffer.ptr || !r->send_buffer.size ||
                        !r->recv_buffer.ptr || !r->recv_buffer.size ||
                        r->send_buffer.size > FS_MAX_PATH)
                        return badInput;

                    // 不信任发送方的 NUL 终止：定长拷贝后强制收尾。
                    char path[FS_MAX_PATH];
                    const size_t in_len = strnlen((const char *)r->send_buffer.ptr, (size_t)r->send_buffer.size);
                    if (in_len == 0)
                        return badInput;
                    memcpy(path, r->send_buffer.ptr, in_len);
                    path[in_len] = '\0';

                    char name[96] = {};
                    const Result coverRc = impl::EnsureCoverFile(path, name, sizeof(name));
                    const u32 ok = (R_SUCCEEDED(coverRc) && name[0]) ? 1u : 0u;
                    if (ok) {
                        const size_t len = strlen(name);
                        const size_t n = (len < r->recv_buffer.size) ? len : (r->recv_buffer.size - 1);
                        memcpy(r->recv_buffer.ptr, name, n);
                        ((char *)r->recv_buffer.ptr)[n] = '\0';
                    }
                    *out_dataSize = sizeof(ok);
                    memcpy(out_data, &ok, sizeof(ok));
                    return 0;
                }

                case QqMusicIpcCmd_OnlineGetStatus: {
                    auto user = qqmusic::api::GetUserSession();
                    QqMusicOnlineStatus s = {};
                    s.logged_in = user.logged_in ? 1 : 0;
                    snprintf(s.uin, sizeof(s.uin), "%s", user.uin.c_str());
                    snprintf(s.nickname, sizeof(s.nickname), "%s", user.nickname.c_str());
                    *out_dataSize = sizeof(s);
                    memcpy(out_data, &s, sizeof(s));
                    return 0;
                }

                case QqMusicIpcCmd_OnlineStartQrLogin: {
                    if (!r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;
                    g_qr_session = {};
                    bool ok = qqmusic::api::RequestQrCode(g_qr_session);
                    u32 size = 0;
                    if (ok && !g_qr_session.png_bytes.empty()) {
                        size = (u32)std::min<size_t>(g_qr_session.png_bytes.size(), r->recv_buffer.size);
                        memcpy(r->recv_buffer.ptr, g_qr_session.png_bytes.data(), size);
                    }
                    *out_dataSize = sizeof(size);
                    memcpy(out_data, &size, sizeof(size));
                    return 0;
                }

                case QqMusicIpcCmd_OnlinePollQrLogin: {
                    struct {
                        u32 status_code;
                        char msg[64];
                    } out = {};
                    qqmusic::api::PollQrCode(g_qr_session);
                    out.status_code = g_qr_session.status_code;
                    snprintf(out.msg, sizeof(out.msg), "%s", g_qr_session.status_msg.c_str());
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, &out, sizeof(out));
                    return 0;
                }

                case QqMusicIpcCmd_OnlineLogout:
                    qqmusic::api::Logout();
                    return 0;

                case QqMusicIpcCmd_OnlineSearch: {
                    const struct {
                        u32 page;
                        u32 page_size;
                        char query[64];
                    } *in = (const decltype(in))r->data.ptr;
                    // 必须是整个结构体而不只是两个字段：query 有 64 字节，
                    // 校验不足会让 std::string(in->query) 读到未初始化/越界的内存。
                    if (r->data.size < sizeof(*in) || !r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;

                    std::vector<QqMusicOnlineSong> songs;
                    int total = 0;
                    if (!qqmusic::api::SearchSongs(in->query, in->page, in->page_size, songs, total))
                        return OnlineRequestFailed;

                    u32 max_items = r->recv_buffer.size / sizeof(QqMusicOnlineSong);
                    u32 count = (u32)std::min<size_t>(songs.size(), max_items);
                    auto *dst = (QqMusicOnlineSong *)r->recv_buffer.ptr;
                    for (u32 i = 0; i < count; i++) {
                        memcpy(&dst[i], &songs[i], sizeof(QqMusicOnlineSong));
                    }

                    struct {
                        u32 count;
                        u32 total;
                    } out = { count, (u32)total };
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, &out, sizeof(out));
                    return 0;
                }

                case QqMusicIpcCmd_OnlineGetTopChart: {
                    if (r->data.size < sizeof(u32)*3 || !r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;
                    const struct {
                        u32 top_id;
                        u32 offset;
                        u32 count;
                    } *in = (const decltype(in))r->data.ptr;

                    std::vector<QqMusicOnlineSong> songs;
                    if (!qqmusic::api::GetTopChart(in->top_id, in->offset, in->count, songs))
                        return OnlineRequestFailed;

                    u32 max_items = r->recv_buffer.size / sizeof(QqMusicOnlineSong);
                    u32 count = (u32)std::min<size_t>(songs.size(), max_items);
                    auto *dst = (QqMusicOnlineSong *)r->recv_buffer.ptr;
                    for (u32 i = 0; i < count; i++) {
                        memcpy(&dst[i], &songs[i], sizeof(QqMusicOnlineSong));
                    }

                    *out_dataSize = sizeof(count);
                    memcpy(out_data, &count, sizeof(count));
                    return 0;
                }

                case QqMusicIpcCmd_OnlineGetFavorites: {
                    const struct {
                        u32 offset;
                        u32 count;
                    } *in = (const decltype(in))r->data.ptr;
                    if (r->data.size < sizeof(*in) || in->offset > INT32_MAX ||
                        !r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;

                    std::vector<QqMusicOnlineSong> songs;
                    int total = 0;
                    if (!qqmusic::api::GetMyFavorites((int)in->offset, (int)in->count, songs, total))
                        return OnlineRequestFailed;
                    const u32 max_items = r->recv_buffer.size / sizeof(QqMusicOnlineSong);
                    const u32 count = (u32)std::min<size_t>(songs.size(), max_items);
                    auto *dst = (QqMusicOnlineSong *)r->recv_buffer.ptr;
                    for (u32 i = 0; i < count; i++) {
                        memcpy(&dst[i], &songs[i], sizeof(QqMusicOnlineSong));
                    }

                    const struct {
                        u32 count;
                        u32 total;
                    } out = { count, (u32)total };
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, &out, sizeof(out));
                    return 0;
                }

                case QqMusicIpcCmd_OnlineGetGuessRecommend: {
                    if (r->data.size < sizeof(u32) * 2 || !r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;
                    const struct {
                        u32 start_page;
                        u32 pages;
                    } *in = (const decltype(in))r->data.ptr;

                    std::vector<QqMusicOnlineSong> songs;
                    qqmusic::api::GetGuessRecommend((int)in->start_page, (int)in->pages, songs);

                    const u32 max_items = r->recv_buffer.size / sizeof(QqMusicOnlineSong);
                    const u32 count = (u32)std::min<size_t>(songs.size(), max_items);
                    auto *dst = (QqMusicOnlineSong *)r->recv_buffer.ptr;
                    for (u32 i = 0; i < count; i++) {
                        memcpy(&dst[i], &songs[i], sizeof(QqMusicOnlineSong));
                    }

                    const struct {
                        u32 count;
                        u32 next_page;
                    } out = { count, in->start_page + in->pages };
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, &out, sizeof(out));
                    return 0;
                }

                case QqMusicIpcCmd_OnlinePlaySong: {
                    if (r->data.size < sizeof(u32) || !r->send_buffer.ptr || r->send_buffer.size < sizeof(QqMusicOnlineSong))
                        return badInput;
                    const u32 play_now = *(const u32 *)r->data.ptr;
                    const auto *song = (const QqMusicOnlineSong *)r->send_buffer.ptr;

                    char path[FS_MAX_PATH];
                    if (song->album_mid[0])
                        snprintf(path, sizeof(path), "qqm://%s/%s/%s", song->songmid, song->media_mid[0] ? song->media_mid : song->songmid, song->album_mid);
                    else
                        snprintf(path, sizeof(path), "qqm://%s/%s", song->songmid, song->media_mid[0] ? song->media_mid : song->songmid);

                    qqmusic::impl::TrackMeta m{};
                    snprintf(m.title, sizeof(m.title), "%s", song->title);
                    snprintf(m.artist, sizeof(m.artist), "%s", song->artist);
                    snprintf(m.album, sizeof(m.album), "%s", song->album);
                    qqmusic::impl::SetTrackMetaCache(path, m);

                    const size_t path_len = strlen(path);
                    if (play_now) {
                        impl::ClearQueue();
                        Result enq_rc = impl::Enqueue(path, path_len, EnqueueType::Back);
                        if (R_FAILED(enq_rc)) {
                            char b[96];
                            std::snprintf(b, sizeof(b), "SYS OnlinePlaySong Enqueue FAIL: rc=0x%08x\n", (unsigned)enq_rc);
                            sysLog(b);
                            return enq_rc;
                        }
                        impl::Select(0);
                    } else {
                        Result enq_rc = impl::Enqueue(path, path_len, EnqueueType::Back);
                        if (R_FAILED(enq_rc)) {
                            char b[96];
                            std::snprintf(b, sizeof(b), "SYS OnlinePlaySong Enqueue FAIL: rc=0x%08x\n", (unsigned)enq_rc);
                            sysLog(b);
                            return enq_rc;
                        }
                    }
                    return 0;
                }

                case QqMusicIpcCmd_OnlineEnqueueBatch: {
                    if (r->data.size < sizeof(u32) || !r->send_buffer.ptr || !r->send_buffer.size)
                        return badInput;
                    const u32 play_now = *(const u32 *)r->data.ptr;
                    const auto *songs = (const QqMusicOnlineSong *)r->send_buffer.ptr;
                    const u32 count = r->send_buffer.size / sizeof(QqMusicOnlineSong);
                    if (count == 0) return badInput;

                    if (play_now) {
                        impl::ClearQueue();
                    }

                    for (u32 i = 0; i < count; i++) {
                        char path[FS_MAX_PATH];
                        if (songs[i].album_mid[0])
                            snprintf(path, sizeof(path), "qqm://%s/%s/%s", songs[i].songmid, songs[i].media_mid[0] ? songs[i].media_mid : songs[i].songmid, songs[i].album_mid);
                        else
                            snprintf(path, sizeof(path), "qqm://%s/%s", songs[i].songmid, songs[i].media_mid[0] ? songs[i].media_mid : songs[i].songmid);

                        qqmusic::impl::TrackMeta m{};
                        snprintf(m.title, sizeof(m.title), "%s", songs[i].title);
                        snprintf(m.artist, sizeof(m.artist), "%s", songs[i].artist);
                        snprintf(m.album, sizeof(m.album), "%s", songs[i].album);
                        qqmusic::impl::SetTrackMetaCache(path, m);

                        Result enq_rc = impl::Enqueue(path, strlen(path), EnqueueType::Back, false);
                        if (R_FAILED(enq_rc)) {
                            char b[96];
                            std::snprintf(b, sizeof(b), "SYS OnlineBatch Enqueue FAIL: rc=0x%08x\n", (unsigned)enq_rc);
                            sysLog(b);
                        }
                    }
                    impl::SaveQueue();
                    if (play_now) {
                        impl::Select(0);
                    }
                    return 0;
                }

                case QqMusicIpcCmd_OnlineSearchCollections: {
                    const struct {
                        u32 page;
                        u32 page_size;
                        u32 kind;
                        char query[64];
                    } *in = (const decltype(in))r->data.ptr;
                    if (r->data.size < sizeof(*in) || !r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;

                    std::vector<QqMusicOnlineCollection> items;
                    int total = 0;
                    if (!qqmusic::api::SearchCollections(in->query, (int)in->kind, (int)in->page,
                                                         (int)in->page_size, items, total))
                        return OnlineRequestFailed;

                    const u32 max_items = r->recv_buffer.size / sizeof(QqMusicOnlineCollection);
                    const u32 count = (u32)std::min<size_t>(items.size(), max_items);
                    auto *dst = (QqMusicOnlineCollection *)r->recv_buffer.ptr;
                    for (u32 i = 0; i < count; i++) {
                        memcpy(&dst[i], &items[i], sizeof(QqMusicOnlineCollection));
                    }

                    const struct {
                        u32 count;
                        u32 total;
                    } out = { count, (u32)total };
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, &out, sizeof(out));
                    return 0;
                }

                case QqMusicIpcCmd_OnlineGetFavAlbums:
                case QqMusicIpcCmd_OnlineGetFavPlaylists: {
                    const struct {
                        u32 offset;
                        u32 count;
                    } *in = (const decltype(in))r->data.ptr;
                    if (r->data.size < sizeof(*in) || !r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;

                    std::vector<QqMusicOnlineCollection> items;
                    int total = 0;
                    if (r->data.cmdId == QqMusicIpcCmd_OnlineGetFavAlbums) {
                        if (in->offset > INT32_MAX || in->count == 0 ||
                            r->recv_buffer.size < sizeof(QqMusicOnlineCollection)) return badInput;
                        const u32 capacity = r->recv_buffer.size / sizeof(QqMusicOnlineCollection);
                        const int requested = (int)std::min<u32>(in->count, std::min<u32>(capacity, 50));
                        if (!qqmusic::api::GetFavAlbums((int)in->offset, requested, items, total))
                            return OnlineRequestFailed; // 服务端错误/半页，不是「本页为空」
                    } else {
                        if (in->offset > INT32_MAX) return badInput;
                        if (!qqmusic::api::GetFavPlaylists((int)in->offset, (int)in->count, items, total))
                            return OnlineRequestFailed;
                    }

                    const u32 max_items = r->recv_buffer.size / sizeof(QqMusicOnlineCollection);
                    const u32 count = (u32)std::min<size_t>(items.size(), max_items);
                    auto *dst = (QqMusicOnlineCollection *)r->recv_buffer.ptr;
                    for (u32 i = 0; i < count; i++) {
                        memcpy(&dst[i], &items[i], sizeof(QqMusicOnlineCollection));
                    }

                    const struct {
                        u32 count;
                        u32 total;
                    } out = { count, (u32)total };
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, &out, sizeof(out));
                    return 0;
                }

                case QqMusicIpcCmd_OnlineGetAlbumSongs:
                case QqMusicIpcCmd_OnlineGetSingerSongs:
                case QqMusicIpcCmd_OnlineGetPlaylistSongs: {
                    const struct {
                        u32 begin;
                        u32 num;
                        u64 disstid;
                        char mid[64];
                    } *in = (const decltype(in))r->data.ptr;
                    if (r->data.size < sizeof(*in) || in->begin > INT32_MAX ||
                        !r->recv_buffer.ptr || !r->recv_buffer.size)
                        return badInput;

                    std::vector<QqMusicOnlineSong> songs;
                    int total = 0;
                    bool ok = false;
                    if (r->data.cmdId == QqMusicIpcCmd_OnlineGetAlbumSongs) {
                        ok = qqmusic::api::GetAlbumSongs(in->mid, (int)in->begin, (int)in->num, songs, total);
                    } else if (r->data.cmdId == QqMusicIpcCmd_OnlineGetSingerSongs) {
                        ok = qqmusic::api::GetSingerSongs(in->mid, (int)in->begin, (int)in->num, songs, total);
                    } else {
                        ok = qqmusic::api::GetPlaylistSongs(in->disstid, (int)in->begin, (int)in->num, songs, total);
                    }
                    if (!ok)
                        return OnlineRequestFailed;

                    const u32 max_items = r->recv_buffer.size / sizeof(QqMusicOnlineSong);
                    const u32 count = (u32)std::min<size_t>(songs.size(), max_items);
                    auto *dst = (QqMusicOnlineSong *)r->recv_buffer.ptr;
                    for (u32 i = 0; i < count; i++) {
                        memcpy(&dst[i], &songs[i], sizeof(QqMusicOnlineSong));
                    }

                    const struct {
                        u32 count;
                        u32 total;
                    } out = { count, (u32)total };
                    *out_dataSize = sizeof(out);
                    memcpy(out_data, &out, sizeof(out));
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
            Result rc = ipcServerProcess(&g_server, ServiceHandlerFunc, nullptr);
            if (R_FAILED(rc)) {
                char b[96];
                snprintf(b, sizeof(b), "SYS ipc loop rc=0x%08x sessions=%u\n", (unsigned) rc, (unsigned) (g_server.count - 1));
                sysLog(b);
            }
            if (rc == KERNELRESULT(Cancelled))
                break;
        }
    }

} // namespace qqmusic