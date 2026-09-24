#pragma once

#include <tesla.hpp>

class QqMusicOverlayFrame;

class PlaylistGui final : public tsl::Gui {
private:
    QqMusicOverlayFrame *m_frame = nullptr;
    tsl::elm::List *m_list = nullptr;
    tsl::elm::ListItem *m_focus_item = nullptr;
    bool m_reload = false;
    void RefreshQueue();
    void ClearQueue();

public:
    PlaylistGui();
    tsl::elm::Element *createUI() override;
    void update() override;
};
