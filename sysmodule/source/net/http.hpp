#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace qqmusic::net {

    struct HttpResponse {
        long status_code = 0;
        std::string body;
        std::string error;
        std::map<std::string, std::string> headers;
        std::map<std::string, std::string> cookies;

        bool ok() const {
            return status_code >= 200 && status_code < 300;
        }

        std::string get_cookie(const std::string &name) const {
            auto it = cookies.find(name);
            return (it != cookies.end()) ? it->second : "";
        }
    };

    bool Init();
    void Exit();
    bool IsInitialized();

    HttpResponse Get(const std::string &url,
                     const std::vector<std::string> &headers = {},
                     const std::string &cookie_header = "",
                     long timeout_sec = 10);

    HttpResponse Post(const std::string &url,
                      const std::string &post_data,
                      const std::vector<std::string> &headers = {},
                      const std::string &cookie_header = "",
                      long timeout_sec = 10);

} // namespace qqmusic::net
