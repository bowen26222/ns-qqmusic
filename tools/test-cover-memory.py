#!/usr/bin/env python3
"""Exercise the production cover loader with host filesystem/IPC stubs and ASan."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'overlay/source/elm/elm_status_bar.cpp').read_text(encoding='utf-8')
helpers = source[source.index('    void halve_rgba('):source.index('} // namespace')]
loader = source[source.index('void StatusBar::RefreshCover()'):source.index('void StatusBar::draw(')]
preamble = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
#define R_SUCCEEDED(rc) ((rc) == 0)
constexpr int COVER_SIZE = 80;
static std::vector<u8> image;
static int fail_after = -1;
static int alloc_calls = 0;
static void *cover_malloc(size_t n) {
    if (fail_after >= 0 && alloc_calls++ == fail_after) return nullptr;
    return std::malloc(n);
}
#define malloc cover_malloc
static bool SdCoverFsReady() { return true; }
static int qqmusicEnsureCover(const char *, char *name, size_t size) {
    std::snprintf(name, size, "test.ppm");
    return 0;
}
static bool ReadWholeFile(const char *, u8 *out, u32 cap, u32 *size) {
    if (image.size() > cap) return false;
    *size = image.size();
    std::memcpy(out, image.data(), image.size());
    return true;
}
class StatusBar {
public:
    struct { char path[64] = {}; } m_track;
    u8 *m_cover_px = nullptr;
    u32 m_cover_w = 0, m_cover_h = 0;
    char m_cover_path[64] = {};
    bool m_cover_done = false;
    int m_cover_retry = 0, m_cover_cooldown = 0;
    ~StatusBar() { free(m_cover_px); }
    void RefreshCover();
};
'''
main = r'''
#undef malloc
static void ppm(int w, int h, bool pixels = true) {
    const auto header = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    image.assign(header.begin(), header.end());
    if (pixels) image.resize(image.size() + size_t(w) * h * 3, 127);
}
int main() {
    // 250x250 fits the compressed-file bound, but leaks 250KB per load before fix.
    for (const auto &size : {std::pair{250,250}, {151,97}, {4,250}, {1,250}, {20,10}}) {
        ppm(size.first, size.second);
        StatusBar bar;
        for (int i = 0; i < 100; ++i) {
            std::snprintf(bar.m_track.path, sizeof(bar.m_track.path), "track-%d", i);
            bar.RefreshCover();
            assert(bar.m_cover_px && bar.m_cover_done);
            assert(bar.m_cover_w > 0 && bar.m_cover_w <= COVER_SIZE);
            assert(bar.m_cover_h > 0 && bar.m_cover_h <= COVER_SIZE);
            for (size_t p = 0; p < size_t(bar.m_cover_w) * bar.m_cover_h; ++p) {
                assert(bar.m_cover_px[p*4] == 127);
                assert(bar.m_cover_px[p*4+3] == 255);
            }
        }
        bar.m_track.path[0] = 0;
        bar.RefreshCover();
        assert(!bar.m_cover_px && !bar.m_cover_done);
    }
    // Every resize allocation failure must release both decoded and intermediate pixels.
    ppm(250,250);
    for (int failure = 0; failure < 2; ++failure) {
        StatusBar bar;
        std::strcpy(bar.m_track.path, "allocation-failure");
        fail_after = failure;
        alloc_calls = 0;
        bar.RefreshCover();
        assert(!bar.m_cover_px && !bar.m_cover_done);
    }
    fail_after = -1;
    for (int kind = 0; kind < 2; ++kind) {
        if (kind == 0) image = {0,1,2,3};
        else ppm(8192,8192,false);
        StatusBar bar;
        std::strcpy(bar.m_track.path, "invalid-cover");
        bar.RefreshCover();
        assert(!bar.m_cover_px && !bar.m_cover_done);
    }
    std::puts("PASS: 500 cover changes, empty track, allocation failures, malformed/oversized images");
}
'''
with tempfile.TemporaryDirectory(prefix='qqmusic-cover-') as tmp:
    cpp = Path(tmp) / 'cover.cpp'
    binary = Path(tmp) / 'cover'
    cpp.write_text(preamble + helpers + loader + main, encoding='utf-8')
    subprocess.run(['g++', '-std=c++20', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I' + str(ROOT / 'third_party/stb'),
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
