#include "gui_keyboard.hpp"
#include "pinyin_table.hpp"

#include <cctype>
#include <algorithm>

namespace {

    // 候选栏每页条数（数字键 1~5 与本页一一对应；5条留足横向呼吸间距）
    constexpr int kCandPerPage = 5;

    // Key rows. Special keys: 中/英, ↑, 删除, 取消, 空格, 完成.
    const std::vector<std::vector<std::string>> g_rows = {
        {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
        {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
        {"a", "s", "d", "f", "g", "h", "j", "k", "l", ":"},
        {"z", "x", "c", "v", "b", "n", "m", ".", "/", "-"},
        {"中/英", "↑", "_", "@", "?", "=", "&", "%", "删除"},
        {"取消", "空格", "完成"},
    };

    class KeyboardElement final : public tsl::elm::Element {
        std::string m_text;                        // 已上屏文本
        std::string m_pinyin;                      // 正在输入的拼音
        std::vector<std::string> m_candidates;     // 全部候选（可多页）
        int m_cand_idx = -1;                       // -1 = 焦点在键盘；>=0 = 候选绝对下标

        KeyboardGui::DoneCb m_on_done;
        int m_row = 1, m_col = 0;
        bool m_shift = false;
        bool m_chinese_mode = true;

      public:
        KeyboardElement(const std::string &initial, KeyboardGui::DoneCb on_done)
            : m_text(initial), m_on_done(std::move(on_done)) {}

        tsl::elm::Element *requestFocus(tsl::elm::Element *, tsl::FocusDirection) override { return this; }

        // ---- 候选分页 ----

        int PageCount() const {
            if (m_candidates.empty()) return 0;
            return (int)((m_candidates.size() + kCandPerPage - 1) / kCandPerPage);
        }
        int CurPage() const { return m_cand_idx < 0 ? 0 : m_cand_idx / kCandPerPage; }
        int PageStart(int page) const { return page * kCandPerPage; }
        int PageEnd(int page) const {  // 本页末项的绝对下标（含）
            return std::min((int)m_candidates.size() - 1, (page + 1) * kCandPerPage - 1);
        }

        // 候选栏右移：页尾继续右移 → 翻到下一页首项
        void CandRight() {
            if (m_cand_idx < 0 || m_candidates.empty()) return;
            const int page = CurPage();
            if (m_cand_idx >= PageEnd(page)) {
                const int next_first = PageStart(page + 1);
                if (next_first < (int)m_candidates.size())
                    m_cand_idx = next_first;
                return;
            }
            m_cand_idx++;
        }

        // 候选栏左移：页首继续左移 → 翻到上一页末项
        void CandLeft() {
            if (m_cand_idx < 0 || m_candidates.empty()) return;
            const int page = CurPage();
            if (m_cand_idx <= PageStart(page)) {
                if (page == 0) return;
                m_cand_idx = PageEnd(page - 1);
                return;
            }
            m_cand_idx--;
        }

        void CandPrevPage() {
            if (m_cand_idx < 0 || m_candidates.empty()) return;
            const int page = CurPage();
            if (page > 0) m_cand_idx = PageStart(page - 1);
        }

        void CandNextPage() {
            if (m_cand_idx < 0 || m_candidates.empty()) return;
            const int page = CurPage();
            if (page + 1 < PageCount()) m_cand_idx = PageStart(page + 1);
        }

        void clampCol() {
            if (m_cand_idx >= 0) {
                if (m_candidates.empty()) m_cand_idx = -1;
                else m_cand_idx = std::clamp(m_cand_idx, 0, (int)m_candidates.size() - 1);
                return;
            }
            const int n = (int)g_rows[m_row].size();
            if (m_col >= n) m_col = n - 1;
            if (m_col < 0) m_col = 0;
        }

        void updateCandidates() {
            m_candidates = pinyin::GetCandidates(m_pinyin);
            if (m_candidates.empty()) m_cand_idx = -1;
            else if (m_cand_idx >= (int)m_candidates.size()) m_cand_idx = 0;
        }

        void commitCandidate(size_t index) {
            if (index < m_candidates.size()) {
                m_text += m_candidates[index];
                m_pinyin.clear();
                m_candidates.clear();
                m_cand_idx = -1;
            }
        }

        void popTextChar() {
            if (m_text.empty()) return;
            // UTF-8 字符安全退格（处理 1~4 字节中文）
            while (!m_text.empty()) {
                const unsigned char c = (unsigned char)m_text.back();
                m_text.pop_back();
                if ((c & 0xC0) != 0x80) break;
            }
        }

        void resetPinyin() {
            m_pinyin.clear();
            m_candidates.clear();
            m_cand_idx = -1;
        }

        void activate(const std::string &key) {
            if (key == "中/英") {
                m_chinese_mode = !m_chinese_mode;
                if (!m_chinese_mode && !m_pinyin.empty()) {
                    m_text += m_pinyin;
                    resetPinyin();
                }
            } else if (key == "↑") {
                m_shift = !m_shift;
            } else if (key == "删除" || key == "DEL") {
                if (!m_pinyin.empty()) {
                    m_pinyin.pop_back();
                    updateCandidates();
                } else {
                    popTextChar();
                }
            } else if (key == "空格" || key == "SPACE") {
                if (!m_candidates.empty()) {
                    commitCandidate((size_t)std::max(0, m_cand_idx));
                } else if (!m_pinyin.empty()) {
                    m_text += m_pinyin;
                    resetPinyin();
                } else {
                    m_text += ' ';
                }
            } else if (key == "取消" || key == "CANCEL") {
                tsl::goBack();
            } else if (key == "完成" || key == "DONE") {
                if (!m_pinyin.empty()) {
                    if (!m_candidates.empty()) commitCandidate((size_t)std::max(0, m_cand_idx));
                    else { m_text += m_pinyin; m_pinyin.clear(); }
                }
                if (m_on_done) m_on_done(m_text);
            } else {
                const char c = key[0];
                if (m_chinese_mode && std::isalpha((unsigned char)c) && !m_shift) {
                    m_pinyin += (char)std::tolower((unsigned char)c);
                    updateCandidates();
                } else if (m_chinese_mode && std::isdigit((unsigned char)c) && !m_candidates.empty()) {
                    // 数字 1~8 选当前页的对应候选
                    const int slot = c - '1';
                    if (slot >= 0 && slot < kCandPerPage) {
                        const int abs_idx = PageStart(CurPage()) + slot;
                        if (abs_idx < (int)m_candidates.size()) commitCandidate((size_t)abs_idx);
                        else m_text += c;
                    } else {
                        m_text += c;
                    }
                } else {
                    char out = c;
                    if (m_shift && std::isalpha((unsigned char)c)) out = (char)std::toupper((unsigned char)c);
                    m_text += out;
                    m_shift = false;
                }
            }
        }

        bool onClick(u64 keys) override {
            if (keys & HidNpadButton_AnyUp) {
                if (m_cand_idx < 0 && m_row == 0 && !m_candidates.empty()) {
                    m_cand_idx = 0; // 从首行上移进入候选栏
                } else if (m_cand_idx < 0 && m_row > 0) {
                    m_row--;
                    clampCol();
                }
                return true;
            }
            if (keys & HidNpadButton_AnyDown) {
                if (m_cand_idx >= 0) {
                    m_cand_idx = -1; // 回到键盘
                } else if (m_row < (int)g_rows.size() - 1) {
                    m_row++;
                    clampCol();
                }
                return true;
            }
            if (keys & HidNpadButton_AnyLeft) {
                if (m_cand_idx >= 0) CandLeft();
                else if (m_col > 0) m_col--;
                return true;
            }
            if (keys & HidNpadButton_AnyRight) {
                if (m_cand_idx >= 0) CandRight();
                else if (m_col < (int)g_rows[m_row].size() - 1) m_col++;
                return true;
            }
            // 肩键 / 扳机键翻候选页
            if (keys & (HidNpadButton_ZL | HidNpadButton_L)) {
                if (!m_candidates.empty()) {
                    if (m_cand_idx < 0) m_cand_idx = 0;
                    CandPrevPage();
                }
                return true;
            }
            if (keys & (HidNpadButton_ZR | HidNpadButton_R)) {
                if (!m_candidates.empty()) {
                    if (m_cand_idx < 0) m_cand_idx = 0;
                    CandNextPage();
                }
                return true;
            }
            if (keys & HidNpadButton_A) {
                if (m_cand_idx >= 0 && (size_t)m_cand_idx < m_candidates.size())
                    commitCandidate((size_t)m_cand_idx);
                else
                    activate(g_rows[m_row][m_col]);
                return true;
            }
            if (keys & HidNpadButton_X) { activate("删除"); return true; }
            if (keys & HidNpadButton_Y) { activate("中/英"); return true; }
            if (keys & HidNpadButton_Plus) { activate("完成"); return true; }
            if (keys & HidNpadButton_B) {
                if (!m_pinyin.empty()) { resetPinyin(); return true; }
                tsl::goBack();
                return true;
            }
            return false;
        }

        // ---- 触摸 ----

        s32 CandBarY(s32 y) const { return y + 60; }
        s32 GridTop(s32 y) const { return y + (m_candidates.empty() ? 64 : 104); }

        bool TouchCandidate(s32 x, s32 y, s32 w, s32 cx, s32 cy) {
            if (m_candidates.empty()) return false;
            const s32 bar_y = CandBarY(y);
            if (cy < bar_y || cy >= bar_y + 36) return false;

            const s32 arrow_w = 26;
            const s32 inner_x = x + 14 + arrow_w;
            const s32 inner_w = w - 28 - arrow_w * 2;
            const s32 cell_w = inner_w / kCandPerPage;

            // 左右翻页箭头
            if (cx < x + 14 + arrow_w) { if (m_cand_idx < 0) m_cand_idx = 0; CandPrevPage(); return true; }
            if (cx >= x + w - 14 - arrow_w) { if (m_cand_idx < 0) m_cand_idx = 0; CandNextPage(); return true; }

            const int page = CurPage();
            const int slot = (cx - inner_x) / std::max(1, cell_w);
            if (slot >= 0 && slot < kCandPerPage) {
                const int abs_idx = PageStart(page) + slot;
                if (abs_idx < (int)m_candidates.size()) { commitCandidate((size_t)abs_idx); return true; }
            }
            return true;
        }

        bool TouchGrid(s32 x, s32 y, s32 w, s32 cx, s32 cy) {
            const s32 grid_top = GridTop(y);
            const s32 row_h = 44;
            if (cy < grid_top) return false;
            const int r = (cy - grid_top) / row_h;
            if (r < 0 || r >= (int)g_rows.size()) return false;

            const int n = (int)g_rows[r].size();
            const s32 cell_w = (w - 28) / 10;
            const s32 row_w = (r >= 4) ? (w - 28) : (cell_w * n);
            const s32 sw = (r >= 4) ? (row_w / n) : cell_w;
            const s32 rx = x + 14 + ((r >= 4) ? 0 : (w - 28 - row_w) / 2);

            const int c = (cx - rx) / std::max(1, sw);
            if (c >= 0 && c < n) {
                m_row = r;
                m_col = c;
                m_cand_idx = -1;
                activate(g_rows[r][c]);
                return true;
            }
            return false;
        }

        bool onTouch(tsl::elm::TouchEvent event, s32 currX, s32 currY, s32, s32, s32, s32) override {
            if (event != tsl::elm::TouchEvent::Release) return false;
            const s32 x = this->getX(), y = this->getY(), w = this->getWidth();
            if (TouchCandidate(x, y, w, currX, currY)) return true;
            return TouchGrid(x, y, w, currX, currY);
        }

        // ---- 绘制 ----

        void draw(tsl::gfx::Renderer *renderer) override {
            const s32 x = this->getX(), y = this->getY(), w = this->getWidth();

            // 1. 输入框 (高对比深色底框)
            const s32 field_y = y + 14;
            renderer->drawRect(x + 14, field_y, w - 28, 40, tsl::Color{0x1, 0x1, 0x2, 0xF});
            renderer->drawRect(x + 14, field_y, w - 28, 1, tsl::Color{0x3, 0x5, 0x7, 0xF});
            renderer->drawRect(x + 14, field_y + 39, w - 28, 1, tsl::Color{0x3, 0x5, 0x7, 0xF});

            std::string display_str = m_text;
            if (!m_pinyin.empty()) display_str += " [" + m_pinyin + "]";
            if (display_str.empty())
                display_str = m_chinese_mode ? "<拼音输入歌名/歌手，Y 键切英文>" : "<英文/数字输入>";
            renderer->drawString(display_str.c_str(), false, x + 24, field_y + 26, 20,
                                 tsl::Color{0xF, 0xF, 0xF, 0xF});

            renderer->drawString(m_chinese_mode ? "拼音" : "EN", false, x + w - 52, field_y + 26, 16,
                                 tsl::Color{0x0, 0xD, 0xF, 0xF});

            // 2. 候选栏（带翻页，纯黑底色 + 独立候选气泡，高对比易辨识）
            s32 grid_top = field_y + 50;
            if (!m_candidates.empty()) {
                const s32 bar_y = CandBarY(y);
                renderer->drawRect(x + 14, bar_y, w - 28, 38, tsl::Color{0x1, 0x1, 0x1, 0xF});

                const int page = CurPage();
                const int pages = PageCount();
                const int start = PageStart(page);
                const int end = PageEnd(page);

                const s32 arrow_w = 28;
                const bool can_prev = page > 0;
                const bool can_next = page + 1 < pages;

                // 左右翻页箭头 + 页码
                char page_buf[16];
                std::snprintf(page_buf, sizeof(page_buf), "%d/%d", page + 1, pages);
                renderer->drawString(can_prev ? "\u25C0" : " ", false, x + 18, bar_y + 26, 18,
                                     can_prev ? tsl::Color{0x0, 0xD, 0xF, 0xF} : tsl::Color{0x4, 0x4, 0x4, 0xF});
                renderer->drawString(can_next ? "\u25B6" : " ", false, x + w - 30, bar_y + 26, 18,
                                     can_next ? tsl::Color{0x0, 0xD, 0xF, 0xF} : tsl::Color{0x4, 0x4, 0x4, 0xF});
                renderer->drawString(page_buf, false, x + w - 76, bar_y + 26, 14,
                                     tsl::Color{0x8, 0x9, 0xA, 0xF});

                const s32 inner_x = x + 14 + arrow_w;
                const s32 inner_w = w - 28 - arrow_w * 2 - 40;
                const s32 cell_w = std::max(1, inner_w / kCandPerPage);

                for (int i = start; i <= end; i++) {
                    const s32 cx = inner_x + (i - start) * cell_w;
                    const bool focused = (m_cand_idx == i);
                    const std::string label = std::to_string(i - start + 1) + "." + m_candidates[i];

                    // 候选气泡：选中鲜亮青色黑字，未选中深色按钮白字，清晰不伤眼
                    renderer->drawRect(cx + 3, bar_y + 4, cell_w - 6, 30,
                                       focused ? tsl::Color{0x0, 0xA, 0xD, 0xF} : tsl::Color{0x2, 0x2, 0x3, 0xF});
                    renderer->drawString(label.c_str(), false, cx + 8, bar_y + 26, 18,
                                         focused ? tsl::Color{0x0, 0x0, 0x0, 0xF} : tsl::Color{0xF, 0xF, 0xF, 0xF});
                }

                grid_top = bar_y + 44;
            }

            // 3. 键盘网格：纯黑/深灰按键底框 + 纯白文字，按键间隙增大到 6px，彻底消除模糊
            const s32 row_h = 44;
            for (int r = 0; r < (int)g_rows.size(); r++) {
                const int n = (int)g_rows[r].size();
                const s32 cell_w = (w - 28) / 10;
                const s32 row_w = (r >= 4) ? (w - 28) : (cell_w * n);
                const s32 sw = (r >= 4) ? (row_w / n) : cell_w;
                const s32 rx = x + 14 + ((r >= 4) ? 0 : (w - 28 - row_w) / 2);
                const s32 ry = grid_top + r * row_h;

                for (int c = 0; c < n; c++) {
                    const s32 kx = rx + c * sw;
                    const bool focused = (m_cand_idx < 0 && r == m_row && c == m_col);
                    const std::string &label = g_rows[r][c];

                    // 增大按键间隙（kx + 3, sw - 6），深色按键底色，焦点亮青色
                    const tsl::Color key_bg = focused
                        ? tsl::Color{0x0, 0xA, 0xD, 0xF}
                        : (r >= 4 ? tsl::Color{0x2, 0x2, 0x3, 0xF} : tsl::Color{0x1, 0x1, 0x2, 0xF});
                    renderer->drawRect(kx + 3, ry + 3, sw - 6, row_h - 6, key_bg);

                    std::string lbl = label;
                    if (m_shift && label.size() == 1 && std::isalpha((unsigned char)label[0]))
                        lbl[0] = (char)std::toupper((unsigned char)label[0]);
                    auto [tw, th] = renderer->drawString(lbl.c_str(), false, 0, 0, 19,
                                                         tsl::style::color::ColorTransparent);
                    const tsl::Color text_color = focused
                        ? tsl::Color{0x0, 0x0, 0x0, 0xF}
                        : tsl::Color{0xF, 0xF, 0xF, 0xF};
                    renderer->drawString(lbl.c_str(), false, kx + (sw - (s32)tw) / 2, ry + row_h / 2 + 6, 19,
                                         text_color);
                }
            }
        }
        void layout(u16, u16, u16, u16) override {
            this->setBoundaries(this->getX(), this->getY(), this->getWidth(), this->getHeight());
        }
    };

} // namespace

KeyboardGui::KeyboardGui(const std::string &title, const std::string &initial, DoneCb on_done)
    : m_title(title), m_initial(initial), m_on_done(std::move(on_done)) {}

tsl::elm::Element *KeyboardGui::createUI() {
    auto frame = new tsl::elm::OverlayFrame(
        m_title,
        "\uE0E0 选字  \uE0E2 退格  \uE0E3 中/英  L/R 翻页  + 完成");
    frame->setContent(new KeyboardElement(m_initial, m_on_done));
    return frame;
}
