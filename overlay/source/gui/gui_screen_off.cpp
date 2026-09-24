#include "gui_screen_off.hpp"

#include <switch.h>

extern "C" void crashTrace(const char *s);

ScreenOffGui::ScreenOffGui() {
    crashTrace("screen_off: enter");
    // 1. 初始化背光控制服务并关闭背光
    if (R_SUCCEEDED(lblInitialize())) {
        m_active = true;
        lblSwitchBacklightOff(0); // 瞬间全黑，硬件功耗归零
    }
    // 2. 标记媒体播放中，抑制系统自动深度睡眠
    appletSetMediaPlaybackState(true);
}

ScreenOffGui::~ScreenOffGui() {
    WakeUp();
    crashTrace("screen_off: exit");
}

void ScreenOffGui::WakeUp() {
    if (m_woken) return;
    m_woken = true;
    if (m_active) {
        lblSwitchBacklightOn(0); // 即刻唤醒屏幕
        lblExit();
        m_active = false;
    }
    // 恢复系统默认自动休眠策略
    appletSetMediaPlaybackState(false);
}

tsl::elm::Element *ScreenOffGui::createUI() {
    // 息屏时不需要任何渲染元素
    return nullptr;
}

void ScreenOffGui::update() {
    // 保持静默
}

bool ScreenOffGui::handleInput(u64 keysDown, u64, const HidTouchState &touchPos,
                              HidAnalogStickState, HidAnalogStickState) {
    // 任意按键按下或触摸屏幕：唤醒亮屏并返回上一层界面
    if (keysDown != 0 || touchPos.x != 0 || touchPos.y != 0) {
        WakeUp();
        tsl::goBack();
        return true;
    }
    return false;
}
