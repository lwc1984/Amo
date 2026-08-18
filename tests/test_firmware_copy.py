"""固件是 C，导不了 phrases.py，只能靠这条测试挡住三端文案漂移。

与 tests/test_page_copy.py 同一个思路：页面和固件各自硬编码一份中文，
这两条测试是唯一保证它们和 phrases.py 一致的东西。
"""
import re
from pathlib import Path

import phrases

HEADER = Path(__file__).resolve().parent.parent / "amo-display" / "components" / "agent_ui" / "ui_strings.h"

MAPPING = {
    "UI_S_WAITING": "waiting",
    "UI_S_RUNNING": "running",
    "UI_S_IDLE": "idle",
    "UI_S_STALE": "stale",
}


def _defines(text: str) -> dict:
    return dict(re.findall(r'#define\s+(UI_S_\w+)\s+"([^"]*)"', text))


def test_firmware_state_words_match_phrases():
    d = _defines(HEADER.read_text(encoding="utf-8"))
    for macro, state in MAPPING.items():
        assert macro in d, f"ui_strings.h 缺少 {macro}"
        assert d[macro] == phrases.STATE_LABEL_SHORT[state], (
            f"{macro} 与 phrases.STATE_LABEL_SHORT[{state!r}] 不一致"
        )


def test_nolink_is_distinct_from_stale():
    """连不上任何主机 与 某个会话失联 是两回事，合并会掩盖故障。"""
    d = _defines(HEADER.read_text(encoding="utf-8"))
    assert d["UI_S_NOLINK"] != d["UI_S_STALE"]


def test_all_firmware_strings_are_chinese():
    d = _defines(HEADER.read_text(encoding="utf-8"))
    for macro, value in d.items():
        assert not value.isascii(), f"{macro} 应为中文口语: {value!r}"


def test_status_words_fit_the_narrow_screen():
    """状态词用 28px 大字号显示，368px 宽度下最多容纳 4 个字。"""
    d = _defines(HEADER.read_text(encoding="utf-8"))
    for macro in list(MAPPING) + ["UI_S_NOLINK"]:
        assert len(d[macro]) <= 4, f"{macro} 超过 4 个字: {d[macro]}"
