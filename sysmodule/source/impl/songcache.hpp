#pragma once

#include <memory>
#include <string>

#include "source.hpp"

// 在线歌曲本地缓存：把播放过的歌落盘到 SD 卡，最多保留最近 20 首。
// 作用：1) 断网/弱网时仍可播放已缓存歌曲；2) 重播与来回切歌不再走网络，显著减少断流。
namespace qqmusic::songcache {

    constexpr const char *CACHE_DIR = "/qqmusic-cache";

    // 缓存容量策略见 config::get_cache_mode()：
    //   0 = 跟随播放队列（队列有多少首就缓存多少首）
    //   1 = 指定数量（config::get_cache_limit()）
    // player 侧每次队列变动后调用 SetQueueSize 同步当前队列长度。
    void SetQueueSize(u32 count);

    // 启动时：创建缓存目录、清理未完成的半成品、把数量压回上限。
    void Init();

    // 命中缓存则返回本地路径（可直接用 FileBackend 打开，完全不联网）。
    bool Lookup(const std::string &songmid, std::string &out_path);

    // 超出当前上限时按修改时间淘汰最旧的缓存文件。
    void Evict();

    // 边下边存：包装 HTTP backend，读取的同时把字节写入本地缓存；
    // 整首读完后校验长度再落盘（.part -> .mp3），失败/中断则丢弃半成品。
    std::unique_ptr<IoBackend> MakeCachingBackend(std::unique_ptr<IoBackend> inner,
                                                  const std::string &songmid);

} // namespace qqmusic::songcache
