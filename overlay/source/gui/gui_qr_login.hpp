#pragma once

#include <tesla.hpp>
#include <vector>
#include <string>

class QrLoginGui final : public tsl::Gui {
  private:
    tsl::elm::OverlayFrame *m_frame = nullptr;
    std::vector<uint8_t> m_pixels;
    int m_img_w = 0;
    int m_img_h = 0;
    uint32_t m_timer = 0;
    int m_last_status = -1;
    bool m_loaded = false;

    void refreshQr();

  public:
    QrLoginGui();
    ~QrLoginGui() override;

    tsl::elm::Element *createUI() override;
    void update() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override;
};
