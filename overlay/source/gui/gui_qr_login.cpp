#include "gui_qr_login.hpp"
#include "client.h"

#include <stb_image.h>
#include <algorithm>
#include <cstring>

namespace {

    class QrImageElement : public tsl::elm::Element {
      private:
        const std::vector<uint8_t> &m_pixels;
        int m_w;
        int m_h;

      public:
        QrImageElement(const std::vector<uint8_t> &pixels, int w, int h)
            : m_pixels(pixels), m_w(w), m_h(h) {}

        void draw(tsl::gfx::Renderer *renderer) override {
            if (m_pixels.empty() || m_w <= 0 || m_h <= 0)
                return;

            const s32 scale = 2;
            const s32 total_w = m_w * scale;
            const s32 total_h = m_h * scale;
            const s32 start_x = this->getX() + (this->getWidth() - total_w) / 2;
            const s32 start_y = this->getY() + 10;

            // White padding/background behind QR
            renderer->drawRect(start_x - 8, start_y - 8, total_w + 16, total_h + 16, 0xFFFF);

            for (int y = 0; y < m_h; y++) {
                for (int x = 0; x < m_w; x++) {
                    const int idx = (y * m_w + x) * 4;
                    const u8 r = m_pixels[idx];
                    const u8 g = m_pixels[idx + 1];
                    const u8 b = m_pixels[idx + 2];
                    if (r < 128 && g < 128 && b < 128) {
                        renderer->drawRect(start_x + x * scale, start_y + y * scale, scale, scale, 0xF000);
                    }
                }
            }
        }

        void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
            this->setBoundaries(this->getX(), this->getY(), this->getWidth(), m_h * 2 + 30);
        }
    };

} // namespace

QrLoginGui::QrLoginGui() {
    refreshQr();
}

QrLoginGui::~QrLoginGui() = default;

void QrLoginGui::refreshQr() {
    m_pixels.clear();
    m_img_w = 0;
    m_img_h = 0;
    m_loaded = false;

    std::vector<u8> png_buf(32 * 1024);
    u32 png_size = 0;
    Result rc = qqmusicOnlineStartQrLogin(png_buf.data(), png_buf.size(), &png_size);
    if (R_SUCCEEDED(rc) && png_size > 0) {
        int w = 0, h = 0, comp = 0;
        u8 *rgba = stbi_load_from_memory(png_buf.data(), png_size, &w, &h, &comp, 4);
        if (rgba && w > 0 && h > 0) {
            m_img_w = w;
            m_img_h = h;
            m_pixels.assign(rgba, rgba + (w * h * 4));
            stbi_image_free(rgba);
            m_loaded = true;
        }
    }
}

tsl::elm::Element *QrLoginGui::createUI() {
    m_frame = new tsl::elm::OverlayFrame("扫码登录", "请使用手机QQ或QQ音乐扫码");
    auto list = new tsl::elm::List();

    if (m_loaded) {
        list->addItem(new QrImageElement(m_pixels, m_img_w, m_img_h));
        list->addItem(new tsl::elm::CategoryHeader("1. 打开手机QQ或QQ音乐"));
        list->addItem(new tsl::elm::CategoryHeader("2. 扫描上方二维码授权登录"));
    } else {
        list->addItem(new tsl::elm::CategoryHeader("加载二维码失败"));
        list->addItem(new tsl::elm::ListItem("按 \uE0E0 键重试"));
    }

    m_frame->setContent(list);
    return m_frame;
}

void QrLoginGui::update() {
    m_timer++;
    if (m_timer >= 60) {
        m_timer = 0;
        u32 status_code = 0;
        char msg[64] = {};
        if (R_SUCCEEDED(qqmusicOnlinePollQrLogin(&status_code, msg, sizeof(msg)))) {
            m_last_status = static_cast<int>(status_code);
            if (m_frame) {
                if (status_code == 66) {
                    m_frame->setSubtitle("等待扫码中...");
                } else if (status_code == 67) {
                    m_frame->setSubtitle("已扫码，请在手机上确认");
                } else if (status_code == 65) {
                    m_frame->setSubtitle("二维码已过期，按 \uE0E0 刷新");
                } else if (status_code == 0) {
                    m_frame->setSubtitle("登录成功！");
                    tsl::goBack();
                }
            }
        }
    }
}

bool QrLoginGui::handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        tsl::goBack();
        return true;
    }
    if (keysDown & HidNpadButton_A) {
        if (!m_loaded || m_last_status == 65) {
            refreshQr();
            if (m_frame) {
                tsl::changeTo<QrLoginGui>();
            }
            return true;
        }
    }
    return false;
}
