#include "config.hpp"
#include "sdmc/sdmc.hpp"
#include "minIni/minIni.h"
#include <cstdio>

namespace config {

namespace {

const char CONFIG_PATH[]{"/config/qqmusic/config.ini"};

void create_config_dir() {
    /* Creating directory on every set call looks sus, but the user may delete the dir */
    /* whilst the sys-mod is running and then any changes made via the overlay */
    /* is lost, which sucks. */
    sdmc::CreateFolder("/config");
    sdmc::CreateFolder("/config/qqmusic");
}

auto get_tid_str(u64 tid) -> std::string {
    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)tid);
    return buf;
}

}

auto get_shuffle() -> bool {
    return ini_getbool("config", "shuffle", false, CONFIG_PATH);
}

void set_shuffle(bool value) {
    create_config_dir();
    ini_putl("config", "shuffle", value, CONFIG_PATH);
}

auto get_repeat() -> int {
    return ini_getl("config", "repeat", 1, CONFIG_PATH);
}

void set_repeat(int value) {
    create_config_dir();
    ini_putl("config", "repeat", value, CONFIG_PATH);
}

auto get_volume() -> float {
    return ini_getf("config", "volume", 1.f, CONFIG_PATH);
}

void set_volume(float value) {
    create_config_dir();
    ini_putf("config", "volume", value, CONFIG_PATH);
}

auto has_title_enabled(u64 tid) -> bool {
    return ini_haskey("title", get_tid_str(tid).c_str(), CONFIG_PATH);
}

auto get_title_enabled(u64 tid) -> bool {
    return ini_getbool("title", get_tid_str(tid).c_str(), true, CONFIG_PATH);
}

void set_title_enabled(u64 tid, bool value) {
    create_config_dir();
    ini_putl("title", get_tid_str(tid).c_str(), value, CONFIG_PATH);
}

auto get_title_enabled_default() -> bool {
    return ini_getbool("title", "default", true, CONFIG_PATH);
}

void set_title_enabled_default(bool value) {
    create_config_dir();
    ini_putl("title", "default", value, CONFIG_PATH);
}

auto has_title_volume(u64 tid) -> bool {
    return ini_haskey("volume", get_tid_str(tid).c_str(), CONFIG_PATH);
}

auto get_title_volume(u64 tid) -> float {
    return ini_getf("volume", get_tid_str(tid).c_str(), 1.f, CONFIG_PATH);
}

void set_title_volume(u64 tid, float value) {
    create_config_dir();
    ini_putf("volume", get_tid_str(tid).c_str(), value, CONFIG_PATH);
}

auto get_default_title_volume() -> float {
    return ini_getf("config", "global_volume", 1.f, CONFIG_PATH);
}

void set_default_title_volume(float value) {
    create_config_dir();
    ini_putf("config", "global_volume", value, CONFIG_PATH);
}

/* ---- Jellyfin sign-in (shared by overlay + sysmodule) ---- */

auto get_jelly_server(char* out, int max_len) -> int {
    return ini_gets("jelly", "server", "", out, max_len, CONFIG_PATH);
}
void set_jelly_server(const char* value) {
    create_config_dir();
    ini_puts("jelly", "server", value, CONFIG_PATH);
}

auto get_jelly_token(char* out, int max_len) -> int {
    return ini_gets("jelly", "token", "", out, max_len, CONFIG_PATH);
}
void set_jelly_token(const char* value) {
    create_config_dir();
    ini_puts("jelly", "token", value, CONFIG_PATH);
}

auto get_jelly_userid(char* out, int max_len) -> int {
    return ini_gets("jelly", "userid", "", out, max_len, CONFIG_PATH);
}
void set_jelly_userid(const char* value) {
    create_config_dir();
    ini_puts("jelly", "userid", value, CONFIG_PATH);
}

auto get_seek_skip_seconds() -> int {
    const int v = (int)ini_getl("config", "seek_skip_seconds", 10, CONFIG_PATH);
    switch (v) {
        case 5: case 10: case 15: case 30: case 60: return v;
        default: return 10;
    }
}

void set_seek_skip_seconds(int value) {
    create_config_dir();
    ini_putl("config", "seek_skip_seconds", value, CONFIG_PATH);
}

auto get_queue_position() -> u32 {
    return (u32)ini_getl("config", "queue_pos", 0, CONFIG_PATH);
}

void set_queue_position(u32 value) {
    create_config_dir();
    ini_putl("config", "queue_pos", (long)value, CONFIG_PATH);
}

// 注意：键名带 v2 —— 上一版的 sort_mode（1=最新在前、2=首字母）与现在的两档语义
// （0=从新到旧、1=按字母）不兼容，沿用旧值会把「我喜欢」误判成按字母排序。
auto get_sort_mode() -> int {
    const int v = (int)ini_getl("config", "sort_mode_v2", 0, CONFIG_PATH);
    return (v == 0 || v == 1) ? v : 0;
}

void set_sort_mode(int value) {
    create_config_dir();
    ini_putl("config", "sort_mode_v2", (long)(value ? 1 : 0), CONFIG_PATH);
}

auto get_cache_mode() -> int {
    const int v = (int)ini_getl("config", "cache_mode", 1, CONFIG_PATH);
    return (v == 0 || v == 1) ? v : 1;
}

void set_cache_mode(int value) {
    create_config_dir();
    ini_putl("config", "cache_mode", (long)(value ? 1 : 0), CONFIG_PATH);
}

auto get_cache_limit() -> int {
    const int v = (int)ini_getl("config", "cache_limit", 20, CONFIG_PATH);
    if (v < 1) return 1;
    if (v > 2000) return 2000;
    return v;
}

void set_cache_limit(int value) {
    if (value < 1) value = 1;
    if (value > 2000) value = 2000;
    create_config_dir();
    ini_putl("config", "cache_limit", (long)value, CONFIG_PATH);
}

} // namespace config
