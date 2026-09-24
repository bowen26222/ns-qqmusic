#pragma once

#include <tesla.hpp>
#include "client.h"
#include <memory>
#include <optional>
#include <cstdio>

/**
 * @brief The base frame which can contain another view
 *
 */
class QqMusicOverlayFrame final : public tsl::elm::Element {
public:
    /**
     * @brief Constructor
     *
     * @param title Name of the Overlay drawn bolt at the top
     * @param subtitle Subtitle drawn bellow the title e.g version number
     */
    QqMusicOverlayFrame() : tsl::elm::Element() {}

    void draw(tsl::gfx::Renderer *renderer) override {
        renderer->fillScreen(a(tsl::style::color::ColorFrameBackground));
        renderer->drawRect(tsl::cfg::FramebufferWidth - 1, 0, 1, tsl::cfg::FramebufferHeight, a(0xF222));

        renderer->drawString("QQ音乐", false, 20, 50, 30, a(tsl::style::color::ColorText));
        renderer->drawString(VERSION, false, 20, 70, 15, a(tsl::style::color::ColorDescription));

        renderer->drawRect(15, tsl::cfg::FramebufferHeight - 73, tsl::cfg::FramebufferWidth - 30, 1, a(tsl::style::color::ColorText));

        renderer->drawString(m_description, false, 30, 693, 23, a(tsl::style::color::ColorText));

        if (m_contentElement != nullptr)
            m_contentElement->frame(renderer);

        if (m_toast) {
            s32 width = tsl::cfg::FramebufferWidth - 20;
            s32 height = 110;
            s32 startX = 10;
            s32 startY = tsl::cfg::FramebufferHeight - height;

            u32 fadeDuration = 10;
            if (m_toast->Current < fadeDuration) {
                s32 offset = height - (height / fadeDuration) * m_toast->Current;
                startY += offset;
            }
            if (m_toast->Duration - m_toast->Current < fadeDuration) {
                s32 offset = height - (height / fadeDuration) * (m_toast->Duration - m_toast->Current);
                startY += offset;
            }

            renderer->drawRect(startX, startY, width, height, a(tsl::style::color::ColorText));
            renderer->drawRect(startX + 3, startY + 3, width - 6, height - 6, a(tsl::style::color::ColorFrameBackground));
            renderer->drawString(m_toast->Header.c_str(), false, startX + 10, startY + 40, 26, a(tsl::style::color::ColorText));
            renderer->drawString(m_toast->Content.c_str(), false, startX + 10, startY + 80, 23, a(tsl::style::color::ColorText), width - 20);

            ++m_toast->Current;
            if (m_toast->Duration <= m_toast->Current) m_toast = std::nullopt;
        }
    }

    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
        setBoundaries(parentX, parentY, parentWidth, parentHeight);

        if (m_contentElement != nullptr) {
            m_contentElement->setBoundaries(parentX + 35, parentY + 95, parentWidth - 85, parentHeight - 73 - 95);
            m_contentElement->invalidate();
        }
    }

    Element* requestFocus(tsl::elm::Element *oldFocus, tsl::FocusDirection direction) override {
        if (m_contentElement != nullptr)
            return m_contentElement->requestFocus(oldFocus, direction);
        else
            return nullptr;
    }

    bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32 prevX, s32 prevY, s32 initialX, s32 initialY) override {
        if (m_contentElement == nullptr)
            return false;
        // Releases must reach the touched child even after leaving the frame.
        if (event != tsl::elm::TouchEvent::Release && !m_contentElement->inBounds(currX, currY))
            return false;
        return m_contentElement->onTouch(event, currX, currY, prevX, prevY, initialX, initialY);
    }
    bool handleInput(u64 keysDown, u64, const HidTouchState&, HidAnalogStickState, HidAnalogStickState) override {
        if (keysDown & HidNpadButton_L) {
            qqmusicPrev();
            setToast("上一曲", "正在切歌...");
            return true;
        }
        if (keysDown & HidNpadButton_R) {
            qqmusicNext();
            setToast("下一曲", "正在切歌...");
            return true;
        }
        return false;
    }


    /**
     * @brief Sets the content of the frame
     *
     * @param content Element
     */
    void setContent(tsl::elm::Element *content) {
        m_contentElement = std::unique_ptr<tsl::elm::Element>(content);

        if (content != nullptr) {
            m_contentElement->setParent(this);
            this->invalidate();
        }
    }

    void setDescription(const char *description) {
        m_description = description;
    }

    struct Toast {
        std::string Header;
        std::string Content;
        u32 Duration;
        u32 Current;
    };

    void setToast(std::string header, std::string content) {
        m_toast = Toast { header, content, 150, 0 };
    }

    void showError(const char *action, Result rc) {
        char result[32];
        std::snprintf(result, sizeof(result), "错误代码 2%03X-%04X", R_MODULE(rc), R_DESCRIPTION(rc));
        setToast(action, result);
    }

private:
    std::unique_ptr<tsl::elm::Element> m_contentElement;
    const char *m_description = "\uE0E1  返回     \uE0E0  确定";

    std::optional<Toast> m_toast;
};
