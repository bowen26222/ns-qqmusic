#pragma once

#include "tesla.hpp"
#include "elm_overlayframe.hpp"

#include <algorithm>
#include <cmath>

// TrackBar uses percent values for both touch and controller input (0..100).
class ElmVolume final : public tsl::elm::TrackBar {
public:
    ElmVolume(const char icon[3], const std::string &name, QqMusicOverlayFrame *frame,
              Result (*getVolume)(float *), Result (*setVolume)(float))
        : TrackBar{icon}, m_name{name} {
        float volume = 0.f;
        const Result rc = getVolume(&volume);
        m_available = R_SUCCEEDED(rc);
        if (m_available)
            m_committed = ToProgress(volume);
        else
            frame->showError(name.c_str(), rc);
        setProgress(m_committed);
        setValueChangedListener([this, frame, getVolume, setVolume](u16 value) {
            const Result rc = setVolume(static_cast<float>(value) / 100.f);
            if (R_SUCCEEDED(rc)) {
                m_committed = value;
                m_available = true;
            } else {
                float actual = 0.f;
                m_available = R_SUCCEEDED(getVolume(&actual));
                if (m_available)
                    m_committed = ToProgress(actual);
                setProgress(m_committed);
                frame->showError(m_name.c_str(), rc);
            }
        });
    }

    void draw(tsl::gfx::Renderer *renderer) override {
        const char *label = m_available ? m_name.c_str() : "Volume unavailable";
        const auto [width, height] = renderer->drawString(label, false, 0, 0, 15, tsl::style::color::ColorTransparent);
        renderer->drawString(label, false, ((this->getX() + 60) + (this->getWidth() - 95) / 2) - (width / 2), this->getY() + 20, 15, a(tsl::style::color::ColorDescription));
        TrackBar::draw(renderer);
    }

private:
    static u16 ToProgress(float volume) {
        return std::isfinite(volume) ? static_cast<u16>(std::clamp(volume, 0.f, 1.f) * 100.f + 0.5f) : 0;
    }

    const std::string m_name;
    u16 m_committed = 0;
    bool m_available = false;
};
