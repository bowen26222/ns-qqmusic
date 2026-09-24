// QQMusic overlay：Ultrahand overlay 入口。
// 连接 sysmodule named port "qqmusic"（自启 / psmdl 兜底拉起），
// API 版本校验后进入 MainGui（Streamfin 移植 UI）。
#define TESLA_INIT_IMPL
#include "client.h"

#include "gui_main.hpp"
#include "gui_playlist.hpp"
#include "gui_settings.hpp"

#include <cstdio>
#include <memory>

#include <tesla.hpp>

using namespace tsl;

namespace {

    u32 g_api   = 0;
    Result g_fail = 0;
    const char *g_msg = nullptr;

    const NcmProgramLocation kSysmoduleLocation{
        .program_id = 0x42000000004B4D51,
        .storageID  = NcmStorageId_None,
    };

    // sysmodule 未自启时经 pmshell 拉起（Streamfin 同款兜底）
    Result ConnectSysmodule(const char **stage) {
        *stage = "连接后台服务失败";
        Result rc = qqmusicInitialize();
        if (R_VALUE(rc) == KERNELRESULT(NotFound) || R_VALUE(rc) == KERNELRESULT(ConnectionRefused)) {
            u64 pid = 0;
            *stage = "初始化系统服务失败";
            rc = pmshellInitialize();
            if (R_SUCCEEDED(rc)) {
                *stage = "启动后台服务失败";
                rc = pmshellLaunchProgram(0, &kSysmoduleLocation, &pid);
                pmshellExit();
            }
            if (R_SUCCEEDED(rc) && pid != 0) {
                svcSleepThread(500'000'000ULL);
                *stage = "重连后台服务失败";
                rc = qqmusicInitialize();
            }
        }
        return rc;
    }
} // namespace

class ErrorGui final : public tsl::Gui {
  public:
    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("QQ音乐", "异常诊断");
        auto *elem  = new class TextLine(g_msg, g_fail);
        frame->setContent(elem);
        return frame;
    }

  private:
    class TextLine final : public tsl::elm::Element {
      public:
        TextLine(const char *msg, Result fail) {
            m_isItem = false;
            snprintf(m_msg, sizeof(m_msg), "%s", msg ? msg : "未知错误");
            snprintf(m_fail, sizeof(m_fail), "错误代码: 0x%08X", fail);
        }
        void draw(gfx::Renderer *renderer) override {
            renderer->drawString(m_msg, false, this->x + 20, this->y + 40, 24, {0xFF, 0x66, 0x66, 0xFF});
            renderer->drawString(m_fail, true, this->x + 20, this->y + 90, 20, {0xFF, 0x88, 0x88, 0xFF});
        }
        void layout(u16 x, u16 y, u16 w, u16 h) override {
            this->x = x;
            this->y = y;
            this->amplitude = h;
        }

      private:
        char m_msg[96];
        char m_fail[32];
    };
};
extern "C" void crashErr(const char *s, u32 rc);

class QqMusicOverlay final : public tsl::Overlay {
  public:
    void initServices() override {
        tsl::highlightColor1 = {0x0, 0xA, 0xD, 0xF};
        tsl::highlightColor2 = {0x0, 0xA, 0xD, 0xF};
        tsl::clickColor      = {0x0, 0xA, 0xD, 0x7};
        disableJumpTo        = true;

        // 重试 10s；分别保留连接/拉起/GetApiVersion 的实际失败阶段。
        Result rc = 0xFF;
        for (u32 attempt = 0; attempt < 40; attempt++) {
            qqmusicExit();
            rc = ConnectSysmodule(&g_msg);
            if (R_SUCCEEDED(rc)) {
                g_msg = "获取后台服务版本失败";
                rc = qqmusicGetApiVersion(&g_api);
                if (R_SUCCEEDED(rc)) {
                    g_msg = nullptr;
                    break;
                }
            }
            svcSleepThread(250'000'000ULL);
        }
        if (R_FAILED(rc)) {
            g_fail = rc;
            crashErr(g_msg, rc);
            return;
        }
        if (g_api != QQMUSIC_API_VERSION) {
            g_fail = 0;
            g_msg  = "后台服务版本不匹配";
            crashErr(g_msg, g_api);
            return;
        }
    }

    void exitServices() override {
        qqmusicExit();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        if (g_msg)
            return std::make_unique<ErrorGui>();
        return std::make_unique<MainGui>();
    }
};

extern "C" void crashBoot(void);
extern "C" void crashErr(const char *s, u32 rc);

int main(int argc, char **argv) {
    crashBoot();
    return tsl::loop<QqMusicOverlay>(argc, argv);
}