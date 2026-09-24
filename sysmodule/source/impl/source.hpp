#pragma once

#include <nxExt.h>
#include <memory>
#include <array>
#include <cstddef>
#include <atomic>
#include "resamplers/SDL_audioEX.h"

enum class SourceType {
    NONE,
    MP3,
    FLAC,
    WAV,
};

// Pluggable byte backend behind Source: the decoders never know if bytes come
// from an SD file or an HTTP stream. read() must fetch up to `size` bytes at an
// absolute offset and return how many it got; size() is the total length.
struct IoBackend {
    virtual ~IoBackend() {}
    virtual u64 read(s64 offset, void *buf, u64 size) = 0;
    virtual s64 size() = 0;
};

// Owned by the playback thread and kept alive until its Source is destroyed.
// Only that thread updates expected; control requests increment generation.
struct SourceIoRequest {
    const std::atomic<u64>& generation;
    u64 expected;

    bool Cancelled() const { return generation.load() != expected; }
};

std::unique_ptr<IoBackend> MakeFileBackend(FsFile &&file);
std::unique_ptr<IoBackend> MakeHttpBackend(const std::string &url, const SourceIoRequest &request);

class Source {
  private:
    std::unique_ptr<IoBackend> m_backend;
    s64 m_offset = 0;
    s64 m_size = 0;

  protected:
    // SOURCE: https://dev.krzaq.cc/post/you-dont-need-a-stateful-deleter-in-your-unique_ptr-usually/
    // used for deconstructor for smart pointers.
    template<auto func>
    struct FunctionCaller {
        template<typename... Us>
        auto operator()(Us&&... us) const {
            return func(std::forward<Us...>(us...));
        }
    };
    template<auto func>
    using Deleter = FunctionCaller<func>;

    template<size_t Size>
    struct BufferedFileData {
        u8 data[Size];
        s64 off;
        s64 size;
  };

  protected:
    // increasing the size of this buffer also increases the memory used by the resampler.
    static inline std::array<s16, 1024 * 4> m_resample_buffer;
    // 弱网抗抖动：读缓冲由 256KB 翻倍至 512KB（约 13 秒音频），完全抵消 WiFi 抖动
    static inline BufferedFileData<1024 * 512> m_buffered;
    LockableMutex m_mutex;

  private:
    using UniqueAudioStream = std::unique_ptr<SDL_AudioStream, Deleter<&SDL_FreeAudioStreamEX>>;
    UniqueAudioStream m_sdl_stream{nullptr};
    bool m_native_stream{};

  public:
    Source(std::unique_ptr<IoBackend> backend);
    virtual ~Source();

    bool SetupResampler(int output_channels, int output_sample_rate);
    s64 Resample(u8* out, std::size_t size);

    size_t ReadFile(void *buffer, size_t read_size);
    s64 TellFile();
    bool SeekFile(s64 offset, int origin);

    virtual bool IsOpen() = 0;
    virtual size_t Decode(size_t sample_count, s16 *data) = 0;
    virtual std::pair<u32, u32> Tell() = 0;
    virtual bool Seek(u64 target) = 0;

    bool Done();

    s64 GetFileSize() const { return m_size; }
    virtual int GetSampleRate() = 0;
    virtual int GetChannelCount() = 0;
};

std::unique_ptr<Source> OpenFile(const char *path, const SourceIoRequest &request);
SourceType GetSourceType(const char* path);
