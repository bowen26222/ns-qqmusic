#include "qqmusic_api.hpp"
#include "http.hpp"
#include "minIni/minIni.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <initializer_list>
#include <climits>
#include <memory>
#include <sstream>
#include "sdmc/sdmc.hpp"
#include <ctime>

extern "C" void sysLog(const char *s);

namespace qqmusic::api {

namespace {

    constexpr const char *CREDENTIAL_PATH = "/config/qqmusic/credential.ini";
    UserSession g_user{};
    std::string g_album_euin;


    uint32_t Hash33(const std::string &str) {
        uint32_t e = 0;
        for (char c : str) {
            e += (e << 5) + static_cast<uint8_t>(c);
        }
        return e & 0x7FFFFFFF;
    }
    uint32_t GetGtk(const std::string &skey) {
        uint32_t hash = 5381;
        for (char c : skey) {
            hash += (hash << 5) + static_cast<uint8_t>(c);
        }
        return hash & 0x7FFFFFFF;
    }

    std::string WebCommonParams() {
        uint32_t gtk = GetGtk(g_user.musickey);
        return "&g_tk=" + std::to_string(gtk) +
               "&loginUin=" + g_user.uin +
               "&format=json&inCharset=utf8&outCharset=utf-8&notice=0&platform=yqq.json&needNewCode=0";
    }


    void SafeCopy(char *dst, size_t dst_size, const std::string &src) {
        if (!dst || dst_size == 0) return;
        size_t len = std::min(src.size(), dst_size - 1);
        std::memcpy(dst, src.data(), len);
        dst[len] = 0;
    }

    // 请求体是按字符串拼接的 JSON，插值必须转义：搜索框由用户任意输入
    // （引号/反斜杠/中文都合法），未转义会让整个请求体变成非法 JSON，
    // 服务端直接拒绝——搜索「她说"你好"」必然失败。返回值带首尾引号，
    // 直接紧跟冒号输出即可。
    std::string JsonEscape(const std::string &src) {
        std::string out;
        out.reserve(src.size() + 2);
        out += '"';
        for (const char raw : src) {
            const unsigned char c = (unsigned char)raw;
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                default:
                    if (c < 0x20) {
                        char esc[8];
                        std::snprintf(esc, sizeof(esc), "\\u%04x", (unsigned)c);
                        out += esc;
                    } else {
                        out += (char)c;
                    }
                    break;
            }
        }
        out += '"';
        return out;
    }

    // Decode a raw JSON string body: handles \" \\ \/ \b \f \n \r \t and \uXXXX
    // (including surrogate pairs). The CDN URLs returned by CgiGetVkey carry
    // \u0026 in place of '&', so an undecoded value yields a broken audio URL.
    std::string UnescapeJson(const std::string &in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); i++) {
            if (in[i] != '\\' || i + 1 >= in.size()) {
                out += in[i];
                continue;
            }
            const char esc = in[++i];
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    if (i + 4 >= in.size()) break;
                    u32 cp = 0;
                    bool ok = true;
                    for (int k = 1; k <= 4; k++) {
                        const char h = in[i + k];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')      cp |= (u32)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (u32)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (u32)(h - 'A' + 10);
                        else { ok = false; break; }
                    }
                    if (!ok) { out += "\\u"; break; }
                    i += 4;
                    // Surrogate pair: combine into a single code point.
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < in.size() &&
                        in[i + 1] == '\\' && in[i + 2] == 'u') {
                        u32 lo = 0;
                        bool lo_ok = true;
                        for (int k = 3; k <= 6; k++) {
                            const char h = in[i + k];
                            lo <<= 4;
                            if (h >= '0' && h <= '9')      lo |= (u32)(h - '0');
                            else if (h >= 'a' && h <= 'f') lo |= (u32)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') lo |= (u32)(h - 'A' + 10);
                            else { lo_ok = false; break; }
                        }
                        if (lo_ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                            i += 6;
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                    }
                    if (cp < 0x80) {
                        out += (char)cp;
                    } else if (cp < 0x800) {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else if (cp < 0x10000) {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    } else {
                        out += (char)(0xF0 | (cp >> 18));
                        out += (char)(0x80 | ((cp >> 12) & 0x3F));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += esc; break;
            }
        }
        return out;
    }

    std::string ExtractField(const std::string &src, const std::string &key) {
        size_t p = src.find(key);
        if (p == std::string::npos) return "";
        p += key.size();
        while (p < src.size() && (src[p] == ' ' || src[p] == '\t' || src[p] == ':' || src[p] == '"')) {
            if (src[p] == '"') {
                p++;
                break;
            }
            p++;
        }
        // Walk to the closing quote, honouring backslash escapes.
        size_t end = p;
        while (end < src.size()) {
            if (src[end] == '\\') { end += 2; continue; }
            if (src[end] == '"') break;
            end++;
        }
        // No closing quote (truncated body, or the escape skipped past the end):
        // the "value" would be the rest of the document, not a field. Report empty.
        if (end >= src.size()) return "";
        return UnescapeJson(src.substr(p, end - p));
    }

    void ParseSongsFromRawJson(const std::string &text, std::vector<QqMusicOnlineSong> &out_songs, size_t max_songs) {
        out_songs.clear();
        size_t arr_start = std::string::npos;

        // Check "song":{"list":[ first for search
        size_t song_p = text.find("\"song\"");
        if (song_p != std::string::npos) {
            size_t list_p = text.find("\"list\"", song_p);
            if (list_p != std::string::npos) {
                size_t bracket = text.find('[', list_p);
                if (bracket != std::string::npos) {
                    arr_start = bracket + 1;
                }
            }
        }

        if (arr_start == std::string::npos) {
            // "songList"（大写 L）：GetAlbumSongList / GetSingerSongList 的条目在 data.songList[].songInfo。
            const char *keys[] = { "\"songInfoList\"", "\"songList\"", "\"songlist\"", "\"VecSongs\"", "\"tracks\"", "\"list\"" };
            for (const char *k : keys) {
                size_t p = text.find(k);
                if (p != std::string::npos) {
                    size_t bracket = text.find('[', p);
                    if (bracket != std::string::npos) {
                        arr_start = bracket + 1;
                        break;
                    }
                }
            }
        }

        if (arr_start == std::string::npos) return;

        size_t pos = arr_start;
        int depth = 0;
        bool in_string = false;
        size_t obj_start = std::string::npos;

        while (pos < text.size() && out_songs.size() < max_songs) {
            const char ch = text[pos];
            // Braces and brackets inside string literals are data, not structure:
            // QQ Music embeds a nested JSON document in its "pingpong" field.
            if (in_string) {
                if (ch == '\\') { pos += 2; continue; }
                if (ch == '"') in_string = false;
                pos++;
                continue;
            }
            if (ch == '"') {
                in_string = true;
            } else if (ch == '{') {
                if (depth == 0) obj_start = pos;
                depth++;
            } else if (ch == '}') {
                depth--;
                if (depth == 0 && obj_start != std::string::npos) {
                    std::string obj_str = text.substr(obj_start, pos - obj_start + 1);
                    std::string mid = ExtractField(obj_str, "\"mid\"");
                    std::string title = ExtractField(obj_str, "\"title\"");
                    if (title.empty()) title = ExtractField(obj_str, "\"name\"");
                    std::string media_mid = ExtractField(obj_str, "\"media_mid\"");
                    if (media_mid.empty()) media_mid = mid;

                    std::string artist;
                    size_t singer_p = obj_str.find("\"singer\"");
                    if (singer_p != std::string::npos) {
                        artist = ExtractField(obj_str.substr(singer_p, 300), "\"name\"");
                        if (artist.empty()) artist = ExtractField(obj_str.substr(singer_p, 300), "\"title\"");
                    }

                    std::string album;
                    std::string album_mid;
                    size_t album_p = obj_str.find("\"album\"");
                    if (album_p != std::string::npos) {
                        album = ExtractField(obj_str.substr(album_p, 300), "\"name\"");
                        if (album.empty()) album = ExtractField(obj_str.substr(album_p, 300), "\"title\"");
                        album_mid = ExtractField(obj_str.substr(album_p, 300), "\"mid\"");
                    }
                    if (album_mid.empty()) {
                        album_mid = ExtractField(obj_str, "\"albumMid\"");
                        if (album_mid.empty()) album_mid = ExtractField(obj_str, "\"albummid\"");
                    }

                    uint32_t interval = 0;
                    size_t int_p = obj_str.find("\"interval\"");
                    if (int_p != std::string::npos) {
                        size_t colon = obj_str.find(':', int_p);
                        if (colon != std::string::npos) {
                            size_t num_start = colon + 1;
                            while (num_start < obj_str.size() && (obj_str[num_start] == ' ' || obj_str[num_start] == '\t')) num_start++;
                            size_t num_end = num_start;
                            while (num_end < obj_str.size() && std::isdigit(obj_str[num_end])) num_end++;
                            if (num_end > num_start) {
                                // strtoul 而不是 std::stoul：后者在数字超长时抛异常，
                                // 而本模块以 -fno-exceptions 编译（抛异常 = abort = sysmodule 崩溃），
                                // 服务端返回的文本不可信，必须按值截断。
                                const unsigned long long parsed =
                                    std::strtoull(obj_str.c_str() + num_start, nullptr, 10);
                                interval = (u32)std::min<unsigned long long>(parsed, UINT32_MAX);
                            }
                        }
                    }

                    // 发行日期 time_public "YYYY-MM-DD" → YYYYMMDD（供「最新」排序）
                    u32 publish_date = 0;
                    {
                        std::string tpub = ExtractField(obj_str, "\"time_public\"");
                        if (tpub.size() >= 10 && tpub[4] == '-' && tpub[7] == '-' &&
                            std::isdigit((unsigned char)tpub[0]) && std::isdigit((unsigned char)tpub[1]) &&
                            std::isdigit((unsigned char)tpub[2]) && std::isdigit((unsigned char)tpub[3])) {
                            const u32 y = (u32)std::strtoul(tpub.substr(0, 4).c_str(), nullptr, 10);
                            const u32 m = (u32)std::strtoul(tpub.substr(5, 2).c_str(), nullptr, 10);
                            const u32 d = (u32)std::strtoul(tpub.substr(8, 2).c_str(), nullptr, 10);
                            if (y >= 1900 && m >= 1 && m <= 12 && d >= 1 && d <= 31)
                                publish_date = y * 10000u + m * 100u + d;
                        }
                    }

                    if (!mid.empty() && !title.empty()) {
                        QqMusicOnlineSong item{};
                        SafeCopy(item.songmid, sizeof(item.songmid), mid);
                        SafeCopy(item.media_mid, sizeof(item.media_mid), media_mid);
                        SafeCopy(item.title, sizeof(item.title), title);
                        SafeCopy(item.artist, sizeof(item.artist), artist);
                        SafeCopy(item.album, sizeof(item.album), album);
                        SafeCopy(item.album_mid, sizeof(item.album_mid), album_mid);
                        item.duration_sec = interval;
                        item.publish_date = publish_date;
                        out_songs.push_back(item);
                    }
                    obj_start = std::string::npos;
                }
            } else if (ch == ']' && depth == 0) {
                break;
            }
            pos++;
        }
    }

    // 取键对应的原始值：字符串值走 ExtractField（含转义解码），裸数字按字面量返回，
    // 对象/数组返回空串——让调用方继续尝试下一个候选键。
    std::string RawField(const std::string &src, const char *key) {
        const size_t p = src.find(key);
        if (p == std::string::npos) return "";
        size_t q = p + std::strlen(key);
        while (q < src.size() && (src[q] == ' ' || src[q] == '\t')) q++;
        if (q >= src.size() || src[q] != ':') return "";
        q++;
        while (q < src.size() && (src[q] == ' ' || src[q] == '\t')) q++;
        if (q >= src.size()) return "";
        if (src[q] == '"') return ExtractField(src, key);
        if (src[q] == '{' || src[q] == '[') return "";
        size_t end = q;
        while (end < src.size() && src[end] != ',' && src[end] != '}' && src[end] != ']' &&
               src[end] != ' ' && src[end] != '\n' && src[end] != '\r') {
            end++;
        }
        const std::string v = src.substr(q, end - q);
        return (v == "null") ? "" : v;
    }

    // 候选键按序取第一个非空值：QQ 各接口同名字段拼写不一（mid / albumMid / albumMID …），
    // 缺字段时留空而不是丢掉整条。
    std::string FirstField(const std::string &src, std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            const std::string v = RawField(src, k);
            if (!v.empty()) return v;
        }
        return "";
    }

    u32 FirstNumber(const std::string &src, std::initializer_list<const char *> keys) {
        for (const char *k : keys) {
            const std::string v = RawField(src, k);
            if (!v.empty()) return (u32)std::strtoul(v.c_str(), nullptr, 10);
        }
        return 0;
    }

    // 嵌套字段（"singers":[{"name":…}] / "creator":{"nick":…}）先在 scope 键之后的窗口内取值。
    std::string ScopedField(const std::string &src, std::initializer_list<const char *> scope_keys,
                            std::initializer_list<const char *> value_keys) {
        for (const char *s : scope_keys) {
            const size_t p = src.find(s);
            if (p == std::string::npos) continue;
            const std::string window = src.substr(p, std::min<size_t>(src.size() - p, 256));
            const std::string v = FirstField(window, value_keys);
            if (!v.empty()) return v;
        }
        return "";
    }

    // 容器键之后的第一个 '['：容器本身也是对象（{\"list\":[…]}）时，它就是条目数组起点。
    size_t FindArrayStart(const std::string &text, std::initializer_list<const char *> container_keys) {
        for (const char *k : container_keys) {
            const size_t p = text.find(k);
            if (p == std::string::npos) continue;
            const size_t bracket = text.find('[', p + std::strlen(k));
            if (bracket != std::string::npos) return bracket + 1;
        }
        return std::string::npos;
    }

    // 解析集合条目（歌手 / 专辑 / 歌单）：与 ParseSongsFromRawJson 同一套路（纯字符串扫描，
    // 不引入 JSON 库），字段名各接口不统一故按候选键取第一个非空值。
    // kind: 1=歌手 2=专辑 3=歌单；container_keys: 容器键候选（首个能定位到数组的生效）。
    void ParseCollectionsFromRawJson(const std::string &text, int kind,
                                     std::initializer_list<const char *> container_keys,
                                     std::vector<QqMusicOnlineCollection> &out_items, size_t max_items) {
        out_items.clear();
        if (max_items == 0) return;

        const size_t arr_start = FindArrayStart(text, container_keys);
        if (arr_start == std::string::npos) return;

        size_t pos = arr_start;
        int depth = 0;
        bool in_string = false;
        size_t obj_start = std::string::npos;

        while (pos < text.size() && out_items.size() < max_items) {
            const char ch = text[pos];
            // 字符串里的括号是数据：QQ 的条目里嵌着 pingpong 之类的内层 JSON 文本。
            if (in_string) {
                if (ch == '\\') { pos += 2; continue; }
                if (ch == '"') in_string = false;
                pos++;
                continue;
            }
            if (ch == '"') {
                in_string = true;
            } else if (ch == '{') {
                if (depth == 0) obj_start = pos;
                depth++;
            } else if (ch == '}') {
                depth--;
                if (depth == 0 && obj_start != std::string::npos) {
                    const std::string obj = text.substr(obj_start, pos - obj_start + 1);
                    std::string id, title, sub;
                    u32 num = 0;

                    if (kind == 1) {
                        id = FirstField(obj, { "\"singerMID\"", "\"singerMid\"", "\"mid\"" });
                        title = FirstField(obj, { "\"singerName\"", "\"singer_name\"", "\"name\"", "\"title\"" });
                        sub = FirstField(obj, { "\"publicTime\"", "\"pubtime\"" });
                        num = FirstNumber(obj, { "\"songNum\"", "\"songnum\"", "\"song_count\"", "\"total\"" });
                    } else if (kind == 2) {
                        // 1. 专辑标题：优先取专属字段 albumname/albumName/album_name
                        title = FirstField(obj, { "\"albumname\"", "\"albumName\"", "\"album_name\"" });
                        if (title.empty()) title = ScopedField(obj, { "\"album\"" }, { "\"name\"", "\"title\"", "\"albumName\"" });
                        if (title.empty()) {
                            // 必须确保不是 singer 内部的 name
                            size_t s_pos = obj.find("\"singer\"");
                            size_t n_pos = obj.find("\"name\"");
                            if (s_pos == std::string::npos || n_pos < s_pos) {
                                title = FirstField(obj, { "\"name\"", "\"title\"" });
                            }
                        }

                        // 2. 专辑 MID：优先取专属字段 albummid/albumMid/albumMID，绝不误取 singer.mid
                        id = FirstField(obj, { "\"albummid\"", "\"albumMid\"", "\"albumMID\"", "\"album_mid\"" });
                        if (id.empty()) id = ScopedField(obj, { "\"album\"" }, { "\"mid\"", "\"albumMid\"" });
                        if (id.empty()) {
                            size_t s_pos = obj.find("\"singer\"");
                            size_t m_pos = obj.find("\"mid\"");
                            if (s_pos == std::string::npos || m_pos < s_pos) {
                                id = FirstField(obj, { "\"mid\"" });
                            }
                        }

                        // 3. 歌手名存入 sub
                        sub = FirstField(obj, { "\"singerName\"", "\"singer_name\"", "\"singername\"" });
                        if (sub.empty()) sub = ScopedField(obj, { "\"singers\"", "\"v_singer\"", "\"singer\"" }, { "\"name\"", "\"title\"" });
                        if (sub.empty()) sub = FirstField(obj, { "\"publicTime\"", "\"pubtime\"", "\"publish_date\"" });
                        num = FirstNumber(obj, { "\"song_count\"", "\"song_cnt\"", "\"songnum\"", "\"songNum\"", "\"cur_song_num\"", "\"totalNum\"", "\"total\"" });
                    } else {
                        // 注意顺序：dirid 是"目录 id"（如 201=我喜欢），不是 CgiGetDiss 需要的
                        // disstid/pid。收藏歌单条目同时带 id 与 dirid，故显式 id 必须排在 dirid 之前。
                        id = FirstField(obj, { "\"dissid\"", "\"id\"", "\"tid\"", "\"dirId\"", "\"dirid\"" });
                        title = FirstField(obj, { "\"dissname\"", "\"diss_name\"", "\"dirName\"", "\"name\"", "\"title\"" });
                        sub = FirstField(obj, { "\"nickname\"" });
                        if (sub.empty()) sub = ScopedField(obj, { "\"creator\"" }, { "\"nick\"", "\"name\"" });
                        if (sub.empty()) sub = FirstField(obj, { "\"nick\"" });
                        num = FirstNumber(obj, { "\"song_count\"", "\"song_cnt\"", "\"songnum\"", "\"songNum\"", "\"listennum\"" });
                    }

                    if (!id.empty() && !title.empty() && id != "0") {
                        QqMusicOnlineCollection item{};
                        item.kind = (u32)kind;
                        SafeCopy(item.id, sizeof(item.id), id);
                        SafeCopy(item.title, sizeof(item.title), title);
                        SafeCopy(item.subtitle, sizeof(item.subtitle), sub);
                        if (num) {
                            std::snprintf(item.extra, sizeof(item.extra), "%u 首", (unsigned)num);
                        }
                        out_items.push_back(item);
                    }
                    obj_start = std::string::npos;
                }
            } else if (ch == ']' && depth == 0) {
                break;
            }
            pos++;
        }
    }

    // 取出音乐 API 顶层 "code" 值：HTTP 200 也可能带回服务端错误（如 500001 限流），
    // 仅凭“解析出 0 首歌”无法区分“没有更多”与“服务端拒绝”。
    int ServerCode(const std::string &body) {
        const size_t p = body.find("\"code\"");
        if (p == std::string::npos) return -1;
        size_t q = body.find(':', p);
        if (q == std::string::npos) return -1;
        while (++q < body.size() && (body[q] == ' ' || body[q] == '\t')) {}
        size_t end = q;
        while (end < body.size() && std::isdigit((unsigned char)body[end])) end++;
        if (end == q) return -1;
        return std::atoi(body.substr(q, end - q).c_str());
    }

    // 空结果时把服务端原因写进 SD 日志，避免只看到“没有更多”。
    void LogEmptyResult(const char *what, const std::string &body) {
        char b[160];
        std::snprintf(b, sizeof(b), "SYS %s returned no songs (server code=%d, bytes=%u)\n",
                      what, ServerCode(body), (unsigned)body.size());
        sysLog(b);
    }

    std::string BuildCookieHeader() {
        if (!g_user.logged_in) return "";
        std::string h = "uin=" + g_user.uin + "; p_uin=" + g_user.uin + ";";
        if (!g_user.musickey.empty()) {
            h += "skey=" + g_user.musickey + "; p_skey=" + g_user.musickey + ";";
        }
        if (!g_user.musicid.empty()) {
            h += "pt4_token=" + g_user.musicid + ";";
        }
        return h;
    }

} // namespace

void Init() {
    LoadCredential();
}

UserSession GetUserSession() {
    return g_user;
}

bool LoadCredential() {
    g_album_euin.clear();
    char uin_buf[64]{};
    char nick_buf[128]{};
    char key_buf[256]{};
    char id_buf[64]{};

    ini_gets("account", "uin", "", uin_buf, sizeof(uin_buf), CREDENTIAL_PATH);
    ini_gets("account", "nickname", "", nick_buf, sizeof(nick_buf), CREDENTIAL_PATH);
    ini_gets("account", "musickey", "", key_buf, sizeof(key_buf), CREDENTIAL_PATH);
    ini_gets("account", "musicid", "", id_buf, sizeof(id_buf), CREDENTIAL_PATH);

    if (uin_buf[0] != 0) {
        g_user.logged_in = true;
        g_user.uin = uin_buf;
        g_user.nickname = nick_buf[0] ? nick_buf : uin_buf;
        g_user.musickey = key_buf;
        g_user.musicid = id_buf;
        return true;
    }
    g_user = {};
    return false;
}

bool SaveCredential(const std::string &uin, const std::string &musickey, const std::string &musicid, const std::string &nickname) {
    g_album_euin.clear();
    g_user.logged_in = true;
    g_user.uin = uin;
    g_user.musickey = musickey;
    g_user.musicid = musicid;
    g_user.nickname = nickname.empty() ? uin : nickname;

    ini_puts("account", "uin", uin.c_str(), CREDENTIAL_PATH);
    ini_puts("account", "nickname", g_user.nickname.c_str(), CREDENTIAL_PATH);
    ini_puts("account", "musickey", musickey.c_str(), CREDENTIAL_PATH);
    ini_puts("account", "musicid", musicid.c_str(), CREDENTIAL_PATH);
    return true;
}

void Logout() {
    g_album_euin.clear();
    g_user = {};
    ini_puts("account", "uin", "", CREDENTIAL_PATH);
    ini_puts("account", "nickname", "", CREDENTIAL_PATH);
    ini_puts("account", "musickey", "", CREDENTIAL_PATH);
    ini_puts("account", "musicid", "", CREDENTIAL_PATH);
}

bool RequestQrCode(QrCodeSession &out_session) {
    const std::string url = "https://ssl.ptlogin2.qq.com/ptqrshow?appid=716027609&e=2&l=M&s=3&d=72&v=4&t=0.5482&daid=383&pt_3rd_aid=100497308";
    auto resp = net::Get(url, {"Referer: https://y.qq.com/"}, "", 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    out_session.qrsig = resp.get_cookie("qrsig");
    if (out_session.qrsig.empty()) {
        return false;
    }

    out_session.png_bytes.assign(resp.body.begin(), resp.body.end());
    out_session.status_code = 66; // waiting for scan
    out_session.status_msg = "Waiting for scan";
    return true;
}

bool PollQrCode(QrCodeSession &session) {
    if (session.qrsig.empty()) {
        session.status_code = -1;
        session.status_msg = "No active QR session";
        return false;
    }

    uint32_t token = Hash33(session.qrsig);
    std::string url = "https://ssl.ptlogin2.qq.com/ptqrlogin?u1=https%3A%2F%2Fgraph.qq.com%2Foauth2.0%2Flogin_jump&ptqrtoken=" +
                      std::to_string(token) +
                      "&ptredirect=0&h=1&t=1&g=1&from_ui=1&ptlang=2052&action=0-0-1600000000000&js_ver=21010614&js_type=1&login_sig=&pt_uistyle=40&aid=716027609&daid=383&pt_3rd_aid=100497308";

    std::string cookie_header = "qrsig=" + session.qrsig + ";";
    auto resp = net::Get(url, {"Referer: https://xui.ptlogin2.qq.com/"}, cookie_header, 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    // Extract arguments from ptuiCB('0','0','url','0','msg','nickname')
    std::vector<std::string> args;
    size_t pos = 0;
    while (pos < resp.body.size()) {
        auto q1 = resp.body.find('\'', pos);
        if (q1 == std::string::npos) break;
        auto q2 = resp.body.find('\'', q1 + 1);
        if (q2 == std::string::npos) break;
        args.push_back(resp.body.substr(q1 + 1, q2 - q1 - 1));
        pos = q2 + 1;
    }

    if (args.empty()) {
        return false;
    }

    session.status_code = std::atoi(args[0].c_str());

    if (session.status_code == 66) {
        session.status_msg = "Waiting for scan";
    } else if (session.status_code == 67) {
        session.status_msg = "Scanned, confirm on phone";
    } else if (session.status_code == 65) {
        session.status_msg = "QR Code expired";
    } else if (session.status_code == 0) {
        session.status_msg = "Login success";
        std::string nickname = args.size() >= 6 ? args[5] : "";

        std::string uin;
        if (args.size() >= 3) {
            const std::string &jump_url = args[2];
            auto uin_pos = jump_url.find("uin=");
            if (uin_pos != std::string::npos) {
                size_t start = uin_pos + 4;
                size_t end = start;
                while (end < jump_url.size() && std::isdigit(jump_url[end])) end++;
                if (end > start) {
                    uin = jump_url.substr(start, end - start);
                }
            }
        }
        if (uin.empty()) {
            uin = resp.get_cookie("p_uin");
            if (uin.empty()) uin = resp.get_cookie("uin");
            if (!uin.empty() && uin[0] == 'o') uin.erase(0, 1);
        }

        std::string skey = resp.get_cookie("p_skey");
        if (skey.empty()) skey = resp.get_cookie("skey");
        std::string pt4_token = resp.get_cookie("pt4_token");

        SaveCredential(uin, skey, pt4_token, nickname);
        char log_b[128];
        std::snprintf(log_b, sizeof(log_b), "SYS Login success: uin=%s nick=%s\n", uin.c_str(), nickname.c_str());
        sysLog(log_b);
        return true;
    }

    return true;
}

bool SearchSongs(const std::string &query, int page, int page_size, std::vector<QqMusicOnlineSong> &out_songs, int &out_total) {
    out_songs.clear();
    out_total = 0;

    int safe_page_size = std::clamp(page_size, 1, 50);
    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":19,\"cv\":1873,\"uin\":\"" << (g_user.logged_in ? g_user.uin : "0")
            << "\"},\"req\":{\"method\":\"DoSearchForQQMusicDesktop\",\"module\":\"music.search.SearchCgiService\",\"param\":{\"query\":"
            << JsonEscape(query) << ",\"num_per_page\":" << safe_page_size << ",\"page_num\":" << page << ",\"search_type\":0}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 10);
    // 「服务端答了但本页为空」与「根本没拿到响应」是两件事：前者是合法成功
    // （界面应显示空结果/没有更多），后者才是错误（网络或服务端拒绝）。
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    ParseSongsFromRawJson(resp.body, out_songs, safe_page_size);
    if (out_songs.empty()) LogEmptyResult("SearchSongs", resp.body);
    out_total = static_cast<int>(out_songs.size());
    return true;
}

// 集合搜索：kind 同时决定 search_type 与响应数组（1 -> singer.list, 2 -> album.list, 3 -> songlist.list）。
bool SearchCollections(const std::string &query, int kind, int page, int page_size,
                       std::vector<QqMusicOnlineCollection> &out_items, int &out_total) {
    out_items.clear();
    out_total = 0;

    kind = std::clamp(kind, 1, 3);
    int safe_page_size = std::clamp(page_size, 1, 50);
    if (page < 1) page = 1;

    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":19,\"cv\":1873,\"uin\":\"" << (g_user.logged_in ? g_user.uin : "0")
            << "\"},\"req\":{\"method\":\"DoSearchForQQMusicDesktop\",\"module\":\"music.search.SearchCgiService\",\"param\":{\"query\":"
            << JsonEscape(query) << ",\"num_per_page\":" << safe_page_size << ",\"page_num\":" << page
            << ",\"search_type\":" << kind << "}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    switch (kind) {
        case 1:
            ParseCollectionsFromRawJson(resp.body, kind, { "\"singer\"", "\"singerList\"" }, out_items, safe_page_size);
            break;
        case 2:
            ParseCollectionsFromRawJson(resp.body, kind, { "\"album\"", "\"albumList\"" }, out_items, safe_page_size);
            break;
        default:
            ParseCollectionsFromRawJson(resp.body, kind, { "\"songlist\"", "\"songList\"" }, out_items, safe_page_size);
            break;
    }
    if (out_items.empty()) LogEmptyResult("SearchCollections", resp.body);
    // 与 SearchSongs 一致：搜索接口没有可信总数，用本页条数充当 total。
    out_total = static_cast<int>(out_items.size());
    return true;
}

bool GetTopChart(int top_id, int offset, int count, std::vector<QqMusicOnlineSong> &out_songs) {
    out_songs.clear();

    int safe_count = std::clamp(count, 1, 100);
    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":24,\"cv\":0},\"req\":{\"module\":\"musicToplist.ToplistInfoServer\",\"method\":\"GetDetail\",\"param\":{\"topId\":"
            << top_id << ",\"offset\":" << offset << ",\"num\":" << safe_count << "}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    ParseSongsFromRawJson(resp.body, out_songs, safe_count);
    if (out_songs.empty()) LogEmptyResult("GetTopChart", resp.body);
    return true;
}

// 我喜欢的歌：服务端原生顺序就是最新收藏在前（song_begin=0 为最新收藏），直接按偏移分页即可。
bool GetMyFavorites(int offset, int count, std::vector<QqMusicOnlineSong> &out_songs, int &out_total) {
    out_songs.clear();
    out_total = 0;
    if (!g_user.logged_in) return false;

    const int safe_count = std::clamp(count, 1, 200);
    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":24,\"cv\":0,\"uin\":\"" << g_user.uin
            << "\"},\"req\":{\"module\":\"music.srfDissInfo.DissInfo\",\"method\":\"CgiGetDiss\",\"param\":{\"disstid\":0,\"dirid\":201,\"song_begin\":"
            << std::max(0, offset) << ",\"song_num\":" << safe_count << "}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    out_total = (int)FirstNumber(resp.body, { "\"total_song_num\"", "\"totalNum\"" });
    ParseSongsFromRawJson(resp.body, out_songs, safe_count);
    if (out_songs.empty()) LogEmptyResult("GetMyFavorites", resp.body);
    return true;
}
// 收藏的专辑：只走官方 Web 资产接口（仅需登录 Cookie，无加密票据/严格校验依赖，最稳）。
bool GetFavAlbums(int offset, int count, std::vector<QqMusicOnlineCollection> &out_items, int &out_total) {
    out_items.clear();
    out_total = 0;
    if (!g_user.logged_in) return false;

    const int safe_count = std::clamp(count, 1, 50);
    const int begin = std::max(0, offset);
    const std::string url = "https://c.y.qq.com/fav/fcgi-bin/fcg_get_profile_order_asset.fcg?ct=20&cid=205360956&userid=" +
                            g_user.uin + "&reqtype=2&sin=" + std::to_string(begin) +
                            "&ein=" + std::to_string(begin + safe_count - 1) +
                            "&_=" + std::to_string(std::time(nullptr)) + WebCommonParams();
    auto resp = net::Get(url, {"Referer: https://y.qq.com/portal/profile.html"}, BuildCookieHeader(), 8);
    if (!resp.ok() || resp.body.empty()) {
        LogEmptyResult("GetFavAlbums", resp.body);
        return false;
    }
    out_total = (int)FirstNumber(resp.body, { "\"total\"" });
    ParseCollectionsFromRawJson(resp.body, 2, { "\"albumlist\"", "\"v_list\"", "\"list\"" }, out_items, safe_count);
    if (out_total <= 0) out_total = (int)out_items.size();
    if (out_items.empty()) LogEmptyResult("GetFavAlbums", resp.body);
    return true;
}
// 收藏的歌单：合并「我创建的歌单」与「我收藏的歌单」，两者都只用 Web 接口。
// 自建歌单只出现在首屏，所以收藏接口的偏移要单独按「自建数量」折算，
// 分页不会与自建条目混算，不会丢条目。
bool GetFavPlaylists(int offset, int count, std::vector<QqMusicOnlineCollection> &out_items, int &out_total) {
    out_items.clear();
    out_total = 0;
    if (!g_user.logged_in) return false;

    const int safe_count = std::clamp(count, 1, 50);
    const int begin = std::max(0, offset);
    bool answered = false;

    // 1) 我创建的歌单（条目少，一次取全）
    std::vector<QqMusicOnlineCollection> created;
    {
        const std::string url = "https://c.y.qq.com/rsc/fcgi-bin/fcg_user_created_diss?hostUin=0&hostuin=" +
                                g_user.uin + "&sin=0&size=100" + WebCommonParams();
        auto resp = net::Get(url, {"Referer: https://y.qq.com/portal/profile.html"}, BuildCookieHeader(), 8);
        if (resp.ok() && !resp.body.empty()) {
            answered = true;
            ParseCollectionsFromRawJson(resp.body, 3, { "\"disslist\"", "\"v_playlist\"", "\"list\"" }, created, 100);
        }
    }
    const int created_count = (int)created.size();

    // 2) 我收藏的歌单：按（本页偏移 - 自建数量）请求对应的收藏窗口
    std::vector<QqMusicOnlineCollection> favs;
    int fav_total = 0;
    const int fav_begin = std::max(0, begin - created_count);
    {
        const std::string url = "https://c.y.qq.com/fav/fcgi-bin/fcg_get_profile_order_asset.fcg?ct=20&cid=205360956&userid=" +
                                g_user.uin + "&reqtype=3&sin=" + std::to_string(fav_begin) +
                                "&ein=" + std::to_string(fav_begin + safe_count - 1) +
                                "&_=" + std::to_string(std::time(nullptr)) + WebCommonParams();
        auto resp = net::Get(url, {"Referer: https://y.qq.com/portal/profile.html"}, BuildCookieHeader(), 8);
        if (resp.ok() && !resp.body.empty()) {
            answered = true;
            fav_total = (int)FirstNumber(resp.body, { "\"total\"" });
            ParseCollectionsFromRawJson(resp.body, 3, { "\"cdlist\"", "\"disslist\"" }, favs, safe_count);
        }
    }
    out_total = created_count + (fav_total > 0 ? fav_total : (int)favs.size());

    // 3) 本页切片：自建歌单在前，收藏歌单在后
    for (int i = begin; i < created_count && (int)out_items.size() < safe_count; ++i)
        out_items.push_back(created[(size_t)i]);
    const size_t fav_skip = (size_t)std::max(0, begin - created_count);
    for (size_t i = fav_skip; i < favs.size() && (int)out_items.size() < safe_count; ++i)
        out_items.push_back(favs[i]);

    if (out_items.empty()) LogEmptyResult("GetFavPlaylists", favs.empty() ? "empty" : "page end");
    return answered;
}

// 专辑曲目：条目是 data.songList[].songInfo，总数在 data.totalNum。
bool GetAlbumSongs(const std::string &albummid, int begin, int num, std::vector<QqMusicOnlineSong> &out_songs, int &out_total) {
    out_songs.clear();
    out_total = 0;

    int safe_num = std::clamp(num, 1, 300);
    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":24,\"cv\":0},\"req\":{\"module\":\"music.musichallAlbum.AlbumSongList\",\"method\":\"GetAlbumSongList\",\"param\":{\"albumMid\":"
            << JsonEscape(albummid) << ",\"begin\":" << begin << ",\"num\":" << safe_num << "}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    ParseSongsFromRawJson(resp.body, out_songs, safe_num);
    if (out_songs.empty()) LogEmptyResult("GetAlbumSongs", resp.body);
    out_total = (int)FirstNumber(resp.body, { "\"totalNum\"", "\"total\"" });
    return true;
}

// 歌手曲目：musichall.song_list_server/GetSingerSongList，同为 data.songList[].songInfo + data.totalNum。
bool GetSingerSongs(const std::string &singermid, int begin, int num, std::vector<QqMusicOnlineSong> &out_songs, int &out_total) {
    out_songs.clear();
    out_total = 0;

    int safe_num = std::clamp(num, 1, 100);
    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":24,\"cv\":0},\"req\":{\"module\":\"musichall.song_list_server\",\"method\":\"GetSingerSongList\",\"param\":{\"singerMid\":"
            << JsonEscape(singermid) << ",\"begin\":" << begin << ",\"num\":" << safe_num << "}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    ParseSongsFromRawJson(resp.body, out_songs, safe_num);
    if (out_songs.empty()) LogEmptyResult("GetSingerSongs", resp.body);
    out_total = (int)FirstNumber(resp.body, { "\"totalNum\"", "\"total\"" });
    return true;
}

// 歌单曲目：DissInfo/CgiGetDiss，条目在 data.songlist[]，总数在 data.total_song_num。
bool GetPlaylistSongs(u64 disstid, int begin, int num, std::vector<QqMusicOnlineSong> &out_songs, int &out_total) {
    out_songs.clear();
    out_total = 0;

    const int safe_num = std::clamp(num, 1, 200);
    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":24,\"cv\":0},\"req\":{\"module\":\"music.srfDissInfo.DissInfo\",\"method\":\"CgiGetDiss\",\"param\":{\"disstid\":"
            << disstid << ",\"dirid\":0,\"song_begin\":" << std::max(0, begin)
            << ",\"song_num\":" << safe_num << "}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    out_total = (int)FirstNumber(resp.body, { "\"total_song_num\"", "\"totalNum\"" });
    ParseSongsFromRawJson(resp.body, out_songs, safe_num);
    if (out_songs.empty()) LogEmptyResult("GetPlaylistSongs", resp.body);
    return true;
}

// 个人雷达推荐：一次 HTTP 请求内并发打包 pages 个雷达页（每页约 10 首）。
bool GetRadarSongs(int start_page, int pages, std::vector<QqMusicOnlineSong> &out_songs) {
    out_songs.clear();
    if (start_page < 1) start_page = 1;
    pages = std::clamp(pages, 1, 6);

    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":19,\"cv\":1873,\"uin\":\"" << (g_user.logged_in ? g_user.uin : "0") << "\"}";
    for (int i = 0; i < pages; i++) {
        json_ss << ",\"req_" << i
                << "\":{\"module\":\"music.recommend.TrackRelationServer\",\"method\":\"GetRadarSong\",\"param\":{\"Page\":"
                << (start_page + i) << ",\"ReqType\":0,\"FavSongs\":[],\"EntranceSongs\":[]}}";
    }
    json_ss << "}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 10);
    if (!resp.ok() || resp.body.empty()) {
        return false;
    }

    // 每个 req_N 独立成段解析：以「下一个 req 键」为界，避免跨请求串读导致重复。
    // 雷达页之间会重叠（未登录会话尤其明显），按 songmid 去重。
    for (int i = 0; i < pages; i++) {
        const std::string key = "\"req_" + std::to_string(i) + "\"";
        const size_t begin = resp.body.find(key);
        if (begin == std::string::npos) {
            continue;
        }
        size_t end = resp.body.size();
        const std::string next_key = "\"req_" + std::to_string(i + 1) + "\"";
        const size_t next_pos = resp.body.find(next_key, begin + key.size());
        if (next_pos != std::string::npos) {
            end = next_pos;
        }
        std::vector<QqMusicOnlineSong> page_songs;
        ParseSongsFromRawJson(resp.body.substr(begin, end - begin), page_songs, 12);
        for (const auto &s : page_songs) {
            const bool dup = std::any_of(out_songs.begin(), out_songs.end(),
                [&](const QqMusicOnlineSong &e) { return std::strcmp(e.songmid, s.songmid) == 0; });
            if (!dup) {
                out_songs.push_back(s);
            }
        }
    }
    if (out_songs.empty()) LogEmptyResult("GetRadarSongs", resp.body);
    return !out_songs.empty();
}

bool GetGuessRecommend(int start_page, int pages, std::vector<QqMusicOnlineSong> &out_songs) {
    // 猜你喜欢＝心动电台，同一雷达流；失败退回飙升榜。
    if (GetRadarSongs(start_page, pages, out_songs)) {
        return true;
    }
    if (start_page <= 1) {
        return GetTopChart(62, 0, 30, out_songs);
    }
    out_songs.clear();
    return false;
}

std::string GetSongAlbumMid(const std::string &songmid) {
    if (songmid.empty())
        return "";

    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":24,\"cv\":0},\"req\":{\"module\":\"music.pf_song_detail_svr\","
               "\"method\":\"get_song_detail\",\"param\":{\"song_mid\":" << JsonEscape(songmid) << "}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 6);
    if (!resp.ok() || resp.body.empty())
        return "";

    // track_info.album.{mid}：先定位 "album"，再取紧随其后的 "mid"。
    const size_t album_p = resp.body.find("\"album\"");
    if (album_p == std::string::npos)
        return "";
    std::string mid = ExtractField(resp.body.substr(album_p, 300), "\"mid\"");
    if (mid.empty())
        mid = ExtractField(resp.body.substr(album_p, 300), "\"albumMid\"");
    return mid;
}

std::string GetSongPlayUrl(const std::string &songmid, const std::string &media_mid) {
    const std::string m_mid = media_mid.empty() ? songmid : media_mid;
    const std::string mp3_filename = "M500" + m_mid + ".mp3";

    static u64 s_guid = 0;
    if (s_guid == 0) {
        s_guid = (randomGet64() % 9000000000ull) + 1000000000ull;
    }
    const std::string guid_str = std::to_string(s_guid);

    uint32_t gtk = GetGtk(g_user.musickey);
    std::ostringstream json_ss;
    json_ss << "{\"comm\":{\"ct\":24,\"cv\":4747474,\"format\":\"json\",\"platform\":\"yqq.json\",\"uin\":\""
            << (g_user.logged_in ? g_user.uin : "0") << "\",\"g_tk\":" << gtk << ",\"g_tk_new_20200303\":" << gtk
            << "},\"req\":{\"module\":\"vkey.GetVkeyServer\",\"method\":\"CgiGetVkey\",\"param\":{\"guid\":\""
            << guid_str << "\",\"songmid\":[" << JsonEscape(songmid) << "],\"filename\":[" << JsonEscape(mp3_filename)
            << "],\"songtype\":[0],\"uin\":\"" << (g_user.logged_in ? g_user.uin : "0")
            << "\",\"loginflag\":1,\"platform\":\"20\"}}}";

    auto resp = net::Post("https://u.y.qq.com/cgi-bin/musicu.fcg", json_ss.str(),
                          {"Content-Type: application/json", "Referer: https://y.qq.com/"}, BuildCookieHeader(), 8);
    if (!resp.ok() || resp.body.empty()) {
        return "";
    }

    std::string purl = ExtractField(resp.body, "\"purl\":\"");
    if (!purl.empty()) {
        // 绝不使用 aqqmusic.tc.qq.com（会报 403 Forbidden Error: -1031），必须直连官方音频主流节点！
        return "http://dl.stream.qqmusic.qq.com/" + purl;
    }
    return "";
}

bool GetSongLyric(const std::string &songmid, std::string &out_lrc) {
    out_lrc.clear();
    if (songmid.empty()) return false;

    // 官方 Web 歌词接口（nobase64=1 直接下发标准明文 UTF-8 LRC 文本，免解密）
    std::string url = "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_new.fcg?songmid=" +
                      songmid + "&format=json&nobase64=1&songtype=0" + WebCommonParams();
    auto resp = net::Get(url, {"Referer: https://y.qq.com/"}, BuildCookieHeader(), 8);
    if (!resp.ok() || resp.body.empty()) {
        std::string fb_url = "https://c.y.qq.com/lyric/fcgi-bin/fcg_query_lyric_yqq.fcg?songmid=" +
                             songmid + "&format=json&nobase64=1" + WebCommonParams();
        resp = net::Get(fb_url, {"Referer: https://y.qq.com/"}, BuildCookieHeader(), 8);
    }
    if (!resp.ok() || resp.body.empty())
        return false;

    std::string lrc = ExtractField(resp.body, "\"lyric\"");
    if (lrc.empty()) return false;

    out_lrc = std::move(lrc);
    return true;
}

} // namespace qqmusic::api
