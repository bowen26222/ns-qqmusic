#include "gui_playlist.hpp"

#include "elm_overlayframe.hpp"
#include "client.h"

#include <cstring>

namespace {
    class ButtonListItem final : public tsl::elm::ListItem {
    public:
        explicit ButtonListItem(const std::string &text, const std::string &value = "\uE098") : ListItem(text, value) {}
        bool onTouch(tsl::elm::TouchEvent event, s32 x, s32 y, s32 prevX, s32 prevY, s32 initialX, s32 initialY) override {
            if (event == tsl::elm::TouchEvent::Touch)
                m_touched = inBounds(x, y);
            if (event != tsl::elm::TouchEvent::Release || !m_touched)
                return false;
            m_touched = false;
            if (Element::getInputMode() != tsl::InputMode::Touch || !inBounds(x, y))
                return false;
            return onClick(x >= getRightBound() - getHeight() ? HidNpadButton_Y : HidNpadButton_A);
        }
    };
}

PlaylistGui::PlaylistGui() = default;

tsl::elm::Element *PlaylistGui::createUI() {
    m_frame = new QqMusicOverlayFrame();
    m_list = new tsl::elm::List();
    m_frame->setContent(m_list);
    m_frame->setDescription("\uE0E1 返回   \uE0E0 播放   \uE0E3 移除   \uE0E2 清空");
    RefreshQueue();
    return m_frame;
}

void PlaylistGui::ClearQueue() {
    const Result rc = qqmusicClearQueue();
    if (R_FAILED(rc)) m_frame->showError("清空队列失败", rc);
    else m_reload = true;
}

void PlaylistGui::RefreshQueue() {
    removeFocus();
    m_focus_item = nullptr;
    m_list->clear();
    m_reload = false;

    auto refresh = new tsl::elm::ListItem("刷新队列");
    refresh->setClickListener([this](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        m_reload = true;
        return true;
    });
    m_list->addItem(refresh);
    auto clear = new tsl::elm::ListItem("清空队列");
    clear->setClickListener([this](u64 keys) {
        if (!(keys & HidNpadButton_A)) return false;
        if (!m_reload) ClearQueue();
        return true;
    });
    m_list->addItem(clear);

    u32 count = 0;
    Result rc = qqmusicGetQueueSize(&count);
    if (R_FAILED(rc)) {
        m_frame->showError("读取队列失败", rc);
        m_list->addItem(new tsl::elm::ListItem("队列不可用，请按A刷新重试"));
        return;
    }
    if (count == 0) {
        m_list->addItem(new tsl::elm::ListItem("播放列表为空，请先添加歌曲"));
        return;
    }
    u32 radio_on = 0;
    qqmusicGetRadioMode(&radio_on);
    if (radio_on) {
        m_list->addItem(new tsl::elm::CategoryHeader("心动电台模式: 智能无限推荐流"));
    }

    QqMusicCurrentTrack current = {};
    const Result current_rc = qqmusicGetCurrentTrack(&current);
    if (R_FAILED(current_rc)) m_frame->showError("读取当前曲目失败", current_rc);
    // Current-track information is optional: idle queues are still fully usable.
    for (u32 i = 0; i < count; ++i) {
        char path[FS_MAX_PATH] = {};
        rc = qqmusicGetQueueItem(i, path, sizeof(path));
        if (R_FAILED(rc)) {
            // The player thread prunes unplayable tracks asynchronously, so the
            // queue can shrink between the size query and this read. A stale
            // index is expected, not an error: stop and offer a refresh.
            m_list->addItem(new tsl::elm::CategoryHeader("队列已在刷新中发生变化"));
            auto refresh_row = new tsl::elm::ListItem("刷新队列", "显示最新播放列表");
            refresh_row->setClickListener([this](u64 keys) {
                if (!(keys & HidNpadButton_A)) return false;
                m_reload = true;
                return true;
            });
            m_list->addItem(refresh_row);
            break;
        }
        const std::string item_path(path);
        const size_t slash = item_path.find_last_of('/');
        std::string name = item_path.substr(slash == std::string::npos ? 0 : slash + 1);
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos && dot != 0) name.resize(dot);

        QqMusicTrackMeta meta = {};
        std::string display_name = name;
        std::string display_sub = "\uE098";
        if (R_SUCCEEDED(qqmusicGetTrackMeta(i, &meta)) && meta.valid && meta.title[0]) {
            display_name = meta.title;
            if (meta.artist[0]) {
                display_sub = std::string(meta.artist) + "  \uE098";
            }
        }
        auto item = new ButtonListItem(display_name, display_sub);
        item->setClickListener([this, i, item_path](u64 keys) {
            if (!(keys & (HidNpadButton_A | HidNpadButton_Y | HidNpadButton_X))) return false;
            if (m_reload) return true;
            if (keys & HidNpadButton_X) {
                ClearQueue();
                return true;
            }
            // A service-side queue change must never silently target a different row.
            char actual[FS_MAX_PATH] = {};
            Result rc = qqmusicGetQueueItem(i, actual, sizeof(actual));
            if (R_FAILED(rc)) {
                m_frame->showError("读取曲目失败", rc);
                m_reload = true;
                return true;
            }
            if (item_path != actual) {
                m_frame->setToast("队列已变动", "请重新选择曲目");
                m_reload = true;
                return true;
            }
            const bool play = (keys & HidNpadButton_A) != 0;
            rc = play ? qqmusicSelect(i) : qqmusicRemove(i);
                if (R_FAILED(rc)) m_frame->showError(play ? "播放曲目失败" : "移除曲目失败", rc);
            else m_reload = true;
            return true;
        });
        m_list->addItem(item);
        if (R_SUCCEEDED(current_rc) && current.path[0] && current.index == i)
            m_focus_item = item;
    }
}

void PlaylistGui::update() {
    if (m_reload) RefreshQueue();
    if (m_focus_item) {
        const auto index = m_list->getIndexInList(m_focus_item);
        if (index >= 0) {
            removeFocus();
            requestFocus(m_focus_item, tsl::FocusDirection::None);
            m_list->setFocusedIndex(index);
            m_focus_item = nullptr;
        }
    }
}
