#include "gui_lyric.hpp"
#include "client.h"
#include "elm_overlayframe.hpp"
#include "lyric_parser.hpp"
#include "gui_main.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

    bool ReadSdText(const char *path, std::string &out_text) {
        out_text.clear();
        FsFileSystem fs;
        if (R_FAILED(fsOpenSdCardFileSystem(&fs))) return false;
        FsFile f;
        bool ok = false;
        if (R_SUCCEEDED(fsFsOpenFile(&fs, path, FsOpenMode_Read, &f))) {
            s64 sz = 0;
            // 歌词文本硬上限 48KB：彻底杜绝大文件打爆内存
            if (R_SUCCEEDED(fsFileGetSize(&f, &sz)) && sz > 0 && sz <= 48 * 1024) {
                out_text.resize((size_t)sz);
                u64 got = 0;
                if (R_SUCCEEDED(fsFileRead(&f, 0, out_text.data(), (u64)sz, 0, &got)) && got == (u64)sz) {
                    ok = true;
                } else {
                    out_text.clear();
                }
            }
            fsFileClose(&f);
        }
        fsFsClose(&fs);
        return ok;
    }

    class LyricElement final : public tsl::elm::Element {
    private:
        qqmusic::LyricParser m_parser;
        char m_loaded_path[FS_MAX_PATH]{};
        bool m_lyric_loaded = false;
        bool m_floating_mode = false;     // 精简单行显示模式
        int m_manual_offset = 0;          // 手动翻阅偏差行数
        u32 m_manual_timer = 0;           // 手动翻阅倒计时（数秒后自动归位）
        u16 m_cooldown = 0;               // 异步拉取重试倒计时
        u8 m_retry = 0;                  // 失败重试次数上限
        bool m_touch_down = false;        // 触摸在内部按下标志
        s32 m_touch_start_y = 0;
        std::string m_title;
        std::string m_artist;
        QqMusicCurrentTrack m_track{};
        QqMusicOverlayFrame *m_frame = nullptr;

        void LoadTrackLyric(const char *path) {
            m_parser.Clear();
            m_lyric_loaded = false;
            m_manual_offset = 0;
            m_manual_timer = 0;

            if (!path || !path[0]) {
                m_loaded_path[0] = '\0';
                m_title = "未选择曲目";
                m_artist = "";
                return;
            }

            QqMusicTrackMeta meta{};
            if (R_SUCCEEDED(qqmusicGetTrackMeta(QQMUSIC_TRACKMETA_CURRENT, &meta)) && meta.valid && meta.title[0]) {
                m_title = meta.title;
                m_artist = meta.artist;
            } else {
                const char *slash = std::strrchr(path, '/');
                m_title = slash ? slash + 1 : path;
                const size_t dot = m_title.find_last_of('.');
                if (dot != std::string::npos && dot != 0) m_title.resize(dot);
                m_artist = "";
            }

            if (m_retry >= 20) {
                m_cooldown = 180;
                return;
            }

            char name[96] = {};
            if (R_SUCCEEDED(qqmusicEnsureLyric(path, name, sizeof(name))) && name[0]) {
                char file_path[160];
                std::snprintf(file_path, sizeof(file_path), "/qqmusic-cache/%s", name);
                std::string lrc_text;
                if (ReadSdText(file_path, lrc_text) && !lrc_text.empty()) {
                    m_lyric_loaded = m_parser.Parse(lrc_text);
                    if (m_lyric_loaded) {
                        std::snprintf(m_loaded_path, sizeof(m_loaded_path), "%s", path);
                        m_retry = 0;
                        m_cooldown = 0;
                        return;
                    }
                }
            }

            m_retry++;
            m_cooldown = 30;
        }

    public:
        explicit LyricElement(QqMusicOverlayFrame *frame) : m_frame(frame) {}

        tsl::elm::Element *requestFocus(tsl::elm::Element *, tsl::FocusDirection) override {
            return this;
        }

        void updateLyrics() {
            QqMusicCurrentTrack track{};
            if (R_SUCCEEDED(qqmusicGetCurrentTrack(&track))) {
                m_track = track;
                if (std::strcmp(m_loaded_path, track.path) != 0) {
                    m_retry = 0;
                    m_cooldown = 0;
                    LoadTrackLyric(track.path);
                } else if (!m_lyric_loaded) {
                    if (m_cooldown > 0) {
                        m_cooldown--;
                    } else {
                        LoadTrackLyric(track.path);
                    }
                }
            }
            if (m_manual_timer > 0) {
                m_manual_timer--;
                if (m_manual_timer == 0)
                    m_manual_offset = 0; // 自动回归当前正在唱的歌词
            }
        }

        void draw(tsl::gfx::Renderer *renderer) override {
            updateLyrics();

            const s32 x = this->getX(), y = this->getY(), w = this->getWidth(), h = this->getHeight();

            // 1. 顶部当前曲目信息卡
            renderer->drawRect(x + 10, y + 10, w - 20, 52, tsl::Color{0x1, 0x1, 0x2, 0xF});
            renderer->drawRect(x + 10, y + 61, w - 20, 1, tsl::Color{0x2, 0x3, 0x4, 0xF});

            std::string header_str = m_title;
            if (!m_artist.empty()) header_str += " - " + m_artist;
            renderer->drawString(header_str.c_str(), false, x + 24, y + 42, 21, tsl::Color{0xF, 0xF, 0xF, 0xF}, w - 48);

            // 2. 歌词主体
            if (!m_lyric_loaded || m_parser.IsEmpty()) {
                renderer->drawString(m_track.path[0] ? "正在获取歌词 / 本曲暂无歌词" : "播放曲目后即可查看实时歌词",
                                     false, x + 40, y + h / 2, 20, tsl::Color{0x8, 0x8, 0x9, 0xF});
                return;
            }

            const u32 current_ms = (m_track.sample_rate > 0)
                ? (u32)((u64)m_track.current_frame * 1000 / m_track.sample_rate) : 0;
            const int active_idx = m_parser.GetCurrentIndex(current_ms);

            // 前奏阶段（active_idx == -1）
            if (active_idx < 0) {
                const s32 bar_y = y + h / 2 - 35;
                renderer->drawString("\uE098 (音乐前奏)", false, x + 30, bar_y + 40, 22, tsl::Color{0x0, 0xD, 0xF, 0xF});
                return;
            }

            const int target_center = std::clamp(active_idx + m_manual_offset, 0, (int)m_parser.LineCount() - 1);

            if (m_floating_mode) {
                // ---- 精简单行显示模式 (Single Line Bar) ----
                const s32 bar_h = 70;
                const s32 bar_y = y + h / 2 - 35;
                renderer->drawRect(x + 12, bar_y, w - 24, bar_h, tsl::Color{0x1, 0x1, 0x2, 0xF});
                renderer->drawRect(x + 12, bar_y, w - 24, 2, tsl::Color{0x0, 0xA, 0xD, 0xF});
                renderer->drawString("\uE098", false, x + 24, bar_y + 44, 22, tsl::Color{0x0, 0xD, 0xF, 0xF});

                const std::string line_text = m_parser.GetLine((size_t)target_center).text;
                renderer->drawString(line_text.empty() ? "(音乐过门)" : line_text.c_str(),
                                     false, x + 58, bar_y + 44, 22, tsl::Color{0xF, 0xF, 0xF, 0xF}, w - 80);
                return;
            }

            // ---- 全屏动态滚动歌词模式 (Full Scrolling Lyrics) ----
            const s32 center_y = y + 260; // 视口居中基准线
            constexpr s32 line_spacing = 42;

            // 绘制当前高亮行背景光晕
            renderer->drawRect(x + 10, center_y - 28, w - 20, 36, tsl::Color{0x1, 0x2, 0x3, 0xE});
            renderer->drawRect(x + 10, center_y - 28, 4, 36, tsl::Color{0x0, 0xA, 0xD, 0xF});

            // 围绕 target_center 上下展示各 5 行（共 11 行视口）
            constexpr int kHalfWindow = 5;
            for (int offset = -kHalfWindow; offset <= kHalfWindow; ++offset) {
                const int line_idx = target_center + offset;
                if (line_idx < 0 || line_idx >= (int)m_parser.LineCount())
                    continue;

                const auto &lyric = m_parser.GetLine((size_t)line_idx);
                const s32 cur_y = center_y + offset * line_spacing;

                // 超过视口上下界则裁剪
                if (cur_y < y + 80 || cur_y > y + h - 40)
                    continue;

                std::string txt = lyric.text.empty() ? "\u2022 \u2022 \u2022" : lyric.text;

                if (offset == 0) {
                    // 当前唱到的一句：大字号、鲜艳高亮亮青色
                    renderer->drawString(txt.c_str(), false, x + 30, cur_y, 23, tsl::Color{0x0, 0xD, 0xF, 0xF}, w - 60);
                } else {
                    // 伴随距离渐变淡出的已唱 / 待唱行
                    const int dist = std::abs(offset);
                    const tsl::Color c = (dist == 1) ? tsl::Color{0xD, 0xD, 0xE, 0xF}
                                       : (dist == 2) ? tsl::Color{0x8, 0x8, 0x9, 0xF}
                                       : (dist == 3) ? tsl::Color{0x5, 0x5, 0x6, 0xF}
                                                     : tsl::Color{0x3, 0x3, 0x4, 0xF};
                    renderer->drawString(txt.c_str(), false, x + 30, cur_y, 18, c, w - 60);
                }
            }

            // 若正处于手动浏览状态，显示提示
            if (m_manual_offset != 0) {
                renderer->drawString("\uE0E0 点击跳转该句播放   左右恢复同步", false, x + 30, y + h - 20, 14,
                                     tsl::Color{0x0, 0xD, 0xF, 0xF});
            }
        }

        void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
            // 严防 Element::invalidate 错误覆盖为全屏：严格使用 QqMusicOverlayFrame 内缩安全边界
            setBoundaries(parentX + 35, parentY + 95, parentWidth - 85, parentHeight - 73 - 95);
        }

        bool onClick(u64 keys) override {
            if (keys & (HidNpadButton_AnyUp | HidNpadButton_StickLUp)) {
                if (m_lyric_loaded && !m_parser.IsEmpty()) {
                    m_manual_offset--;
                    m_manual_timer = 240; // 4秒无操作自动归位
                }
                return true;
            }
            if (keys & (HidNpadButton_AnyDown | HidNpadButton_StickLDown)) {
                if (m_lyric_loaded && !m_parser.IsEmpty()) {
                    m_manual_offset++;
                    m_manual_timer = 240;
                }
                return true;
            }
            if (keys & (HidNpadButton_AnyLeft | HidNpadButton_AnyRight)) {
                m_manual_offset = 0;
                m_manual_timer = 0;
                if (m_frame) m_frame->setToast("已恢复自动跟随", "歌词正在实时同步滚动");
                return true;
            }
            if (keys & HidNpadButton_X) {
                tsl::changeTo<OsdLyricGui>();
                return true;
            }
            if (keys & HidNpadButton_A) {
                // 点句即播：直接跳转播放到选中的歌词时间戳！
                if (m_lyric_loaded && !m_parser.IsEmpty() && m_track.sample_rate > 0) {
                    const u32 current_ms = (u32)((u64)m_track.current_frame * 1000 / m_track.sample_rate);
                    const int active_idx = m_parser.GetCurrentIndex(current_ms);
                    const int base_idx = (active_idx >= 0) ? active_idx : 0;
                    const int target_idx = std::clamp(base_idx + m_manual_offset, 0, (int)m_parser.LineCount() - 1);
                    const u32 target_ms = m_parser.GetLine((size_t)target_idx).time_ms;
                    const u32 target_frame = (u32)((u64)target_ms * m_track.sample_rate / 1000);
                    const Result seek_rc = qqmusicSeek(target_frame);
                    if (R_SUCCEEDED(seek_rc)) {
                        m_manual_offset = 0;
                        m_manual_timer = 0;
                        if (m_frame) {
                            char tbuf[32];
                            std::snprintf(tbuf, sizeof(tbuf), "已跳转至 %02u:%02u", target_ms / 60000, (target_ms % 60000) / 1000);
                            m_frame->setToast("点句跳转播放", tbuf);
                        }
                    } else if (m_frame) {
                        m_frame->showError("跳转失败", seek_rc);
                    }
                }
                return true;
            }
            return false;
        }

        bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32, s32, s32 initialX, s32 initialY) override {
            const s32 x = this->getX(), y = this->getY(), w = this->getWidth(), h = this->getHeight();

            if (event == tsl::elm::TouchEvent::Touch) {
                // 触摸必须严格始于本歌词元素内部
                if (currX >= x && currX <= x + w && currY >= y && currY <= y + h) {
                    m_touch_down = true;
                    m_touch_start_y = currY;
                    return true;
                }
                return false;
            }

            if (event == tsl::elm::TouchEvent::Release) {
                if (!m_touch_down) return false;
                m_touch_down = false;

                // 触摸释放必须在元素内部且不是拖动操作
                if (currX < x || currX > x + w || currY < y || currY > y + h)
                    return false;
                if (std::abs(currY - m_touch_start_y) > 25 || std::abs(currX - initialX) > 25)
                    return false;

                if (m_floating_mode) {
                    const s32 bar_y = y + h / 2 - 35;
                    if (currY >= bar_y && currY <= bar_y + 70) {
                        return onClick(HidNpadButton_A);
                    }
                } else {
                    const s32 center_y = y + 260;
                    if (currY < center_y - 20) {
                        m_manual_offset--;
                        m_manual_timer = 240;
                    } else if (currY > center_y + 20) {
                        m_manual_offset++;
                        m_manual_timer = 240;
                    } else {
                        return onClick(HidNpadButton_A);
                    }
                }
                return true;
            }
            return false;
        }
    };

} // namespace

LyricGui::LyricGui() = default;
LyricGui::~LyricGui() = default;

tsl::elm::Element *LyricGui::createUI() {
    m_frame = new QqMusicOverlayFrame();
    m_frame->setDescription("\uE0E1 返回   \uE0E0 点句即播   \uE0E2 精简/全屏   L/R 切歌");
    auto elem = new LyricElement(m_frame);
    m_content = elem;
    m_frame->setContent(elem);
    return m_frame;
}

void LyricGui::update() {
    // 歌词随帧刷新由 LyricElement::draw 驱动
}

bool LyricGui::handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & HidNpadButton_B) {
        tsl::goBack();
        return true;
    }
    return false;
}

namespace {
    class OsdLyricElement final : public tsl::elm::Element {
    private:
        qqmusic::LyricParser m_parser;
        char m_loaded_path[FS_MAX_PATH]{};
        bool m_lyric_loaded = false;
        u16 m_cooldown = 0;
        u8 m_retry = 0;
        std::string m_title;
        QqMusicCurrentTrack m_track{};

        void LoadTrackLyric(const char *path) {
            m_parser.Clear();
            m_lyric_loaded = false;

            if (!path || !path[0]) {
                m_loaded_path[0] = '\0';
                m_title = "未选择曲目";
                return;
            }

            QqMusicTrackMeta meta{};
            if (R_SUCCEEDED(qqmusicGetTrackMeta(QQMUSIC_TRACKMETA_CURRENT, &meta)) && meta.valid && meta.title[0]) {
                m_title = meta.title;
            } else {
                m_title = "正在播放";
            }

            if (m_retry >= 20) {
                m_cooldown = 180;
                return;
            }

            char name[96] = {};
            if (R_SUCCEEDED(qqmusicEnsureLyric(path, name, sizeof(name))) && name[0]) {
                char file_path[160];
                std::snprintf(file_path, sizeof(file_path), "/qqmusic-cache/%s", name);
                std::string lrc_text;
                if (ReadSdText(file_path, lrc_text) && !lrc_text.empty()) {
                    m_lyric_loaded = m_parser.Parse(lrc_text);
                    if (m_lyric_loaded) {
                        std::snprintf(m_loaded_path, sizeof(m_loaded_path), "%s", path);
                        m_retry = 0;
                        m_cooldown = 0;
                        return;
                    }
                }
            }
            m_retry++;
            m_cooldown = 30;
        }

    public:
        OsdLyricElement() = default;

        void draw(tsl::gfx::Renderer *renderer) override {
            // 全屏填充 100% 透明，让底层游戏画面完全透出
            renderer->fillScreen({0x0, 0x0, 0x0, 0x0});

            QqMusicCurrentTrack track{};
            if (R_SUCCEEDED(qqmusicGetCurrentTrack(&track))) {
                m_track = track;
                if (std::strcmp(m_loaded_path, track.path) != 0) {
                    m_retry = 0;
                    m_cooldown = 0;
                    LoadTrackLyric(track.path);
                } else if (!m_lyric_loaded) {
                    if (m_cooldown > 0) m_cooldown--;
                    else LoadTrackLyric(track.path);
                }
            }

            const s32 pill_w = 400;
            const s32 pill_h = 46;
            const s32 pill_x = (tsl::cfg::FramebufferWidth - pill_w) / 2;
            const s32 pill_y = 16;

            // 悬浮药丸胶囊：深色半透明黑色磨砂 + 细微高亮顶边
            renderer->drawRect(pill_x, pill_y, pill_w, pill_h, tsl::Color{0x0, 0x0, 0x1, 0xC});
            renderer->drawRect(pill_x + 1, pill_y, pill_w - 2, 2, tsl::Color{0x0, 0xD, 0xF, 0xF});

            // 音符指示标
            renderer->drawString("\uE098", false, pill_x + 14, pill_y + 30, 20, tsl::Color{0x0, 0xD, 0xF, 0xF});

            if (!m_track.path[0]) {
                renderer->drawString("QQ音乐 · 暂未播放", false, pill_x + 40, pill_y + 30, 18, tsl::Color{0x8, 0x8, 0x9, 0xF}, pill_w - 50);
                return;
            }

            if (!m_lyric_loaded || m_parser.IsEmpty()) {
                std::string txt = m_title.empty() ? "正在播放" : m_title;
                txt += " · 获取歌词中...";
                renderer->drawString(txt.c_str(), false, pill_x + 40, pill_y + 30, 18, tsl::Color{0xC, 0xC, 0xD, 0xF}, pill_w - 50);
                return;
            }

            const u32 current_ms = (m_track.sample_rate > 0)
                ? (u32)((u64)m_track.current_frame * 1000 / m_track.sample_rate) : 0;
            const int active_idx = m_parser.GetCurrentIndex(current_ms);

            std::string line_text;
            if (active_idx < 0) {
                line_text = m_title + " (前奏)";
            } else {
                const auto &l = m_parser.GetLine((size_t)active_idx);
                line_text = l.text.empty() ? "(音乐过门)" : l.text;
            }

            renderer->drawString(line_text.c_str(), false, pill_x + 40, pill_y + 30, 19, tsl::Color{0xF, 0xF, 0xF, 0xF}, pill_w - 50);
        }
        void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override {
            setBoundaries(parentX, parentY, parentWidth, parentHeight);
        }


        bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32, s32, s32, s32) override {
            if (event != tsl::elm::TouchEvent::Release) return false;
            const s32 pill_w = 400;
            const s32 pill_h = 56;
            const s32 pill_x = (tsl::cfg::FramebufferWidth - pill_w) / 2;
            const s32 pill_y = 10;
            // 触摸悬浮药丸：立即切回主菜单
            if (currX >= pill_x && currX <= pill_x + pill_w && currY >= pill_y && currY <= pill_y + pill_h) {
                tsl::changeTo<MainGui>();
                return true;
            }
            return false;
        }
    };
} // namespace

OsdLyricGui::OsdLyricGui() {
    lastMode = "mini";
    FullMode = false;
    deactivateOriginalFooter = true;
    TeslaFPS = 15;
    tsl::hlp::requestForeground(false);
}

OsdLyricGui::~OsdLyricGui() {
    lastMode = "full";
    FullMode = true;
    deactivateOriginalFooter = false;
    TeslaFPS = 60;
    tsl::hlp::requestForeground(true);
}

tsl::elm::Element *OsdLyricGui::createUI() {
    return new OsdLyricElement();
}

void OsdLyricGui::update() {
}

bool OsdLyricGui::handleInput(u64 keysDown, u64, const HidTouchState &, HidAnalogStickState, HidAnalogStickState) {
    if (keysDown & (HidNpadButton_B | HidNpadButton_X)) {
        tsl::changeTo<MainGui>();
        return true;
    }
    return false;
}
