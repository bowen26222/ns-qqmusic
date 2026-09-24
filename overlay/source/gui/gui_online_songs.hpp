#pragma once

#include "client.h"
#include "elm_overlayframe.hpp"
#include <tesla.hpp>
#include <string>
#include <vector>

// 各在线来源单次取数上限：目标是一次把整张专辑/整个歌单取回来，
// 这样「Play All / Add All」立刻就能把全部曲目入队，不必先手动 Load More。
namespace online_fetch {
    constexpr u32 kFavorites = 50;  // 我喜欢的歌：单批 50 首（秒开防卡死，支持翻页到 2000+）
    constexpr u32 kChart     = 100; // 榜单
    constexpr u32 kSearch    = 50;  // 搜索（按页翻）
    constexpr u32 kAlbum     = 300; // 专辑曲目（合集类专辑上限 300 足够）
    constexpr u32 kSinger    = 100; // 歌手热门曲目
    constexpr u32 kPlaylist  = 200; // 歌单曲目
}

// 在线列表的来源与分页游标：让同一套「加载更多」逻辑驱动全部在线列表。
struct OnlineListSpec {
    enum class Kind {
        None,      // 固定列表（如搜索首屏外的一次性结果）
        Chart,     // 榜单：cursor = 已加载歌曲数（作为 offset）
        Search,    // 搜索：cursor = 下一个页码
        Favorites, // 我喜欢：cursor = 已加载歌曲数（作为 song_begin）
        Album,     // 专辑曲目：cursor = 已加载歌曲数（作为 begin），id = albumMID
        Singer,    // 歌手曲目：cursor = 已加载歌曲数（作为 begin），id = singerMID
        Playlist,  // 歌单曲目：cursor = 已加载歌曲数（作为 begin），id = disstid（十进制字符串）
    };

    Kind kind = Kind::None;
    u32 chart_id = 0;
    u32 cursor = 0;
    std::string query;
    std::string id;    // Album / Singer / Playlist：集合条目的标识
    std::string title; // Album / Singer / Playlist：列表标题（用于「没有更多」提示）

    static OnlineListSpec ChartTop(u32 top_id, u32 loaded) {
        OnlineListSpec s;
        s.kind = Kind::Chart;
        s.chart_id = top_id;
        s.cursor = loaded;
        return s;
    }

    static OnlineListSpec SearchOf(const std::string &q, u32 next_page) {
        OnlineListSpec s;
        s.kind = Kind::Search;
        s.query = q;
        s.cursor = next_page;
        return s;
    }

    static OnlineListSpec FavoritesFrom(u32 loaded) {
        OnlineListSpec s;
        s.kind = Kind::Favorites;
        s.cursor = loaded;
        return s;
    }

    static OnlineListSpec AlbumOf(const std::string &albummid, const std::string &title, u32 loaded) {
        OnlineListSpec s;
        s.kind = Kind::Album;
        s.id = albummid;
        s.title = title;
        s.cursor = loaded;
        return s;
    }

    static OnlineListSpec SingerOf(const std::string &singermid, const std::string &title, u32 loaded) {
        OnlineListSpec s;
        s.kind = Kind::Singer;
        s.id = singermid;
        s.title = title;
        s.cursor = loaded;
        return s;
    }

    static OnlineListSpec PlaylistOf(const std::string &disstid, const std::string &title, u32 loaded) {
        OnlineListSpec s;
        s.kind = Kind::Playlist;
        s.id = disstid;
        s.title = title;
        s.cursor = loaded;
        return s;
    }
};

class OnlineSongsGui final : public tsl::Gui {
  private:
    std::string m_title;
    std::vector<QqMusicOnlineSong> m_songs;
    OnlineListSpec m_spec{};
    QqMusicOverlayFrame *m_frame = nullptr;
    tsl::elm::List *m_list = nullptr;
    bool m_reload = false;
    bool m_busy = false;
    bool m_load_first = false;   // 排序切换后需要重新拉取第一页
    size_t m_visible_count = 30; // 界面当前渲染的条目数（按需展示，避免 200 首控件一次性测量卡死）
    int m_sort_mode = 0;         // 0=默认 1=最新在前 2=首字母

    void Rebuild();
    void ApplySort();
    void FetchMore(bool first);
    bool CanLoadMore() const;

  public:
    OnlineSongsGui(const std::string &title,
                   const std::vector<QqMusicOnlineSong> &songs,
                   const OnlineListSpec &spec = {});
    ~OnlineSongsGui() override;

    tsl::elm::Element *createUI() override;
    void update() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override;
};
