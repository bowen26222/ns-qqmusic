#!/usr/bin/env python3
"""Exercise the production C++ LyricParser with ASan and UBSan."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PARSER_HPP = ROOT / 'overlay/source/core/lyric_parser.hpp'

CPP_SOURCE = r'''
#include "lyric_parser.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

int main() {
    using namespace qqmusic;

    // 1. 标准歌词解析与多时间戳单行展开
    {
        LyricParser parser;
        std::string lrc =
            "[ti:晴天]\n"
            "[ar:周杰伦]\n"
            "[al:叶惠美]\n"
            "[by:]\n"
            "[00:00.00]晴天 - 周杰伦 (Jay Chou)\n"
            "[00:02.25]词：周杰伦\n"
            "[00:04.50]曲：周杰伦\n"
            "[00:29.00]故事的小黄花\n"
            "[00:32.40]从出生那年就飘着\n"
            "[01:00.00][02:00.00]但偏偏 雨渐渐 大到我看你不见\n";

        assert(parser.Parse(lrc));
        assert(parser.LineCount() == 7);
        assert(parser.GetLine(0).time_ms == 0);
        assert(parser.GetLine(0).text == "晴天 - 周杰伦 (Jay Chou)");
        assert(parser.GetLine(1).time_ms == 2250);
        assert(parser.GetLine(4).time_ms == 32400);
        assert(parser.GetLine(4).text == "从出生那年就飘着");

        // 多时间戳单行在排序后各占一条
        assert(parser.GetLine(5).time_ms == 60000);
        assert(parser.GetLine(5).text == "但偏偏 雨渐渐 大到我看你不见");
        assert(parser.GetLine(6).time_ms == 120000);
        assert(parser.GetLine(6).text == "但偏偏 雨渐渐 大到我看你不见");

        // 二分查找时间线：前奏阶段必须返回 -1
        assert(parser.GetCurrentIndex(0) == 0);
        assert(parser.GetCurrentIndex(1000) == 0);
        assert(parser.GetCurrentIndex(2249) == 0);
        assert(parser.GetCurrentIndex(2250) == 1);
        assert(parser.GetCurrentIndex(30000) == 3);
        assert(parser.GetCurrentIndex(32400) == 4);
        assert(parser.GetCurrentIndex(999999) == 6);
    }

    // 2. 前奏期间（第一句开始前）二分查找必须返回 -1
    {
        LyricParser parser;
        std::string lrc = "[00:15.00]第一句在第15秒才唱\n";
        assert(parser.Parse(lrc));
        assert(parser.GetCurrentIndex(0) == -1);
        assert(parser.GetCurrentIndex(14999) == -1);
        assert(parser.GetCurrentIndex(15000) == 0);
        assert(parser.GetCurrentIndex(16000) == 0);
    }

    // 3. UTF-8 BOM 过滤（0xEF, 0xBB, 0xBF）
    {
        LyricParser parser;
        std::string lrc = "\xEF\xBB\xBF[00:01.00]带BOM的首行歌词\n";
        assert(parser.Parse(lrc));
        assert(parser.LineCount() == 1);
        assert(parser.GetLine(0).time_ms == 1000);
        assert(parser.GetLine(0).text == "带BOM的首行歌词");
    }

    // 4. [offset:xxx] 标签平移时间戳
    {
        LyricParser parser;
        std::string lrc =
            "[offset:+500]\n"
            "[00:02.00]延后0.5秒\n";
        assert(parser.Parse(lrc));
        assert(parser.GetLine(0).time_ms == 2500);

        LyricParser parser2;
        std::string lrc2 =
            "[offset:-500]\n"
            "[00:02.00]提前0.5秒\n";
        assert(parser2.Parse(lrc2));
        assert(parser2.GetLine(0).time_ms == 1500);
    }

    // 5. 严格格式校验：杜绝 sscanf("%f") 引发的 NaN、Inf 与溢出
    {
        LyricParser parser;
        // 非法秒数、含有字母、NaN、Inf 必须全部安全跳过
        std::string invalid_lrc =
            "[00:nan]非法NaN\n"
            "[00:inf]非法Inf\n"
            "[00:01oops]非法后缀\n"
            "[00:99.00]非法超大秒数\n"
            "[00:05.00]唯一合法行\n";
        assert(parser.Parse(invalid_lrc));
        assert(parser.LineCount() == 1);
        assert(parser.GetLine(0).time_ms == 5000);
        assert(parser.GetLine(0).text == "唯一合法行");
    }

    // 6. 内存硬防御：恶意极多时间戳与超长文本必须硬截断，绝不吃穿内存
    {
        LyricParser parser;
        std::string bomb;
        for (int i = 0; i < 5000; i++) bomb += "[00:01.00]";
        bomb += std::string(10000, 'X');
        assert(parser.Parse(bomb));
        assert(parser.LineCount() <= 1500);

        size_t total_bytes = 0;
        for (const auto &line : parser.GetLines()) total_bytes += line.text.size();
        assert(total_bytes <= 48 * 1024);
    }

    std::puts("PASS: production C++ LyricParser ASan/UBSan validated (BOM, offset, no-NaN, bounds, O(log N) lookup)");
    return 0;
}
'''

def main():
    assert PARSER_HPP.exists(), f"Missing {PARSER_HPP}"
    with tempfile.TemporaryDirectory(prefix="qqmusic-lyric-") as tmp:
        cpp = Path(tmp) / "test_lyric.cpp"
        bin_path = Path(tmp) / "test_lyric"
        cpp.write_text(CPP_SOURCE, encoding="utf-8")

        include_dir = PARSER_HPP.parent
        cmd_compile = [
            "g++", "-std=c++20", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer",
            f"-I{include_dir}",
            str(cpp), "-o", str(bin_path)
        ]
        subprocess.run(cmd_compile, check=True)
        subprocess.run([str(bin_path)], check=True)

if __name__ == "__main__":
    main()
