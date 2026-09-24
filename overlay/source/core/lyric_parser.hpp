#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace qqmusic {

struct LyricLine {
    uint32_t time_ms = 0;       // 毫秒时间戳
    std::string text;           // 歌词内容
};

class LyricParser {
private:
    std::vector<LyricLine> m_lines;

public:
    LyricParser() = default;

    void Clear() {
        m_lines.clear();
    }

    bool IsEmpty() const {
        return m_lines.empty();
    }

    size_t LineCount() const {
        return m_lines.size();
    }

    const LyricLine& GetLine(size_t index) const {
        return m_lines[index];
    }

    const std::vector<LyricLine>& GetLines() const {
        return m_lines;
    }

    // 解析标准 LRC 文本（支持 [mm:ss.xx] 与 [mm:ss.xxx] 以及多时间戳单行）
    bool Parse(const std::string &lrc_content) {
        m_lines.clear();
        if (lrc_content.empty())
            return false;

        std::istringstream ss(lrc_content);
        std::string line;
        while (std::getline(ss, line)) {
            // 去除末尾 \r, \n, 空格
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
                line.pop_back();
            if (line.empty()) continue;

            // 提取所有时间戳 [mm:ss.xx]
            std::vector<uint32_t> timestamps;
            size_t pos = 0;
            while (pos < line.size() && line[pos] == '[') {
                size_t close_bracket = line.find(']', pos);
                if (close_bracket == std::string::npos) break;

                std::string tag = line.substr(pos + 1, close_bracket - pos - 1);
                // 检查是否为时间标签 (含冒号)
                size_t colon = tag.find(':');
                if (colon != std::string::npos && colon > 0 && colon + 1 < tag.size()) {
                    uint32_t min = 0;
                    float sec = 0.0f;
                    if (std::sscanf(tag.c_str(), "%u:%f", &min, &sec) == 2) {
                        uint32_t ms = min * 60000 + static_cast<uint32_t>(sec * 1000.0f + 0.5f);
                        timestamps.push_back(ms);
                    }
                }
                pos = close_bracket + 1;
            }

            if (!timestamps.empty()) {
                std::string lyric_text = line.substr(pos);
                // 去除歌词首尾空白
                while (!lyric_text.empty() && (lyric_text.front() == ' ' || lyric_text.front() == '\t'))
                    lyric_text.erase(0, 1);
                while (!lyric_text.empty() && (lyric_text.back() == ' ' || lyric_text.back() == '\t'))
                    lyric_text.pop_back();

                // 为每个时间戳添加一行
                for (uint32_t ts : timestamps) {
                    m_lines.push_back({ts, lyric_text});
                }
            }
        }

        if (m_lines.empty())
            return false;

        // 按时间升序排序
        std::stable_sort(m_lines.begin(), m_lines.end(), [](const LyricLine &a, const LyricLine &b) {
            return a.time_ms < b.time_ms;
        });

        return true;
    }

    // 根据当前毫秒获取当前正在唱的歌词下标（二分查找 O(log N)）
    int GetCurrentIndex(uint32_t current_ms) const {
        if (m_lines.empty()) return -1;
        if (current_ms < m_lines[0].time_ms) return 0;

        auto it = std::upper_bound(m_lines.begin(), m_lines.end(), current_ms,
            [](uint32_t ms, const LyricLine &l) {
                return ms < l.time_ms;
            });

        int idx = static_cast<int>(std::distance(m_lines.begin(), it)) - 1;
        return std::clamp(idx, 0, static_cast<int>(m_lines.size() - 1));
    }
};

} // namespace qqmusic
