#include "elm_status_bar.hpp"

#include "config/config.hpp"
#include "elm_overlayframe.hpp"
#include "stb_image.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {

    // 覆盖层自持的 SD 会话：封面直接读文件，避免逐块 IPC 往返。
    FsFileSystem &SdCoverFs() {
        static FsFileSystem fs;
        return fs;
    }

    bool SdCoverFsReady() {
        static int state = 0; // 0=未尝试 1=可用 2=失败
        if (state == 0)
            state = R_SUCCEEDED(fsOpenSdCardFileSystem(&SdCoverFs())) ? 1 : 2;
        return state == 1;
    }

    // 整文件读入（不做分配：调用方给定上限缓冲）。
    bool ReadWholeFile(const char *path, u8 *out, u32 out_cap, u32 *out_size) {
        *out_size = 0;
        FsFile f;
        if (R_FAILED(fsFsOpenFile(&SdCoverFs(), path, FsOpenMode_Read, &f)))
            return false;

        s64 size = 0;
        bool ok = false;
        if (R_SUCCEEDED(fsFileGetSize(&f, &size)) && size > 0 && (u64)size <= out_cap) {
            u64 got = 0;
            if (R_SUCCEEDED(fsFileRead(&f, 0, out, (u64)size, 0, &got)) && got == (u64)size) {
                *out_size = (u32)size;
                ok = true;
            }
        }
        fsFileClose(&f);
        return ok;
    }

    std::string DisplayName(const char *path) {
        const char *slash = std::strrchr(path, '/');
        std::string name = slash ? slash + 1 : path;
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos && dot != 0)
            name.resize(dot);
        return name;
    }

    void FormatTime(u32 frame, u32 sample_rate, char *buf, size_t size) {
        if (sample_rate == 0) {
            std::snprintf(buf, size, "--:--");
            return;
        }
        const u64 ms = (u64)frame * 1000 / sample_rate;
        const u32 total_sec = (u32)(ms / 1000);
        std::snprintf(buf, size, "%u:%02u", total_sec / 60, total_sec % 60);
    }


    /* 2x2 box 减半（RGBA）。dst 尺寸 (sw+1)/2 x (sh+1)/2。 */
    void halve_rgba(const u8 *src, int sw, int sh, u8 *dst) {
        const int dw = (sw + 1) / 2;
        const int dh = (sh + 1) / 2;
        for (int y = 0; y < dh; y++) {
            const u8 *r0 = src + (size_t)y * 2 * sw * 4;
            const u8 *r1 = (y * 2 + 1 < sh) ? r0 + (size_t)sw * 4 : r0;
            for (int x = 0; x < dw; x++) {
                const int x1i = (x * 2 + 1 < sw) ? x * 2 + 1 : x * 2;
                const u8 *p0 = r0 + (size_t)x * 2 * 4;
                const u8 *p1 = r0 + (size_t)x1i * 4;
                const u8 *q0 = r1 + (size_t)x * 2 * 4;
                const u8 *q1 = r1 + (size_t)x1i * 4;
                u8 *o = dst + ((size_t)y * dw + x) * 4;
                for (int c = 0; c < 4; c++)
                    o[c] = (u8)((p0[c] + p1[c] + q0[c] + q1[c]) >> 2);
            }
        }
    }

    /* 最终 area-box 缩放（RGBA）；调用方保证输入不超过输出 2 倍。 */
    void box_to_rgba(const u8 *src, int sw, int sh, u8 *dst, int dw, int dh) {
        for (int y = 0; y < dh; y++) {
            const int y0 = y * sh / dh;
            const int y1 = (y + 1) * sh / dh;
            for (int x = 0; x < dw; x++) {
                const int x0 = x * sw / dw;
                const int x1 = (x + 1) * sw / dw;
                u32 sum[4] = {0, 0, 0, 0};
                for (int yy = y0; yy < y1; yy++) {
                    const u8 *row = src + (size_t)yy * sw * 4;
                    for (int xx = x0; xx < x1; xx++) {
                        const u8 *p = row + (size_t)xx * 4;
                        sum[0] += p[0];
                        sum[1] += p[1];
                        sum[2] += p[2];
                        sum[3] += p[3];
                    }
                }
                const u32 n = (u32)((y1 - y0) * (x1 - x0));
                u8 *o = dst + ((size_t)y * dw + x) * 4;
                for (int c = 0; c < 4; c++)
                    o[c] = (u8)(sum[c] / n);
            }
        }
    }
} // namespace

StatusBar::StatusBar() = default;

tsl::elm::Element *StatusBar::requestFocus(tsl::elm::Element *oldFocus, tsl::FocusDirection direction) {
    return this;
}

void StatusBar::RefreshState() {
    Result error = 0;
    u32 flags = 0;
    Result rc = qqmusicGetStatus(&flags);
    if (R_SUCCEEDED(rc)) {
        m_playing = (flags & 1) != 0;
        m_audio_unavailable = (flags & 4) != 0;
        m_blacklist_paused = (flags & 8) != 0;
        m_play_failed = (flags & 16) != 0;
    } else {
        error = rc;
        m_playing = false;
        m_audio_unavailable = false;
        m_blacklist_paused = false;
        m_play_failed = false;
    }

    QqMusicCurrentTrack track = {};
    rc = qqmusicGetCurrentTrack(&track);
    if (R_FAILED(rc) && R_SUCCEEDED(error)) error = rc;
    m_track = R_SUCCEEDED(rc) ? track : QqMusicCurrentTrack{};
    m_percentage = m_track.total_frames ? std::clamp(static_cast<float>(m_track.current_frame) / m_track.total_frames, 0.f, 1.f) : 0.f;
    FormatTime(m_track.current_frame, m_track.sample_rate, m_current_buffer, sizeof(m_current_buffer));
    FormatTime(m_track.total_frames, m_track.sample_rate, m_total_buffer, sizeof(m_total_buffer));

    m_meta_valid = false;
    if (m_track.path[0]) {
        QqMusicTrackMeta meta = {};
        rc = qqmusicGetTrackMeta(QQMUSIC_TRACKMETA_CURRENT, &meta);
        if (R_SUCCEEDED(rc)) {
            m_meta = meta;
            m_meta_valid = meta.valid != 0;
        } else if (R_SUCCEEDED(error)) error = rc;
    }
    RefreshName(m_track.path);
    u32 repeat = 0, shuffle = 0;
    rc = qqmusicGetMode(&repeat, &shuffle);
    if (R_SUCCEEDED(rc)) {
        m_repeat = repeat;
        m_shuffle = shuffle;
    } else if (R_SUCCEEDED(error)) error = rc;
    if (R_FAILED(error) && error != m_state_error)
        ReportError("读取播放状态失败", error);
    m_state_error = error;
}

void StatusBar::ReportError(const char *action, Result rc) {
    if (m_frame && R_FAILED(rc)) m_frame->showError(action, rc);
}

void StatusBar::RefreshName(const char *path) {
    std::string name;
    if (m_meta_valid && m_meta.title[0]) {
        name = m_meta.title;
        if (m_meta.artist[0]) {
            name += " - ";
            name += m_meta.artist;
        }
    } else {
        name = path[0] ? DisplayName(path) : "No track selected";
    }
    if (name == m_current_track) return;
    m_current_track = std::move(name);
    m_truncated = false;
    m_scroll_offset = 0;
    m_text_width = 0;
    m_counter = 0;
    m_scroll_text.clear();
}

StatusBar::~StatusBar() {
    free(this->m_cover_px);
    this->m_cover_px = nullptr;
}

void StatusBar::RefreshLyric() {
    if (this->m_track.path[0] == '\0') {
        this->m_lyric_available = false;
        this->m_lyric_track_path[0] = '\0';
        this->m_lyric_parser.Clear();
        this->m_lyric_retry = 0;
        this->m_lyric_cooldown = 0;
        return;
    }

    if (std::strcmp(this->m_lyric_track_path, this->m_track.path) != 0) {
        std::snprintf(this->m_lyric_track_path, sizeof(this->m_lyric_track_path), "%s", this->m_track.path);
        this->m_lyric_available = false;
        this->m_lyric_parser.Clear();
        this->m_lyric_retry = 0;
        this->m_lyric_cooldown = 0;
    }

    if (this->m_lyric_available)
        return;

    if (this->m_lyric_cooldown > 0) {
        this->m_lyric_cooldown--;
        return;
    }

    char name[96] = {};
    if (R_SUCCEEDED(qqmusicEnsureLyric(this->m_track.path, name, sizeof(name))) && name[0] && SdCoverFsReady()) {
        static u8 lrc_buf[48 * 1024];
        char file_path[160];
        std::snprintf(file_path, sizeof(file_path), "/qqmusic-cache/%s", name);
        u32 total = 0;
        if (ReadWholeFile(file_path, lrc_buf, (u32)sizeof(lrc_buf), &total) && total > 0) {
            std::string lrc_text((const char *)lrc_buf, total);
            this->m_lyric_available = this->m_lyric_parser.Parse(lrc_text);
        }
    }

    if (!this->m_lyric_available) {
        this->m_lyric_retry++;
        this->m_lyric_cooldown = 30; // 30 帧（0.5秒）后重试
        if (this->m_lyric_retry >= 40)
            this->m_lyric_cooldown = 300; // 20秒后放宽至 5 秒轮询一次
    }
}

/* 封面懒加载：每个曲目路径只取一次。
   后台（sysmodule）确保 /qqmusic-cache/<name> 已落盘 → 覆盖层直接读文件 →
   stb_image 解码 RGBA → 2x2 box 减半至不超过目标 2 倍 → 一次 area-box 到 (nw,nh)
   （80x80 盒内保持宽高比）→ 缓存 RGBA。 */
void StatusBar::RefreshCover() {
    if (this->m_track.path[0] == 0) {
        free(m_cover_px);
        m_cover_px = nullptr;
        m_cover_w = m_cover_h = 0;
        m_cover_path[0] = 0;
        m_cover_done = false;
        m_cover_retry = 0;
        m_cover_cooldown = 0;
        return;
    }

    // 曲目切换：重置重试计数并释放上一首封面
    if (std::strcmp(this->m_cover_path, this->m_track.path) != 0) {
        free(this->m_cover_px);
        this->m_cover_px = nullptr;
        this->m_cover_w  = 0;
        this->m_cover_h  = 0;
        std::snprintf(this->m_cover_path, sizeof(this->m_cover_path), "%s", this->m_track.path);
        this->m_cover_done = false;
        this->m_cover_retry = 0;
        this->m_cover_cooldown = 0;
    }

    if (this->m_cover_done)
        return;
    if (this->m_cover_cooldown > 0) {
        this->m_cover_cooldown--;
        return;
    }

    // 1) 让后台把封面落盘（在线曲目走 QQ 音乐 CDN，本地曲目导出内嵌 APIC），
    //    再由覆盖层直接读文件解码：一次 IPC 换取整张封面，避免逐块往返。
    char name[96] = {};
    bool cached = false;

    if (R_SUCCEEDED(qqmusicEnsureCover(this->m_track.path, name, sizeof(name))) && name[0] && SdCoverFsReady()) {
        static u8 cover_buf[256 * 1024]; // 封面硬上限（与 sysmodule 侧 COVER_MAX 一致）
        char file_path[160];
        std::snprintf(file_path, sizeof(file_path), "/qqmusic-cache/%s", name);

        u32 total = 0;
        if (ReadWholeFile(file_path, cover_buf, (u32)sizeof(cover_buf), &total)) {
            int w = 0, h = 0, comp = 0;
            // Reject oversized headers before the decoder allocates their pixel buffer.
            u8 *rgba = nullptr;
            if (stbi_info_from_memory(cover_buf, (int)total, &w, &h, &comp) &&
                w > 0 && h > 0 && (u64)w * (u64)h <= 4096u * 4096u)
                rgba = stbi_load_from_memory(cover_buf, (int)total, &w, &h, &comp, 4);
            if (rgba == nullptr) {
                free(rgba);
            } else {
                const float scale = std::min((float)COVER_SIZE / (float)w, (float)COVER_SIZE / (float)h);
                if (scale < 1.f) {
                    const int nw = std::max(1, (int)(w * scale + 0.5f));
                    const int nh = std::max(1, (int)(h * scale + 0.5f));
                    int cw = w, ch = h;
                    u8 *cur = rgba;
                    while (cw > 2 * nw || ch > 2 * nh) {
                        u8 *next = (u8 *)malloc((size_t)((cw + 1) / 2) * ((ch + 1) / 2) * 4);
                        if (next == nullptr) {
                            free(cur);
                            cur = nullptr;
                            break;
                        }
                        halve_rgba(cur, cw, ch, next);
                        free(cur);
                        cur = next;
                        cw = (cw + 1) / 2;
                        ch = (ch + 1) / 2;
                    }
                    if (cur != nullptr) {
                        u8 *final_px = (u8 *)malloc((size_t)nw * nh * 4);
                        if (final_px == nullptr) {
                            free(cur);
                            cur = nullptr;
                        } else {
                            box_to_rgba(cur, cw, ch, final_px, nw, nh);
                            free(cur);
                            cur = final_px;
                            w = nw;
                            h = nh;
                        }
                    }
                    rgba = cur;
                }
                if (rgba != nullptr) {
                    this->m_cover_px = rgba;
                    this->m_cover_w  = (u32)w;
                    this->m_cover_h  = (u32)h;
                    cached = true;
                }
            }
        }
    }

    if (cached) {
        this->m_cover_done = true;
        this->m_cover_retry = 0;
    } else {
        // 封面由后台线程取（下载/导出），这里只是周期性来问一句「落盘了吗」，
        // 每次调用都是纯文件检查、不阻塞，所以可以放心多轮询一会儿：
        // 20 帧（约 0.33 秒）一轮，最多 30 轮（约 10 秒）后放弃。
        this->m_cover_retry++;
        this->m_cover_cooldown = 20;
        if (this->m_cover_retry >= 30)
            this->m_cover_done = true;
    }
}

void StatusBar::draw(tsl::gfx::Renderer *renderer) {
    if (this->m_touched && Element::getInputMode() == tsl::InputMode::Touch) {
        renderer->drawRect(ELEMENT_BOUNDS(this), a(tsl::style::color::ColorClickAnimation));
    }

    /* 封面（M2）：逐像素 blit（80x80 盒内居中）。 */
    if (this->m_cover_px != nullptr && this->m_cover_w > 0 && this->m_cover_h > 0) {
        const s32 ox = this->CoverX() + (COVER_SIZE - (s32)this->m_cover_w) / 2;
        const s32 oy = this->CoverY() + (COVER_SIZE - (s32)this->m_cover_h) / 2;
        const u32 cw = this->m_cover_w;
        const u32 ch = this->m_cover_h;
        for (u32 y = 0; y < ch; y++) {
            for (u32 x = 0; x < cw; x++) {
                const u8 *p = this->m_cover_px + (size_t)(y * cw + x) * 4;
                const u8 a8 = p[3];
                if (a8 == 0)
                    continue;
                const tsl::Color c(
                    static_cast<u8>(p[0] >> 4),
                    static_cast<u8>(p[1] >> 4),
                    static_cast<u8>(p[2] >> 4),
                    static_cast<u8>(a8 >> 4)
                );
                if (a8 >= 0xF0)
                    renderer->setPixel((u32)(ox + (s32)x), (u32)(oy + (s32)y), c);
                else
                    renderer->setPixelBlendSrc((u32)(ox + (s32)x), (u32)(oy + (s32)y), c);
            }
        }
    }

    /* 标题（过宽则滚动；封面右侧左对齐）。 */
    const s32 title_x = this->TitleX();
    const s32 title_avail = this->TitleAvail();
    if (this->m_text_width == 0 && !this->m_current_track.empty()) {
        auto [width, height] = renderer->drawString(this->m_current_track.data(), false, 0, 0, 23, tsl::style::color::ColorTransparent);
        this->m_truncated = static_cast<s32>(width) > title_avail;
        if (this->m_truncated) {
            this->m_scroll_text = std::string(this->m_current_track).append("       ");
            auto [ex_width, ex_height] = renderer->drawString(this->m_scroll_text.c_str(), false, 0, 0, 23, tsl::style::color::ColorTransparent);
            this->m_scroll_text.append(this->m_current_track);
            this->m_text_width = ex_width;
        } else {
            this->m_text_width = width;
        }
    }
    if (this->m_truncated) {
        renderer->enableScissoring((u32)title_x, (u32)(this->TitleY() - 26), (u32)title_avail, 34);
        renderer->drawString(this->m_scroll_text.c_str(), false, title_x - (s32)this->m_scroll_offset, this->TitleY(), 23, tsl::style::color::ColorText);
        renderer->disableScissoring();
        if (this->m_counter == 120) {
            if (this->m_scroll_offset == this->m_text_width) {
                this->m_scroll_offset = 0;
                this->m_counter = 0;
            } else {
                this->m_scroll_offset++;
            }
        } else {
            this->m_counter++;
        }
    } else {
        renderer->drawString(this->m_current_track.data(), false, title_x, this->TitleY(), 23, tsl::style::color::ColorText);
    }

    /* 进度条。 */
    const s32 bar_x   = this->SeekBarX();
    const u32 bar_len = this->SeekBarLen();
    const s32 bar_y   = this->SeekY();
    constexpr s32 kSeekThumbRadius = 7;
    const s32 track_cy = bar_y + 1;
    const bool seek_focused = this->m_focused && (this->m_focus == 5);
    const tsl::Color seek_fill = {0x0, 0xA, 0xD, 0xF};
    if (seek_focused) {
        renderer->drawRect(bar_x - 1, bar_y - 3, bar_len + 2, 9, a({0x0, 0xA, 0xD, 0xA}));
    }
    renderer->drawRect(bar_x, bar_y, bar_len, 3, seek_focused ? 0xFCCC : 0xF666);
    const u32 fill = (this->m_track.total_frames > 0) ? (u32)(bar_len * this->m_percentage) : 0;
    if (fill > 0) {
        renderer->drawRect(bar_x, bar_y - 2, fill, 7, a(seek_fill));
    }
    if (this->m_track.total_frames > 0) {
        const s32 thumb_x = bar_x + (s32)fill;
        renderer->drawCircle(thumb_x, track_cy, kSeekThumbRadius, true, a(seek_fill));
    }
    renderer->drawString(this->m_current_buffer, false, bar_x, bar_y + 20, 18, 0xffff);
    renderer->drawString(this->m_total_buffer, false, this->getX() + this->getWidth() - 55, bar_y + 20, 18, 0xffff);

    /* 传输控制。 */
    if (this->m_focused && this->m_focus != 5) {
        const s32 hs = 40;
        const s32 fx = this->m_focus == 0 ? this->GetRepeatX()
                    : this->m_focus == 1 ? this->GetPrevX()
                    : this->m_focus == 3 ? this->GetNextX()
                    : this->m_focus == 4 ? this->GetShuffleX()
                    : this->GetPlayStateX();
        renderer->drawRect(fx - hs / 2, this->ControlsY() - hs / 2, hs, hs, 0x4FFF);
    }

    /* 状态提示（M2：音频不可用 / 黑名单自动暂停）。 */
    if (this->m_audio_unavailable || this->m_blacklist_paused || this->m_play_failed) {
        const char *hint = this->m_audio_unavailable
            ? "音频不可用: 游戏正独占音频服务"
            : this->m_blacklist_paused
            ? "已自动暂停: 游戏屏蔽运行中"
            : "播放失败: 无法解析曲目或网络超时";
        renderer->drawString(hint, false, this->getX() + 15, this->HintY(), 16, a({0xF, 0xF, 0xE, 0}));
    }
    else {
        RefreshLyric();
        if (this->m_lyric_available && !this->m_lyric_parser.IsEmpty() && this->m_track.sample_rate > 0) {
            const u32 cur_ms = (u32)((u64)this->m_track.current_frame * 1000 / this->m_track.sample_rate);
            const int line_idx = this->m_lyric_parser.GetCurrentIndex(cur_ms);
            std::string line_str;
            if (line_idx >= 0 && line_idx < (int)this->m_lyric_parser.LineCount()) {
                const auto &l = this->m_lyric_parser.GetLine((size_t)line_idx);
                line_str = l.text.empty() ? "(音乐过门)" : l.text;
            } else {
                line_str = "(音乐前奏)";
            }
            if (!line_str.empty()) {
                renderer->drawString("\uE098", false, this->getX() + 15, this->HintY(), 16, a({0x0, 0xD, 0xF, 0xF}));
                renderer->drawString(line_str.c_str(), false, this->getX() + 38, this->HintY(), 16, a({0xF, 0xF, 0xF, 0xF}), this->getWidth() - 50);
            }
        }
        else if (this->m_focused) {
            renderer->drawString("L/R: 切歌   ZL/ZR: 快进退   左右: 选中控制", false, this->getX() + 15, this->HintY(), 14, a(tsl::style::color::ColorDescription));
        }
    }

    const auto repeat_color = this->m_repeat ? tsl::style::color::ColorHighlight : tsl::style::color::ColorHeaderBar;
    if (this->m_repeat == 1)
        symbol::repeat::one::symbol.draw(this->GetRepeatX(), this->GetRepeatY(), renderer, repeat_color);
    else
        symbol::repeat::all::symbol.draw(this->GetRepeatX(), this->GetRepeatY(), renderer, repeat_color);

    const auto shuffle_color = this->m_shuffle ? tsl::style::color::ColorHighlight : tsl::style::color::ColorHeaderBar;
    symbol::shuffle::symbol.draw(this->GetShuffleX(), this->GetShuffleY(), renderer, shuffle_color);

    symbol::prev::symbol.draw(this->GetPrevX(), this->GetPrevY(), renderer, tsl::style::color::ColorText);
    const AlphaSymbol &play_symbol = this->m_playing ? symbol::pause::symbol : symbol::play::symbol;
    play_symbol.draw(this->GetPlayStateX(), this->GetPlayStateY(), renderer, tsl::style::color::ColorText);
    symbol::next::symbol.draw(this->GetNextX(), this->GetNextY(), renderer, tsl::style::color::ColorText);
}

void StatusBar::layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) {
    this->setBoundaries(this->getX(), this->getY(), this->getWidth(), HEIGHT);
}

bool StatusBar::onClick(u64 keys) {
    if (keys & HidNpadButton_AnyLeft) {
        m_focus = (m_focus + 5) % 6;
    } else if (keys & HidNpadButton_AnyRight) {
        m_focus = (m_focus + 1) % 6;
    } else if (keys & HidNpadButton_A) {
        ActivateControl(m_focus == 5 ? 2 : m_focus);
    } else if (keys & HidNpadButton_L) {
        ActivateControl(1);
    } else if (keys & (HidNpadButton_R | HidNpadButton_X)) {
        ActivateControl(3);
    } else if (keys & HidNpadButton_ZL) {
        SeekBySeconds(-config::get_seek_skip_seconds());
    } else if (keys & HidNpadButton_ZR) {
        SeekBySeconds(config::get_seek_skip_seconds());
    } else {
        return false;
    }
    return true;
}

void StatusBar::ActivateControl(int index) {
    Result rc = 0;
    const char *action = "播放操作失败";
    switch (index) {
        case 0:
        case 4: {
            u32 repeat = 0, shuffle = 0;
            rc = qqmusicGetMode(&repeat, &shuffle);
            if (R_SUCCEEDED(rc))
                rc = index == 0 ? qqmusicSetRepeatMode((repeat + 1) % 3) : qqmusicSetShuffleMode(!shuffle);
            action = index == 0 ? "切换循环模式失败" : "切换随机播放失败";
            break;
        }
        case 1:
            rc = qqmusicPrev();
            action = "上一曲失败";
            break;
        case 2: {
            u32 flags = 0;
            rc = qqmusicGetStatus(&flags);
            if (R_FAILED(rc)) break;
            if (flags & 1) {
                rc = qqmusicPause();
            } else {
                u32 count = 0;
                rc = qqmusicGetQueueSize(&count);
                if (R_FAILED(rc)) break;
                if (count == 0) {
                    if (m_frame) m_frame->setToast("播放列表为空", "请在在线音乐或本地音乐中添加歌曲");
                    return;
                }
                rc = qqmusicPlay();
            }
            break;
        }
        case 3:
            rc = qqmusicNext();
            action = "下一曲失败";
            break;
        default:
            return;
    }
    m_focus = index;
    if (R_FAILED(rc)) ReportError(action, rc);
    else RefreshState();
}

bool StatusBar::onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32 prevX, s32 prevY, s32 initialX, s32 initialY) {
    if (event == tsl::elm::TouchEvent::Touch) {
        this->m_touched = true;
    } else if (event == tsl::elm::TouchEvent::Release) {
        this->m_touched = false;
        // 点击（非拖动）命中控件？
        const bool in_bar = currY > (this->SeekY() - 20) && currY < (this->SeekY() + 20);
        const bool in_ctl = currY > (this->ControlsY() - 25) && currY < (this->ControlsY() + 25);
        const bool dragged = (std::abs(currX - initialX) > 10);
        if (!dragged && in_bar) {
            this->m_focus = 5;
            this->SeekToPercentage((float)(currX - this->SeekBarX()) / (float)this->SeekBarLen());
            return true;
        }
        if (!dragged && in_ctl) {
            const s32 dx = std::abs(currX - this->GetRepeatX());
            const s32 dp = std::abs(currX - this->GetPrevX());
            const s32 dplay = std::abs(currX - this->GetPlayStateX());
            const s32 dn = std::abs(currX - this->GetNextX());
            const s32 ds = std::abs(currX - this->GetShuffleX());
            int best = 2;
            s32 best_d = dplay;
            if (dx < best_d) { best = 0; best_d = dx; }
            if (dp < best_d) { best = 1; best_d = dp; }
            if (dn < best_d) { best = 3; best_d = dn; }
            if (ds < best_d) { best = 4; best_d = ds; }
            if (best_d <= 40) {
                this->m_focus = best;
                this->ActivateControl(best);
                return true;
            }
        }
        return false;
    } else if (event == tsl::elm::TouchEvent::Scroll) {
        if (currY > (this->SeekY() - 25) && currY < (this->SeekY() + 25)) {
            this->m_focus = 5;
            this->SeekToPercentage((float)(currX - this->SeekBarX()) / (float)this->SeekBarLen());
            return true;
        }
    }
    return false;
}

void StatusBar::SeekToPercentage(float pct) {
    QqMusicCurrentTrack track = {};
    const Result rc = qqmusicGetCurrentTrack(&track);
    if (R_FAILED(rc)) {
        ReportError("Read seek position failed", rc);
        return;
    }
    if (!track.total_frames || !track.sample_rate) {
        if (m_frame) m_frame->setToast("Seek unavailable", "Play a track first.");
        return;
    }
    const u32 frame = static_cast<u32>((track.total_frames - 1) * static_cast<double>(std::clamp(pct, 0.f, 1.f)));
    const Result seek_rc = qqmusicSeek(frame);
    if (R_FAILED(seek_rc)) ReportError("Seek failed", seek_rc);
    else RefreshState();
}

void StatusBar::SeekBySeconds(int delta_sec) {
    QqMusicCurrentTrack track = {};
    const Result rc = qqmusicGetCurrentTrack(&track);
    if (R_FAILED(rc)) {
        ReportError("Read seek position failed", rc);
        return;
    }
    if (!track.total_frames || !track.sample_rate) {
        if (m_frame) m_frame->setToast("Seek unavailable", "Play a track first.");
        return;
    }
    const s64 target = std::clamp<s64>(static_cast<s64>(track.current_frame) + static_cast<s64>(delta_sec) * track.sample_rate, 0, track.total_frames - 1);
    const Result seek_rc = qqmusicSeek(static_cast<u32>(target));
    if (R_FAILED(seek_rc)) ReportError("Seek failed", seek_rc);
    else RefreshState();
}

void StatusBar::update() {
    this->RefreshState();
    this->RefreshCover();
}