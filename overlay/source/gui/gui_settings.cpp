#include "gui_settings.hpp"

#include "client.h"
#include "config/config.hpp"
#include "elm_overlayframe.hpp"
#include "elm_volume.hpp"
#include "gui_keyboard.hpp"
#include "gui_lyric.hpp"

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>

namespace {
    constexpr int kSeekOptions[] = { 5, 10, 15, 30, 60 };

    std::string SeekText(int seconds) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "快进跨度: %d秒", seconds);
        return buf;
    }

    // 缓存数量数字框：左右 ±1、L/R ±10、A 键调出数字键盘直接输入、支持触控点按。
    // 仅在「缓存策略 = 指定数量」时出现。
    class ElmCacheLimit final : public tsl::elm::Element {
      public:
        static constexpr s32 HEIGHT = 62;

        explicit ElmCacheLimit(QqMusicOverlayFrame *frame) : m_frame(frame) {
            m_value = config::get_cache_limit();
        }

        tsl::elm::Element *requestFocus(tsl::elm::Element *, tsl::FocusDirection) override { return this; }

        bool onClick(u64 keys) override {
            if (keys & HidNpadButton_A) { OpenInput(); return true; }
            if (keys & HidNpadButton_AnyLeft)  { Add(-1);  return true; }
            if (keys & HidNpadButton_AnyRight) { Add(+1);  return true; }
            if (keys & HidNpadButton_L)        { Add(-10); return true; }
            if (keys & HidNpadButton_R)        { Add(+10); return true; }
            return false;
        }

        bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32, s32, s32, s32, s32) override {
            if (event != tsl::elm::TouchEvent::Release) return false;
            const s32 box_x = this->getX() + this->getWidth() - kBoxW - 20;
            const s32 box_mid = box_x + kBoxW / 2;
            if (currX < box_x) return false;
            if (currX < box_mid) Add(-1);
            else if (currX < box_x + kBoxW) Add(+1);
            else return false;
            return true;
        }

        void draw(tsl::gfx::Renderer *renderer) override {
            const s32 x = this->getX(), y = this->getY(), w = this->getWidth();

            if (this->m_focused)
                renderer->drawRect(x + 4, y, w - 8, this->getHeight(), a(tsl::style::color::ColorFrame));

            renderer->drawString("缓存数量 (首)", false, x + 19, y + 34, 23, a(tsl::style::color::ColorText));
            renderer->drawString("左右 ±1   L/R ±10   A 手动输入", false, x + 19, y + this->getHeight() - 5, 14,
                                 a(tsl::style::color::ColorDescription));

            const s32 box_x = x + w - kBoxW - 20;
            const s32 box_y = y + (this->getHeight() - kBoxH) / 2;
            renderer->drawRect(box_x, box_y, kBoxW, kBoxH, a(tsl::style::color::ColorHeaderBar));
            renderer->drawRect(box_x, box_y + kBoxH - 2, kBoxW, 2, a(tsl::style::color::ColorText));

            renderer->drawString("\u25C0", false, box_x + 12, box_y + 28, 20, a(tsl::style::color::ColorHighlight));
            renderer->drawString("\u25B6", false, box_x + kBoxW - 30, box_y + 28, 20, a(tsl::style::color::ColorHighlight));

            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d 首", m_value);
            auto [tw, th] = renderer->drawString(buf, false, 0, 0, 22, tsl::style::color::ColorTransparent);
            renderer->drawString(buf, false, box_x + (kBoxW - (s32)tw) / 2, box_y + 28, 22,
                                 a(tsl::style::color::ColorText));
        }

        void layout(u16, u16, u16, u16) override {
            this->setBoundaries(this->getX(), this->getY(), this->getWidth(), HEIGHT);
        }

      private:
        static constexpr s32 kBoxW = 190;
        static constexpr s32 kBoxH = 40;

        void Add(int delta) {
            int v = m_value + delta;
            if (v < 1) v = 1;
            if (v > 2000) v = 2000;
            if (v == m_value) return;
            m_value = v;
            config::set_cache_limit(v);
        }

        void OpenInput() {
            // 数字键盘：直接键入 1..2000；完成后回到设置页刷新数字框。
            tsl::swapTo<KeyboardGui>("缓存数量 (1-2000)", std::to_string(m_value),
                                     [](const std::string &text) {
                const int v = std::atoi(text.c_str());
                config::set_cache_limit(v);
                tsl::swapTo<SettingsGui>();
            });
        }

        QqMusicOverlayFrame *m_frame = nullptr;
        int m_value = 20;
    };
}

SettingsGui::SettingsGui() {
    m_seek_skip = config::get_seek_skip_seconds();
}

tsl::elm::Element *SettingsGui::createUI() {
    auto frame = new QqMusicOverlayFrame();
    m_list = new tsl::elm::List();
    m_list->addItem(new tsl::elm::CategoryHeader("音乐目录", true));
    m_list->addItem(new tsl::elm::ListItem("默认目录: /music"));
    m_list->addItem(new tsl::elm::CategoryHeader("播放设置", true));
    auto seek_item = new tsl::elm::ListItem(SeekText(m_seek_skip));
    seek_item->setClickListener([seek_item](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        const int cur = config::get_seek_skip_seconds();
        int next = 15;
        for (size_t i = 0; i < std::size(kSeekOptions); ++i) {
            if (cur == kSeekOptions[i]) {
                next = kSeekOptions[(i + 1) % std::size(kSeekOptions)];
                break;
            }
        }
        config::set_seek_skip_seconds(next);
        seek_item->setText(SeekText(config::get_seek_skip_seconds()));
        return true;
    });
    m_list->addItem(seek_item);
    m_list->addItem(new ElmVolume("\uE13C", "音乐音量", frame, qqmusicGetVolume, qqmusicSetVolume));

    // ---- 在线列表排序 ----
    static const char *kSortNames[] = { "默认", "按字母 A-Z" };
    static const char *kSortHints[] = {
        "QQ音乐默认顺序（最近添加/收藏的在前）",
        "按名称拼音首字母排列",
    };
    m_list->addItem(new tsl::elm::CategoryHeader("在线列表排序", true));
    {
        const int mode = config::get_sort_mode();
        auto sort_item = new tsl::elm::ListItem(std::string("排序方式: ") + kSortNames[mode], kSortHints[mode]);
        sort_item->setClickListener([frame, sort_item](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            const int next = (config::get_sort_mode() + 1) % 2;
            config::set_sort_mode(next);
            sort_item->setText(std::string("排序方式: ") + kSortNames[next]);
            sort_item->setValue(kSortHints[next]);
            frame->setToast("排序已切换", kSortNames[next]);
            return true;
        });
        m_list->addItem(sort_item);
    }

    // ---- 在线缓存 ----
    m_list->addItem(new tsl::elm::CategoryHeader("在线缓存", true));
    {
        const int cache_mode = config::get_cache_mode();
        auto mode_item = new tsl::elm::ListItem(
            std::string("缓存策略: ") + (cache_mode == 0 ? "跟随播放队列" : "指定数量"),
            cache_mode == 0 ? "队列有多少首就缓存多少首" : "按下方数量缓存（超出按时间淘汰）");
        mode_item->setClickListener([](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            config::set_cache_mode(config::get_cache_mode() == 0 ? 1 : 0);
            // 切换后需要重新构建页面：数字框只在「指定数量」时出现
            tsl::swapTo<SettingsGui>();
            return true;
        });
        m_list->addItem(mode_item);

        if (cache_mode == 1)
            m_list->addItem(new ElmCacheLimit(frame));
    }

    m_list->addItem(new tsl::elm::CategoryHeader("游戏独立控制", true));
    u32 active_valid = 0;
    u64 active_tid = 0;
    char active_name[64] = {};
    Result rc = qqmusicGetActiveApp(&active_valid, &active_tid, active_name, sizeof(active_name));
    if (R_FAILED(rc)) {
        frame->showError("读取当前游戏失败", rc);
        m_list->addItem(new tsl::elm::ListItem("无法获取当前游戏"));
    } else if (!active_valid) {
        m_list->addItem(new tsl::elm::ListItem("当前无前台运行游戏"));
    } else {
        m_list->addItem(new tsl::elm::ListItem(std::string("当前游戏: ") + active_name));
        u32 has = 0, enabled = 1;
        rc = qqmusicGetTitleEnabled(active_tid, &has, &enabled);
        if (R_FAILED(rc)) frame->showError("读取游戏配置失败", rc);
        auto toggle = new tsl::elm::ListItem(std::string("当前游戏背景音乐: ") + (R_FAILED(rc) ? "不可用" : enabled ? "开启" : "关闭"));
        toggle->setClickListener([frame, active_tid, toggle](u64 keys) {
            if (!(keys & HidNpadButton_A)) return false;
            u32 has = 0, enabled = 1;
            Result rc = qqmusicGetTitleEnabled(active_tid, &has, &enabled);
            const u32 next = !enabled;
            if (R_SUCCEEDED(rc)) rc = qqmusicSetTitleEnabled(active_tid, next);
            if (R_FAILED(rc)) frame->showError("修改游戏配置失败", rc);
            else toggle->setText(std::string("当前游戏背景音乐: ") + (next ? "开启" : "关闭"));
            return true;
        });
        m_list->addItem(toggle);
    }

    u32 enabled = 1;
    rc = qqmusicGetTitleEnabledDefault(&enabled);
    if (R_FAILED(rc)) frame->showError("读取默认配置失败", rc);
    auto toggle = new tsl::elm::ListItem(std::string("默认规则 (未配置游戏): ") + (R_FAILED(rc) ? "不可用" : enabled ? "开启" : "关闭"));
    toggle->setClickListener([frame, toggle](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        u32 enabled = 1;
        Result rc = qqmusicGetTitleEnabledDefault(&enabled);
        const u32 next = !enabled;
        if (R_SUCCEEDED(rc)) rc = qqmusicSetTitleEnabledDefault(next);
        if (R_FAILED(rc)) frame->showError("修改默认配置失败", rc);
        else toggle->setText(std::string("默认规则 (未配置游戏): ") + (next ? "开启" : "关闭"));
        return true;
    });
    m_list->addItem(toggle);

    u32 flags = 0;
    rc = qqmusicGetStatus(&flags);
    if (R_FAILED(rc)) frame->showError("读取音频状态失败", rc);
    m_list->addItem(new tsl::elm::ListItem(std::string("音频服务状态: ") +
        (R_FAILED(rc) ? "不可用" : (flags & 4) ? "游戏独占中" : "空闲可用")));
    frame->setContent(m_list);
    frame->setDescription("\uE0E1  返回");
    return frame;
}
