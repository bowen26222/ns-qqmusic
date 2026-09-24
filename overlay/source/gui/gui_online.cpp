#include "gui_online.hpp"
#include "config/config.hpp"
#include "gui_qr_login.hpp"
#include "gui_online_songs.hpp"
#include "gui_online_collections.hpp"
#include "gui_keyboard.hpp"
#include "gui_playlist.hpp"
#include "elm_overlayframe.hpp"
#include "gui_screen_off.hpp"

#include <vector>

extern "C" void crashTrace(const char *s);

namespace {

    constexpr u32 kPageSongs = 50;   // 榜单/搜索/我喜欢 单页
    constexpr u32 kPageItems = 30;   // 专辑/歌手/歌单/收藏 单页
    constexpr u32 kRadarPages = 3;   // 雷达 3 页 ≈ 30 首

    // 收藏接口需要登录态：点击时重新查询，避免沿用创建菜单时的旧状态。
    bool IsLoggedIn() {
        QqMusicOnlineStatus status{};
        return R_SUCCEEDED(qqmusicOnlineGetStatus(&status)) && status.logged_in != 0;
    }

    // 辅助检查是否登录
} // namespace

OnlineGui::OnlineGui() {
    memset(&m_status, 0, sizeof(m_status));
    qqmusicOnlineGetStatus(&m_status);
}

OnlineGui::~OnlineGui() = default;

tsl::elm::Element *OnlineGui::createUI() {
    auto frame = new QqMusicOverlayFrame();
    auto list = new tsl::elm::List();

    // ---- 账号 ----
    if (m_status.logged_in) {
        std::string nick = m_status.nickname[0] ? m_status.nickname : m_status.uin;
        if (m_status.uin[0] && m_status.nickname[0] && nick != m_status.uin) {
            nick += " (" + std::string(m_status.uin) + ")";
        }
        list->addItem(new tsl::elm::CategoryHeader("账号: " + nick, true));
    } else {
        list->addItem(new tsl::elm::CategoryHeader("账号: 未登录", true));

        auto login_btn = new tsl::elm::ListItem("扫码登录");
        login_btn->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;
            tsl::changeTo<QrLoginGui>();
            return true;
        });
        list->addItem(login_btn);
    }

    // ---- 我的音乐（登录后可用） ----
    if (m_status.logged_in) {
        list->addItem(new tsl::elm::CategoryHeader("我的音乐"));

        auto fav_btn = new tsl::elm::ListItem("我喜欢");
        fav_btn->setClickListener([this, frame](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;

            crashTrace("favorites: fetch start");
            // 首屏取一批；「最新在前」由服务端倒序分页实现，后续可继续加载更多。
            std::vector<QqMusicOnlineSong> songs(online_fetch::kFavorites);
            u32 count = 0;
            const Result rc = qqmusicOnlineGetFavorites(0, online_fetch::kFavorites,
                                                        songs.data(), (u32)songs.size(), &count, nullptr);
            crashTrace("favorites: fetch done");
            if (R_SUCCEEDED(rc) && count > 0) {
                songs.resize(count);
                tsl::changeTo<OnlineSongsGui>("我喜欢", songs,
                                              OnlineListSpec::FavoritesFrom(count));
            } else {
                frame->setToast("我喜欢为空", "加载失败或暂无喜欢的歌曲");
                m_busy = false;
            }
            return true;
        });
        list->addItem(fav_btn);

    auto fav_albums_btn = new tsl::elm::ListItem("收藏的专辑");
    fav_albums_btn->setClickListener([this, frame](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        if (m_busy) return false;
        m_busy = true;

        if (!IsLoggedIn()) {
            frame->setToast("未登录", "请先扫码登录后查看收藏专辑");
            m_busy = false;
            return true;
        }

        crashTrace("fav albums: fetch start");
        std::vector<QqMusicOnlineCollection> items(kPageItems);
        u32 count = 0;
        const Result rc = qqmusicOnlineGetFavAlbums(0, kPageItems,
                                                    items.data(), (u32)items.size(), &count, nullptr);
        crashTrace("fav albums: fetch done");
        if (R_SUCCEEDED(rc)) {
            items.resize(count);
            tsl::changeTo<OnlineCollectionsGui>("收藏的专辑", items,
                                                OnlineCollectionsSpec::FavAlbumsFrom(count));
        } else {
            char code[32];
            std::snprintf(code, sizeof(code), "0x%08X", (unsigned)rc);
            frame->setToast("加载专辑失败", code);
            m_busy = false;
        }
        return true;
    });
    list->addItem(fav_albums_btn);

    auto fav_playlists_btn = new tsl::elm::ListItem("收藏的歌单");
    fav_playlists_btn->setClickListener([this, frame](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        if (m_busy) return false;
        m_busy = true;

        if (!IsLoggedIn()) {
            frame->setToast("未登录", "请先扫码登录后查看收藏歌单");
            m_busy = false;
            return true;
        }

        crashTrace("fav playlists: fetch start");
        std::vector<QqMusicOnlineCollection> items(kPageItems);
        u32 count = 0;
        const Result rc = qqmusicOnlineGetFavPlaylists(0, kPageItems,
                                                       items.data(), (u32)items.size(), &count, nullptr);
        crashTrace("fav playlists: fetch done");
        if (R_SUCCEEDED(rc)) {
            items.resize(count);
            tsl::changeTo<OnlineCollectionsGui>("收藏的歌单", items,
                                                OnlineCollectionsSpec::FavPlaylistsFrom(count));
        } else {
            char code[32];
            std::snprintf(code, sizeof(code), "0x%08X", (unsigned)rc);
            frame->setToast("加载歌单失败", code);
            m_busy = false;
        }
        return true;
    });
    list->addItem(fav_playlists_btn);

        u32 radio_on = 0;
        qqmusicGetRadioMode(&radio_on);
        std::string guess_title = radio_on ? "猜你喜欢 (电台已开启)" : "猜你喜欢 (心动电台)";
        std::string guess_sub = radio_on ? "点击关闭电台模式" : "无限推荐流，播完自动加载";
        auto guess_btn = new tsl::elm::ListItem(guess_title, guess_sub);
        guess_btn->setClickListener([this, frame, radio_on](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;

            if (radio_on) {
                qqmusicSetRadioMode(0);
                frame->setToast("电台模式已关闭", "已恢复为常规列表播放");
                m_busy = false;
                tsl::changeTo<OnlineGui>();
                return true;
            }

            crashTrace("guess: fetch start");
            std::vector<QqMusicOnlineSong> songs(kPageSongs);
            u32 count = 0, next_page = 1;
            const Result rc = qqmusicOnlineGetGuessRecommend(1, kRadarPages, songs.data(),
                                                             (u32)songs.size(), &count, &next_page);
            crashTrace("guess: fetch done");
            if (R_FAILED(rc) || count == 0) {
                frame->setToast("电台为空", "加载猜你喜欢曲目失败");
                m_busy = false;
                return true;
            }
            songs.resize(count);

            qqmusicSetRadioMode(1);
            const Result qrc = qqmusicOnlineEnqueueBatch(songs.data(), (u32)songs.size(), 1);
            crashTrace("guess: enqueue done");
            if (R_FAILED(qrc)) {
                frame->setToast("入队失败", "错误代码 0x" + std::to_string(qrc));
                m_busy = false;
                return true;
            }
            frame->setToast("心动电台已开启", "播放完当前曲目将自动加载新推荐");
            tsl::changeTo<PlaylistGui>();
            return true;
        });
        list->addItem(guess_btn);

        auto logout_btn = new tsl::elm::ListItem("退出登录");
        logout_btn->setClickListener([this](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;
            qqmusicOnlineLogout();
            memset(&m_status, 0, sizeof(m_status));
            tsl::swapTo<OnlineGui>();
            return true;
        });
        list->addItem(logout_btn);

        auto screen_off_btn = new tsl::elm::ListItem("息屏播放");
        screen_off_btn->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                tsl::changeTo<ScreenOffGui>();
                return true;
            }
            return false;
        });
        list->addItem(screen_off_btn);
    }

    // ---- 发现与搜索 ----
    list->addItem(new tsl::elm::CategoryHeader("发现与搜索"));

    auto search_btn = new tsl::elm::ListItem("搜索歌曲");
    search_btn->setClickListener([this, frame](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        if (m_busy) return false;
        tsl::changeTo<KeyboardGui>("搜索歌曲", "", [this, frame](const std::string &keyword) {
            if (keyword.empty()) {
                tsl::goBack();
                return;
            }
            crashTrace("search: fetch start");
            std::vector<QqMusicOnlineSong> songs(online_fetch::kSearch);
            u32 count = 0, total = 0;
            const Result rc = qqmusicOnlineSearch(keyword.c_str(), 1, online_fetch::kSearch,
                                                  songs.data(), (u32)songs.size(), &count, &total);
            crashTrace("search: fetch done");
            if (R_SUCCEEDED(rc) && count > 0) {
                songs.resize(count);
                tsl::swapTo<OnlineSongsGui>("歌曲搜索: " + keyword, songs,
                                            OnlineListSpec::SearchOf(keyword, 2));
            } else {
                tsl::swapTo<OnlineSongsGui>("歌曲搜索: " + keyword,
                                            std::vector<QqMusicOnlineSong>{},
                                            OnlineListSpec::SearchOf(keyword, 2));
            }
        });
        return true;
    });
    list->addItem(search_btn);

    auto album_search_btn = new tsl::elm::ListItem("搜索专辑");
    album_search_btn->setClickListener([this, frame](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        if (m_busy) return false;
        tsl::changeTo<KeyboardGui>("搜索专辑", "", [this, frame](const std::string &keyword) {
            if (keyword.empty()) {
                tsl::goBack();
                return;
            }
            crashTrace("search coll: fetch start");
            std::vector<QqMusicOnlineCollection> items(kPageItems);
            u32 count = 0, total = 0;
            const Result rc = qqmusicOnlineSearchCollections(keyword.c_str(), 2, 1, kPageItems,
                                                             items.data(), (u32)items.size(),
                                                             &count, &total);
            crashTrace("search coll: fetch done");
            if (R_SUCCEEDED(rc) && count > 0) {
                items.resize(count);
            } else {
                items.clear();
            }
            tsl::swapTo<OnlineCollectionsGui>("专辑搜索: " + keyword, items,
                                              OnlineCollectionsSpec::SearchOf(keyword, 2, 2));
        });
        return true;
    });
    list->addItem(album_search_btn);

    auto singer_search_btn = new tsl::elm::ListItem("搜索歌手");
    singer_search_btn->setClickListener([this, frame](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        if (m_busy) return false;
        tsl::changeTo<KeyboardGui>("搜索歌手", "", [this, frame](const std::string &keyword) {
            if (keyword.empty()) {
                tsl::goBack();
                return;
            }
            crashTrace("search coll: fetch start");
            std::vector<QqMusicOnlineCollection> items(kPageItems);
            u32 count = 0, total = 0;
            const Result rc = qqmusicOnlineSearchCollections(keyword.c_str(), 1, 1, kPageItems,
                                                             items.data(), (u32)items.size(),
                                                             &count, &total);
            crashTrace("search coll: fetch done");
            if (R_SUCCEEDED(rc) && count > 0) {
                items.resize(count);
            } else {
                items.clear();
            }
            tsl::swapTo<OnlineCollectionsGui>("歌手搜索: " + keyword, items,
                                              OnlineCollectionsSpec::SearchOf(keyword, 1, 2));
        });
        return true;
    });
    list->addItem(singer_search_btn);

    // ---- 榜单 ----
    list->addItem(new tsl::elm::CategoryHeader("官方榜单"));
    struct ChartEntry { u32 top_id; const char *name; };
    static const ChartEntry charts[] = {
        { 26, "热歌榜" },
        { 27, "新歌榜" },
        {  4, "流行指数榜" },
    };
    for (const auto &c : charts) {
        auto btn = new tsl::elm::ListItem(c.name);
        btn->setClickListener([this, frame, c](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            if (m_busy) return false;
            m_busy = true;

            crashTrace("chart: fetch start");
            std::vector<QqMusicOnlineSong> songs(online_fetch::kChart);
            u32 count = 0;
            const Result rc = qqmusicOnlineGetTopChart(c.top_id, 0, online_fetch::kChart,
                                                       songs.data(), (u32)songs.size(), &count);
            crashTrace("chart: fetch done");
            if (R_SUCCEEDED(rc) && count > 0) {
                songs.resize(count);
                tsl::changeTo<OnlineSongsGui>(c.name, songs,
                                              OnlineListSpec::ChartTop(c.top_id, count));
            } else {
                frame->setToast("排行榜为空", "加载榜单歌曲失败");
                m_busy = false;
            }
            return true;
        });
        list->addItem(btn);
    }
    frame->setContent(list);
    return frame;
}


void OnlineGui::update() {
    // 当从子页面（如歌曲列表、搜索结果等）按 B 返回时，本页面重新成为前台。
    // 重置 m_busy 锁，确保所有按钮能立即响应新的点击。
    m_busy = false;

    // 检查扫码登录成功或账号变动：自动刷新界面，展示「我的音乐」全套入口
    QqMusicOnlineStatus current{};
    if (R_SUCCEEDED(qqmusicOnlineGetStatus(&current))) {
        if (current.logged_in != m_status.logged_in || std::strcmp(current.uin, m_status.uin) != 0) {
            m_status = current;
            tsl::swapTo<OnlineGui>();
        }
    }
}

bool OnlineGui::handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        tsl::goBack();
        return true;
    }
    return false;
}
