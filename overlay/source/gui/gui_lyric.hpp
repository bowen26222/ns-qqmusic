#pragma once

#include "client.h"
#include "elm_overlayframe.hpp"
#include "lyric_parser.hpp"

#include <tesla.hpp>
#include <string>
#include <vector>

class LyricGui final : public tsl::Gui {
private:
    QqMusicOverlayFrame *m_frame = nullptr;
    tsl::elm::Element *m_content = nullptr;

public:
    LyricGui();
    ~LyricGui() override;

    tsl::elm::Element *createUI() override;
    void update() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos,
                     HidAnalogStickState leftJoyStick, HidAnalogStickState rightJoyStick) override;
};
