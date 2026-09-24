#include "gui_online_songs.hpp"

#include "config/config.hpp"
#include "pinyin_table.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void crashTrace(const char *s);

namespace {

    // 单次「加载更多」在榜单/搜索/我喜欢里拉取的歌曲数（接口实测上限：榜单 >=100，搜索 50）。
    constexpr u32 kPageSongs = 50;

} // namespace

namespace {

    // 每个来源的单次取数上限（详见 gui_online_songs.hpp 的 online_fetch）
    u32 PageSizeFor(OnlineListSpec::Kind kind) {
        switch (kind) {
            case OnlineListSpec::Kind::Favorites: return online_fetch::kFavorites;
            case OnlineListSpec::Kind::Chart:     return online_fetch::kChart;
            case OnlineListSpec::Kind::Search:    return online_fetch::kSearch;
            case OnlineListSpec::Kind::Album:     return online_fetch::kAlbum;
            case OnlineListSpec::Kind::Singer:    return online_fetch::kSinger;
            case OnlineListSpec::Kind::Playlist:  return online_fetch::kPlaylist;
            default:                              return kPageSongs;
        }
    }

} // namespace

OnlineSongsGui::OnlineSongsGui(const std::string &title,
                               const std::vector<QqMusicOnlineSong> &songs,
                               const OnlineListSpec &spec)
    : m_title(title), m_songs(songs), m_spec(spec),
      m_visible_count(std::min<size_t>(30, songs.size())) {
    m_sort_mode = config::get_sort_mode();
    ApplySort();
}

OnlineSongsGui::~OnlineSongsGui() = default;

bool OnlineSongsGui::CanLoadMore() const {
    return m_spec.kind != OnlineListSpec::Kind::None;
}

// 按当前排序方式整理已加载的曲目。0 = 默认（直接用服务端顺序，新的在前）；1 = 按字母。
void OnlineSongsGui::ApplySort() {
    if (m_songs.size() < 2 || m_sort_mode != 1) return;

    // 首字母 A-Z：预计算拼音首字母，避免比较器内重复二分查找。
    struct Keyed {
        char letter;
        const char *title;
        size_t idx;
    };
    std::vector<Keyed> keys;
    keys.reserve(m_songs.size());
    for (size_t i = 0; i < m_songs.size(); i++)
        keys.push_back({ pinyin::GetInitial(m_songs[i].title), m_songs[i].title, i });

    std::stable_sort(keys.begin(), keys.end(), [](const Keyed &a, const Keyed &b) {
        if (a.letter != b.letter) return a.letter < b.letter;
        return std::strcmp(a.title, b.title) < 0;
    });

    std::vector<QqMusicOnlineSong> sorted;
    sorted.reserve(m_songs.size());
    for (const auto &k : keys) sorted.push_back(m_songs[k.idx]);
    m_songs.swap(sorted);
}

tsl::elm::Element *OnlineSongsGui::createUI() {
    crashTrace("songs: createUI start");
    m_frame = new QqMusicOverlayFrame();
    m_frame->setDescription("\uE0E1 返回  \uE0E0 播放  \uE0E2 加入队列");
    m_list = new tsl::elm::List();
    m_frame->setContent(m_list);
    Rebuild();
    crashTrace("songs: createUI done");
    return m_frame;
}

void OnlineSongsGui::update() {
    if (m_load_first && !m_busy) {
        m_load_first = false;
        m_busy = true;
        FetchMore(true);
    }
    if (m_reload) {
        m_reload = false;
        crashTrace("songs: rebuild start");
        Rebuild();
        crashTrace("songs: rebuild done");
    }
}

// first=true：清空后重新拉第一页（排序切换后使用）；false：按游标追加下一页。
void OnlineSongsGui::FetchMore(bool first) {
    const u32 page_size = PageSizeFor(m_spec.kind);
    std::vector<QqMusicOnlineSong> page(page_size);
    u32 count = 0;
    // first=true 表示「重新从第一页取」（排序切换后使用）：必须显式用游标 0 请求，
    // 否则会把上一次的游标当成起始位置，取回半截数据后覆盖掉完整列表。
    const u32 request_cursor = first ? 0 : m_spec.cursor;
    u32 next_cursor = request_cursor;
    Result rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    std::string failed_msg = "没有更多内容";

    switch (m_spec.kind) {
        case OnlineListSpec::Kind::Chart:
            rc = qqmusicOnlineGetTopChart(m_spec.chart_id, request_cursor, page_size,
                                          page.data(), (u32)page.size(), &count);
            next_cursor = request_cursor + count;
            failed_msg = "榜单没有更多歌曲";
            break;
        case OnlineListSpec::Kind::Search: {
            u32 total = 0;
            rc = qqmusicOnlineSearch(m_spec.query.c_str(), request_cursor, page_size,
                                     page.data(), (u32)page.size(), &count, &total);
            next_cursor = request_cursor + 1;
            failed_msg = "搜索没有更多结果";
            break;
        }
        case OnlineListSpec::Kind::Favorites: {
            u32 total = 0;
            rc = qqmusicOnlineGetFavorites(request_cursor, page_size,
                                           page.data(), (u32)page.size(), &count, &total);
            next_cursor = request_cursor + count;
            failed_msg = "我喜欢没有更多歌曲";
            break;
        }
        case OnlineListSpec::Kind::Album: {
            u32 total = 0;
            rc = qqmusicOnlineGetAlbumSongs(m_spec.id.c_str(), request_cursor, page_size,
                                            page.data(), (u32)page.size(), &count, &total);
            next_cursor = request_cursor + count;
            failed_msg = "专辑没有更多歌曲";
            break;
        }
        case OnlineListSpec::Kind::Singer: {
            u32 total = 0;
            rc = qqmusicOnlineGetSingerSongs(m_spec.id.c_str(), request_cursor, page_size,
                                             page.data(), (u32)page.size(), &count, &total);
            next_cursor = request_cursor + count;
            failed_msg = "歌手没有更多歌曲";
            break;
        }
        case OnlineListSpec::Kind::Playlist: {
            u32 total = 0;
            rc = qqmusicOnlineGetPlaylistSongs(std::strtoull(m_spec.id.c_str(), nullptr, 10),
                                               request_cursor, page_size,
                                               page.data(), (u32)page.size(), &count, &total);
            next_cursor = request_cursor + count;
            failed_msg = "歌单没有更多歌曲";
            break;
        }
        default:
            break;
    }

    if (R_SUCCEEDED(rc) && count > 0) {
        page.resize(count);
        if (first) m_songs.clear();

        // 各来源的分页都可能有重叠，按 songmid 去重后再追加。
        u32 added = 0;
        for (const auto &s : page) {
            const bool dup = std::any_of(m_songs.begin(), m_songs.end(),
                [&](const QqMusicOnlineSong &e) { return std::strcmp(e.songmid, s.songmid) == 0; });
            if (!dup) {
                m_songs.push_back(s);
                added++;
            }
        }
        m_spec.cursor = next_cursor;
        ApplySort();
        if (first)
            m_visible_count = std::min<size_t>(30, m_songs.size());
        else
            m_visible_count = std::min<size_t>(m_visible_count + count, m_songs.size());
        m_reload = true;
        if (m_frame) {
            if (added) {
                m_frame->setToast("已加载 " + std::to_string(added) + " 首",
                                  "当前共 " + std::to_string(m_songs.size()) + " 首歌曲");
            } else {
                m_frame->setToast("暂无新曲目", "已展示返回的所有曲目");
            }
        }
    } else {
        // 把确切原因显式暴露出来（结果码 / 空页），便于一次复现即定位。
        char code[32];
        std::snprintf(code, sizeof(code), "0x%08X", (unsigned)rc);
        if (m_frame) {
            if (R_FAILED(rc))
                m_frame->setToast("加载失败", std::string(code));
            else
                m_frame->setToast("没有更多歌曲", failed_msg + " (" + code + ")");
        }
        m_reload = true;
    }
    m_busy = false;
}

void OnlineSongsGui::Rebuild() {
    crashTrace("songs: rebuild body entry");
    m_list->clear();
    removeFocus();
    crashTrace("songs: list cleared");

    m_list->addItem(new tsl::elm::CategoryHeader(
        m_title + " (" + std::to_string(m_songs.size()) + ")", true));

    // 排序方式切换：默认顺序 → 最新在前 → 首字母 A-Z → …
    {
        static const char *kSortNames[] = { "默认", "按字母 A-Z" };
        std::string sub = (m_sort_mode == 0)
            ? "QQ音乐默认顺序（最近添加的在前）"
            : "按歌名拼音首字母排列（已加载部分）";

        auto sort_btn = new tsl::elm::ListItem(
            std::string("排序: ") + kSortNames[m_sort_mode], sub);
        sort_btn->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_sort_mode = (m_sort_mode + 1) % 2;
            config::set_sort_mode(m_sort_mode);
            // 排序变化需要重新取数：交给 FetchMore(true) 从第一页重取，
            // 只有新数据取回成功才替换已显示内容（失败时保留旧列表，
            // 否则一次网络抖动就会把用户正在看的曲目全清空）。
            m_reload = true;
            m_load_first = true;
            return true;
        });
        m_list->addItem(sort_btn);
    }

    if (!m_songs.empty()) {
        auto play_all = new tsl::elm::ListItem("播放全部", "替换当前队列并播放");
        play_all->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;
            const Result rc = qqmusicOnlineEnqueueBatch(m_songs.data(), (u32)m_songs.size(), 1);
            if (R_SUCCEEDED(rc)) {
                if (m_frame)
                    m_frame->setToast("正在播放全部", "已入队 " + std::to_string(m_songs.size()) + " 首歌曲");
            } else if (m_frame) {
                m_frame->setToast("播放全部失败", "错误代码 0x" + std::to_string(rc));
            }
            m_busy = false;
            return true;
        });
        m_list->addItem(play_all);

        auto append_all = new tsl::elm::ListItem("全部加入队列", "追加到当前播放列表");
        append_all->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;
            const Result rc = qqmusicOnlineEnqueueBatch(m_songs.data(), (u32)m_songs.size(), 0);
            if (R_SUCCEEDED(rc)) {
                if (m_frame)
                    m_frame->setToast("已加入队列", "已追加 " + std::to_string(m_songs.size()) + " 首歌曲");
            } else if (m_frame) {
                m_frame->setToast("加入队列失败", "错误代码 0x" + std::to_string(rc));
            }
            m_busy = false;
            return true;
        });
        m_list->addItem(append_all);
    }

    const size_t show_n = std::min<size_t>(m_visible_count, m_songs.size());
    for (size_t i = 0; i < show_n; i++) {
        const auto &song = m_songs[i];
        std::string sub = song.artist[0] ? song.artist : "";
        if (song.album[0]) {
            if (!sub.empty()) sub += " \u2022 ";
            sub += song.album;
        }

        auto item = new tsl::elm::ListItem(song.title[0] ? song.title : "未知曲目", sub);
        item->setClickListener([this, song](u64 keys) {
            if (m_busy) return false;
            if (keys & HidNpadButton_A) {
                m_busy = true;
                const Result rc = qqmusicOnlinePlaySong(&song, 1);
                if (m_frame)
                    m_frame->setToast(R_SUCCEEDED(rc) ? "正在播放" : "播放失败", song.title);
                m_busy = false;
                return true;
            }
            if (keys & HidNpadButton_X) {
                m_busy = true;
                const Result rc = qqmusicOnlinePlaySong(&song, 0);
                if (m_frame && R_SUCCEEDED(rc))
                    m_frame->setToast("已加入队列", song.title);
                m_busy = false;
                return true;
            }
            return false;
        });
        m_list->addItem(item);
    }

    if (m_songs.empty()) {
        m_list->addItem(new tsl::elm::CategoryHeader("未找到歌曲"));
    }

    if (m_visible_count < m_songs.size()) {
        const size_t rem = m_songs.size() - m_visible_count;
        const size_t step = std::min<size_t>(rem, 30);
        auto expand = new tsl::elm::ListItem("展开更多 +" + std::to_string(step) + " (剩余 " + std::to_string(rem) + " 首)", "显示下一批歌曲");
        expand->setClickListener([this, step](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            m_visible_count += step;
            m_reload = true;
            return true;
        });
        m_list->addItem(expand);
    } else if (CanLoadMore()) {
        auto more = new tsl::elm::ListItem("加载更多 +" + std::to_string(PageSizeFor(m_spec.kind)),
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
    crashTrace("songs: rebuild rows added");
}

bool OnlineSongsGui::handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        tsl::goBack();
        return true;
    }
    return false;
}
