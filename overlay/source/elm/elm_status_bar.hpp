#pragma once

#include "client.h"
#include "symbol.hpp"

#include "lyric_parser.hpp"
#include <tesla.hpp>

class QqMusicOverlayFrame;
// 顶部"正在播放"状态条：封面 + 标题/艺术家 + 进度条 + 传输控制（repeat/shuffle/prev/play/next）
// + 状态提示（音频不可用 / 黑名单自动暂停）。数据全部来自 sysmodule IPC（qqmusic*）。
class StatusBar final : public tsl::elm::Element {
  private:
    bool m_playing = false;
    QqMusicOverlayFrame *m_frame = nullptr;
    Result m_state_error = 0;
    u32 m_repeat = 0;      // 0=off 1=one 2=all
    u32 m_shuffle = 0;     // 0/1
    QqMusicCurrentTrack m_track = {};

    float m_percentage = 0.f;

    std::string m_current_track;

    // M2 元数据（懒加载，仅当前曲目）
    QqMusicTrackMeta m_meta = {};
    bool m_meta_valid = false;

    // M2 状态位（GetStatus bit2/bit3）
    bool m_audio_unavailable = false;
    bool m_blacklist_paused = false;
    bool m_play_failed = false;
    // M2 封面（懒加载 + 按需取块；解码后缓存 RGBA 缩放图）
    char m_cover_path[FS_MAX_PATH]{};
    u8 *m_cover_px = nullptr;
    u32 m_cover_w = 0;
    u32 m_cover_h = 0;
    bool m_cover_done = false;
    u8 m_cover_retry = 0;
    u8 m_cover_cooldown = 0;
    std::string m_scroll_text;
    u32 m_text_width = 0;
    u32 m_scroll_offset = 0;
    bool m_truncated = false;
    u8 m_counter = 0;

    int m_focus = 2;       // 0=repeat 1=prev 2=play 3=next 4=shuffle 5=seek bar
    bool m_touched = false;

    char m_current_buffer[0x20]{};
    char m_total_buffer[0x20]{};

    void RefreshState();
    void ReportError(const char *action, Result rc);
    void RefreshName(const char *path);
    void RefreshCover();
    void ActivateControl(int index);
    void SeekToPercentage(float pct);
    void SeekBySeconds(int delta_sec);
    void RefreshLyric();
    qqmusic::LyricParser m_lyric_parser;
    char m_lyric_track_path[FS_MAX_PATH]{};
    bool m_lyric_available = false;
    u8 m_lyric_retry = 0;
    u16 m_lyric_cooldown = 0;

  public:
    StatusBar();
    ~StatusBar();
    void setFrame(QqMusicOverlayFrame *frame) { m_frame = frame; }

    tsl::elm::Element *requestFocus(tsl::elm::Element *oldFocus, tsl::FocusDirection direction) override;
    void draw(tsl::gfx::Renderer *renderer) override;
    void layout(u16 parentX, u16 parentY, u16 parentWidth, u16 parentHeight) override;
    bool onClick(u64 keys) override;
    bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32 prevX, s32 prevY, s32 initialX, s32 initialY) override;

    void update();

  public:
    static constexpr s32 HEIGHT = 230;
    static constexpr s32 COVER_SIZE = 80;

  private:
    ALWAYS_INLINE s32 CoverX()      { return this->getX() + 14; }
    ALWAYS_INLINE s32 CoverY()      { return this->getY() + 12; }
    ALWAYS_INLINE s32 TitleX()      { return this->getX() + 14 + COVER_SIZE + 16; }
    ALWAYS_INLINE s32 TitleAvail()  { return this->getWidth() - (14 + COVER_SIZE + 16) - 15; }
    ALWAYS_INLINE s32 TitleY()      { return this->getY() + 44; }
    ALWAYS_INLINE s32 SeekY()       { return this->getY() + 96; }
    ALWAYS_INLINE s32 SeekBarX()    { return this->getX() + 15; }
    ALWAYS_INLINE u32  SeekBarLen() { return this->getWidth() - 30; }
    ALWAYS_INLINE s32 ControlsY()   { return this->getY() + 160; }
    ALWAYS_INLINE s32 HintY()       { return this->getY() + 212; }

    ALWAYS_INLINE s32 GetRepeatX()    { return CenterX() - 110; }
    ALWAYS_INLINE s32 GetShuffleX()   { return CenterX() + 110; }
    ALWAYS_INLINE s32 GetPrevX()      { return CenterX() - 55; }
    ALWAYS_INLINE s32 GetPlayStateX() { return CenterX(); }
    ALWAYS_INLINE s32 GetNextX()      { return CenterX() + 55; }
    ALWAYS_INLINE s32 GetRepeatY()    { return ControlsY(); }
    ALWAYS_INLINE s32 GetShuffleY()   { return ControlsY(); }
    ALWAYS_INLINE s32 GetPrevY()      { return ControlsY(); }
    ALWAYS_INLINE s32 GetPlayStateY() { return ControlsY(); }
    ALWAYS_INLINE s32 GetNextY()      { return ControlsY(); }
    ALWAYS_INLINE s32 CenterX()       { return this->getX() + this->getWidth() / 2; }
};