#!/usr/bin/env python3
"""Host regression for the online API layer: request escaping, parser robustness and
transport-vs-empty semantics.

Compiles the REAL sysmodule/source/net/qqmusic_api.cpp against a mock transport, so
each assertion exercises production code paths (no network, no credentials).
Run from WSL: python3 tools/check-api-robustness.py
"""
from pathlib import Path
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
struct FsFile {}; struct FsDir {}; struct FsFileSystem {}; typedef int FsDirEntryType;
#define FsOpenMode_Read 1
Result fsFileGetSize(FsFile*, s64*);
Result fsFileRead(FsFile*, u64, void*, u64, int, u64*);
void fsFileClose(FsFile*);
u64 randomGet64();
'''

PROGRAM = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "cJSON.h"
#include "qqmusic_api.cpp"

extern "C" void sysLog(const char*) {}
extern "C" int ini_puts(const char*, const char*, const char*, const char*) { return 1; }
Result fsFileGetSize(FsFile*, s64* size) { *size = 0; return 0; }
Result fsFileRead(FsFile*, u64, void*, u64, int, u64* got) { *got = 0; return 0; }
void fsFileClose(FsFile*) {}
u64 randomGet64() { return 0x1234; }

// ---- mock transport ---------------------------------------------------------
static long post_status = 200;
static std::string post_body;
static long get_status = 200;
static std::string get_body;
static std::vector<std::string> posts;   // captured request bodies

namespace qqmusic::net {
HttpResponse Get(const std::string&, const std::vector<std::string>&, const std::string&, long) {
    HttpResponse r; r.status_code = get_status; r.body = get_body; return r;
}
HttpResponse Post(const std::string&, const std::string& request,
                  const std::vector<std::string>&, const std::string&, long) {
    posts.push_back(request);
    HttpResponse r; r.status_code = post_status; r.body = post_body; return r;
}
} // namespace qqmusic::net

namespace sdmc {
Result OpenFile(FsFile*, const char*, int) { return 1; }
Result CreateFolder(const char*) { return 0; }
Result DeleteFile(const char*) { return 0; }
Result RenameFile(const char*, const char*) { return 0; }
Result ListDir(const char*, std::vector<DirEntry>*) { return 1; }
bool FileExists(const char*) { return false; }
Result WriteFile(const char*, const void*, size_t) { return 0; }
} // namespace sdmc

// ---- tiny cJSON helpers (the production module no longer needs cJSON) -------
static cJSON* parse(const std::string& body) {
    cJSON* doc = cJSON_Parse(body.c_str());
    assert(doc && "request body must be valid JSON");
    return doc;
}
static std::string strAt(const cJSON* node, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(node, key);
    assert(v && cJSON_IsString(v) && v->valuestring);
    return v->valuestring;
}
static std::string param(const std::string& key) {         // req.param.<key> of the last POST
    assert(!posts.empty());
    cJSON* doc = parse(posts.back());
    cJSON* req = cJSON_GetObjectItemCaseSensitive(doc, "req");
    cJSON* p = cJSON_GetObjectItemCaseSensitive(req, "param");
    const std::string out = strAt(p, key.c_str());
    cJSON_Delete(doc);
    return out;
}

int main() {
    using namespace qqmusic::api;
    SaveCredential("123456", "mock-only-not-a-secret", "", "test");

    const auto song = [](const std::string& mid, const std::string& title, const std::string& interval) {
        return std::string("{\"mid\":\"") + mid + "\",\"media_mid\":\"" + mid + "\",\"title\":" + title +
               ",\"singer\":[{\"name\":\"S\"}],\"album\":{\"name\":\"Al\",\"mid\":\"am\"},\"interval\":" + interval + "}";
    };
    const auto list = [&](const std::string& items) {
        return "{\"code\":0,\"req\":{\"code\":0,\"data\":{\"songlist\":[" + items + "]}}}";
    };

    // ---- 1. user-supplied text must be escaped into the JSON request body ----
    // A query with a quote/backslash is legal input from the on-screen keyboard;
    // unescaped it produced a malformed body that the server rejected outright.
    const std::string tricky = "他说\"你好\"\\ world\nline";
    post_body = list(song("m1", "\"T\"", "180"));
    std::vector<QqMusicOnlineSong> songs; int total = 0;
    assert(SearchSongs(tricky, 1, 30, songs, total));
    assert(param("query") == tricky);
    std::vector<QqMusicOnlineCollection> items; int ctotal = 0;
    assert(SearchCollections(tricky, 2, 1, 30, items, ctotal));
    assert(param("query") == tricky);
    assert(GetAlbumSongs("alb\"um", 0, 30, songs, total));
    assert(param("albumMid") == "alb\"um");
    assert(GetSingerSongs("sing\\er", 0, 30, songs, total));
    assert(param("singerMid") == "sing\\er");
    assert(GetSongAlbumMid("mid\"x") == "am");
    assert(param("song_mid") == "mid\"x");
    post_body = R"({"code":0,"req":{"code":0,"data":{"midurlinfo":[{"purl":"x.mp3"}]}}})";
    assert(GetSongPlayUrl("mid\"x", "med\\y") == "http://dl.stream.qqmusic.qq.com/x.mp3");
    {
        cJSON* doc = parse(posts.back());
        cJSON* req = cJSON_GetObjectItemCaseSensitive(doc, "req");
        cJSON* p = cJSON_GetObjectItemCaseSensitive(req, "param");
        const cJSON* mids = cJSON_GetObjectItemCaseSensitive(p, "songmid");
        const cJSON* files = cJSON_GetObjectItemCaseSensitive(p, "filename");
        assert(cJSON_IsArray(mids) && cJSON_IsArray(files));
        assert(std::string(cJSON_GetArrayItem(mids, 0)->valuestring) == "mid\"x");
        assert(std::string(cJSON_GetArrayItem(files, 0)->valuestring) == "M500med\\y.mp3");
        cJSON_Delete(doc);
    }

    // ---- 2. untrusted numbers must not abort the sysmodule -------------------
    // std::stoul throws on overflow and this module builds with -fno-exceptions,
    // where a throw is an abort (the whole player dies on a malformed response).
    post_body = list(song("m2", "\"T\"", "99999999999999999999999"));
    assert(SearchSongs("q", 1, 30, songs, total));
    assert(songs.size() == 1 && songs[0].duration_sec == 0xFFFFFFFFu);

    // ---- 3. a truncated body must not leak document text into a field --------
    post_body = std::string("{\"code\":0,\"req\":{\"code\":0,\"data\":{\"track_info\":{\"album\":{\"mid\":\"am1");
    assert(GetSongAlbumMid("m3").empty());

    // ---- 4. transport failure must be distinguishable from an empty page -----
    post_status = 0; post_body.clear();
    assert(!SearchSongs("q", 1, 30, songs, total));
    assert(!GetTopChart(26, 0, 30, songs));
    assert(!GetMyFavorites(0, 30, songs, total));
    assert(!GetAlbumSongs("am", 0, 30, songs, total));
    assert(!GetSingerSongs("sm", 0, 30, songs, total));
    assert(!GetPlaylistSongs(1234, 0, 30, songs, total));
    assert(!SearchCollections("q", 2, 1, 30, items, ctotal));
    post_status = 200; post_body = list("");        // answered, but this page is empty
    assert(SearchSongs("q", 1, 30, songs, total) && songs.empty() && total == 0);
    assert(GetTopChart(26, 0, 30, songs) && songs.empty());
    assert(GetMyFavorites(0, 30, songs, total) && songs.empty());
    assert(GetAlbumSongs("am", 0, 30, songs, total) && songs.empty());
    assert(GetSingerSongs("sm", 0, 30, songs, total) && songs.empty());
    assert(GetPlaylistSongs(1234, 0, 30, songs, total) && songs.empty());
    assert(SearchCollections("q", 2, 1, 30, items, ctotal) && items.empty());

    // ---- 5. favourites playlists: empty success vs. no answer at all ---------
    get_status = 200;
    get_body = R"({"code":0,"data":{"total":0,"cdlist":[],"disslist":[]}})";
    assert(GetFavPlaylists(0, 30, items, ctotal) && items.empty() && ctotal == 0);
    get_status = 0; get_body.clear();
    assert(!GetFavPlaylists(0, 30, items, ctotal) && items.empty() && ctotal == 0);
    get_status = 200;
    get_body = R"({"code":0,"data":{"total":0,"albumlist":[]}})";
    assert(GetFavAlbums(0, 30, items, ctotal) && items.empty() && ctotal == 0);
    get_status = 0;
    assert(!GetFavAlbums(0, 30, items, ctotal));

    std::cout << "PASS: request escaping, overflow clamp, truncated-body rejection, transport-vs-empty semantics\n";
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix="qqmusic-api-") as dirname:
        temp = Path(dirname)
        (temp / "switch.h").write_text(SWITCH, encoding="utf-8")
        (temp / "check.cpp").write_text(PROGRAM, encoding="utf-8")
        subprocess.run(["gcc", "-c", str(ROOT / "common/cJSON/cJSON.c"), "-o", str(temp / "cJSON.o")], check=True)
        includes = [temp, ROOT / "sysmodule/source/net", ROOT / "common", ROOT / "common/ipc", ROOT / "common/cJSON"]
        command = ["g++", "-std=c++17", "-O0", "-g", "-ffunction-sections", "-fdata-sections"]
        command += ["-I" + str(path) for path in includes]
        command += [str(temp / "check.cpp"), str(temp / "cJSON.o"), "-Wl,--gc-sections", "-o", str(temp / "check")]
        subprocess.run(command, check=True)
        subprocess.run([str(temp / "check")], check=True)

if __name__ == "__main__":
    main()
