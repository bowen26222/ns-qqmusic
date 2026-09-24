#include "gui_online_collections.hpp"

#include "config/config.hpp"
#include "gui_online_songs.hpp"
#include "pinyin_table.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void crashTrace(const char *s);

namespace {

    // 单次「加载更多」在集合列表里拉取的条目数（收藏接口单页上限较小，30 稳定）。
    constexpr u32 kPageItems = 30;
    // 集合条目展开成曲目列表时，首屏与后续页的歌曲数（与歌曲列表一致）。
    constexpr u32 kPageSongs = 50;

} // namespace

OnlineCollectionsGui::OnlineCollectionsGui(const std::string &title,
                                           const std::vector<QqMusicOnlineCollection> &items,
                                           const OnlineCollectionsSpec &spec)
    : m_title(title), m_items(items), m_spec(spec) {
    m_sort_mode = config::get_sort_mode();
    ApplySort();
}

// 0 = 服务端顺序；1 = 按字母 A-Z。
void OnlineCollectionsGui::ApplySort() {
    if (m_sort_mode != 1 || m_items.size() < 2) return;

    struct Keyed {
        char letter;
        const char *title;
        size_t idx;
    };
    std::vector<Keyed> keys;
    keys.reserve(m_items.size());
    for (size_t i = 0; i < m_items.size(); i++)
        keys.push_back({ pinyin::GetInitial(m_items[i].title), m_items[i].title, i });

    std::stable_sort(keys.begin(), keys.end(), [](const Keyed &a, const Keyed &b) {
        if (a.letter != b.letter) return a.letter < b.letter;
        return std::strcmp(a.title, b.title) < 0;
    });

    std::vector<QqMusicOnlineCollection> sorted;
    sorted.reserve(m_items.size());
    for (const auto &k : keys) sorted.push_back(m_items[k.idx]);
    m_items.swap(sorted);
}

OnlineCollectionsGui::~OnlineCollectionsGui() = default;

bool OnlineCollectionsGui::CanLoadMore() const {
    return m_spec.kind != OnlineCollectionsSpec::Kind::None;
}

tsl::elm::Element *OnlineCollectionsGui::createUI() {
    crashTrace("collections: createUI start");
    m_frame = new QqMusicOverlayFrame();
    m_frame->setDescription("\uE0E1 返回  \uE0E0 查看曲目");
    m_list = new tsl::elm::List();
    m_frame->setContent(m_list);
    Rebuild();
    crashTrace("collections: createUI done");
    return m_frame;
}

void OnlineCollectionsGui::update() {
    m_busy = false;
    if (m_load_first && !m_busy) {
        m_load_first = false;
        m_busy = true;
        FetchMore(true);
    }
    if (m_reload) {
        m_reload = false;
        crashTrace("collections: rebuild start");
        Rebuild();
        crashTrace("collections: rebuild done");
    }
}

void OnlineCollectionsGui::OpenSongs(const QqMusicOnlineCollection &item) {
    const std::string title = item.title[0] ? item.title : "曲目列表";

    crashTrace("collections: open songs start");
    // 一次取全整个列表（专辑 300 / 歌手 100 / 歌单 200），便于直接整单入队。
    const u32 fetch_size = (item.kind == 2) ? online_fetch::kAlbum
                         : (item.kind == 1) ? online_fetch::kSinger
                                            : online_fetch::kPlaylist;
    std::vector<QqMusicOnlineSong> songs(fetch_size);
    u32 count = 0, total = 0;
    Result rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    OnlineListSpec spec;

    switch (item.kind) {
        case 2: // 专辑
            rc = qqmusicOnlineGetAlbumSongs(item.id, 0, fetch_size, songs.data(),
                                            (u32)songs.size(), &count, &total);
            spec = OnlineListSpec::AlbumOf(item.id, title, count);
            break;
        case 1: // 歌手
            rc = qqmusicOnlineGetSingerSongs(item.id, 0, fetch_size, songs.data(),
                                             (u32)songs.size(), &count, &total);
            spec = OnlineListSpec::SingerOf(item.id, title, count);
            break;
        case 3: // 歌单（id 为十进制 disstid）
            rc = qqmusicOnlineGetPlaylistSongs(std::strtoull(item.id, nullptr, 10), 0, fetch_size,
                                               songs.data(), (u32)songs.size(), &count, &total);
            spec = OnlineListSpec::PlaylistOf(item.id, title, count);
            break;
        default:
            crashTrace("collections: open songs unsupported kind");
            if (m_frame) m_frame->setToast("不支持的条目", "无法解析该条目的歌曲");
            m_busy = false;
            return;
    }
    crashTrace("collections: open songs fetch done");

    if (R_SUCCEEDED(rc) && count > 0) {
        songs.resize(count);
        crashTrace("collections: open songs changeTo start");
        tsl::changeTo<OnlineSongsGui>(title, songs, spec);
        crashTrace("collections: open songs changeTo done");
        return;
    }

    // 把确切原因显式暴露出来（结果码），便于一次复现即定位。
    char code[32];
    std::snprintf(code, sizeof(code), "0x%08X", (unsigned)rc);
    if (m_frame) {
        m_frame->setToast(R_FAILED(rc) ? "加载歌曲失败" : "无曲目",
                          R_FAILED(rc) ? std::string(code)
                                       : std::string("该条目暂无曲目") + " (" + code + ")");
    }
    m_busy = false;
}

// first=true：请求第一页；只有成功后才替换旧列表和游标。
void OnlineCollectionsGui::FetchMore(bool first) {
    std::vector<QqMusicOnlineCollection> page(kPageItems);
    u32 count = 0;
    const u32 request_cursor = first
        ? (m_spec.kind == OnlineCollectionsSpec::Kind::Search ? 1 : 0)
        : m_spec.cursor;
    u32 next_cursor = m_spec.cursor;
    u32 total = 0;
    Result rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    std::string failed_msg = "没有更多内容";

    switch (m_spec.kind) {
        case OnlineCollectionsSpec::Kind::FavAlbums: {
            QqMusicOnlineStatus status{};
            if (R_FAILED(qqmusicOnlineGetStatus(&status)) || !status.logged_in) {
                if (m_frame) m_frame->setToast("未登录", "请先登录查看收藏专辑");
                m_busy = false;
                return;
            }
            rc = qqmusicOnlineGetFavAlbums(request_cursor, kPageItems,
                                           page.data(), (u32)page.size(), &count, &total);
            next_cursor = request_cursor + count;
            failed_msg = "没有更多收藏专辑";
            break;
        }
        case OnlineCollectionsSpec::Kind::FavPlaylists: {
            QqMusicOnlineStatus status{};
            if (R_FAILED(qqmusicOnlineGetStatus(&status)) || !status.logged_in) {
                if (m_frame) m_frame->setToast("未登录", "请先登录查看收藏歌单");
                m_busy = false;
                return;
            }
            rc = qqmusicOnlineGetFavPlaylists(request_cursor, kPageItems,
                                              page.data(), (u32)page.size(), &count, &total);
            next_cursor = request_cursor + count;
            failed_msg = "没有更多收藏歌单";
            break;
        }
        case OnlineCollectionsSpec::Kind::Search: {
            rc = qqmusicOnlineSearchCollections(m_spec.query.c_str(), m_spec.search_kind,
                                                request_cursor, kPageItems, page.data(),
                                                (u32)page.size(), &count, &total);
            next_cursor = request_cursor + 1;
            failed_msg = (m_spec.search_kind == 2) ? "没有更多专辑" : "没有更多歌手";
            break;
        }
        default:
            break;
    }

    if (R_SUCCEEDED(rc)) {
        page.resize(count);
        if (first) m_items.clear();

        u32 added = 0;
        for (const auto &e : page) {
            const bool dup = std::any_of(m_items.begin(), m_items.end(),
                [&](const QqMusicOnlineCollection &o) { return std::strcmp(o.id, e.id) == 0; });
            if (!dup) {
                m_items.push_back(e);
                added++;
            }
        }
        m_spec.cursor = next_cursor;
        ApplySort();
        m_reload = true;
        if (m_frame) {
            if (added) {
                m_frame->setToast("已加载 " + std::to_string(added) + " 个",
                                  "当前共 " + std::to_string(m_items.size()) + " 个条目");
            } else if (count == 0) {
                m_frame->setToast("没有更多内容", failed_msg);
            } else {
                m_frame->setToast("暂无新内容", "已展示返回的所有内容");
            }
        }
    } else {
        char code[32];
        std::snprintf(code, sizeof(code), "0x%08X", (unsigned)rc);
        if (m_frame) m_frame->setToast("加载失败", std::string(code));
        m_reload = true;
    }
    m_busy = false;
}

void OnlineCollectionsGui::Rebuild() {
    crashTrace("collections: rebuild body entry");
    m_list->clear();
    removeFocus();
    crashTrace("collections: list cleared");

    m_list->addItem(new tsl::elm::CategoryHeader(
        m_title + " (" + std::to_string(m_items.size()) + ")", true));

    // 排序切换后重新取第一页，失败时保留已显示的内容。
    {
        static const char *kSortNames[] = { "默认", "按字母 A-Z" };
        std::string sub = (m_sort_mode == 0)
            ? "QQ音乐服务端顺序"
            : "按名称拼音首字母排列（已加载部分）";

        auto sort_btn = new tsl::elm::ListItem(std::string("排序: ") + kSortNames[m_sort_mode], sub);
        sort_btn->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_sort_mode = (m_sort_mode + 1) % 2;
            config::set_sort_mode(m_sort_mode);
            m_reload = true;
            m_load_first = true;
            return true;
        });
        m_list->addItem(sort_btn);
    }

    for (const auto &entry : m_items) {
        std::string sub = entry.subtitle[0] ? entry.subtitle : "";
        if (entry.extra[0]) {
            if (!sub.empty()) sub += " \u2022 ";
            sub += entry.extra;
        }

        auto item = new tsl::elm::ListItem(entry.title[0] ? entry.title : "未知", sub);
        item->setClickListener([this, entry](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;
            OpenSongs(entry);
            return true;
        });
        m_list->addItem(item);
    }

    if (m_items.empty()) {
        m_list->addItem(new tsl::elm::CategoryHeader("未找到内容"));
    }


    if (m_spec.kind == OnlineCollectionsSpec::Kind::FavAlbums ||
        m_spec.kind == OnlineCollectionsSpec::Kind::FavPlaylists) {
        const std::string refresh_title = (m_spec.kind == OnlineCollectionsSpec::Kind::FavAlbums)
            ? "刷新收藏专辑" : "刷新歌单列表";
        auto refresh = new tsl::elm::ListItem(refresh_title, "重新从第一页获取；失败时保留当前列表");
        refresh->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A) || m_busy) return false;
            m_load_first = true;
            return true;
        });
        m_list->addItem(refresh);
    }
    if (CanLoadMore()) {
        auto more = new tsl::elm::ListItem("加载更多 +" + std::to_string(kPageItems),
                                           "从网络加载下一页");
        more->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;
            FetchMore(false);
            return true;
        });
        m_list->addItem(more);
    }
    crashTrace("collections: rebuild rows added");
}

bool OnlineCollectionsGui::handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        tsl::goBack();
        return true;
    }
    return false;
}
