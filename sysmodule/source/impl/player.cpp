#include "player.hpp"

#include "../result.hpp"
#include "../service.hpp"
#include "sdmc/sdmc.hpp"
#include "pm/pm.hpp"
#include "aud_wrapper.h"
#include "config/config.hpp"
#include "source.hpp"
#include "songcache.hpp"
#include "resamplers/SDL_audioEX.h"

#include "meta.hpp"
#include "net/qqmusic_api.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <atomic>
#include <cmath>
#include <span>
#include <string>
#include <nxExt.h>
#include <sstream>

namespace qqmusic::impl {

    namespace {
        constexpr float VOLUME_MAX = 1.f;
        constexpr auto PLAYLIST_ENTRY_MAX = 300;

        struct PlaylistID {
            u32 id{UINT32_MAX};

            bool IsValid() const {
                return id != UINT32_MAX;
            }

            void Reset() {
                id = UINT32_MAX;
            }
        };

        class PlayList {
        public:
            void Init() {
                Clear();
                m_playlist.reserve(PLAYLIST_ENTRY_MAX);
                m_shuffle_playlist.reserve(PLAYLIST_ENTRY_MAX);
            }

            bool Add(const char* path, EnqueueType type) {
                u32 index;
                if (!FindNextFreeEntry(index)) {
                    return false;
                }

                if (!m_entries[index].Add(path)) {
                    return false;
                }

                if (type == EnqueueType::Front) {
                    m_playlist.emplace(m_playlist.cbegin(), index);
                } else {
                    m_playlist.emplace_back(index);
                }

                // add new entry id to shuffle_playlist_list
                const auto shuffle_playlist_size = m_shuffle_playlist.size() + 1;
                const auto shuffle_index = randomGet64() % shuffle_playlist_size;
                m_shuffle_playlist.emplace(m_shuffle_playlist.cbegin() + shuffle_index, index);

                return true;
            }

            bool Remove(u32 index, ShuffleMode shuffle) {
                const auto entry = Get(index, shuffle);
                R_UNLESS(entry.IsValid(), false);

                // 两个列表始终包含同一批 id：先各自定位，绝不在「找不到」时
                // 退化为第 0 项——那会删掉别的曲目并让两个列表永久错位。
                u32 plain_index = 0, shuffle_index = 0;
                R_UNLESS(IndexOfID(entry, ShuffleMode::Off, plain_index), false);
                R_UNLESS(IndexOfID(entry, ShuffleMode::On, shuffle_index), false);

                // remove entry.
                m_entries[entry.id].Remove();

                // remove from both playlists.
                m_playlist.erase(m_playlist.begin() + plain_index);
                m_shuffle_playlist.erase(m_shuffle_playlist.begin() + shuffle_index);

                return true;
            }

            bool Swap(u32 src, u32 dst, ShuffleMode shuffle) {
                if (src >= Size() || dst >= Size()) {
                    return false;
                }

                if (shuffle == ShuffleMode::On) {
                    std::swap(m_shuffle_playlist[src], m_shuffle_playlist[dst]);
                } else {
                    std::swap(m_playlist[src], m_playlist[dst]);
                }

                return true;
            }

            void Shuffle() {
                const auto size = m_shuffle_playlist.size();
                if (!size) {
                    return;
                }

                for (auto& e : m_shuffle_playlist) {
                    const auto index = randomGet64() % size;
                    std::swap(e, m_shuffle_playlist[index]);
                }
            }

            const char* GetPath(u32 index, ShuffleMode shuffle) const {
                return GetPath(Get(index, shuffle));
            }

            const char* GetPath(const PlaylistID& entry) const {
                R_UNLESS(entry.IsValid(), nullptr);

                return m_entries[entry.id].GetPath();
            }

            void Clear() {
                for (u32 i = 0; i < m_entries.size(); i++) {
                    m_entries[i].Remove();
                }

                m_playlist.clear();
                m_shuffle_playlist.clear();
            }

            u32 Size() const {
                return m_playlist.size();
            }

            PlaylistID Get(u32 index, ShuffleMode shuffle) const {
                if (index >= Size()) {
                    return {};
                }

                if (shuffle == ShuffleMode::On) {
                    return m_shuffle_playlist[index];
                } else {
                    return m_playlist[index];
                }
            }

            u32 GetIndexFromID(const PlaylistID& entry, ShuffleMode shuffle) const {
                if (!entry.IsValid()) {
                    return 0;
                }

                std::span list{m_playlist};
                if (shuffle == ShuffleMode::On) {
                    list = m_shuffle_playlist;
                }

                for (u32 i = 0; i < list.size(); i++) {
                    if (list[i].id == entry.id) {
                        return i;
                    }
                }

                return 0;
            }

            // 明确区分「找到」与「没找到」：GetIndexFromID 在缺失时返回 0，
            // 与「确实位于第 0 项」无法区分，所以删除路径必须用这个版本。
            bool IndexOfID(const PlaylistID& entry, ShuffleMode shuffle, u32 &out) const {
                if (!entry.IsValid()) {
                    return false;
                }

                std::span list{m_playlist};
                if (shuffle == ShuffleMode::On) {
                    list = m_shuffle_playlist;
                }

                for (u32 i = 0; i < list.size(); i++) {
                    if (list[i].id == entry.id) {
                        out = i;
                        return true;
                    }
                }

                return false;
            }

        private:
            bool FindNextFreeEntry(u32& index) const {
                for (u32 i = 0; i < m_entries.size(); i++) {
                    if (m_entries[i].IsEmpty()) {
                        index = i;
                        return true;
                    }
                }

                return false;
            }

        private:
            struct PlayListNameEntry {
            public:
                bool Add(const char* path) {
                    if (!IsEmpty()) {
                        return false;
                    }

                    if (std::strlen(path) >= sizeof(m_path)) {
                        return false;
                    }

                    std::strcpy(m_path, path);
                    return true;
                }

                bool Remove() {
                    m_path[0] = '\0';
                    return true;
                }

                bool IsEmpty() const {
                    return m_path[0] == '\0';
                }

                const char* GetPath() const {
                    return m_path;
                }

            private:
                char m_path[FS_MAX_PATH]{};
            };

        private:
            std::vector<PlaylistID> m_playlist{};
            std::vector<PlaylistID> m_shuffle_playlist{};
            std::array<PlayListNameEntry, PLAYLIST_ENTRY_MAX> m_entries{};
        };

        PlayList g_playlist;

        PlaylistID g_current;
        u32 g_queue_position;

        LockableMutex g_mutex;

        RepeatMode g_repeat   = RepeatMode::All;
        ShuffleMode g_shuffle = ShuffleMode::Off;
        PlayerStatus g_status = PlayerStatus::FetchNext;
        u64 g_track_generation = 0;
        std::atomic<u64> g_io_generation{0};
        bool g_source_ready = false;
        bool g_seek_pending = false;
        u32 g_seek_position = 0;
        CurrentStats g_progress{44100, 0, 0, 0};
        float g_title_volume = 1.f;
        float g_volume = 1.f;
        float g_default_title_volume = 1.f;
        bool g_use_title_volume = false;

        constexpr auto AUDIO_FREQ          = 48000;
        constexpr auto AUDIO_CHANNEL_COUNT = 2;
        constexpr auto AUDIO_BUFFER_COUNT  = 8;   // 8 个缓冲区，提供高达 680ms 的硬件抗抖动缓冲
        constexpr auto AUDIO_LATENCY_MS    = 85;
        constexpr auto AUDIO_BUFFER_SIZE   = AUDIO_FREQ / 1000 * AUDIO_LATENCY_MS * AUDIO_CHANNEL_COUNT;

        AudioOutBuffer g_audout_buffer[AUDIO_BUFFER_COUNT];
        alignas(0x1000) s16 AudioMemoryPool[AUDIO_BUFFER_COUNT][(AUDIO_BUFFER_SIZE + 0xFFF) & ~0xFFF];
        static_assert((sizeof(AudioMemoryPool[0]) % 0x2000) == 0, "Audio Memory pool needs to be page aligned!");

        // 暂停原因（位掩码）：手动 / 黑名单游戏 / 耳机拔出 —— 各原因独立，互不覆盖。
        enum class PauseReason : u32 {
            None      = 0,
            Manual    = 1u << 0,
            Blacklist = 1u << 1, // 前台游戏被禁用（占用音频会话 / 不兼容）
            Headphone = 1u << 2, // 耳机拔出
        };

        std::atomic<u32> g_pause_reasons{static_cast<u32>(PauseReason::Manual)};
        std::atomic<bool> g_audio_unavailable{false};
        std::atomic<bool> g_last_play_failed{false};
        std::atomic<bool> g_radio_mode{false};
        bool g_audout_init_ok = false;
        std::atomic<bool> g_should_run{true};

        inline bool has_pause_reason(PauseReason r) { return (g_pause_reasons.load() & (u32)r) != 0; }
        inline void add_pause_reason(PauseReason r) { g_pause_reasons.fetch_or((u32)r); }
        inline void clear_pause_reason(PauseReason r) { g_pause_reasons.fetch_and(~((u32)r)); }

        void DiscardAudio() {
            if (g_audout_init_ok) {
                audoutStopAudioOut();
                bool flushed;
                audoutFlushAudioOutBuffers(&flushed);
                AudioOutBuffer* released = nullptr;
                u32 count = 0;
                do {
                    if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &count)))
                        break;
                } while (count);
            }
        }

        // Called with g_mutex held. Invalidates opening/decoding work even if a
        // removed playlist slot is reused or the same row is selected again.
        void FetchNext() {
            ++g_track_generation;
            ++g_io_generation;
            g_status = PlayerStatus::FetchNext;
            g_current.Reset();
            g_source_ready = false;
            g_seek_pending = false;
            g_progress = {44100, 0, 0, 0};
        }

        bool IsCurrentTrack(u64 generation) {
            return g_should_run && g_status == PlayerStatus::Playing && generation == g_track_generation;
        }

        CurrentStats ReadProgress(Source &source) {
            const auto [current, total] = source.Tell();
            return {static_cast<u32>(source.GetSampleRate()), current, total, 0};
        }

        // Called with g_mutex held; automatic advancement never clears manual pause.
        void Advance(bool automatic) {
            const auto size = g_playlist.Size();
            if (!size) {
                g_queue_position = 0;
                add_pause_reason(PauseReason::Manual);
            } else if (!automatic || g_repeat != RepeatMode::One) {
                if (g_queue_position + 1 < size) {
                    ++g_queue_position;
                } else {
                    g_queue_position = 0;
                    if (automatic && g_repeat == RepeatMode::Off) {
                        if (!g_radio_mode.load())
                            add_pause_reason(PauseReason::Manual);
                    }
                }
            }
            FetchNext();
            config::set_queue_position(g_queue_position);
        }

        void SaveQueueToFile() {
            const auto size = g_playlist.Size();
            std::string content;
            for (size_t i = 0; i < size; i++) {
                const char *p = g_playlist.GetPath(i, ShuffleMode::Off);
                if (p && p[0]) {
                    TrackMeta meta{};
                    if (R_SUCCEEDED(GetTrackMeta(p, &meta)) && meta.title[0]) {
                        content += p;
                        content += "\t";
                        content += meta.title;
                        content += "\t";
                        content += meta.artist;
                        content += "\t";
                        content += meta.album;
                        content += "\n";
                    } else {
                        content += p;
                        content += "\n";
                    }
                }
            }
            sdmc::CreateFolder("/config");
            sdmc::CreateFolder("/config/qqmusic");
            sdmc::WriteFile("/config/qqmusic/queue.txt", content.data(), content.size());
            // 「跟随队列」缓存策略下，上限就是当前队列长度
            songcache::SetQueueSize((u32)size);
        }

        void LoadQueueFromFile() {
            FsFile f;
            if (R_SUCCEEDED(sdmc::OpenFile(&f, "/config/qqmusic/queue.txt", FsOpenMode_Read))) {
                s64 fsize = 0;
                fsFileGetSize(&f, &fsize);
                if (fsize > 0 && fsize < 1024 * 1024) {
                    std::string text((size_t)fsize, '\0');
                    u64 read_n = 0;
                    fsFileRead(&f, 0, text.data(), (u64)fsize, 0, &read_n);
                    std::istringstream ss(text);
                    std::string line;
                    while (std::getline(ss, line)) {
                        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
                            line.pop_back();
                        if (line.empty()) continue;

                        std::string path, title, artist, album;
                        auto t1 = line.find('\t');
                        if (t1 != std::string::npos) {
                            path = line.substr(0, t1);
                            auto t2 = line.find('\t', t1 + 1);
                            if (t2 != std::string::npos) {
                                title = line.substr(t1 + 1, t2 - t1 - 1);
                                auto t3 = line.find('\t', t2 + 1);
                                if (t3 != std::string::npos) {
                                    artist = line.substr(t2 + 1, t3 - t2 - 1);
                                    album = line.substr(t3 + 1);
                                } else {
                                    artist = line.substr(t2 + 1);
                                }
                            } else {
                                title = line.substr(t1 + 1);
                            }
                        } else {
                            path = line;
                        }

                        if (!path.empty()) {
                            if (!title.empty()) {
                                TrackMeta meta{};
                                snprintf(meta.title, sizeof(meta.title), "%s", title.c_str());
                                snprintf(meta.artist, sizeof(meta.artist), "%s", artist.c_str());
                                snprintf(meta.album, sizeof(meta.album), "%s", album.c_str());
                                SetTrackMetaCache(path.c_str(), meta);
                            }
                            g_playlist.Add(path.c_str(), EnqueueType::Back);
                        }
                    }
                }
                fsFileClose(&f);
            }
        }

        Result PlayTrack(const char* path, u64 generation, u64 io_generation) {
            R_UNLESS(!g_audio_unavailable, qqmusic::AudioUnavailable);
            SourceIoRequest request{g_io_generation, io_generation};
            auto source = OpenFile(path, request);
            // No decoder (including Tell/Seek) is ever accessed by the IPC
            // thread. Check identity after every operation that may do I/O.
            if (request.Cancelled()) return 0;
            R_UNLESS(source != nullptr && source->IsOpen(), qqmusic::FileOpenFailure);
            R_UNLESS(source->SetupResampler(audoutGetChannelCount(), audoutGetSampleRate()), qqmusic::VoiceInitFailure);
            const auto initial_progress = ReadProgress(*source);
            {
                std::scoped_lock lk(g_mutex);
                if (!IsCurrentTrack(generation)) return 0;
                g_source_ready = true;
                g_progress = initial_progress;
            }

            const auto play = [&]() -> Result {
                bool first = true;
                bool finished = false;
                while (g_should_run) {
                    AudioOutBuffer* buffer = nullptr;
                    bool seek = false;
                    u32 seek_position = 0;
                    {
                        std::scoped_lock lk(g_mutex);
                        if (!IsCurrentTrack(generation)) break;
                        request.expected = g_io_generation.load();
                        if (g_seek_pending) {
                            seek = true;
                            seek_position = g_seek_position;
                            g_seek_pending = false;
                        } else {
                            AudioOutState state;
                            R_TRY(audoutGetAudioOutState(&state));
                            if (g_pause_reasons != 0) {
                                if (state == AudioOutState_Started)
                                    R_TRY(audoutStopAudioOut());
                            } else {
                                if (state == AudioOutState_Stopped)
                                    R_TRY(audoutStartAudioOut());
                                AudioOutBuffer* released = nullptr;
                                u32 released_count = 0;
                                R_TRY(audoutGetReleasedAudioOutBuffer(&released, &released_count));
                                bool queued = false;
                                for (int i = 0; i < AUDIO_BUFFER_COUNT; ++i) {
                                    bool contains = false;
                                    R_TRY(audoutContainsAudioOutBuffer(&g_audout_buffer[i], &contains));
                                    queued |= contains;
                                    if (!contains && !buffer)
                                        buffer = &g_audout_buffer[i];
                                }
                                if (finished) {
                                    if (!queued) {
                                        Advance(true);
                                        break;
                                    }
                                    buffer = nullptr;
                                }
                            }
                        }
                    }

                    if (seek) {
                        source->Seek(seek_position);
                        // Even a failed/interrupted seek may move the decoder.
                        // Never retain pre-seek PCM in the resampler.
                        const bool ready = source->SetupResampler(audoutGetChannelCount(), audoutGetSampleRate());
                        const auto progress = ReadProgress(*source);
                        std::scoped_lock lk(g_mutex);
                        if (!IsCurrentTrack(generation)) break;
                        if (request.Cancelled()) continue;
                        R_UNLESS(ready, qqmusic::VoiceInitFailure);
                        g_progress = progress;
                        finished = false;
                        first = true;
                    } else if (buffer) {
                        // Only this thread writes PCM. The selected buffer is
                        // not owned by audout; control requests can flush, but
                        // cannot submit/reuse it while decoding is in flight.
                        const auto buffer_size = first
                            ? std::min(512 * sizeof(s16), AUDIO_BUFFER_SIZE * sizeof(s16))
                            : AUDIO_BUFFER_SIZE * sizeof(s16);
                        const auto bytes = source->Resample((u8*)buffer->buffer, buffer_size);
                        const auto progress = ReadProgress(*source);
                        std::scoped_lock lk(g_mutex);
                        if (!IsCurrentTrack(generation)) break;
                        if (request.Cancelled()) continue;
                        R_UNLESS(bytes >= 0, qqmusic::FileOpenFailure);
                        g_progress = progress;
                        first = false;
                        if (bytes) {
                            // Pause during a slow read must retain its decoded
                            // PCM without allowing stopped output to restart.
                            if (g_pause_reasons != 0)
                                R_TRY(audoutStopAudioOut());
                            if (g_volume != 1.f) {
                                auto samples = static_cast<s16*>(buffer->buffer);
                                for (size_t i = 0; i < size_t(bytes) / sizeof(s16); ++i)
                                    samples[i] = static_cast<s16>(samples[i] * g_volume);
                            }
                            buffer->data_size = bytes;
                            R_TRY(audoutAppendAudioOutBuffer(buffer));
                        } else {
                            finished = true;
                        }
                    }
                    svcSleepThread(5'000'000);
                }
                return 0;
            };

            for (;;) {
                const Result result = play();
                std::scoped_lock lk(g_mutex);
                // A seek may arrive between an error and acquiring this lock.
                // Consume it with the same decoder instead of reporting an old
                // read failure against the newer request.
                if (IsCurrentTrack(generation) && request.Cancelled())
                    continue;
                // There is only one playback thread; no new source has begun
                // submitting yet, so returning its old audout buffers is safe.
                DiscardAudio();
                if (IsCurrentTrack(generation)) {
                    g_source_ready = false;
                    if (R_FAILED(result) && result != qqmusic::FileOpenFailure)
                        g_audio_unavailable = true;
                }
                return result;
            }
        }

    }

    Result Initialize() {
        // 本地歌曲缓存：建目录、清半成品、按上限淘汰（断网可放 + 减少断流）。
        qqmusic::songcache::Init();
        MetaInit(); // 元数据互斥量：必须早于 IPC / 播放 / 封面三条线程启动

        for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
            g_audout_buffer[i].buffer = AudioMemoryPool[i];
            g_audout_buffer[i].buffer_size = sizeof(AudioMemoryPool[i]);
        }

        // 音频会话可能被游戏占满（AudioRenderer 2 会话上限）→ 不崩溃，
        // 标记"音频不可用"，播放线程周期重试，UI 显式提示。
        if (R_SUCCEEDED(audoutInitialize())) {
            g_audout_init_ok = true;
            audoutSetAudioOutVolume(1.f);
        } else {
            g_audio_unavailable = true;
        }

        g_volume = std::clamp(config::get_volume(), 0.f, VOLUME_MAX);
        if (!std::isfinite(g_volume))
            g_volume = 1.f;
        /* Fetch values from config, sanitize the return value */
        if (auto c = config::get_repeat(); c <= 2 && c >= 0) {
            SetRepeatMode(static_cast<RepeatMode>(c));
        }

        SetShuffleMode(static_cast<ShuffleMode>(config::get_shuffle()));
        SetDefaultTitleVolume(config::get_default_title_volume());

        // reserves memory so that we don't allocate later on.
        g_playlist.Init();

        // 恢复关机前记忆的播放列表与播放进度
        LoadQueueFromFile();
        songcache::SetQueueSize((u32)g_playlist.Size());
        if (g_playlist.Size()) {
            const u32 saved_pos = config::get_queue_position();
            g_queue_position = (saved_pos < g_playlist.Size()) ? saved_pos : 0;
            g_current = g_playlist.Get(g_queue_position, g_shuffle);
            g_status = PlayerStatus::FetchNext;
            add_pause_reason(PauseReason::Manual); // 开机保持就绪待播
        }

        return 0;

    }

    void Exit() {
        g_should_run = false;
        ++g_io_generation;
    }

    void TuneThreadFunc(void *) {

        /* Run as long as we aren't stopped and no error has been encountered. */
        while (g_should_run) {
            /* 音频输出不可用：不消费队列，周期重试（游戏可能释放会话）。 */
            if (g_audio_unavailable) {
                {
                    std::scoped_lock lk(g_mutex);
                    if (!g_audout_init_ok) {
                        if (R_SUCCEEDED(audoutInitialize())) {
                            g_audout_init_ok = true;
                            g_audio_unavailable = false;
                            audoutSetAudioOutVolume(1.f);
                        }
                    } else if (R_SUCCEEDED(audoutStartAudioOut())) {
                        g_audio_unavailable = false;
                    }
                }
                svcSleepThread(2'000'000'000ul);
                continue;
            }

            std::string current_path;
            u64 generation = 0;
            u64 io_generation = 0;
            {
                std::scoped_lock lk(g_mutex);
                g_current.Reset();
                generation = g_track_generation;
                io_generation = g_io_generation.load();
                const auto queue_size = g_playlist.Size();
                if (queue_size && !has_pause_reason(PauseReason::Manual)) {
                    if (g_queue_position >= queue_size)
                        g_queue_position = queue_size - 1;
                    g_current = g_playlist.Get(g_queue_position, g_shuffle);
                    current_path = g_playlist.GetPath(g_current);
                    g_status = PlayerStatus::Playing;
                }
            }

            // 电台模式（猜你喜欢）：快播完当前队列时自动拉取新一波推荐曲目
            if (g_radio_mode.load()) {
                u32 qsize = 0, qpos = 0;
                {
                    std::scoped_lock lk(g_mutex);
                    qsize = g_playlist.Size();
                    qpos = g_queue_position;
                }
                if (qpos + 2 >= qsize) {
                    static u32 s_guess_page = 2;
                    std::vector<QqMusicOnlineSong> batch;
                    if (qqmusic::api::GetGuessRecommend(s_guess_page, 2, batch)) {
                        s_guess_page += 2;
                        std::scoped_lock lk(g_mutex);
                        if (!g_radio_mode.load() || generation != g_track_generation)
                            continue;
                        for (const auto &song : batch) {
                            char path[FS_MAX_PATH];
                            if (song.album_mid[0])
                                 snprintf(path, sizeof(path), "qqm://%s/%s/%s", song.songmid, song.media_mid[0] ? song.media_mid : song.songmid, song.album_mid);
                            else
                                 snprintf(path, sizeof(path), "qqm://%s/%s", song.songmid, song.media_mid[0] ? song.media_mid : song.songmid);
                            TrackMeta m{};
                            snprintf(m.title, sizeof(m.title), "%s", song.title);
                            snprintf(m.artist, sizeof(m.artist), "%s", song.artist);
                            snprintf(m.album, sizeof(m.album), "%s", song.album);
                            SetTrackMetaCache(path, m);
                            g_playlist.Add(path, EnqueueType::Back);
                        }
                        SaveQueueToFile();
                    }
                }
            }

            if (current_path.empty()) {
                svcSleepThread(100'000'000);
                continue;
            }

            static u32 s_consecutive_online_failures = 0;
            Result rc = PlayTrack(current_path.c_str(), generation, io_generation);

            // 在线曲目若首次播放失败，在后台自动重试最多 2 次（间隔 400ms），
            // 解决 WiFi 偶发握手/解析延迟导致进新歌立马暂停的问题。
            const bool is_online = (current_path.rfind("qqm://", 0) == 0 ||
                                    current_path.rfind("http://", 0) == 0 ||
                                    current_path.rfind("https://", 0) == 0);
            if (R_FAILED(rc) && is_online && !g_audio_unavailable) {
                for (int attempt = 0; attempt < 2; ++attempt) {
                    svcSleepThread(400'000'000ull);
                    u64 cur_gen = 0, cur_io = 0;
                    {
                        std::scoped_lock lk(g_mutex);
                        if (!IsCurrentTrack(generation)) break;
                        cur_gen = g_track_generation;
                        cur_io = g_io_generation.load();
                    }
                    rc = PlayTrack(current_path.c_str(), cur_gen, cur_io);
                    if (R_SUCCEEDED(rc)) break;
                }
            }

            std::scoped_lock lk(g_mutex);
            // An obsolete open/read result must not pause, remove or report an
            // error against the new selection, including reused playlist IDs.
            if (!IsCurrentTrack(generation)) continue;
            if (R_FAILED(rc) && !g_audio_unavailable) {
                g_last_play_failed = true;
                if (is_online) {
                    s_consecutive_online_failures++;
                    const auto qsize = g_playlist.Size();
                    // 若重试后该单曲依然不可播（如无版权/VIP），且队列有多首歌曲，
                    // 自动跳到下一首播放，绝不让整条队列停滞；只有连续 3 首歌曲全部失败（证明断网）才自动暂停。
                    if (qsize > 1 && s_consecutive_online_failures < 3 && s_consecutive_online_failures < qsize) {
                        Advance(true);
                    } else {
                        add_pause_reason(PauseReason::Manual);
                        FetchNext();
                    }
                } else {
                    g_playlist.Remove(g_queue_position, g_shuffle);
                    if (g_queue_position >= g_playlist.Size())
                        g_queue_position = 0;
                    if (!g_playlist.Size())
                        add_pause_reason(PauseReason::Manual);
                    FetchNext();
                }
            } else if (R_SUCCEEDED(rc)) {
                g_last_play_failed = false;
                s_consecutive_online_failures = 0;
            }
        }
        if (g_audout_init_ok) {
            DiscardAudio();
            audoutExit();
            g_audout_init_ok = false;
        }

    }

    void GpioThreadFunc(void *ptr) {
        GpioPadSession *session = static_cast<GpioPadSession *>(ptr);

        /* [0] Low == plugged in; [1] High == not plugged in. */
        GpioValue old_value = GpioValue_High;

        while (g_should_run) {
            /* Fetch current gpio value. */
            GpioValue value;
            if (R_SUCCEEDED(gpioPadGetValue(session, &value))) {
                if (old_value == GpioValue_Low && value == GpioValue_High) {
                    add_pause_reason(PauseReason::Headphone);
                } else if (old_value == GpioValue_High && value == GpioValue_Low) {
                    clear_pause_reason(PauseReason::Headphone);
                }
                old_value = value;
            }
            svcSleepThread(10'000'000);
        }
    }

    void PmdmntThreadFunc(void *) {
        u64 last_pid = 0;
        u64 last_tid = 0;
        while (g_should_run) {
            u64 pid = 0, tid = 0;
            const bool active = pm::GetActiveApp(&pid, &tid);
            {
                std::scoped_lock lk(g_mutex);
                if (active && (tid != last_tid || pid != last_pid)) {
                    last_pid = pid;
                    last_tid = tid;
                    const bool enabled = config::has_title_enabled(tid)
                        ? config::get_title_enabled(tid)
                        : config::get_title_enabled_default();
                    if (enabled)
                        clear_pause_reason(PauseReason::Blacklist);
                    else
                        add_pause_reason(PauseReason::Blacklist);
                    g_use_title_volume = config::has_title_volume(tid);
                    g_title_volume = g_use_title_volume
                        ? std::clamp(config::get_title_volume(tid), 0.f, VOLUME_MAX)
                        : g_default_title_volume;
                    if (!std::isfinite(g_title_volume))
                        g_title_volume = 1.f;
                } else if (!active && last_tid != 0) {
                    last_pid = 0;
                    last_tid = 0;
                    g_use_title_volume = false;
                    clear_pause_reason(PauseReason::Blacklist);
                }
                // Titles open their audio services after the process appears;
                // reapply so new sessions receive the current game volume.
                if (last_pid) {
                    const auto volume = g_use_title_volume ? g_title_volume : g_default_title_volume;
                    audWrapperSetProcessMasterVolume(last_pid, 0, volume);
                }
            }
            svcSleepThread(10'000'000);
        }
    }

    bool GetStatus() {
        std::scoped_lock lk(g_mutex);
        return g_playlist.Size() != 0 && g_pause_reasons == 0 && !g_audio_unavailable;
    }

    bool GetAudioUnavailable() {
        return g_audio_unavailable;
    }

    bool GetBlacklistPaused() {
        return has_pause_reason(PauseReason::Blacklist);
    }

    bool GetLastPlayError() {
        return g_last_play_failed.load();
    }

    bool GetRadioMode() {
        return g_radio_mode.load();
    }

    void SetRadioMode(bool enabled) {
        std::scoped_lock lk(g_mutex);
        g_radio_mode.store(enabled);
        if (enabled) {
            g_repeat = RepeatMode::Off;
            g_shuffle = ShuffleMode::Off;
        }
    }

    void Play() {
        std::scoped_lock lk(g_mutex);
        g_last_play_failed = false;
        if (g_playlist.Size()) {
            // An explicit play acknowledges unplugging and permits speakers.
            g_pause_reasons.fetch_and(~((u32)PauseReason::Manual | (u32)PauseReason::Headphone));
        }
    }

    void Pause() {
        add_pause_reason(PauseReason::Manual);
    }

    void Next() {
        std::scoped_lock lk(g_mutex);
        Advance(false);
        clear_pause_reason(PauseReason::Manual);
        g_last_play_failed = false;
        DiscardAudio();
    }

    void Prev() {
        std::scoped_lock lk(g_mutex);
        const auto size = g_playlist.Size();
        if (!size) {
            g_queue_position = 0;
            add_pause_reason(PauseReason::Manual);
        } else {
            g_queue_position = g_queue_position ? g_queue_position - 1 : size - 1;
        }
        clear_pause_reason(PauseReason::Manual);
        g_last_play_failed = false;
        FetchNext();
        DiscardAudio();
    }

    float GetVolume() {
        std::scoped_lock lk(g_mutex);
        return g_volume;
    }

    void SetVolume(float volume) {
        if (!std::isfinite(volume))
            return;
        volume = std::clamp(volume, 0.f, VOLUME_MAX);
        std::scoped_lock lk(g_mutex);
        g_volume = volume;
        config::set_volume(volume);
    }

    float GetTitleVolume() {
        std::scoped_lock lk(g_mutex);
        return g_use_title_volume ? g_title_volume : g_default_title_volume;
    }

    void SetTitleVolume(float volume) {
        if (!std::isfinite(volume))
            return;
        std::scoped_lock lk(g_mutex);
        volume = std::clamp(volume, 0.f, VOLUME_MAX);
        g_title_volume = volume;
        g_use_title_volume = true;
        u64 pid = 0, tid = 0;
        if (pm::GetActiveApp(&pid, &tid))
            audWrapperSetProcessMasterVolume(pid, 0, volume);
    }

    void SetTitleEnabled(u64 tid, bool enabled) {
        std::scoped_lock lk(g_mutex);
        config::set_title_enabled(tid, enabled);
        // 立即生效：当前前台应用恰为该 title 时直接改暂停原因
        u64 pid = 0, cur = 0;
        if (pm::GetActiveApp(&pid, &cur) && cur == tid) {
            if (enabled)
                clear_pause_reason(PauseReason::Blacklist);
            else
                add_pause_reason(PauseReason::Blacklist);
        }
    }

    void SetTitleEnabledDefault(bool enabled) {
        std::scoped_lock lk(g_mutex);
        config::set_title_enabled_default(enabled);
        // 当前前台应用若未设每 title 覆盖，则跟随默认值
        u64 pid = 0, cur = 0;
        if (pm::GetActiveApp(&pid, &cur) && !config::has_title_enabled(cur)) {
            if (enabled)
                clear_pause_reason(PauseReason::Blacklist);
            else
                add_pause_reason(PauseReason::Blacklist);
        }
    }

    float GetDefaultTitleVolume() {
        std::scoped_lock lk(g_mutex);
        return g_default_title_volume;
    }

    void SetDefaultTitleVolume(float volume) {
        if (!std::isfinite(volume))
            return;
        std::scoped_lock lk(g_mutex);
        volume = std::clamp(volume, 0.f, VOLUME_MAX);
        g_default_title_volume = volume;
        // Apply the global slider to this session too; title changes reload
        // persisted per-title overrides.
        g_use_title_volume = false;
        u64 pid = 0, tid = 0;
        if (pm::GetActiveApp(&pid, &tid))
            audWrapperSetProcessMasterVolume(pid, 0, volume);
        config::set_default_title_volume(volume);
    }

    RepeatMode GetRepeatMode() {
        std::scoped_lock lk(g_mutex);
        return g_repeat;
    }

    void SetRepeatMode(RepeatMode mode) {
        std::scoped_lock lk(g_mutex);
        g_repeat = mode;
        config::set_repeat(static_cast<int>(mode));
    }

    ShuffleMode GetShuffleMode() {
        std::scoped_lock lk(g_mutex);
        return g_shuffle;
    }

    void SetShuffleMode(ShuffleMode mode) {
        std::scoped_lock lk(g_mutex);
        const auto selected = g_playlist.Get(g_queue_position, g_shuffle);

        // if we just enabled shuffle mode, re-shuffle the playlist.
        if (g_shuffle == ShuffleMode::Off && mode == ShuffleMode::On) {
            g_playlist.Shuffle();
        }

        g_shuffle = mode;
        if (selected.IsValid())
            g_queue_position = g_playlist.GetIndexFromID(selected, mode);
        config::set_shuffle(mode == ShuffleMode::On);
        config::set_queue_position(g_queue_position);
    }

    u32 GetPlaylistSize() {
        std::scoped_lock lk(g_mutex);

        return g_playlist.Size();
    }

    Result GetPlaylistItem(u32 index, char *buffer, size_t buffer_size) {
        std::scoped_lock lk(g_mutex);

        const auto path = g_playlist.GetPath(index, g_shuffle);
        R_UNLESS(path, qqmusic::OutOfRange);

        const auto length = std::strlen(path);
        R_UNLESS(buffer && buffer_size > length, qqmusic::InvalidArgument);
        std::memcpy(buffer, path, length + 1);

        return 0;
    }

    Result GetCurrentQueueItem(CurrentStats *out, char *buffer, size_t buffer_size) {
        std::scoped_lock lk(g_mutex);
        R_UNLESS(out, qqmusic::InvalidArgument);
        R_UNLESS(g_playlist.Size() > 0, qqmusic::NotPlaying);

        auto cur_id = g_current.IsValid() ? g_current : g_playlist.Get(g_queue_position, g_shuffle);
        const auto path = g_playlist.GetPath(cur_id);
        R_UNLESS(path && path[0], qqmusic::NotPlaying);

        const auto length = std::strlen(path);
        R_UNLESS(buffer && buffer_size > length, qqmusic::InvalidArgument);
        std::memcpy(buffer, path, length + 1);

        *out = g_progress;
        out->index = g_playlist.GetIndexFromID(cur_id, g_shuffle);

        return 0;
    }

    void ClearQueue() {
        std::scoped_lock lk(g_mutex);
        g_playlist.Clear();
        g_queue_position = 0;
        FetchNext();
        add_pause_reason(PauseReason::Manual);
        DiscardAudio();
        sdmc::DeleteFile("/config/qqmusic/queue.txt");
        config::set_queue_position(0);
        songcache::SetQueueSize(0);
    }

    void MoveQueueItem(u32 src, u32 dst) {
        std::scoped_lock lk(g_mutex);

        if (!g_playlist.Swap(src, dst, g_shuffle)) {
            return;
        }

        if (g_queue_position == src) {
            g_queue_position = dst;
        } else if (g_queue_position == dst) {
            g_queue_position = src;
        }
        SaveQueueToFile();
        config::set_queue_position(g_queue_position);
    }

    void Select(u32 index) {
        std::scoped_lock lk(g_mutex);
        if (index >= g_playlist.Size())
            return;
        g_queue_position = index;
        FetchNext();
        g_pause_reasons.fetch_and(~((u32)PauseReason::Manual | (u32)PauseReason::Headphone));
        DiscardAudio();
        g_last_play_failed = false;
        config::set_queue_position(g_queue_position);
    }

    void Seek(u32 position) {
        std::scoped_lock lk(g_mutex);
        if (g_status != PlayerStatus::Playing || !g_source_ready)
            return;
        g_seek_position = std::min(position, g_progress.total_frames);
        g_seek_pending = true;
        ++g_io_generation;
        DiscardAudio();
    }

    Result Enqueue(const char *buffer, size_t buffer_length, EnqueueType type, bool save) {
        R_UNLESS(buffer && buffer_length && buffer_length < FS_MAX_PATH, qqmusic::InvalidPath);
        R_UNLESS(type == EnqueueType::Front || type == EnqueueType::Back, qqmusic::InvalidArgument);
        // The length excludes NUL; do not run string APIs on an unbounded input.
        R_UNLESS(!std::memchr(buffer, '\0', buffer_length), qqmusic::InvalidPath);
        char path[FS_MAX_PATH];
        std::memcpy(path, buffer, buffer_length);
        path[buffer_length] = '\0';
        if (GetSourceType(path) == SourceType::NONE)
            return qqmusic::InvalidPath;

        const bool is_online = (std::strncmp(path, "qqm://", 6) == 0 ||
                                std::strncmp(path, "http://", 7) == 0 ||
                                std::strncmp(path, "https://", 8) == 0);

        if (!is_online && !sdmc::FileExists(path))
            return qqmusic::InvalidPath;
        std::scoped_lock lk(g_mutex);
        const auto selected = g_playlist.Get(g_queue_position, g_shuffle);

        if (!g_playlist.Add(path, type)) {
            return qqmusic::OutOfMemory;
        }

        if (selected.IsValid())
            g_queue_position = g_playlist.GetIndexFromID(selected, g_shuffle);

        if (save) {
            SaveQueueToFile();
            config::set_queue_position(g_queue_position);
        }
        return 0;
    }

    void SaveQueue() {
        std::scoped_lock lk(g_mutex);
        SaveQueueToFile();
        config::set_queue_position(g_queue_position);
    }

    Result Remove(u32 index) {
        std::scoped_lock lk(g_mutex);

        /* Ensure we don't operate out of bounds. */
        R_UNLESS(g_playlist.Size(), qqmusic::QueueEmpty);
        const auto removed = g_playlist.Get(index, g_shuffle);

        if (!g_playlist.Remove(index, g_shuffle)) {
            return qqmusic::OutOfRange;
        }

        /* Fetch a new track if we deleted the current song. */
        const bool fetch_new = g_queue_position == index;

        /* Lower current position if needed. */
        if (g_queue_position > index) {
            g_queue_position--;
        }

        if (g_queue_position >= g_playlist.Size())
            g_queue_position = 0;
        if (removed.id == g_current.id)
            g_current.Reset();
        if (!g_playlist.Size())
            add_pause_reason(PauseReason::Manual);
        if (fetch_new) {
            FetchNext();
            DiscardAudio();
        }
        SaveQueueToFile();
        config::set_queue_position(g_queue_position);
        return 0;
    }

}
