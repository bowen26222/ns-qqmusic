#pragma once

#include "client.h"
#include "elm_overlayframe.hpp"
#include <tesla.hpp>
#include <string>
#include <vector>

// 在线集合列表（专辑 / 歌手 / 歌单）的来源与分页游标：与歌曲列表同构，
// 让同一套「加载更多」逻辑驱动全部集合列表。
struct OnlineCollectionsSpec {
    enum class Kind {
        None,         // 固定列表（如首屏之外不再翻页的结果）
        FavAlbums,    // 收藏的专辑：cursor = 已加载条目数（作为 offset）
        FavPlaylists, // 收藏的歌单：cursor = 已加载条目数（作为 offset）
        Search,       // 集合搜索：cursor = 下一个页码
    };

    Kind kind = Kind::None;
    u32 cursor = 0;
    std::string query;
    u32 search_kind = 0; // Search: 1=歌手 2=专辑

    static OnlineCollectionsSpec FavAlbumsFrom(u32 loaded) {
        OnlineCollectionsSpec s;
        s.kind = Kind::FavAlbums;
        s.cursor = loaded;
        return s;
    }

    static OnlineCollectionsSpec FavPlaylistsFrom(u32 loaded) {
        OnlineCollectionsSpec s;
        s.kind = Kind::FavPlaylists;
        s.cursor = loaded;
        return s;
    }

    static OnlineCollectionsSpec SearchOf(const std::string &q, u32 kind, u32 next_page) {
        OnlineCollectionsSpec s;
        s.kind = Kind::Search;
        s.query = q;
        s.search_kind = kind;
        s.cursor = next_page;
        return s;
    }
};

class OnlineCollectionsGui final : public tsl::Gui {
  private:
    std::string m_title;
    std::vector<QqMusicOnlineCollection> m_items;
    OnlineCollectionsSpec m_spec{};
    QqMusicOverlayFrame *m_frame = nullptr;
    tsl::elm::List *m_list = nullptr;
    bool m_reload = false;
    bool m_busy = false;
    bool m_load_first = false;
    int m_sort_mode = 0;

    void Rebuild();
    void ApplySort();
    void FetchMore(bool first);
    bool CanLoadMore() const;
    // 打开条目对应的曲目列表（专辑 / 歌手 / 歌单），失败时提示并复位 m_busy。
    void OpenSongs(const QqMusicOnlineCollection &item);

  public:
    OnlineCollectionsGui(const std::string &title,
                         const std::vector<QqMusicOnlineCollection> &items,
                         const OnlineCollectionsSpec &spec = {});
    ~OnlineCollectionsGui() override;

    tsl::elm::Element *createUI() override;
    void update() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override;
};
