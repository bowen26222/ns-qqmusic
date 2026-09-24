#include "source.hpp"

#include "songcache.hpp"
#include "sdmc/sdmc.hpp"

#include <cstring>
#include <strings.h>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <sys/select.h>
#include <curl/curl.h>
#include "net/http.hpp"
#include "net/qqmusic_api.hpp"
extern "C" void sysLog(const char *s);
// ----- byte backends behind Source -----
namespace {

    class FileBackend final : public IoBackend {
        FsFile m_file;
      public:
        FileBackend(FsFile &&file) : m_file(file) {}
        ~FileBackend() override { fsFileClose(&m_file); }
        u64 read(s64 offset, void *buf, u64 size) override {
            u64 br = 0;
            if (R_FAILED(fsFileRead(&m_file, offset, buf, size, 0, &br))) return 0;
            return br;
        }
        s64 size() override {
            s64 sz = 0;
            if (R_FAILED(fsFileGetSize(&m_file, &sz))) return 0;
            return sz;
        }
    };


    class HttpBackend final : public IoBackend {
        std::string m_url;
        s64 m_total_size = 0;
        CURL *m_curl = nullptr;
        const SourceIoRequest &m_request;

        static int ProgressCb(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            return static_cast<HttpBackend *>(userdata)->m_request.Cancelled() ? 1 : 0;
        }

        struct WriteContext {
            u8 *dst;
            u64 max_bytes;
            u64 written;
        };

        static size_t WriteCb(void *ptr, size_t size, size_t nmemb, void *userdata) {
            auto *ctx = static_cast<WriteContext *>(userdata);
            size_t total = size * nmemb;
            size_t to_copy = std::min<size_t>(total, ctx->max_bytes - ctx->written);
            if (to_copy > 0) {
                std::memcpy(ctx->dst + ctx->written, ptr, to_copy);
                ctx->written += to_copy;
            }
            return total;
        }
        static size_t HeaderCb(char *buffer, size_t size, size_t nitems, void *userdata) {
            size_t total = size * nitems;
            auto *self = static_cast<HttpBackend *>(userdata);
            std::string line(buffer, total);
            auto p = line.find("Content-Range:");
            if (p == std::string::npos) p = line.find("content-range:");
            if (p != std::string::npos) {
                auto slash = line.find('/', p);
                if (slash != std::string::npos) {
                    s64 sz = std::atoll(line.c_str() + slash + 1);
                    if (sz > 0) self->m_total_size = sz;
                }
            }
            return total;
        }

      public:
        HttpBackend(const std::string &url, const SourceIoRequest &request) : m_url(url), m_request(request) {
            if (m_request.Cancelled()) return;
            qqmusic::net::Init();
            m_curl = curl_easy_init();
            if (m_curl) {
                curl_easy_setopt(m_curl, CURLOPT_URL, m_url.c_str());
                curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, 0L);
                curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
                // 强制 HTTP/1.1：与播放线程栈预算匹配（见 main.cpp 播放线程栈说明）。
                curl_easy_setopt(m_curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
                // 掌机弱网环境连接建立放宽至 10s
                curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, 10L);
                curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 20L);
                curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
                curl_easy_setopt(m_curl, CURLOPT_REFERER, "https://y.qq.com/");
                curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPALIVE, 1L);
                curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPIDLE, 30L);
                curl_easy_setopt(m_curl, CURLOPT_TCP_KEEPINTVL, 15L);
                curl_easy_setopt(m_curl, CURLOPT_NOPROGRESS, 0L);
                curl_easy_setopt(m_curl, CURLOPT_XFERINFOFUNCTION, ProgressCb);
                curl_easy_setopt(m_curl, CURLOPT_XFERINFODATA, this);

                u8 dummy = 0;
                WriteContext dummy_ctx{ &dummy, 1, 0 };
                curl_easy_setopt(m_curl, CURLOPT_RANGE, "0-0");
                curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, WriteCb);
                curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &dummy_ctx);
                curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, HeaderCb);
                curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, this);
                CURLcode res = curl_easy_perform(m_curl);
                if (res != CURLE_OK && !m_request.Cancelled()) {
                    svcSleepThread(200'000'000ull);
                    if (!m_request.Cancelled())
                        res = curl_easy_perform(m_curl);
                }
                curl_easy_setopt(m_curl, CURLOPT_HEADERFUNCTION, nullptr);
                curl_easy_setopt(m_curl, CURLOPT_HEADERDATA, nullptr);

                if (m_total_size == 0) {
                    curl_off_t cl = 0;
                    curl_easy_getinfo(m_curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
                    if (cl > 0) m_total_size = cl;
                }
                char b[128];
                std::snprintf(b, sizeof(b), "SYS HttpBackend init: res=%d size=%lld\n", (int)res, (long long)m_total_size);
                sysLog(b);
            }
        }

        ~HttpBackend() override {
            if (m_curl) {
                curl_easy_cleanup(m_curl);
                m_curl = nullptr;
            }
        }

        u64 read(s64 offset, void *buf, u64 size) override {
            if (!m_curl || size == 0 || m_request.Cancelled()) return 0;
            if (m_total_size > 0 && offset >= m_total_size) return 0;

            s64 end_byte = offset + size - 1;
            if (m_total_size > 0 && end_byte >= m_total_size) {
                end_byte = m_total_size - 1;
            }
            if (end_byte < offset) return 0;

            char range_hdr[64];
            std::snprintf(range_hdr, sizeof(range_hdr), "%lld-%lld", (long long)offset, (long long)end_byte);
            // 弱网抗掉包重试：单个音频分段若因 WiFi 瞬时掉包失败，最多重试 3 次（带渐进等待），
            // 绝不因单个 TCP 分段失败将正在播放的歌曲判定为 EOF 强行截断。
            for (int attempt = 0; attempt < 3; ++attempt) {
                WriteContext ctx{ static_cast<u8 *>(buf), size, 0 };
                curl_easy_setopt(m_curl, CURLOPT_RANGE, range_hdr);
                curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, WriteCb);
                curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &ctx);

                CURLcode res = curl_easy_perform(m_curl);
                if (m_request.Cancelled()) return 0;
                if (ctx.written > 0) return ctx.written;
                if (res == CURLE_OK) return 0; // 真正的正常 EOF

                svcSleepThread(150'000'000ull * (attempt + 1));
                if (m_request.Cancelled()) return 0;
            }
            return 0;
        }
        s64 size() override {
            return m_total_size;
        }
    };


}

std::unique_ptr<IoBackend> MakeFileBackend(FsFile &&file) { return std::make_unique<FileBackend>(std::move(file)); }
std::unique_ptr<IoBackend> MakeHttpBackend(const std::string &url, const SourceIoRequest &request) {
    return std::make_unique<HttpBackend>(url, request);
}
// NOTE: when updating dr_libs, check for TUNE-FIX comment for patches.
#ifdef WANT_FLAC
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_STDIO
#include "dr_flac.h"
#endif

#ifdef WANT_MP3
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DRMP3_DATA_CHUNK_SIZE DRMP3_MIN_DATA_CHUNK_SIZE
#include "dr_mp3.h"
#endif

#ifdef WANT_WAV
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "dr_wav.h"
#endif

namespace {

    enum SeekOrigin {
        SeekOrigin_SET,
        SeekOrigin_CUR,
        SeekOrigin_END
    };

    size_t ReadCallback(void *pUserData, void *pBufferOut, size_t bytesToRead) {
        auto data = static_cast<Source *>(pUserData);

        return data->ReadFile(pBufferOut, bytesToRead);
    }

#ifdef WANT_FLAC
    drflac_bool32 FlacSeekCallback(void *pUserData, int offset, drflac_seek_origin origin) {
        auto data = static_cast<Source *>(pUserData);

        return data->SeekFile(offset, origin);
    }

    drflac_bool32 FlacTellCallback(void *pUserData, drflac_int64* pCursor) {
        auto data = static_cast<Source *>(pUserData);

        *pCursor = data->TellFile();
        return true;
    }
#endif

#ifdef WANT_MP3
    drmp3_bool32 Mp3SeekCallback(void *pUserData, int offset, drmp3_seek_origin origin) {
        auto data = static_cast<Source *>(pUserData);

        return data->SeekFile(offset, origin);
    }

    drmp3_bool32 Mp3TellCallback(void *pUserData, drmp3_int64* pCursor) {
        auto data = static_cast<Source *>(pUserData);

        *pCursor = data->TellFile();
        return true;
    }
#endif

#ifdef WANT_WAV
    drwav_bool32 WavSeekCallback(void *pUserData, int offset, drwav_seek_origin origin) {
        auto data = static_cast<Source *>(pUserData);

        return data->SeekFile(offset, origin);
    }

    drwav_bool32 WavTellCallback(void *pUserData, drwav_int64* pCursor) {
        auto data = static_cast<Source *>(pUserData);

        *pCursor = data->TellFile();
        return true;
    }
#endif

#ifdef DEBUG
    void *log_malloc(size_t sz, void *) {
        std::printf("malloc: 0x%lX\n", sz);
        return malloc(sz);
    }
    void *log_realloc(void *p, size_t sz, void *) {
        std::printf("realloc: %p, 0x%lX\n", p, sz);
        return realloc(p, sz);
    }
    void log_free(void *p, void *) {
        std::printf("free: %p\n", p);
        free(p);
    }
    constexpr const drflac_allocation_callbacks flac_alloc = {
        .onMalloc  = log_malloc,
        .onRealloc = log_realloc,
        .onFree    = log_free,
    };
    constexpr const drmp3_allocation_callbacks mp3_alloc = {
        .onMalloc  = log_malloc,
        .onRealloc = log_realloc,
        .onFree    = log_free,
    };
    constexpr const drwav_allocation_callbacks wav_alloc = {
        .onMalloc  = log_malloc,
        .onRealloc = log_realloc,
        .onFree    = log_free,
    };
    constexpr const drflac_allocation_callbacks *flac_alloc_ptr = &flac_alloc;
    constexpr const drmp3_allocation_callbacks *mp3_alloc_ptr   = &mp3_alloc;
    constexpr const drwav_allocation_callbacks *wav_alloc_ptr   = &wav_alloc;
#else
#ifdef WANT_FLAC
    constexpr const drflac_allocation_callbacks *flac_alloc_ptr = nullptr;
#endif
#ifdef WANT_MP3
    constexpr const drmp3_allocation_callbacks *mp3_alloc_ptr   = nullptr;
#endif
#ifdef WANT_WAV
    constexpr const drwav_allocation_callbacks *wav_alloc_ptr   = nullptr;
#endif
#endif

}

Source::Source(std::unique_ptr<IoBackend> backend) : m_backend(std::move(backend)), m_offset(0), m_size(0) {
    m_buffered.off = m_buffered.size = 0;
    this->m_size = this->m_backend ? this->m_backend->size() : 0;
    if (this->m_size < 0)
        this->m_size = 0;
}

Source::~Source() {
    this->m_backend.reset();   // closes the file / socket state
    this->m_offset = 0;
    this->m_size   = 0;
}

bool Source::SetupResampler(int output_channels, int output_sample_rate) {
    // check if we even need the resampler.
    m_native_stream = GetChannelCount() == output_channels && GetSampleRate() == output_sample_rate;
    if (m_native_stream) {
        return true;
    }

    m_sdl_stream = UniqueAudioStream{
        SDL_NewAudioStreamEX(
        AUDIO_S16, GetChannelCount(), GetSampleRate(),
        AUDIO_S16, output_channels, output_sample_rate)
    };

    return m_sdl_stream != nullptr;
}

s64 Source::Resample(u8* out, std::size_t size) {
    if (!out || !size) {
        return -1;
    }

    if (m_native_stream) {
        return Decode(size / sizeof(s16), (s16*)out);
    } else {
        s64 data_read = 0;
        while (size > 0) {
            const auto sz = SDL_AudioStreamGetEX(m_sdl_stream.get(), out, size);

            if (sz < 0) {
                return -1;
            } else if (sz > 0) {
                size -= sz;
                out += sz;
                data_read += sz;
            } else {
                const auto dec_got = Decode(m_resample_buffer.size(), m_resample_buffer.data());
                if (dec_got == 0) {
                    return data_read;
                }
                if (0 != SDL_AudioStreamPutEX(m_sdl_stream.get(), m_resample_buffer.data(), dec_got)) {
                    return -1;
                }
            }
        }

        return data_read;
    }
}

size_t Source::ReadFile(void *_buffer, size_t read_size) {
    auto dst = static_cast<u8*>(_buffer);
    size_t amount = 0;

    // check if we already have this data buffered.
    if (m_buffered.size) {
        // check if we can read this data into the beginning of dst.
        if (this->m_offset < m_buffered.off + m_buffered.size && this->m_offset >= m_buffered.off) {
            const auto off = this->m_offset - m_buffered.off;
            const auto size = std::min<s64>(read_size, m_buffered.size - off);
            std::memcpy(dst, m_buffered.data + off, size);

            read_size -= size;
            m_offset += size;
            amount += size;
            dst += size;
        }
    }

    if (read_size) {
        u64 bytes_read = 0;

        // if the dst dst is big enough, read data in place.
        if (read_size >= sizeof(m_buffered.data)) {
            if ((bytes_read = this->m_backend->read(this->m_offset, dst, read_size)) != 0) {
                read_size -= bytes_read;
                m_offset += bytes_read;
                amount += bytes_read;
                dst += bytes_read;

                // save the last chunk of data to the m_buffered io.
                const auto max_advance = std::min(amount, sizeof(m_buffered.data));
                m_buffered.off = m_offset - max_advance;
                m_buffered.size = max_advance;
                std::memcpy(m_buffered.data, dst - max_advance, max_advance);
            }
        } else {
            // A cancelled transfer can overwrite part of this storage before
            // returning zero. Its old range must not remain readable on seek.
            m_buffered.size = 0;
            bytes_read = this->m_backend->read(this->m_offset, m_buffered.data, sizeof(m_buffered.data));
            if (bytes_read) {
                const auto max_advance = std::min(read_size, bytes_read);
                std::memcpy(dst, m_buffered.data, max_advance);

                m_buffered.off = m_offset;
                m_buffered.size = bytes_read;

                read_size -= max_advance;
                m_offset += max_advance;
                amount += max_advance;
                dst += max_advance;
            }
        }
    }

    return amount;
}

bool Source::SeekFile(s64 offset, int origin) {
    s64 new_offset;
    switch (origin) {
        case SeekOrigin_SET:
            new_offset = offset;
            break;
        case SeekOrigin_CUR:
            new_offset = this->m_offset + offset;
            break;
        case SeekOrigin_END:
            new_offset = this->m_size + offset;
            break;
        default:
            return false;
    }

    if (new_offset >= 0 && new_offset <= this->m_size) {
        this->m_offset = new_offset;
        return true;
    } else {
        return false;
    }
}

s64 Source::TellFile() {
    return this->m_offset;
}

bool Source::Done() {
    auto [current, total] = this->Tell();

    return current == total;
}

#ifdef WANT_FLAC
class FlacFile final : public Source {
  private:
    drflac *m_flac;

  public:
    FlacFile(std::unique_ptr<IoBackend> backend) : Source(std::move(backend)) {
        this->m_flac = drflac_open(ReadCallback, FlacSeekCallback, FlacTellCallback, this, flac_alloc_ptr);
        if (this->m_flac && (this->m_flac->channels == 0 || this->m_flac->sampleRate == 0)) {
            drflac_close(this->m_flac);
            this->m_flac = nullptr;
        }
    }
    ~FlacFile() {
        if (this->m_flac != nullptr)
            drflac_close(this->m_flac);
    }

    bool IsOpen() override {
        return this->m_flac != nullptr;
    }

    size_t Decode(size_t sample_count, s16 *data) override {
        std::scoped_lock lk(this->m_mutex);

        return GetChannelCount() * sizeof(s16) * drflac_read_pcm_frames_s16(this->m_flac, sample_count / GetChannelCount(), data);
    }

    std::pair<u32, u32> Tell() override {
        std::scoped_lock lk(this->m_mutex);

        return {this->m_flac->currentPCMFrame, this->m_flac->totalPCMFrameCount};
    }

    bool Seek(u64 target) override {
        std::scoped_lock lk(this->m_mutex);

        return drflac_seek_to_pcm_frame(this->m_flac, target);
    }

    int GetSampleRate() override {
        return this->m_flac->sampleRate;
    }

    int GetChannelCount() override {
        return this->m_flac->channels;
    }
};
#endif

#ifdef WANT_MP3
class Mp3File final : public Source {
  private:
    drmp3 m_mp3;
    bool initialized = false;
    u64 m_total_frame_count;

  public:
    Mp3File(std::unique_ptr<IoBackend> backend) : Source(std::move(backend)) {
        if (drmp3_init(&this->m_mp3, ReadCallback, Mp3SeekCallback, Mp3TellCallback, nullptr, this, mp3_alloc_ptr)) {
            if (this->m_mp3.channels == 0 || this->m_mp3.sampleRate == 0) {
                drmp3_uninit(&this->m_mp3);
                return;
            }
            if (this->m_mp3.totalPCMFrameCount != DRMP3_UINT64_MAX) {
                this->m_total_frame_count = drmp3_get_pcm_frame_count(&this->m_mp3);
            } else if (this->GetFileSize() > 0 && this->m_mp3.sampleRate > 0) {
                u64 seconds = (this->GetFileSize() * 8) / 128000;
                this->m_total_frame_count = seconds * this->m_mp3.sampleRate;
            } else {
                this->m_total_frame_count = drmp3_get_pcm_frame_count(&this->m_mp3);
            }
            this->initialized = true;
        }
    }
    ~Mp3File() {
        if (initialized)
            drmp3_uninit(&this->m_mp3);
    }

    bool IsOpen() override {
        return initialized;
    }

    size_t Decode(size_t sample_count, s16 *data) override {
        std::scoped_lock lk(this->m_mutex);

        return GetChannelCount() * sizeof(s16) * drmp3_read_pcm_frames_s16(&this->m_mp3, sample_count / GetChannelCount(), data);
    }

    std::pair<u32, u32> Tell() override {
        std::scoped_lock lk(this->m_mutex);

        return {this->m_mp3.currentPCMFrame, this->m_total_frame_count};
    }

    bool Seek(u64 target) override {
        std::scoped_lock lk(this->m_mutex);

        return drmp3_seek_to_pcm_frame(&this->m_mp3, target);
    }

    int GetSampleRate() override {
        return this->m_mp3.sampleRate;
    }

    int GetChannelCount() override {
        return this->m_mp3.channels;
    }
};
#endif

#ifdef WANT_WAV
class WavFile final : public Source {
  private:
    drwav m_wav;
    bool initialized = false;
    s32 m_bytes_per_pcm;

  public:
    WavFile(std::unique_ptr<IoBackend> backend) : Source(std::move(backend)) {
        if (drwav_init(&this->m_wav, ReadCallback, WavSeekCallback, WavTellCallback, this, wav_alloc_ptr)) {
            this->m_bytes_per_pcm = drwav_get_bytes_per_pcm_frame(&this->m_wav);
            if (this->m_wav.channels == 0 || this->m_wav.sampleRate == 0 || this->m_bytes_per_pcm <= 0) {
                drwav_uninit(&this->m_wav);
                return;
            }
            this->initialized     = true;
        }
    }
    ~WavFile() {
        if (initialized)
            drwav_uninit(&this->m_wav);
    }

    bool IsOpen() override {
        return initialized;
    }

    size_t Decode(size_t sample_count, s16 *data) override {
        std::scoped_lock lk(this->m_mutex);

        return GetChannelCount() * sizeof(s16) * drwav_read_pcm_frames_s16(&this->m_wav, sample_count / GetChannelCount(), data);
    }

    std::pair<u32, u32> Tell() override {
        std::scoped_lock lk(this->m_mutex);

        if (this->m_bytes_per_pcm <= 0)
            return {0, 0};
        u64 byte_position = this->m_wav.dataChunkDataSize - this->m_wav.bytesRemaining;
        return {byte_position / this->m_bytes_per_pcm, this->m_wav.totalPCMFrameCount};
    }

    bool Seek(u64 target) override {
        std::scoped_lock lk(this->m_mutex);

        return drwav_seek_to_pcm_frame(&this->m_wav, target);
    }

    int GetSampleRate() override {
        return this->m_wav.sampleRate;
    }

    int GetChannelCount() override {
        return this->m_wav.channels;
    }
};
#endif

namespace {
    // Construct the right decoder over a ready backend. Backend is consumed.
    std::unique_ptr<Source> make_decoder(SourceType type, std::unique_ptr<IoBackend> backend) {
        if (!backend) return nullptr;
        if (false) {}
#ifdef WANT_MP3
        else if (type == SourceType::MP3)  return std::make_unique<Mp3File>(std::move(backend));
#endif
#ifdef WANT_FLAC
        else if (type == SourceType::FLAC) return std::make_unique<FlacFile>(std::move(backend));
#endif
#ifdef WANT_WAV
        else if (type == SourceType::WAV)  return std::make_unique<WavFile>(std::move(backend));
#endif
        return nullptr;
    }
}

std::unique_ptr<Source> OpenFile(const char *path, const SourceIoRequest &request) {
    if (!path || request.Cancelled()) return nullptr;

    if (std::strncmp(path, "qqm://", 6) == 0) {
        std::string s(path + 6);
        std::string songmid, media_mid;
        auto s1 = s.find('/');
        if (s1 != std::string::npos) {
            songmid = s.substr(0, s1);
            auto s2 = s.find('/', s1 + 1);
            if (s2 != std::string::npos) {
                media_mid = s.substr(s1 + 1, s2 - (s1 + 1));
            } else {
                media_mid = s.substr(s1 + 1);
            }
        } else {
            songmid = s;
            media_mid = s;
        }
        if (media_mid.empty()) media_mid = songmid;
        // 1) 本地缓存优先：命中则完全不联网（断网可放，且没有网络抖动导致的断流）。
        std::string cached;
        if (qqmusic::songcache::Lookup(songmid, cached)) {
            FsFile cached_file;
            if (R_SUCCEEDED(sdmc::OpenFile(&cached_file, cached.c_str()))) {
                const std::string msg = "SYS OpenFile cache hit: " + songmid + "\n";
                sysLog(msg.c_str());
                return make_decoder(SourceType::MP3, MakeFileBackend(std::move(cached_file)));
            }
        }

        std::string play_url = qqmusic::api::GetSongPlayUrl(songmid, media_mid);
        if (play_url.empty() && !request.Cancelled()) {
            svcSleepThread(250'000'000ull);
            if (!request.Cancelled())
                play_url = qqmusic::api::GetSongPlayUrl(songmid, media_mid);
        }
        if (request.Cancelled()) return nullptr;
        if (play_url.empty()) {
            char b[128];
            std::snprintf(b, sizeof(b), "SYS OpenFile GetSongPlayUrl empty: %s\n", songmid.c_str());
            sysLog(b);
            return nullptr;
        }

        // 2) 未命中：边下边存，听完自动落盘，下次播放即走本地。
        auto backend = qqmusic::songcache::MakeCachingBackend(MakeHttpBackend(play_url, request), songmid);
        if ((!backend || backend->size() == 0) && !request.Cancelled()) {
            svcSleepThread(250'000'000ull);
            if (!request.Cancelled())
                backend = qqmusic::songcache::MakeCachingBackend(MakeHttpBackend(play_url, request), songmid);
        }
        if (request.Cancelled() || !backend || backend->size() == 0) {
            char b[128];
            std::snprintf(b, sizeof(b), "SYS OpenFile backend size 0 for: %s\n", songmid.c_str());
            sysLog(b);
            return nullptr;
        }
        return make_decoder(SourceType::MP3, std::move(backend));
    }

    if (std::strncmp(path, "http://", 7) == 0 || std::strncmp(path, "https://", 8) == 0) {
        auto backend = MakeHttpBackend(path, request);
        if (request.Cancelled() || !backend || backend->size() == 0) return nullptr;
        auto type = GetSourceType(path);
        if (type == SourceType::NONE) type = SourceType::MP3;
        return make_decoder(type, std::move(backend));
    }

    const auto type = GetSourceType(path);
    if (type == SourceType::NONE)
        return nullptr;

    FsFile file;
    if (R_FAILED(sdmc::OpenFile(&file, path)))
        return nullptr;

    return make_decoder(type, MakeFileBackend(std::move(file)));
}


SourceType GetSourceType(const char* path) {
    if (!path) return SourceType::NONE;
    if (std::strncmp(path, "qqm://", 6) == 0) {
        return SourceType::MP3;
    }

    const auto ext = std::strrchr(path, '.');
    if (!ext) {
        return SourceType::NONE;
    }

    if (!strcasecmp(ext, ".mp3")) {
        return SourceType::MP3;
    } else if (!strcasecmp(ext, ".flac")) {
        return SourceType::FLAC;
    } else if (!strcasecmp(ext, ".wav") || !strcasecmp(ext, ".wave")) {
        return SourceType::WAV;
    }

    return SourceType::NONE;
}
