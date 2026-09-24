#!/usr/bin/env python3
"""Host regression for favourite albums / playlists (simple web-endpoint design).

Compiles the REAL sysmodule/source/net/qqmusic_api.cpp plus the REAL collection GUI
against stubs and a mock transport:
  * every album/playlist list comes from the two documented Web endpoints
    (fcg_get_profile_order_asset.fcg reqtype=2/3 and fcg_user_created_diss), so the
    implementation needs nothing but the login cookie;
  * a single broken/delisted entry never discards the rest of the page;
  * page offsets are requested explicitly (sin/ein), and playlist paging accounts for
    the user-created entries that only appear on the first page.
Run from WSL: python3 tools/check-fav-albums.py
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

SWITCH = r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef uint64_t u64; typedef int64_t s64; typedef uint32_t Result;
#define FS_MAX_PATH 768
#define R_SUCCEEDED(r) ((r) == 0)
#define R_FAILED(r) ((r) != 0)
#define MAKERESULT(m,d) ((Result)((m) | ((d) << 9)))
#define Module_Libnx 345
#define LibnxError_BadInput 1
#define HidNpadButton_A 1
#define HidNpadButton_B 2
struct HidTouchState {}; struct HidAnalogStickState {};
struct FsFile {}; struct FsDir {}; struct FsFileSystem {}; typedef int FsDirEntryType;
#define FsOpenMode_Read 1
Result fsFileGetSize(FsFile*, s64*);
Result fsFileRead(FsFile*, u64, void*, u64, int, u64*);
void fsFileClose(FsFile*);
u64 randomGet64();
'''

TESLA = r'''
#pragma once
#include "switch.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace tsl {
namespace elm {
struct Element { virtual ~Element() = default; };
struct ListItem : Element {
    std::string text; std::function<bool(u64)> click;
    ListItem(const std::string& t, const std::string& = "") : text(t) {}
    void setClickListener(std::function<bool(u64)> c) { click = std::move(c); }
    void setText(const std::string& t) { text = t; }
    void setValue(const std::string&) {}
};
struct CategoryHeader : ListItem {
    CategoryHeader(const std::string& t, bool = false) : ListItem(t) {}
};
struct List : Element {
    std::vector<std::unique_ptr<Element>> rows;
    void clear() { rows.clear(); }
    void addItem(Element* e) { rows.emplace_back(e); }
    int getIndexInList(Element*) { return -1; }
    void setFocusedIndex(int) {}
};
}
struct Gui {
    virtual ~Gui() = default;
    virtual elm::Element* createUI() = 0;
    virtual void update() {}
    virtual bool handleInput(u64,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) { return false; }
    void removeFocus() {}
    void requestFocus(elm::Element*, int) {}
};
enum class FocusDirection { None };
template<class T, class... Args> void changeTo(Args&&...) {}
template<class T, class... Args> void swapTo(Args&&...) {}
inline void goBack() {}
}
'''

FRAME = r'''
#pragma once
#include <tesla.hpp>
struct QqMusicOverlayFrame : tsl::elm::Element {
    std::unique_ptr<tsl::elm::Element> content;
    std::string toast;
    void setDescription(const char*) {}
    void setContent(tsl::elm::Element* e) { content.reset(e); }
    void setToast(const std::string& a, const std::string& b) { toast = a + ": " + b; }
    void showError(const char* a, Result rc) { toast = std::string(a) + ": err"; }
};
'''

SONGS = r'''
#pragma once
#include "client.h"
namespace online_fetch { constexpr u32 kAlbum=300, kSinger=100, kPlaylist=200; }
struct OnlineSongsGui {};
struct OnlineListSpec {
    static OnlineListSpec AlbumOf(const char*, const std::string&, u32) { return {}; }
    static OnlineListSpec SingerOf(const char*, const std::string&, u32) { return {}; }
    static OnlineListSpec PlaylistOf(const char*, const std::string&, u32) { return {}; }
};
'''

PROGRAM = r'''
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "qqmusic_api.cpp"
#include "gui_online_collections.cpp"

extern "C" void sysLog(const char*) {}
extern "C" void crashTrace(const char*) {}
extern "C" int ini_puts(const char*, const char*, const char*, const char*) { return 1; }
Result fsFileGetSize(FsFile*, s64* size) { *size = 0; return 0; }
Result fsFileRead(FsFile*, u64, void*, u64, int, u64* got) { *got = 0; return 0; }
void fsFileClose(FsFile*) {}
u64 randomGet64() { return 0x1234; }
namespace config { int get_sort_mode() { return 0; } void set_sort_mode(int) {} }

// ---- mock transport: one body per Web endpoint ------------------------------
static long http_status = 200;
static std::string album_body;     // reqtype=2
static std::string fav_body;       // reqtype=3
static std::string created_body;   // fcg_user_created_diss
static int album_calls = 0, fav_calls = 0, created_calls = 0, post_calls = 0;
static int last_album_sin = -1, last_album_ein = -1;
static int last_fav_sin = -1, last_fav_ein = -1;

static int query_int(const std::string& url, const char* key) {
    const std::string k = std::string(key) + "=";
    const size_t p = url.find(k);
    if (p == std::string::npos) return -1;
    return std::atoi(url.c_str() + p + k.size());
}

namespace qqmusic::net {
HttpResponse Get(const std::string& url, const std::vector<std::string>&, const std::string&, long) {
    HttpResponse r;
    r.status_code = http_status;
    if (http_status == 0) return r;                       // transport failure
    if (url.find("fcg_user_created_diss") != std::string::npos) {
        ++created_calls;
        r.body = created_body;
    } else if (url.find("reqtype=2") != std::string::npos) {
        ++album_calls;
        last_album_sin = query_int(url, "sin");
        last_album_ein = query_int(url, "ein");
        r.body = album_body;
    } else if (url.find("reqtype=3") != std::string::npos) {
        ++fav_calls;
        last_fav_sin = query_int(url, "sin");
        last_fav_ein = query_int(url, "ein");
        r.body = fav_body;
    }
    return r;
}
HttpResponse Post(const std::string&, const std::string&, const std::vector<std::string>&,
                  const std::string&, long) {
    ++post_calls;                                          // the simple design posts nothing
    HttpResponse r; r.status_code = 200; return r;
}
} // namespace qqmusic::net

Result qqmusicOnlineGetStatus(QqMusicOnlineStatus* out) { out->logged_in = 1; return 0; }
Result qqmusicOnlineGetFavAlbums(u32 offset, u32 size, QqMusicOnlineCollection* out, u32 capacity, u32* count, u32* total) {
    std::vector<QqMusicOnlineCollection> items; int found_total = 0;
    if (!qqmusic::api::GetFavAlbums(offset, std::min(size, capacity), items, found_total))
        return MAKERESULT(420, 40);
    std::copy(items.begin(), items.end(), out);
    *count = items.size(); *total = found_total; return 0;
}
Result qqmusicOnlineGetFavPlaylists(u32 offset, u32 size, QqMusicOnlineCollection* out, u32 capacity, u32* count, u32* total) {
    std::vector<QqMusicOnlineCollection> items; int found_total = 0;
    if (!qqmusic::api::GetFavPlaylists(offset, std::min(size, capacity), items, found_total))
        return MAKERESULT(420, 40);
    std::copy(items.begin(), items.end(), out);
    *count = items.size(); *total = found_total; return 0;
}
Result qqmusicOnlineSearchCollections(const char*,u32,u32,u32,QqMusicOnlineCollection*,u32,u32*,u32*) { return 1; }
Result qqmusicOnlineGetAlbumSongs(const char*,u32,u32,QqMusicOnlineSong*,u32,u32*,u32*) { return 1; }
Result qqmusicOnlineGetSingerSongs(const char*,u32,u32,QqMusicOnlineSong*,u32,u32*,u32*) { return 1; }
Result qqmusicOnlineGetPlaylistSongs(u64,u32,u32,QqMusicOnlineSong*,u32,u32*,u32*) { return 1; }

// The Web endpoints hand out plain objects: "data" -> list of entries.
static std::string album_page(const std::string& entries) {
    return "{\"code\":0,\"data\":{\"total\":7,\"albumlist\":[" + entries + "]}}";
}
static std::string fav_page(const std::string& entries) {
    return "{\"code\":0,\"data\":{\"total\":5,\"cdlist\":[" + entries + "]}}";
}
static std::string created_page(const std::string& entries) {
    return "{\"code\":0,\"data\":{\"total\":2,\"disslist\":[" + entries + "]}}";
}
static std::string albums(const std::string& mid, const std::string& name) {
    return "{\"albummid\":\"" + mid + "\",\"albumname\":\"" + name +
           "\",\"singer\":[{\"name\":\"Singer\"}],\"song_cnt\":9}";
}
static std::string playlist(const std::string& id, const std::string& name) {
    return "{\"dissid\":" + id + ",\"dissname\":\"" + name + "\",\"song_cnt\":12}";
}

static tsl::elm::ListItem* row(QqMusicOverlayFrame* frame, const std::string& prefix) {
    auto* list = static_cast<tsl::elm::List*>(frame->content.get());
    for (auto& e : list->rows) {
        auto* item = dynamic_cast<tsl::elm::ListItem*>(e.get());
        if (item && item->text.find(prefix) == 0) return item;
    }
    return nullptr;
}
static void click(OnlineCollectionsGui& gui, QqMusicOverlayFrame* frame, const std::string& prefix) {
    auto* item = row(frame, prefix); assert(item && item->click);
    assert(item->click(HidNpadButton_A)); gui.update();
}

int main() {
    using namespace qqmusic::api;
    SaveCredential("123456", "mock-only-not-a-secret", "", "test");
    std::vector<QqMusicOnlineCollection> items; int total = -1;

    // ---- albums: plain Web endpoint, no encrypted ticket, no POST ----------
    album_body = album_page(albums("newest-album", "Latest Album") + "," + albums("older-album", "Older Album"));
    assert(GetFavAlbums(0, 30, items, total));
    assert(post_calls == 0);
    assert(total == 7 && items.size() == 2);
    assert(std::string(items[0].id) == "newest-album" && std::string(items[0].title) == "Latest Album");
    assert(std::string(items[0].subtitle) == "Singer");
    assert(last_album_sin == 0 && last_album_ein == 29);

    // The page offset is requested explicitly instead of being derived from a cursor.
    assert(GetFavAlbums(30, 30, items, total));
    assert(last_album_sin == 30 && last_album_ein == 59);

    // A single broken entry must not throw away the rest of the page.
    album_body = album_page(albums("keep-album", "Keep Me") + ",{\"albumname\":\"No Mid\"}");
    assert(GetFavAlbums(0, 30, items, total));
    assert(items.size() == 1 && std::string(items[0].id) == "keep-album");

    // A valid but empty list is a success (the page opens with "未找到内容"), not an error.
    album_body = album_page("");
    assert(GetFavAlbums(0, 30, items, total) && items.empty());

    // Only a real transport failure is reported as an error.
    http_status = 0;
    assert(!GetFavAlbums(0, 30, items, total) && items.empty() && total == 0);
    http_status = 200;

    // ---- playlists: created + favourited merged, offsets kept exact --------
    created_body = created_page(playlist("101", "My Created 1") + "," + playlist("102", "My Created 2"));
    fav_body = fav_page(playlist("201", "Fav 1") + "," + playlist("202", "Fav 2"));
    std::vector<QqMusicOnlineCollection> pl; int pl_total = 0;
    assert(GetFavPlaylists(0, 30, pl, pl_total));
    assert(pl.size() == 4 && pl_total == 7);       // 2 created + 5 favourited
    assert(std::string(pl[0].id) == "101" && std::string(pl[1].id) == "102");
    assert(std::string(pl[2].id) == "201" && std::string(pl[3].id) == "202");
    assert(last_fav_sin == 0);

    // Second page: the favourited request skips the two created entries.
    assert(GetFavPlaylists(30, 30, pl, pl_total));
    assert(last_fav_sin == 28 && last_fav_ein == 57);

    // Created playlists alone are still shown (they used to be dropped entirely).
    fav_body = fav_page("");
    pl.clear();
    assert(GetFavPlaylists(0, 30, pl, pl_total));
    assert(pl.size() == 2 && std::string(pl[0].title) == "My Created 1");

    // Transport failure on both endpoints -> real error.
    http_status = 0;
    assert(!GetFavPlaylists(0, 30, pl, pl_total) && pl.empty() && pl_total == 0);
    http_status = 200;

    // ---- GUI: refresh keeps the list, paging keeps its own cursor ----------
    album_body = album_page(albums("seed-album", "Seed Album"));
    assert(GetFavAlbums(0, 30, items, total));
    OnlineCollectionsGui gui("Albums", items, OnlineCollectionsSpec::FavAlbumsFrom(60));
    std::unique_ptr<QqMusicOverlayFrame> frame(static_cast<QqMusicOverlayFrame*>(gui.createUI()));
    assert(row(frame.get(), "Seed Album") && row(frame.get(), "刷新收藏专辑"));

    album_body = album_page(albums("fresh-album", "Fresh Album"));
    click(gui, frame.get(), "刷新收藏专辑");
    assert(last_album_sin == 0 && row(frame.get(), "Fresh Album") && !row(frame.get(), "Seed Album"));

    // A failed refresh keeps the previously loaded rows on screen.
    http_status = 0;
    click(gui, frame.get(), "刷新收藏专辑");
    assert(frame->toast.find("加载失败") == 0 && row(frame.get(), "Fresh Album"));
    http_status = 200;

    // "Load more" continues from the stored cursor (the fresh page held one entry).
    click(gui, frame.get(), "加载更多");
    assert(last_album_sin == 1);

    // ---- GUI: playlist page offers its own refresh entry -------------------
    created_body = created_page(playlist("101", "My Created 1"));
    fav_body = fav_page(playlist("201", "Fav 1"));
    std::vector<QqMusicOnlineCollection> pls;
    assert(GetFavPlaylists(0, 30, pls, pl_total));
    OnlineCollectionsGui pgui("Playlists", pls, OnlineCollectionsSpec::FavPlaylistsFrom(2));
    std::unique_ptr<QqMusicOverlayFrame> pframe(static_cast<QqMusicOverlayFrame*>(pgui.createUI()));
    assert(row(pframe.get(), "刷新歌单列表"));
    click(pgui, pframe.get(), "刷新歌单列表");
    assert(last_fav_sin == 0 && row(pframe.get(), "My Created 1") && row(pframe.get(), "Fav 1"));

    std::cout << "PASS: web-endpoint album/playlist lists, resilient entries, explicit offsets, created+fav merge\n";
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix="qqmusic-albums-") as dirname:
        temp = Path(dirname)
        files = {
            "switch.h": SWITCH, "tesla.hpp": TESLA,
            "elm_overlayframe.hpp": FRAME, "gui_online_songs.hpp": SONGS,
            "pinyin_table.hpp": '#pragma once\nnamespace pinyin { inline char GetInitial(const char* s) { return *s; } }\n',
            "check.cpp": PROGRAM,
        }
        for name, text in files.items():
            (temp / name).write_text(text, encoding="utf-8")
        # Real production sources under test.
        for name in ("gui_online_collections.cpp", "gui_online_collections.hpp"):
            shutil.copyfile(ROOT / "overlay/source/gui" / name, temp / name)
        includes = [temp, ROOT / "sysmodule/source/net", ROOT / "common", ROOT / "common/ipc"]
        command = ["g++", "-std=c++17", "-O0", "-g", "-ffunction-sections", "-fdata-sections"]
        command += ["-I" + str(path) for path in includes]
        command += [str(temp / "check.cpp"), "-Wl,--gc-sections", "-o", str(temp / "check")]
        subprocess.run(command, check=True)
        subprocess.run([str(temp / "check")], check=True)

if __name__ == "__main__":
    main()
