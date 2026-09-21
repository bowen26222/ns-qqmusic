// M0 骨架：Ultrahand overlay —— 连接 sysmodule named port "qqmusic"，
// 屏显 IPC 往返结果（心跳计数 / 音量读写），验证双向通路。
#define TESLA_INIT_IMPL
#include "client.h"

#include <cstdio>

#include <tesla.hpp>

using namespace tsl;

namespace {

    u32 g_flags  = 0;
    u64 g_tick   = 0;
    u32 g_api    = 0;
    float g_volume = 0.0f;
    Result g_fail = 0;
    const char *g_msg = nullptr;

    const NcmProgramLocation kSysmoduleLocation{
        .program_id = 0x42000000004B4D51,
        .storageID  = NcmStorageId_None,
    };

    // sysmodule 未自启时经 pmshell 拉起（Streamfin 同款兜底）
    Result ConnectSysmodule() {
        Result rc = qqmusicInitialize();
        if (R_VALUE(rc) == KERNELRESULT(NotFound) || R_VALUE(rc) == KERNELRESULT(ConnectionRefused)) {
            u64 pid = 0;
            rc = pmshellInitialize();
            if (R_SUCCEEDED(rc)) {
                rc = pmshellLaunchProgram(0, &kSysmoduleLocation, &pid);
                pmshellExit();
            }
            if (R_SUCCEEDED(rc) && pid != 0) {
                svcSleepThread(500'000'000ULL);
                rc = qqmusicInitialize();
            }
        }
        return rc;
    }

    void RefreshState() {
        qqmusicGetStatus(&g_flags);
        qqmusicGetTick(&g_tick);
        qqmusicGetVolume(&g_volume);
    }
} // namespace

// 状态行：显示 api/flags/tick/volume
class StatusLine final : public tsl::elm::Element {
  public:
    StatusLine() { m_isItem = false; }

    void draw(gfx::Renderer *renderer) override {
        char line[96];
        s32 y = this->y + 8;

        snprintf(line, sizeof(line), "api     : %u (want %u)", g_api, QQMUSIC_API_VERSION);
        renderer->drawString(line, true, this->x + 20, y, 20, {0xFF, 0xFF, 0xFF, 0xFF});
        y += 28;

        snprintf(line, sizeof(line), "flags   : 0x%08X", g_flags);
        renderer->drawString(line, true, this->x + 20, y, 20, {0xFF, 0xFF, 0xFF, 0xFF});
        y += 28;

        snprintf(line, sizeof(line), "tick    : %llu", (unsigned long long)g_tick);
        renderer->drawString(line, true, this->x + 20, y, 20, {0xFF, 0xFF, 0xFF, 0xFF});
        y += 28;

        snprintf(line, sizeof(line), "volume  : %.2f  (X:+  Y:-)", g_volume);
        renderer->drawString(line, true, this->x + 20, y, 20, {0xFF, 0xFF, 0xFF, 0xFF});
    }

    void layout(u16 x, u16 y, u16 w, u16 h) override {
        this->x = x;
        this->y = y;
        this->amplitude = h;
    }
};

class PingGui final : public tsl::Gui {
  private:
    u64 m_lastUpdate = 0;

  public:
    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("QQMusic M0", "IPC ping-pong");
        auto *list  = new tsl::elm::List();

        list->addItem(new StatusLine(), 130);

        auto *tick = new tsl::elm::ListItem("A: 刷新状态 (GetStatus/GetTick/GetVolume)");
        tick->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_A) {
                RefreshState();
                return true;
            }
            return false;
        });
        list->addItem(tick);

        auto *quit = new tsl::elm::ListItem("B: 退出 (IPC QuitServer)");
        quit->setClickListener([](u64 keys) {
            if (keys & HidNpadButton_B) {
                qqmusicQuit();
                return true;
            }
            return false;
        });
        list->addItem(quit);

        frame->setContent(list);
        return frame;
    }

    void update() override {
        // 每秒自动刷新一次状态，无需按键
        const u64 now = ult::nowNs();
        if (now - m_lastUpdate > 1'000'000'000ULL) {
            m_lastUpdate = now;
            RefreshState();
        }
    }
};

class ErrorGui final : public tsl::Gui {
  public:
    tsl::elm::Element *createUI() override {
        auto *frame = new tsl::elm::OverlayFrame("QQMusic M0", "error");
        auto *elem  = new class TextLine(g_msg, g_fail);
        frame->setContent(elem);
        return frame;
    }

  private:
    class TextLine final : public tsl::elm::Element {
      public:
        TextLine(const char *msg, Result fail) {
            m_isItem = false;
            snprintf(m_msg, sizeof(m_msg), "%s", msg ? msg : "unknown error");
            snprintf(m_fail, sizeof(m_fail), "fail: 0x%08X", fail);
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

class QqMusicOverlay final : public tsl::Overlay {
  public:
    void initServices() override {
        tsl::highlightColor1 = {0x0, 0xA, 0xD, 0xF};
        tsl::highlightColor2 = {0x0, 0xA, 0xD, 0xF};
        tsl::clickColor      = {0x0, 0xA, 0xD, 0x7};

        Result rc = ConnectSysmodule();
        if (R_FAILED(rc)) {
            g_fail = rc;
            g_msg  = "Failed to reach sysmodule";
            return;
        }

        rc = qqmusicGetApiVersion(&g_api);
        if (R_FAILED(rc)) {
            g_fail = rc;
            g_msg  = "GetApiVersion failed";
            return;
        }
        if (g_api != QQMUSIC_API_VERSION) {
            g_fail = 0;
            g_msg  = "Unsupported sysmodule API version";
            return;
        }
        RefreshState();
    }

    void exitServices() override {
        qqmusicExit();
    }

    std::unique_ptr<tsl::Gui> loadInitialGui() override {
        if (g_msg)
            return std::make_unique<ErrorGui>();
        return std::make_unique<PingGui>();
    }
};

int main(int argc, char **argv) {
    return tsl::loop<QqMusicOverlay>(argc, argv);
}