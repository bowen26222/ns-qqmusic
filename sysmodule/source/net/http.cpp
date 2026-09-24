#include "http.hpp"

#include <sys/select.h>
#include <sys/socket.h>
#include <switch.h>
#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <cstring>

extern "C" void sysLog(const char *s);

namespace qqmusic::net {

namespace {

    // 首次初始化状态：0=未初始化 1=初始化中 2=就绪 3=失败。
    // 必须是原子量——本进程有两个线程会走网络：IPC 线程（API 取数）与播放线程
    // （在线音频分段读）。二者在“首次用网”时可能同时进入 Init()：
    // 普通的 bool 会让两边都执行 curl_global_init + socketInitializeDefault；
    // 而 libnx socket 层与 devkitPro newlib 的 fd 句柄表都是进程级状态，
    // 并发重复初始化会留下坏句柄，最终在 libnx poll() → _socketGetFd() 里野指针中止。
    std::atomic<u32> g_init_state{0};

    // API/封面响应体上限。超过即中止传输：宁可这次取数失败，也不能让静态堆被吃穿
    // （-fno-exceptions 下 std::string 分配失败会直接 abort，表现为整个界面卡死）。
    constexpr size_t kMaxBodyBytes = 768 * 1024;

    size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
        size_t total = size * nmemb;
        auto *str = static_cast<std::string *>(userp);
        if (str->size() + total > kMaxBodyBytes)
            return 0; // 返回短计数 → curl 以 CURLE_WRITE_ERROR 干净中止
        str->append(static_cast<const char *>(contents), total);
        return total;
    }

    size_t HeaderCallback(char *buffer, size_t size, size_t nitems, void *userdata) {
        size_t total = size * nitems;
        auto *resp = static_cast<HttpResponse *>(userdata);
        std::string header(buffer, total);

        // Strip trailing \r\n
        while (!header.empty() && (header.back() == '\r' || header.back() == '\n')) {
            header.pop_back();
        }

        auto colon = header.find(':');
        if (colon != std::string::npos) {
            std::string key = header.substr(0, colon);
            std::string val = header.substr(colon + 1);
            // Trim leading whitespace on value
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
                val.erase(0, 1);
            }
            resp->headers[key] = val;

            // Check Set-Cookie
            std::string key_lower = key;
            std::transform(key_lower.begin(), key_lower.end(), key_lower.begin(), ::tolower);
            if (key_lower == "set-cookie") {
                auto eq = val.find('=');
                if (eq != std::string::npos) {
                    std::string cname = val.substr(0, eq);
                    std::string rest = val.substr(eq + 1);
                    auto semi = rest.find(';');
                    std::string cval = (semi != std::string::npos) ? rest.substr(0, semi) : rest;
                    resp->cookies[cname] = cval;
                }
            } else if (key_lower == "content-length") {
                // 按声明长度一次性预留，避免 std::string 逐次翻倍。
                // 400KB 的响应体在翻倍过程中会短暂同时持有 256KB+512KB，足以吃穿静态堆。
                const long long len = std::strtoll(val.c_str(), nullptr, 10);
                if (len > 0 && (size_t)len <= kMaxBodyBytes && resp->body.capacity() < (size_t)len)
                    resp->body.reserve((size_t)len);
            }
        }

        return total;
    }

    HttpResponse PerformRequest(CURL *curl,
                                const std::vector<std::string> &headers,
                                const std::string &cookie_header,
                                long timeout_sec) {
        HttpResponse resp;
        if (!curl) {
            resp.error = "CURL not initialized";
            return resp;
        }

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp);

        // Security / compatibility settings for Switch environment
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        // 强制 HTTP/1.1：避免 h2 协商带来的额外栈开销（sysmodule 线程栈有限）。
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
        // 掌机弱网环境连接建立放宽至 10s，避免因偶发重传引发误报超时
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 30L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 15L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

        if (!cookie_header.empty()) {
            curl_easy_setopt(curl, CURLOPT_COOKIE, cookie_header.c_str());
        }

        struct curl_slist *chunk = nullptr;
        for (const auto &h : headers) {
            chunk = curl_slist_append(chunk, h.c_str());
        }
        if (chunk) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        }

        CURLcode res = CURLE_OK;
        // 弱网防护：偶发掉包超时自动重试 1 次，无缝吸收瞬时网络抖动
        for (int attempt = 0; attempt < 2; ++attempt) {
            resp.body.clear();
            resp.headers.clear();
            res = curl_easy_perform(curl);
            if (res == CURLE_OK) break;
            if (res != CURLE_COULDNT_CONNECT && res != CURLE_OPERATION_TIMEDOUT &&
                res != CURLE_RECV_ERROR && res != CURLE_SEND_ERROR &&
                res != CURLE_SSL_CONNECT_ERROR && res != CURLE_GOT_NOTHING) break;
            svcSleepThread(300'000'000ull);
        }
        char *eff_url = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff_url);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
            char b[256];
            std::snprintf(b, sizeof(b), "NET OK: code=%ld body_len=%zu url=%.128s\n", resp.status_code, resp.body.size(), eff_url ? eff_url : "");
            sysLog(b);
        } else {
            resp.error = curl_easy_strerror(res);
            char b[256];
            std::snprintf(b, sizeof(b), "NET FAIL: res=%d err=%s url=%.128s\n", (int)res, resp.error.c_str(), eff_url ? eff_url : "");
            sysLog(b);
        }
        if (chunk) {
            curl_slist_free_all(chunk);
        }

        return resp;
    }

} // namespace

bool Init() {
    // 快路径：已就绪。
    u32 state = g_init_state.load(std::memory_order_acquire);
    if (state == 2)
        return true;
    if (state == 3)
        return false;

    // 抢到 0 → 1 的线程负责初始化；其余线程等待它结束（最多 100ms）。
    u32 expected = 0;
    if (g_init_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
        // socket is already initialized by __appInit(); fallback if not
        socketInitializeDefault();

        const CURLcode c_rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (c_rc != CURLE_OK) {
            g_init_state.store(3, std::memory_order_release);
            return false;
        }
        g_init_state.store(2, std::memory_order_release);
        return true;
    }

    for (int i = 0; i < 100; ++i) {
        svcSleepThread(1'000'000); // 1ms
        state = g_init_state.load(std::memory_order_acquire);
        if (state != 1)
            return state == 2;
    }
    return false;
}

void Exit() {
    if (g_init_state.exchange(0, std::memory_order_acq_rel) != 2)
        return;

    curl_global_cleanup();
}

bool IsInitialized() {
    return g_init_state.load(std::memory_order_acquire) == 2;
}

HttpResponse Get(const std::string &url,
                 const std::vector<std::string> &headers,
                 const std::string &cookie_header,
                 long timeout_sec) {
    if (!Init()) {
        HttpResponse resp;
        resp.error = "Network socket initialization failed";
        return resp;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        HttpResponse resp;
        resp.error = "curl_easy_init failed";
        return resp;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    HttpResponse resp = PerformRequest(curl, headers, cookie_header, timeout_sec);
    curl_easy_cleanup(curl);
    return resp;
}

HttpResponse Post(const std::string &url,
                  const std::string &post_data,
                  const std::vector<std::string> &headers,
                  const std::string &cookie_header,
                  long timeout_sec) {
    if (!Init()) {
        HttpResponse resp;
        resp.error = "Network socket initialization failed";
        return resp;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        HttpResponse resp;
        resp.error = "curl_easy_init failed";
        return resp;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_data.size()));

    HttpResponse resp = PerformRequest(curl, headers, cookie_header, timeout_sec);
    curl_easy_cleanup(curl);
    return resp;
}

} // namespace qqmusic::net
