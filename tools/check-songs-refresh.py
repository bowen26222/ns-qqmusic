#!/usr/bin/env python3
"""Host regression for the song list refresh path.

Compiles the REAL overlay/source/gui/gui_online_songs.cpp against stubs and drives
its click callbacks: refreshing after a sort must re-request the first page and must
never wipe the visible list when the refetch fails.
Run from WSL: python3 tools/check-songs-refresh.py
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
#define HidNpadButton_X 2
#define HidNpadButton_B 4
struct HidTouchState {}; struct HidAnalogStickState {};
struct FsFile {}; struct FsDir {}; struct FsFileSystem {}; typedef int FsDirEntryType;
#define FsOpenMode_Read 1
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
};
struct CategoryHeader : ListItem {
    CategoryHeader(const std::string& t, bool = false) : ListItem(t) {}
};
struct List : Element {
    std::vector<std::unique_ptr<Element>> rows;
    void clear() { rows.clear(); }
    void addItem(Element* e) { rows.emplace_back(e); }
};
}
struct Gui {
    virtual ~Gui() = default;
    virtual elm::Element* createUI() = 0;
    virtual void update() {}
    virtual bool handleInput(u64,u64,const HidTouchState&,HidAnalogStickState,HidAnalogStickState) { return false; }
    void removeFocus() {}
};
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
};
'''

PROGRAM = r'''
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include "gui_online_songs.cpp"

extern "C" void crashTrace(const char*) {}
namespace config {
int get_sort_mode() { return 0; }
void set_sort_mode(int) {}
}

// ---- mock IPC: records the cursor each fetch asks for ------------------------
static Result mock_rc = 0;
static u32 mock_items = 0;
static u32 last_cursor = 0xDEADBEEF;
static int fetch_calls = 0;
static u32 enqueue_play_now = 0xDEADBEEF, enqueue_count = 0;

static void fill(u32 cursor, QqMusicOnlineSong* out, u32 max, u32* count) {
    last_cursor = cursor;
    ++fetch_calls;
    *count = 0;
    if (R_FAILED(mock_rc))
        return;
    for (u32 i = 0; i < mock_items && i < max; i++) {
        QqMusicOnlineSong s{};
        std::snprintf(s.songmid, sizeof(s.songmid), "mid-%u", cursor + i);
        std::snprintf(s.title, sizeof(s.title), "Song %u", cursor + i);
        out[i] = s;
    }
    *count = mock_items;
}

Result qqmusicOnlineGetTopChart(u32, u32 offset, u32 count, QqMusicOnlineSong* out, u32 max, u32* n) {
    assert(count <= max);
    fill(offset, out, max, n); return mock_rc;
}
Result qqmusicOnlineSearch(const char*, u32 page, u32 count, QqMusicOnlineSong* out, u32 max, u32* n, u32* total) {
    assert(count <= max);
    fill(page, out, max, n); *total = mock_items; return mock_rc;
}
Result qqmusicOnlineGetFavorites(u32 offset, u32 count, QqMusicOnlineSong* out, u32 max, u32* n, u32* total) {
    assert(count <= max);
    fill(offset, out, max, n); *total = mock_items; return mock_rc;
}
Result qqmusicOnlineGetGuessRecommend(u32 p, u32, QqMusicOnlineSong* out, u32 max, u32* n, u32* next) {
    fill(p, out, max, n); *next = p + 1; return mock_rc;
}
Result qqmusicOnlineGetAlbumSongs(const char*, u32 begin, u32 num, QqMusicOnlineSong* out, u32 max, u32* n, u32* total) {
    assert(num <= max);
    fill(begin, out, max, n); *total = mock_items; return mock_rc;
}
Result qqmusicOnlineGetSingerSongs(const char*, u32 begin, u32 num, QqMusicOnlineSong* out, u32 max, u32* n, u32* total) {
    assert(num <= max);
    fill(begin, out, max, n); *total = mock_items; return mock_rc;
}
Result qqmusicOnlineGetPlaylistSongs(u64, u32 begin, u32 num, QqMusicOnlineSong* out, u32 max, u32* n, u32* total) {
    assert(num <= max);
    fill(begin, out, max, n); *total = mock_items; return mock_rc;
}
Result qqmusicOnlineEnqueueBatch(const QqMusicOnlineSong*, u32 count, u32 play_now) {
    enqueue_count = count; enqueue_play_now = play_now; return mock_rc;
}
Result qqmusicOnlinePlaySong(const QqMusicOnlineSong*, u32) { return mock_rc; }

static tsl::elm::ListItem* row(QqMusicOverlayFrame* frame, const std::string& prefix) {
    auto* list = static_cast<tsl::elm::List*>(frame->content.get());
    for (auto& e : list->rows) {
        auto* item = dynamic_cast<tsl::elm::ListItem*>(e.get());
        if (item && item->text.find(prefix) == 0) return item;
    }
    return nullptr;
}
static void click(OnlineSongsGui& gui, QqMusicOverlayFrame* frame, const std::string& prefix) {
    auto* item = row(frame, prefix);
    assert(item && item->click);
    assert(item->click(HidNpadButton_A));
    gui.update();
}
static void load(std::vector<QqMusicOnlineSong> songs) {
    QqMusicOnlineSong s{};
    for (size_t i = 0; i < songs.size(); i++) songs[i] = s;
}

int main() {
    std::vector<QqMusicOnlineSong> songs(2);
    for (size_t i = 0; i < songs.size(); i++) {
        std::snprintf(songs[i].songmid, sizeof(songs[i].songmid), "seed-%zu", i);
        std::snprintf(songs[i].title, sizeof(songs[i].title), "Seed %zu", i);
    }
    OnlineSongsGui gui("Album", songs, OnlineListSpec::AlbumOf("am", "Album", 2));
    std::unique_ptr<QqMusicOverlayFrame> frame(static_cast<QqMusicOverlayFrame*>(gui.createUI()));
    assert(row(frame.get(), "Seed 0") && row(frame.get(), "Seed 1"));

    // A failing "load more" must keep the list and report an error, not "no more songs".
    mock_rc = MAKERESULT(420, 40);
    click(gui, frame.get(), "加载更多");
    assert(last_cursor == 2);                       // continues from the stored cursor
    assert(row(frame.get(), "Seed 0") && row(frame.get(), "Seed 1"));
    assert(frame->toast.find("加载失败") == 0);

    // A successful "load more" appends from the stored cursor.
    mock_rc = 0; mock_items = 1;
    click(gui, frame.get(), "加载更多");
    assert(last_cursor == 2 && row(frame.get(), "Song 2") && row(frame.get(), "Seed 0"));

    // Sorting refetches from the first page — and a failed refetch must NOT wipe
    // what the user is looking at (it used to clear m_songs before the request).
    mock_rc = MAKERESULT(420, 40);
    fetch_calls = 0;
    click(gui, frame.get(), "排序:");
    assert(fetch_calls == 1 && last_cursor == 0);   // first page, not the old cursor
    assert(frame->toast.find("加载失败") == 0);
    assert(row(frame.get(), "Seed 0") && row(frame.get(), "Song 2"));

    // A successful refetch replaces the list with the fresh first page.
    mock_rc = 0; mock_items = 3;
    click(gui, frame.get(), "排序:");
    const bool hasSong0 = row(frame.get(), "Song 0") != nullptr;
    const bool hasSeed0 = row(frame.get(), "Seed 0") != nullptr;
    const bool hasSong2 = row(frame.get(), "Song 2") != nullptr;
    assert(last_cursor == 0);
    assert(hasSong0 && !hasSeed0 && hasSong2);

    // Play All enqueues every loaded song and starts playback.
    mock_rc = 0;
    click(gui, frame.get(), "播放全部");
    assert(enqueue_count == 3 && enqueue_play_now == 1);

    std::cout << "PASS: failed refresh keeps the list, sort refetches page 0, paging keeps its cursor\n";
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix="qqmusic-songs-") as dirname:
        temp = Path(dirname)
        files = {"switch.h": SWITCH, "tesla.hpp": TESLA,
                 "elm_overlayframe.hpp": FRAME, "check.cpp": PROGRAM,
                 "pinyin_table.hpp": '#pragma once\nnamespace pinyin { inline char GetInitial(const char* s) { return *s; } }\n'}
        for name, text in files.items():
            (temp / name).write_text(text, encoding="utf-8")
        # Real production sources under test.
        for name in ("gui_online_songs.cpp", "gui_online_songs.hpp"):
            shutil.copyfile(ROOT / "overlay/source/gui" / name, temp / name)
        includes = [temp, ROOT / "common", ROOT / "common/ipc"]
        command = ["g++", "-std=c++17", "-O0", "-g", "-ffunction-sections", "-fdata-sections"]
        command += ["-I" + str(path) for path in includes]
        command += [str(temp / "check.cpp"), "-Wl,--gc-sections", "-o", str(temp / "check")]
        subprocess.run(command, check=True)
        subprocess.run([str(temp / "check")], check=True)

if __name__ == "__main__":
    main()
