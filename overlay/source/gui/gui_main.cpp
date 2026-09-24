#include "gui_main.hpp"

#include "elm_overlayframe.hpp"
#include "elm_volume.hpp"
#include "gui_browser.hpp"
#include "gui_playlist.hpp"
#include "gui_lyric.hpp"
#include "gui_online.hpp"
#include "gui_settings.hpp"
#include "gui_screen_off.hpp"

MainGui::MainGui() {
    m_status_bar    = new StatusBar();
}

tsl::elm::Element *MainGui::createUI() {
    auto frame = new QqMusicOverlayFrame();
    m_status_bar->setFrame(frame);
    auto list  = new tsl::elm::List();

    /* Current track. */
    list->addItem(this->m_status_bar, StatusBar::HEIGHT);

    /* Queue (current playback queue). */
    auto queue_button = new tsl::elm::ListItem("当前播放列表");
    queue_button->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<PlaylistGui>();
            return true;
        }
        return false;
    });
    list->addItem(queue_button);
    /* 实时歌词：全屏动态滚动歌词、点句即播、左右摇杆下压(L3+R3)返回。 */
    auto lyric_button = new tsl::elm::ListItem("实时歌词", "全屏动态滚动歌词 · 点句即播");
    lyric_button->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<LyricGui>();
            return true;
        }
        return false;
    });
    list->addItem(lyric_button);

    /* QQ 音乐在线：扫码登录、曲库搜索、榜单推荐。 */
    auto online_button = new tsl::elm::ListItem("QQ音乐在线");
    online_button->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<OnlineGui>();
            return true;
        }
        return false;
    });
    list->addItem(online_button);

    /* 本地曲库：浏览 sdmc:/music 并加入播放队列。 */
    auto browse_button = new tsl::elm::ListItem("本地音乐");
    browse_button->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<BrowserGui>();
            return true;
        }
        return false;
    });
    list->addItem(browse_button);

    /* Settings / sign-in. */
    auto settings_button = new tsl::elm::ListItem("设置");
    settings_button->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<SettingsGui>();
            return true;
        }
        return false;
    });
    list->addItem(settings_button);

    auto screen_off_btn = new tsl::elm::ListItem("息屏播放");
    screen_off_btn->setClickListener([](u64 keys) {
        if (keys & HidNpadButton_A) {
            tsl::changeTo<ScreenOffGui>();
            return true;
        }
        return false;
    });
    list->addItem(screen_off_btn);

    list->addItem(new ElmVolume("\uE13C", "音乐音量", frame, qqmusicGetVolume, qqmusicSetVolume));
    // Change the running title's actual volume, not only the default for future titles.
    list->addItem(new ElmVolume("\uE13C", "游戏音量", frame, qqmusicGetTitleVolume, qqmusicSetTitleVolume));

    auto exit_button = new tsl::elm::ListItem("关闭QQ音乐");
    exit_button->setClickListener([frame](u64 keys) {
        if (keys & HidNpadButton_A) {
            const Result rc = qqmusicQuit();
            if (R_FAILED(rc))
                frame->showError("关闭QQ音乐失败", rc);
            else
                tsl::goBack();
            return true;
        }
        return false;
    });
    list->addItem(exit_button);

    frame->setContent(list);

    return frame;
}

void MainGui::update() {
    static u8 tick = 0;
    /* Update status 4 times per second. */
    if ((tick % 15) == 0)
        this->m_status_bar->update();
    tick++;
}
