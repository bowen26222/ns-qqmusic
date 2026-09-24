#pragma once

#include <tesla.hpp>

// 息屏播放：关闭 LCD/OLED 背光（屏幕全黑零功耗），同时抑制系统休眠，
// 音频硬件与播放线程持续输出。按下手柄任意按键或触摸屏幕即刻唤醒亮屏。
class ScreenOffGui final : public tsl::Gui {
  private:
    bool m_active = false;
    bool m_woken = false;

    void WakeUp();

  public:
    ScreenOffGui();
    ~ScreenOffGui() override;

    tsl::elm::Element *createUI() override;
    void update() override;
    bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos,
                     HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override;
};
