#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cstring>

namespace qqmusic {

struct LyricLine {
    uint32_t time_ms = 0;       // 毫秒时间戳
    std::string text;           // 歌词内容
};

class LyricParser {
private:
    std::vector<LyricLine> m_lines;

    static bool ParseTimestamp(const char *tag, size_t len, uint32_t &out_ms) {
        // 格式校验: mm:ss[.xx[x]]
        // 必须全数字与合法冒号/点，杜绝 sscanf("%f") 的 NaN/Inf 或越界转换
        if (len < 5) return false;
        size_t p = 0;

        // 分钟
        uint32_t min = 0;
        size_t min_digits = 0;
        while (p < len && std::isdigit(static_cast<unsigned char>(tag[p]))) {
            min = min * 10 + (tag[p] - '0');
            min_digits++;
            p++;
        }
        if (min_digits == 0 || min_digits > 4 || p >= len || tag[p] != ':')
            return false;
        p++; // 跳过 ':'

        // 秒
        uint32_t sec = 0;
        size_t sec_digits = 0;
        while (p < len && std::isdigit(static_cast<unsigned char>(tag[p]))) {
            sec = sec * 10 + (tag[p] - '0');
            sec_digits++;
            p++;
        }
        if (sec_digits != 2 || sec >= 60)
            return false;

        // 毫秒（可选小数部分）
        uint32_t frac_ms = 0;
        if (p < len && (tag[p] == '.' || tag[p] == ':')) {
            p++; // 跳过 '.'
            uint32_t frac = 0;
            size_t frac_digits = 0;
            while (p < len && std::isdigit(static_cast<unsigned char>(tag[p]))) {
                frac = frac * 10 + (tag[p] - '0');
                frac_digits++;
                p++;
            }
            if (frac_digits == 1) frac_ms = frac * 100;
            else if (frac_digits == 2) frac_ms = frac * 10;
            else if (frac_digits == 3) frac_ms = frac;
            else if (frac_digits > 3) {
                // 超过 3 位取前 3 位
                while (frac_digits > 3) { frac /= 10; frac_digits--; }
                frac_ms = frac;
            }
        }

        if (p != len)
            return false;

        out_ms = min * 60000 + sec * 1000 + frac_ms;
        return true;
    }

public:
    LyricParser() = default;

    void Clear() {
        m_lines.clear();
        m_lines.shrink_to_fit();
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

    // 解析标准 LRC 文本：
    // - 零 sstream / iostream 依赖（极简直接字符指针扫描，避免拖入庞大 locale 符号）
    // - 支持 UTF-8 BOM 过滤
    // - 支持 [offset: +/-ms] 整体时间轴平移
    // - 严格整型时间戳校验（彻底杜绝 sscanf("%f") 引发的 NaN/Inf 与 UBSan 溢出）
    // - 防御性内存与行数硬上限（防止构造恶意多时间戳输入触发数千万字节展开）
    bool Parse(const std::string &lrc_content) {
        Clear();
        if (lrc_content.empty())
            return false;

        const char *ptr = lrc_content.data();
        size_t remaining = lrc_content.size();

        // 过滤 UTF-8 BOM (0xEF, 0xBB, 0xBF)
        if (remaining >= 3 &&
            static_cast<unsigned char>(ptr[0]) == 0xEF &&
            static_cast<unsigned char>(ptr[1]) == 0xBB &&
            static_cast<unsigned char>(ptr[2]) == 0xBF) {
            ptr += 3;
            remaining -= 3;
        }

        int32_t global_offset_ms = 0;
        constexpr size_t kMaxLines = 1500;
        constexpr size_t kMaxTotalTextBytes = 48 * 1024;
        size_t current_text_bytes = 0;

        while (remaining > 0) {
            // 扫描一行
            const char *line_start = ptr;
            size_t line_len = 0;
            while (line_len < remaining && line_start[line_len] != '\n' && line_start[line_len] != '\r') {
                line_len++;
            }

            // 指针前进跳过换行符
            ptr += line_len;
            remaining -= line_len;
            while (remaining > 0 && (*ptr == '\n' || *ptr == '\r')) {
                ptr++;
                remaining--;
            }

            // 去除行末空格
            while (line_len > 0 && (line_start[line_len - 1] == ' ' || line_start[line_len - 1] == '\t')) {
                line_len--;
            }
            if (line_len == 0) continue;

            // 检查元数据标签 [offset:xxx]
            if (line_len >= 8 && std::strncmp(line_start, "[offset:", 8) == 0) {
                const char *val_start = line_start + 8;
                size_t val_len = 0;
                while (8 + val_len < line_len && val_start[val_len] != ']') {
                    val_len++;
                }
                if (8 + val_len < line_len && val_start[val_len] == ']') {
                    int sign = 1;
                    size_t vp = 0;
                    if (vp < val_len && val_start[vp] == '+') vp++;
                    else if (vp < val_len && val_start[vp] == '-') { sign = -1; vp++; }
                    int32_t off = 0;
                    bool has_digit = false;
                    while (vp < val_len && std::isdigit(static_cast<unsigned char>(val_start[vp]))) {
                        off = off * 10 + (val_start[vp] - '0');
                        has_digit = true;
                        vp++;
                    }
                    if (has_digit) {
                        global_offset_ms = sign * off;
                    }
                }
                continue;
            }

            // 提取本行所有时间戳 [mm:ss.xx]
            uint32_t timestamps[32];
            size_t timestamp_count = 0;
            size_t pos = 0;

            while (pos < line_len && line_start[pos] == '[') {
                size_t close_bracket = pos + 1;
                while (close_bracket < line_len && line_start[close_bracket] != ']') {
                    close_bracket++;
                }
                if (close_bracket >= line_len) break;

                const char *tag = line_start + pos + 1;
                const size_t tag_len = close_bracket - pos - 1;

                uint32_t ts_ms = 0;
                if (ParseTimestamp(tag, tag_len, ts_ms)) {
                    if (timestamp_count < sizeof(timestamps) / sizeof(timestamps[0])) {
                        timestamps[timestamp_count++] = ts_ms;
                    }
                }
                pos = close_bracket + 1;
            }

            if (timestamp_count > 0) {
                // 歌词正文
                const char *lyric_ptr = line_start + pos;
                size_t lyric_len = line_len - pos;

                // 去除首尾空白
                while (lyric_len > 0 && (*lyric_ptr == ' ' || *lyric_ptr == '\t')) {
                    lyric_ptr++;
                    lyric_len--;
                }
                while (lyric_len > 0 && (lyric_ptr[lyric_len - 1] == ' ' || lyric_ptr[lyric_len - 1] == '\t')) {
                    lyric_len--;
                }

                // 内存硬防御：限制单行歌词最大长度为 256 字符
                if (lyric_len > 256) lyric_len = 256;

                for (size_t i = 0; i < timestamp_count; ++i) {
                    if (m_lines.size() >= kMaxLines || current_text_bytes >= kMaxTotalTextBytes)
                        break;

                    int64_t adjusted = static_cast<int64_t>(timestamps[i]) + global_offset_ms;
                    uint32_t final_ms = (adjusted < 0) ? 0 : static_cast<uint32_t>(adjusted);

                    m_lines.push_back({final_ms, std::string(lyric_ptr, lyric_len)});
                    current_text_bytes += lyric_len;
                }
            }

            if (m_lines.size() >= kMaxLines || current_text_bytes >= kMaxTotalTextBytes)
                break;
        }

        if (m_lines.empty())
            return false;

        // 按时间戳升序稳定排序
        std::stable_sort(m_lines.begin(), m_lines.end(), [](const LyricLine &a, const LyricLine &b) {
            return a.time_ms < b.time_ms;
        });

        return true;
    }

    // 根据当前播放时间获取当前唱行下标（二分查找 O(log N)）
    // 关键修正：在第一句开始前的过门/前奏阶段，明确返回 -1（表示暂无歌词正在唱）
    int GetCurrentIndex(uint32_t current_ms) const {
        if (m_lines.empty() || current_ms < m_lines[0].time_ms)
            return -1;

        auto it = std::upper_bound(m_lines.begin(), m_lines.end(), current_ms,
            [](uint32_t ms, const LyricLine &l) {
                return ms < l.time_ms;
            });

        int idx = static_cast<int>(std::distance(m_lines.begin(), it)) - 1;
        return std::clamp(idx, 0, static_cast<int>(m_lines.size() - 1));
    }
};

} // namespace qqmusic
