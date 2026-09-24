#include "songcache.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

#include "config/config.hpp"
#include "sdmc/sdmc.hpp"

extern "C" void sysLog(const char *s);

namespace qqmusic::songcache {

    namespace {

        // 由 player 推送的当前播放队列长度（「跟随队列」模式的上限来源）
        std::atomic<u32> g_queue_size{0};

        size_t EffectiveLimit() {
            if (config::get_cache_mode() == 0) {
                // 跟随播放队列：队列有多少首就缓存多少首（队列为空时至少留 1 首）
                const u32 count = g_queue_size.load(std::memory_order_relaxed);
                return count > 0 ? (size_t)count : 1;
            }
            return (size_t)config::get_cache_limit();
        }

        std::string PartPath(const std::string &songmid) {
            return std::string(CACHE_DIR) + "/" + songmid + ".part";
        }

        std::string FinalPath(const std::string &songmid) {
            return std::string(CACHE_DIR) + "/" + songmid + ".mp3";
        }

        // 边下边存 backend：HTTP 读取 + 同步写缓存文件。
        // 设计原则：缓存只是加速手段，任何缓存失败都不得影响播放（失败即静默放弃）。
        class CachingBackend final : public IoBackend {
            std::unique_ptr<IoBackend> m_inner;
            std::string m_songmid;
            FsFile m_file{};
            bool m_open = false;
            bool m_done = false;
            bool m_give_up = false;

            bool EnsureOpen() {
                if (m_open) return true;
                if (m_give_up) return false;

                const std::string part = PartPath(m_songmid);
                sdmc::DeleteFile(part.c_str()); // 丢弃上次残留
                // 先建空文件（fsFsOpenFile 不会创建），再以写模式打开。
                if (R_FAILED(sdmc::WriteFile(part.c_str(), "", 0)) ||
                    R_FAILED(sdmc::OpenFile(&m_file, part.c_str(), FsOpenMode_Write))) {
                    m_give_up = true;
                    return false;
                }
                m_open = true;
                return true;
            }

            // 完成后校验长度，长度不符一律丢弃——宁可没有缓存，也不能缓存半首歌。
            void Finalize() {
                if (!m_open || m_done) return;
                m_done = true;
                fsFileFlush(&m_file);

                s64 written = 0;
                fsFileGetSize(&m_file, &written);
                fsFileClose(&m_file);
                m_open = false;

                const std::string part = PartPath(m_songmid);
                const s64 expect = m_inner->size();
                if (expect <= 0 || written != expect) {
                    char b[128];
                    std::snprintf(b, sizeof(b), "SYS cache size mismatch (%lld/%lld), dropped\n",
                                  (long long)written, (long long)expect);
                    sysLog(b);
                    sdmc::DeleteFile(part.c_str());
                    return;
                }

                const std::string final_path = FinalPath(m_songmid);
                sdmc::DeleteFile(final_path.c_str());
                if (R_SUCCEEDED(sdmc::RenameFile(part.c_str(), final_path.c_str()))) {
                    char b[128];
                    std::snprintf(b, sizeof(b), "SYS cache stored: %s (%lld bytes)\n",
                                  m_songmid.c_str(), (long long)written);
                    sysLog(b);
                    Evict();
                } else {
                    sdmc::DeleteFile(part.c_str());
                }
            }

          public:
            CachingBackend(std::unique_ptr<IoBackend> inner, std::string songmid)
                : m_inner(std::move(inner)), m_songmid(std::move(songmid)) {}

            ~CachingBackend() override {
                if (m_open) {
                    fsFileClose(&m_file);
                    sdmc::DeleteFile(PartPath(m_songmid).c_str()); // 未完成不留半成品
                }
            }

            u64 read(s64 offset, void *buf, u64 size) override {
                const u64 n = m_inner->read(offset, buf, size);
                if (n && offset >= 0 && EnsureOpen()) {
                    if (R_FAILED(fsFileWrite(&m_file, offset, buf, n, 0))) {
                        // 写入失败：放弃缓存，但不影响本次播放。
                        fsFileClose(&m_file);
                        m_open = false;
                        m_give_up = true;
                        sdmc::DeleteFile(PartPath(m_songmid).c_str());
                    } else {
                        const s64 total = m_inner->size();
                        if (total > 0 && offset + (s64)n >= total) Finalize();
                    }
                }
                return n;
            }

            s64 size() override { return m_inner->size(); }
        };

    } // namespace

    void SetQueueSize(u32 count) {
        g_queue_size.store(count, std::memory_order_relaxed);
        Evict(); // 队列变短（或模式变化）时立即压回新上限
    }

    void Init() {
        sdmc::CreateFolder(CACHE_DIR); // 已存在则忽略

        // 清理上次未完成的半成品
        std::vector<sdmc::DirEntry> entries;
        if (R_SUCCEEDED(sdmc::ListDir(CACHE_DIR, &entries))) {
            for (const auto &e : entries) {
                if (e.name.size() > 5 && e.name.compare(e.name.size() - 5, 5, ".part") == 0) {
                    const std::string p = std::string(CACHE_DIR) + "/" + e.name;
                    sdmc::DeleteFile(p.c_str());
                }
            }
        }
        Evict();
    }

    bool Lookup(const std::string &songmid, std::string &out_path) {
        const std::string p = FinalPath(songmid);
        if (!sdmc::FileExists(p.c_str())) return false;
        out_path = p;
        return true;
    }

    void Evict() {
        std::vector<sdmc::DirEntry> entries;
        if (R_FAILED(sdmc::ListDir(CACHE_DIR, &entries))) return;

        std::vector<sdmc::DirEntry> songs;
        for (const auto &e : entries) {
            if (e.name.size() > 4 && e.name.compare(e.name.size() - 4, 4, ".mp3") == 0)
                songs.push_back(e);
        }
        const size_t limit = EffectiveLimit();
        if (songs.size() <= limit) return;

        // 最旧的先删（mtime 升序）
        std::sort(songs.begin(), songs.end(),
                  [](const sdmc::DirEntry &a, const sdmc::DirEntry &b) { return a.mtime < b.mtime; });
        const size_t drop = songs.size() - limit;
        for (size_t i = 0; i < drop; i++) {
            const std::string p = std::string(CACHE_DIR) + "/" + songs[i].name;
            sdmc::DeleteFile(p.c_str());
        }
        char b[96];
        std::snprintf(b, sizeof(b), "SYS cache evicted %zu of %zu (limit %zu)\n",
                      drop, songs.size(), limit);
        sysLog(b);
    }

    std::unique_ptr<IoBackend> MakeCachingBackend(std::unique_ptr<IoBackend> inner,
                                                  const std::string &songmid) {
        if (!inner) return inner;
        return std::make_unique<CachingBackend>(std::move(inner), songmid);
    }

} // namespace qqmusic::songcache
