#pragma once

#include <switch.h>

namespace qqmusic::impl {

// 曲目元数据（标题/艺术家/专辑）+ 封面。
// 文本字段：空串 = 无。封面由 EnsureCoverFile 按曲目落盘到 /qqmusic-cache。
struct TrackMeta {
    char title[128]{};
    char artist[128]{};
    char album[128]{};
    bool has_cover = false;
};

// 纯解析（不触 fs，宿主可单测）：识别
//   - ID3v2.3/v2.4（MP3 文件头 / MP3 文件尾 / WAV 文件尾）：TIT2/TPE1/TALB/APIC
//   - FLAC 元数据块：Vorbis Comment（TITLE/ARTIST/ALBUM）+ PICTURE
//   - WAV RIFF INFO：INAM/IART/IALB
// 封面（内嵌 APIC/PICTURE）经 cover_out/cover_size_out 返回（指向 buf 内部，可空）。
bool ParseTrackMeta(const u8 *buf, u64 size, TrackMeta *out,
                    const u8 **cover_out, u64 *cover_size_out);

// 懒加载：按路径解析 + 静态缓存（文本 128 槽；封面按曲目落盘）。
Result GetTrackMeta(const char *path, TrackMeta *out);
// 初始化元数据互斥量（必须在任何工作线程启动前调用）。
void MetaInit();
// 封面后台取数线程主体（由 main.cpp 创建；栈需容纳 curl/TLS）。
void CoverWorkerThreadFunc(void *arg);
// 通知封面后台线程退出，供 main() 优雅关停使用。
void StopCoverWorker();
// 确保 path 的封面已落盘到 /qqmusic-cache/<name>（JPEG），并把 <name> 写回 out_name。
// **非阻塞**：文件已在则立即返回；否则登记后台任务并返回失败，界面稍后重试。
Result EnsureCoverFile(const char *path, char *out_name, size_t out_name_size);
// 确保 path 的歌词已落盘到 /qqmusic-cache/<name>（.lrc），并把 <name> 写回 out_name。
Result EnsureLyricFile(const char *path, char *out_name, size_t out_name_size);
void SetTrackMetaCache(const char *path, const TrackMeta &meta);

}