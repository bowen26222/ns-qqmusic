#pragma once

#include <switch.h>

namespace config {

// tune shuffle
auto get_shuffle() -> bool;
void set_shuffle(bool value);

// tune repeat
auto get_repeat() -> int;
void set_repeat(int value);

// tune volume
auto get_volume() -> float;
void set_volume(float value);

// per title tune enable
auto has_title_enabled(u64 tid) -> bool;
auto get_title_enabled(u64 tid) -> bool;
void set_title_enabled(u64 tid, bool value);

// default for tune for every title
auto get_title_enabled_default() -> bool;
void set_title_enabled_default(bool value);

// per title volume
auto has_title_volume(u64 tid) -> bool;
auto get_title_volume(u64 tid) -> float;
void set_title_volume(u64 tid, float value);

// default volume for every title
auto get_default_title_volume() -> float;
void set_default_title_volume(float value);

// Jellyfin sign-in (shared by overlay + sysmodule). get_* return string length.
auto get_jelly_server(char* out, int max_len) -> int;   // "host:port"
void set_jelly_server(const char* value);
auto get_jelly_token(char* out, int max_len) -> int;
void set_jelly_token(const char* value);
auto get_jelly_userid(char* out, int max_len) -> int;
void set_jelly_userid(const char* value);

// Overlay seek-bar skip interval in seconds (5, 10, 15, 30, or 60).
auto get_seek_skip_seconds() -> int;
void set_seek_skip_seconds(int value);

// Playback queue position
auto get_queue_position() -> u32;
void set_queue_position(u32 value);

// Online list sort mode: 0=默认(从新到旧) 1=按字母(A-Z)
auto get_sort_mode() -> int;
void set_sort_mode(int value);

// 在线缓存容量策略：0=跟随播放队列（队列有多少首就缓存多少首）1=指定数量
auto get_cache_mode() -> int;
void set_cache_mode(int value);
// 指定数量模式下的上限（1..2000，默认 20）
auto get_cache_limit() -> int;
void set_cache_limit(int value);
}
