#include "meta.hpp"

#include "../result.hpp"
#include "sdmc/sdmc.hpp"

#include "net/http.hpp"
#include "net/qqmusic_api.hpp"
#include <string>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cstdio>

extern "C" void sysLog(const char *s);

namespace qqmusic::impl {

namespace {

    // 元数据/封面共享缓冲（g_parse_buf / g_cover_buf / g_meta_cache）在多线程下必须串行：
    //   IPC 线程  → GetTrackMeta / SetTrackMetaCache
    //   播放线程  → SaveQueueToFile / LoadQueueFromFile 里也会调 GetTrackMeta
    //   封面线程  → ParsePath / LoadCoverFile 导出本地曲目内嵌封面
    // 三方同时写同一块 128KB 解析缓冲会把彼此的数据冲掉，必须加锁。
    Mutex g_meta_mutex;

    struct MetaGuard {
        MetaGuard() { mutexLock(&g_meta_mutex); }
        ~MetaGuard() { mutexUnlock(&g_meta_mutex); }
        MetaGuard(const MetaGuard &) = delete;
        MetaGuard &operator=(const MetaGuard &) = delete;
    };

    constexpr u32 COVER_MAX = 256 * 1024;     // 封面缓冲硬上限
    constexpr u32 PARSE_MAX = 128 * 1024;     // 解析读取上限（标签均在文件头部，128KB足够）
    constexpr u32 META_SLOTS = 350;           // 容纳全部 300 首队列曲目元数据，彻底杜绝乱码

    u8 g_cover_buf[COVER_MAX];
    u32 g_cover_size = 0;

    u8 g_parse_buf[PARSE_MAX];

    struct MetaSlot {
        char path[160]{};
        TrackMeta meta;
    };
    MetaSlot g_meta_cache[META_SLOTS];
    u32 g_meta_next = 0;

    // ---- 文本工具 ----

    void AppendUtf8(u32 cp, char *dst, size_t &out, size_t dst_size) {
        if (cp < 0x80) {
            if (out + 1 < dst_size) dst[out++] = (char)cp;
        } else if (cp < 0x800) {
            if (out + 2 < dst_size) {
                dst[out++] = (char)(0xC0 | (cp >> 6));
                dst[out++] = (char)(0x80 | (cp & 0x3F));
            }
        } else if (cp < 0x10000) {
            if (out + 3 < dst_size) {
                dst[out++] = (char)(0xE0 | (cp >> 12));
                dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                dst[out++] = (char)(0x80 | (cp & 0x3F));
            }
        } else if (cp < 0x110000) {
            if (out + 4 < dst_size) {
                dst[out++] = (char)(0xF0 | (cp >> 18));
                dst[out++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                dst[out++] = (char)(0x80 | (cp & 0x3F));
            }
        }
    }

    // 文本拷贝：保留完整 UTF-8 多字节字符
    void CopyUtf8(const u8 *src, u32 n, char *dst, size_t dst_size) {
        if (dst_size == 0)
            return;
        size_t out = 0;
        for (u32 i = 0; i < n && out + 1 < dst_size; i++) {
            if (src[i] == 0)
                break;
            dst[out++] = (char)src[i];
        }
        dst[out] = 0;
    }

    // ID3 文本帧解码（enc 0/1/2/3）→ ASCII。
    void DecodeId3Text(const u8 *p, u32 len, char *dst, size_t dst_size) {
        dst[0] = 0;
        if (len < 1)
            return;
        const u8 enc = p[0];
        p++;
        len--;

        u32 pos = 0;
        if (enc == 1 || enc == 2) { // UTF-16（跳过 BOM）
            if (len >= 2 && ((p[0] == 0xFF && p[1] == 0xFE) || (p[0] == 0xFE && p[1] == 0xFF)))
                pos = 2;
        }
        size_t out = 0;
        if (enc == 1 || enc == 2) {
            while (pos + 1 < len && out + 1 < dst_size) {
                u16 u;
                if (enc == 2)
                    u = (u16)(((u16)p[pos] << 8) | p[pos + 1]);
                else
                    u = (u16)((u16)p[pos] | ((u16)p[pos + 1] << 8));
                pos += 2;
                if (u == 0)
                    break;
                if (u == 0xFEFF)
                    continue;
                if (u >= 0xD800 && u <= 0xDBFF && pos + 1 < len) {
                    u16 u2;
                    if (enc == 2)
                        u2 = (u16)(((u16)p[pos] << 8) | p[pos + 1]);
                    else
                        u2 = (u16)((u16)p[pos] | ((u16)p[pos + 1] << 8));
                    if (u2 >= 0xDC00 && u2 <= 0xDFFF) {
                        pos += 2;
                        u32 cp = 0x10000 + (((u32)(u & 0x3FF) << 10) | (u2 & 0x3FF));
                        AppendUtf8(cp, dst, out, dst_size);
                        continue;
                    }
                }
                AppendUtf8(u, dst, out, dst_size);
            }
            dst[out] = 0;
        } else {
            CopyUtf8(p, len, dst, dst_size);
        }
    }

    bool IEquals(const u8 *s, u32 n, const char *key) {
        for (u32 i = 0; i < n; i++) {
            char c = (char)s[i];
            if (c >= 'a' && c <= 'z')
                c -= 0x20;
            if (c != key[i])
                return false;
        }
        return key[n] == 0;
    }

    u32 Id3SyncSafe(const u8 *p) {
        return ((u32)p[0] << 21) | ((u32)p[1] << 14) | ((u32)p[2] << 7) | (u32)p[3];
    }

    u32 Le32(const u8 *p) {
        return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
    }

    u32 Be32(const u8 *p) {
        return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
    }

    // ---- 各格式解析 ----

    // tag 指向 'ID3' 头；tag_size 含 10 字节头。
    void ParseId3v2(const u8 *tag, u32 tag_size, TrackMeta *out, const u8 **cover_out, u64 *cover_size_out) {
        if (tag_size < 10)
            return;
        const u8 *p = tag + 10;
        const u32 ver = tag[3];
        u32 off = 0;
        while (off + 10 <= tag_size - 10) {
            if (p[off] == 0)
                break;
            const char id[5] = { (char)p[off], (char)p[off + 1], (char)p[off + 2], (char)p[off + 3], 0 };
            const u32 fsz = (ver >= 4)
                               ? Id3SyncSafe(p + off + 4)
                               : Be32(p + off + 4);
            if (fsz == 0 || off + 10 + fsz > tag_size - 10)
                break;
            const u8 *d = p + off + 10;

            if (strcmp(id, "TIT2") == 0) {
                DecodeId3Text(d, fsz, out->title, sizeof(out->title));
            } else if (strcmp(id, "TPE1") == 0) {
                DecodeId3Text(d, fsz, out->artist, sizeof(out->artist));
            } else if (strcmp(id, "TALB") == 0) {
                DecodeId3Text(d, fsz, out->album, sizeof(out->album));
            } else if (strcmp(id, "APIC") == 0 && !*cover_out && fsz >= 2) {
                // 布局：encoding(1) mime(0 结尾) 图片类型(1) 描述(按 encoding 结尾) 图像数据。
                // q 停在 mime 的 NUL 上，所以描述从 q+2 开始（q+1 是图片类型字节）；
                // 原实现从 q+1 开始扫描，UTF-16 描述的字节对齐会整体错位 1 字节，
                // 交出的“封面”是错位数据，解码必然失败。
                u32 q = 1;
                while (q < fsz && d[q])
                    q++;                       // mime
                if (q + 2 > fsz)
                    break;                     // 头部被截断：本帧不可信
                const u8 enc = d[0];
                u32 dpos = q + 2;
                if (enc == 1 || enc == 2) {
                    // UTF-16 描述：以 0x0000 结束，按 2 字节对齐扫描
                    while (dpos + 1 < fsz && !(d[dpos] == 0 && d[dpos + 1] == 0))
                        dpos += 2;
                    dpos += 2;
                } else {
                    while (dpos < fsz && d[dpos])
                        dpos++;
                    dpos++;
                }
                if (dpos < fsz) {
                    *cover_out = d + dpos;
                    *cover_size_out = (u64)(fsz - dpos);
                }
            }
            off += 10 + fsz;
        }
    }

    // buf 指向 'fLaC' 魔数。
    void ParseFlac(const u8 *buf, u64 size, TrackMeta *out, const u8 **cover_out, u64 *cover_size_out) {
        u64 off = 4;
        // off + 4：元数据块头是 4 字节，不足 4 字节时读取会越过有效数据。
        for (int b = 0; b < 16 && off + 4 <= size; b++) {
            const u8 hdr = buf[off];
            const bool last = hdr & 0x80;
            const u8 type = hdr & 0x7F;
            const u32 len = (u32)(((u32)buf[off + 1] << 16) | ((u32)buf[off + 2] << 8) | (u32)buf[off + 3]);
            off += 4;
            if (off + len > size)
                break;
            const u8 *d = buf + off;
            off += len;

            if (type == 4) { // VORBIS_COMMENT
                u64 q = 0;
                if (q + 4 > (u64)len)
                    continue;
                u32 vendor_len = Le32(d + q);
                q += 4;
                if (q + vendor_len > (u64)len)
                    continue;
                q += vendor_len;
                if (q + 4 > (u64)len)
                    continue;
                u32 count = Le32(d + q);
                q += 4;
                for (u32 i = 0; i < count && q + 4 <= (u64)len; i++) {
                    u32 flen = Le32(d + q);
                    q += 4;
                    if (q + flen > (u64)len)
                        break;
                    const u8 *s = d + q;
                    q += flen;
                    const u8 *eqb = (const u8 *)memchr(s, '=', flen);
                    if (!eqb)
                        continue;
                    const u32 klen = (u32)(eqb - s);
                    const u32 vlen = flen > klen + 1 ? flen - klen - 1 : 0;
                    const u8 *v = s + klen + 1;
                    if (IEquals(s, klen, "TITLE"))
                        CopyUtf8(v, vlen, out->title, sizeof(out->title));
                    else if (IEquals(s, klen, "ARTIST"))
                        CopyUtf8(v, vlen, out->artist, sizeof(out->artist));
                    else if (IEquals(s, klen, "ALBUM"))
                        CopyUtf8(v, vlen, out->album, sizeof(out->album));
                }
            } else if (type == 6) { // PICTURE
                u64 q = 0;
                auto rd = [&]() -> u32 {
                    if (q + 4 > (u64)len)
                        return 0;
                    u32 v = Be32(d + q);
                    q += 4;
                    return v;
                };
                rd();              // picture type
                u32 mime_len = rd();
                q += mime_len;
                u32 desc_len = rd();
                q += desc_len;
                rd();              // width
                rd();              // height
                rd();              // depth
                rd();              // colors
                u32 data_len = rd();
                if (q + 4 <= (u64)len && !*cover_out && (u64)data_len <= (u64)(len - q)) {
                    *cover_out = d + q;
                    *cover_size_out = data_len;
                }
            }
            if (last)
                break;
        }
    }

    void ParseWavInfo(const u8 *buf, u64 size, TrackMeta *out) {
        u64 off = 12;
        while (off + 8 <= size) {
            const char id[5] = { (char)buf[off], (char)buf[off + 1], (char)buf[off + 2], (char)buf[off + 3], 0 };
            const u32 len = Le32(buf + off + 4);
            off += 8;
            if (off + len > size)
                break;
            const u8 *d = buf + off;

            if (strcmp(id, "LIST") == 0 && len >= 8 &&
                d[4] == 'I' && d[5] == 'N' && d[6] == 'F' && d[7] == 'O') {
                u64 q = 8;
                while (q + 8 <= len) {
                    const char sid[5] = { (char)d[q], (char)d[q + 1], (char)d[q + 2], (char)d[q + 3], 0 };
                    const u32 slen = Le32(d + q + 4);
                    q += 8;
                    if (q + slen > len)
                        break;
                    if (strcmp(sid, "INAM") == 0)
                        CopyUtf8(d + q, slen, out->title, sizeof(out->title));
                    else if (strcmp(sid, "IART") == 0)
                        CopyUtf8(d + q, slen, out->artist, sizeof(out->artist));
                    else if (strcmp(sid, "IALB") == 0)
                        CopyUtf8(d + q, slen, out->album, sizeof(out->album));
                    q += (slen + 1) & ~(u32)1;
                }
            }
            off += (len + 1) & ~(u32)1;
        }
    }

    // 文件尾 ID3v2 探测（部分 MP3 把 tag 放文件尾）。
    // 成功返回 tag 在 buf 内的偏移，否则返回 0。
    u64 FindTailId3(const u8 *buf, u64 size) {
        if (size < 20)
            return 0;
        const u32 ts = Id3SyncSafe(buf + size - 10);
        if (ts + 10 > size)
            return 0;
        const u64 start = size - 10 - ts;
        if (start < 10 || memcmp(buf + start, "ID3", 3) != 0)
            return 0;
        return start;
    }

    // 封面兜底文件：<dir>/cover.jpg → <dir>/cover.png → <basename>.jpg → <basename>.png
    bool LoadCoverFile(const char *path) {
        char dir[FS_MAX_PATH]{};
        char base[FS_MAX_PATH]{};
        snprintf(dir, sizeof(dir), "%s", path);
        char *slash = strrchr(dir, '/');
        if (!slash || slash == dir)
            return false;
        *slash = 0;
        snprintf(base, sizeof(base), "%s", path);
        char *bslash = strrchr(base, '/');
        if (!bslash)
            return false;
        bslash++;
        // 去扩展名
        char *dot = strrchr(bslash, '.');
        if (dot)
            *dot = 0;

        char cand[FS_MAX_PATH]{};
        const char *exts[4] = { "/cover.jpg", "/cover.png", ".jpg", ".png" };
        for (int i = 0; i < 4; i++) {
            const int length = snprintf(cand, sizeof(cand), "%s%s", i < 2 ? dir : base, exts[i]);
            if (length < 0 || (size_t)length >= sizeof(cand))
                continue;
            if (!sdmc::FileExists(cand))
                continue;
            FsFile f;
            if (R_SUCCEEDED(sdmc::OpenFile(&f, cand, FsOpenMode_Read))) {
                s64 fsize = 0;
                fsFileGetSize(&f, &fsize);
                if (fsize > 0 && (u64)fsize <= COVER_MAX) {
                    u64 got = 0;
                    fsFileRead(&f, 0, g_cover_buf, (u64)fsize, 0, &got);
                    fsFileClose(&f);
                    if (got == (u64)fsize) {
                        g_cover_size = (u32)got;
                        return true;
                    }
                } else {
                    fsFileClose(&f);
                }
            }
        }
        g_cover_size = 0;
        return false;
    }

    // 读文件（≤PARSE_MAX）→ g_parse_buf，返回实际读取字节数（0 = 失败）。
    u64 ReadParseBuffer(const char *path) {
        FsFile f;
        if (R_FAILED(sdmc::OpenFile(&f, path, FsOpenMode_Read)))
            return 0;
        s64 fsize = 0;
        fsFileGetSize(&f, &fsize);
        u64 n = (fsize < 0 || (u64)fsize > PARSE_MAX) ? ((fsize > 0) ? (u64)PARSE_MAX : 0) : (u64)fsize;
        u64 got = 0;
        fsFileRead(&f, 0, g_parse_buf, n, 0, &got);
        fsFileClose(&f);
        return got;
    }

    // 解析 path 的元数据 + 封面（封面载入 g_cover_buf/g_cover_size）。
    void ParsePath(const char *path, TrackMeta *out) {
        g_cover_size = 0;
        const u64 n = ReadParseBuffer(path);
        if (n == 0)
            return;

        const u8 *cover = nullptr;
        u64 cover_size = 0;
        if (memcmp(g_parse_buf, "ID3", 3) == 0) {
            u32 ts = Id3SyncSafe(g_parse_buf + 6) + 10;
            if (ts > (u32)n)
                ts = (u32)n;
            ParseId3v2(g_parse_buf, ts, out, &cover, &cover_size);
        } else if (memcmp(g_parse_buf, "fLaC", 4) == 0) {
            ParseFlac(g_parse_buf, n, out, &cover, &cover_size);
        } else if (memcmp(g_parse_buf, "RIFF", 4) == 0 && n >= 12 && memcmp(g_parse_buf + 8, "WAVE", 4) == 0) {
            ParseWavInfo(g_parse_buf, n, out);
        }

        if (!out->title[0] && !out->artist[0] && !out->album[0]) {
            const u64 tail = FindTailId3(g_parse_buf, n);
            if (tail)
                ParseId3v2(g_parse_buf + tail, (u32)(n - tail), out, &cover, &cover_size);
        }

        if (cover && cover_size > 0 && cover_size <= COVER_MAX) {
            memcpy(g_cover_buf, cover, cover_size);
            g_cover_size = (u32)cover_size;
            out->has_cover = true;
        } else {
            out->has_cover = false;
        }
    }

    void StoreMetaCache(const char *path, const TrackMeta *meta) {
        for (u32 i = 0; i < META_SLOTS; i++) {
            if (g_meta_cache[i].path[0] && strcmp(g_meta_cache[i].path, path) == 0) {
                g_meta_cache[i].meta = *meta;
                return;
            }
        }
        g_meta_cache[g_meta_next].meta = *meta;
        snprintf(g_meta_cache[g_meta_next].path, sizeof(g_meta_cache[g_meta_next].path), "%s", path);
        g_meta_next = (g_meta_next + 1) % META_SLOTS;
    }

} // namespace

// 由 Initialize() 在任何工作线程启动前调用一次（互斥量必须早于取数/播放线程）。
void MetaInit() {
    mutexInit(&g_meta_mutex);
}

bool ParseTrackMeta(const u8 *buf, u64 size, TrackMeta *out, const u8 **cover_out, u64 *cover_size_out) {
    out->has_cover = false;
    if (cover_out)
        *cover_out = nullptr;
    if (cover_size_out)
        *cover_size_out = 0;
    if (size < 12)
        return false;

    const u8 *cover = nullptr;
    u64 cover_size = 0;
    if (memcmp(buf, "ID3", 3) == 0) {
        u32 ts = Id3SyncSafe(buf + 6) + 10;
        if (ts > (u32)size)
            ts = (u32)size;
        ParseId3v2(buf, ts, out, &cover, &cover_size);
    } else if (memcmp(buf, "fLaC", 4) == 0) {
        ParseFlac(buf, size, out, &cover, &cover_size);
    } else if (memcmp(buf, "RIFF", 4) == 0 && size >= 12 && memcmp(buf + 8, "WAVE", 4) == 0) {
        ParseWavInfo(buf, size, out);
    }
    if (!out->title[0] && !out->artist[0] && !out->album[0]) {
        const u64 tail = FindTailId3(buf, size);
        if (tail)
            ParseId3v2(buf + tail, (u32)(size - tail), out, &cover, &cover_size);
    }

    if (cover && cover_size > 0) {
        if (cover_out)
            *cover_out = cover;
        if (cover_size_out)
            *cover_size_out = cover_size;
    }
    return out->title[0] != 0 || out->artist[0] != 0 || out->album[0] != 0 || cover != nullptr;
}

Result GetTrackMeta(const char *path, TrackMeta *out) {
    if (!path || !out)
        return qqmusic::InvalidArgument;
    MetaGuard guard;

    for (u32 i = 0; i < META_SLOTS; i++) {
        if (strcmp(g_meta_cache[i].path, path) == 0) {
            *out = g_meta_cache[i].meta;
            return 0;
        }
    }

    // 在线流式 URI 无本地文件：直接返回缓存（未命中则空元数据），
    // 绝不用空结果覆盖 SetTrackMetaCache 预填的标签。
    if (strncmp(path, "qqm://", 6) == 0 ||
        strncmp(path, "http://", 7) == 0 ||
        strncmp(path, "https://", 8) == 0) {
        *out = TrackMeta{};
        return 0;
    }

    TrackMeta meta;
    ParsePath(path, &meta);
    StoreMetaCache(path, &meta);
    *out = meta;
    return 0;
}

void SetTrackMetaCache(const char *path, const TrackMeta &meta) {
    MetaGuard guard;
    StoreMetaCache(path, &meta);
}

// 路径 → 8 位十六进制哈希（本地曲目的封面缓存键，避免同名文件互相覆盖）。
static std::string PathHashKey(const char *path) {
    u32 h = 2166136261u;
    for (const char *p = path; *p; p++) {
        h ^= (u8)*p;
        h *= 16777619u;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "local_%08X", (unsigned)h);
    return buf;
}

// 封面缓存键：在线曲目用 songmid，本地曲目用路径哈希。
static std::string CoverKeyFor(const char *path) {
    if (!path || !path[0])
        return "";
    if (strncmp(path, "qqm://", 6) != 0)
        return PathHashKey(path);
    const std::string s(path + 6);
    const auto s1 = s.find('/');
    return (s1 != std::string::npos) ? s.substr(0, s1) : s;
}

// ---- 封面后台取数队列 -------------------------------------------------------
// 关键点：封面要联网（或读本地文件解出内嵌 APIC），这些都不允许发生在 IPC 线程上——
// IPC 线程被占住时，界面侧所有播放控制都会排队等它，表现就是「卡住不播」。
// 因此 IPC 侧只做「文件已在就返回，不在就登记任务立刻返回」，真正的取数交给这条后台线程。
namespace {
    constexpr int kCoverQueueSize = 8;
    char g_cover_queue[kCoverQueueSize][FS_MAX_PATH];
    int g_cover_q_head = 0;
    int g_cover_q_tail = 0;
    Mutex g_cover_q_mutex;
    bool g_cover_q_ready = false;
    std::atomic<bool> g_cover_running{true};

    void CoverQueueInit() {
        if (!g_cover_q_ready) {
            mutexInit(&g_cover_q_mutex);
            g_cover_q_ready = true;
        }
    }

    void EnqueueCoverJob(const char *path) {
        CoverQueueInit();
        mutexLock(&g_cover_q_mutex);
        // 界面按重试周期反复登记同一路径（每个曲目最多 30 次）：队列只有 8 格，
        // 重复项会把其它曲目的请求挤掉，导致它们的封面永远排不上队。
        for (int i = g_cover_q_tail; i != g_cover_q_head; i = (i + 1) % kCoverQueueSize) {
            if (std::strcmp(g_cover_queue[i], path) == 0) {
                mutexUnlock(&g_cover_q_mutex);
                return;
            }
        }
        const int next = (g_cover_q_head + 1) % kCoverQueueSize;
        if (next != g_cover_q_tail) { // 队列满就丢弃：界面下个重试周期会再登记
            snprintf(g_cover_queue[g_cover_q_head], FS_MAX_PATH, "%s", path);
            g_cover_q_head = next;
        }
        mutexUnlock(&g_cover_q_mutex);
    }

    bool DequeueCoverJob(char *out, size_t out_size) {
        CoverQueueInit();
        bool have = false;
        mutexLock(&g_cover_q_mutex);
        if (g_cover_q_head != g_cover_q_tail) {
            snprintf(out, out_size, "%s", g_cover_queue[g_cover_q_tail]);
            g_cover_q_tail = (g_cover_q_tail + 1) % kCoverQueueSize;
            have = true;
        }
        mutexUnlock(&g_cover_q_mutex);
        return have;
    }
} // namespace

// 真正的取数（阻塞）：在线曲目从 QQ 音乐 CDN 取 300x300 专辑封面，
// 本地曲目导出内嵌 APIC / 目录 cover.jpg。成功后落盘为 /qqmusic-cache/<key>.jpg。
static void FetchCoverFileBlocking(const char *path) {
    const std::string key = CoverKeyFor(path);
    if (key.empty())
        return;

    const std::string cache_jpg = std::string("/qqmusic-cache/") + key + ".jpg";
    if (sdmc::FileExists(cache_jpg.c_str()))
        return; // 已被别的路径先落盘（例如同一首歌重复登记）

    std::string album_mid;
    bool online = false;

    if (strncmp(path, "qqm://", 6) == 0) {
        online = true;
        const std::string s(path + 6);
        const auto s1 = s.find('/');
        if (s1 != std::string::npos) {
            const auto s2 = s.find('/', s1 + 1);
            if (s2 != std::string::npos)
                album_mid = s.substr(s2 + 1);
        }
        // 老队列 / 部分搜索结果里的 qqm:// 只有两段（缺 album_mid），按 songmid 现查一次。
        if (album_mid.empty())
            album_mid = qqmusic::api::GetSongAlbumMid(key);
        if (album_mid.empty())
            return;
    }

    if (online) {
        qqmusic::net::Init();
        const std::string url =
            "https://y.qq.com/music/photo_new/T002R300x300M000" + album_mid + ".jpg";
        auto resp = net::Get(url, {"Referer: https://y.qq.com/"}, "", 4);
        if (!resp.ok() || resp.body.empty() || resp.body.size() > COVER_MAX) {
            char b[160];
            std::snprintf(b, sizeof(b), "SYS cover fetch fail: status=%ld len=%zu album=%s\n",
                          resp.status_code, resp.body.size(), album_mid.c_str());
            sysLog(b);
            return;
        }
        sdmc::CreateFolder("/qqmusic-cache");
        if (R_SUCCEEDED(sdmc::WriteFile(cache_jpg.c_str(), resp.body.data(), resp.body.size()))) {
            char b[128];
            std::snprintf(b, sizeof(b), "SYS cover stored: %s (%u bytes)\n", key.c_str(),
                          (unsigned)resp.body.size());
            sysLog(b);
        }
        return;
    }

    // 本地曲目：内嵌封面 → 落盘。ParsePath/LoadCoverFile 会写 g_parse_buf 与
    // g_cover_buf，必须与 IPC/播放线程的元数据访问互斥，并在写出前一直持有锁。
    MetaGuard guard;
    TrackMeta meta;
    ParsePath(path, &meta);
    if (g_cover_size == 0)
        LoadCoverFile(path);
    if (g_cover_size == 0)
        return;
    const u32 cover_size = g_cover_size;
    sdmc::CreateFolder("/qqmusic-cache");
    sdmc::WriteFile(cache_jpg.c_str(), g_cover_buf, cover_size);
}

void StopCoverWorker() {
    g_cover_running = false;
}

// 封面后台线程主体（由 main.cpp 创建，栈需容纳 curl/TLS）。
void CoverWorkerThreadFunc(void *) {
    char path[FS_MAX_PATH];
    while (g_cover_running) {
        if (DequeueCoverJob(path, sizeof(path))) {
            // 给音频流缓冲留出 800ms 的网络优先窗口，避免封面并发请求抢占 WiFi 导致其它功能像断网一样
            for (int i = 0; i < 8 && g_cover_running; ++i)
                svcSleepThread(100'000'000ull);
            if (!g_cover_running) break;
            FetchCoverFileBlocking(path);
        }
        else
            svcSleepThread(150'000'000ull);
    }
}

// IPC 线程调用：只查文件在不在，绝不做网络 I/O。
Result EnsureCoverFile(const char *path, char *out_name, size_t out_name_size) {
    if (!path || !path[0] || !out_name || out_name_size == 0)
        return qqmusic::InvalidArgument;
    out_name[0] = '\0';

    const std::string key = CoverKeyFor(path);
    if (key.empty())
        return qqmusic::InvalidArgument;

    const std::string cache_jpg = std::string("/qqmusic-cache/") + key + ".jpg";
    if (sdmc::FileExists(cache_jpg.c_str())) {
        snprintf(out_name, out_name_size, "%s.jpg", key.c_str());
        return 0;
    }

    // 还没准备好：交给后台线程去取，本次直接回「暂无」。界面按重试周期再来问，
    // 期间完全不会阻塞 UI 与播放控制。
    EnqueueCoverJob(path);
    return qqmusic::FileOpenFailure;
}

}