#pragma once

#include "client.h"
#include <tesla.hpp>

class OnlineGui final : public tsl::Gui {
  private:
    QqMusicOnlineStatus m_status{};
    bool m_busy = false;
  public:
    OnlineGui();
    ~OnlineGui() override;

    tsl::elm::Element *createUI() override;
    void update() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override;
};
