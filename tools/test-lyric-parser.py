#!/usr/bin/env python3
import re
import sys

SAMPLE_LRC = """[ti:晴天]
[ar:周杰伦]
[al:叶惠美]
[by:]
[offset:0]
[00:00.00]晴天 - 周杰伦 (Jay Chou)
[00:02.25]词：周杰伦
[00:04.50]曲：周杰伦
[00:06.75]编曲：周杰伦
[00:29.00]故事的小黄花
[00:32.40]从出生那年就飘着
[00:35.80]童年的荡秋千
[00:39.10]随记忆一直晃到现在
[01:00.00][02:00.00]但偏偏 雨渐渐 大到我看你不见
"""

def parse_lrc(lrc_text):
    lines = []
    for raw in lrc_text.splitlines():
        raw = raw.strip()
        if not raw:
            continue
        # Find all timestamp tags [mm:ss.xx] or [mm:ss.xxx]
        tags = re.findall(r'\[(\d+):(\d+(?:\.\d+)?)\]', raw)
        if not tags:
            continue
        # Strip all tags to get lyric text
        text = re.sub(r'\[\d+:\d+(?:\.\d+)?\]', '', raw).strip()
        for m_str, s_str in tags:
            ms = int(m_str) * 60000 + int(float(s_str) * 1000 + 0.5)
            lines.append((ms, text))
    lines.sort(key=lambda x: x[0])
    return lines

def get_current_index(lines, current_ms):
    if not lines:
        return -1
    if current_ms < lines[0][0]:
        return 0
    # Binary search upper bound - 1
    low = 0
    high = len(lines)
    while low < high:
        mid = (low + high) // 2
        if lines[mid][0] <= current_ms:
            low = mid + 1
        else:
            high = mid
    idx = low - 1
    return max(0, min(idx, len(lines) - 1))

def test_lrc():
    lines = parse_lrc(SAMPLE_LRC)
    assert len(lines) == 10, f"Expected 10 entries (including duplicated multi-timestamp tag), got {len(lines)}"

    # Check timestamps
    assert lines[0] == (0, "晴天 - 周杰伦 (Jay Chou)")
    assert lines[1] == (2250, "词：周杰伦")
    assert lines[4] == (29000, "故事的小黄花")
    assert lines[5] == (32400, "从出生那年就飘着")

    # Multi-timestamp check
    multi_0 = [l for l in lines if l[0] == 60000]
    multi_1 = [l for l in lines if l[0] == 120000]
    assert len(multi_0) == 1 and multi_0[0][1] == "但偏偏 雨渐渐 大到我看你不见"
    assert len(multi_1) == 1 and multi_1[0][1] == "但偏偏 雨渐渐 大到我看你不见"

    # Search tests
    assert get_current_index(lines, 0) == 0
    assert get_current_index(lines, 1000) == 0
    assert get_current_index(lines, 2249) == 0
    assert get_current_index(lines, 2250) == 1
    assert get_current_index(lines, 30000) == 4 # 29000 - 32400
    assert get_current_index(lines, 32400) == 5
    assert get_current_index(lines, 35000) == 5
    assert get_current_index(lines, 999999) == len(lines) - 1

    print("PASS: LRC parsing, multi-timestamp, and O(log N) line lookup tests")

if __name__ == "__main__":
    test_lrc()
