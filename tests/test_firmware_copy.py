"""固件是 C，导不了 phrases.py，只能靠这条测试挡住三端文案漂移。

与 tests/test_page_copy.py 同一个思路：页面和固件各自硬编码一份中文，
这两条测试是唯一保证它们和 phrases.py 一致的东西。
"""
import re
from pathlib import Path

import pytest

import phrases

HEADER = Path(__file__).resolve().parent.parent / "amo-display" / "components" / "agent_ui" / "ui_strings.h"

MAPPING = {
    "UI_S_WAITING": "waiting",
    "UI_S_RUNNING": "running",
    "UI_S_IDLE": "idle",
    "UI_S_STALE": "stale",
}


def _strip_inactive(text: str) -> str:
    """去掉编译器看不见的东西：块注释、行注释、#if 0 块。

    留着它们会让守卫把 `// #define UI_S_WAITING "旧值"` 这种遗留当成活定义，
    于是测试通过、固件却显示别的字 —— 正是这条测试要防的那种漂移。
    """
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"#if\s+0\b.*?#endif", "", text, flags=re.S)
    return text


def _defines(text: str) -> dict:
    pairs = re.findall(r'#define\s+(UI_S_\w+)\s+"([^"]*)"', _strip_inactive(text))
    names = [n for n, _ in pairs]
    dupes = sorted({n for n in names if names.count(n) > 1})
    assert not dupes, f"ui_strings.h 里存在重复定义，编译结果取决于顺序: {dupes}"
    return dict(pairs)


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


def test_line_commented_define_is_ignored():
    """重命名后遗留的注释行不该被当成活定义。"""
    text = '// #define UI_S_WAITING "旧值"\n#define UI_S_WAITING "该你了"\n'
    assert _defines(text) == {"UI_S_WAITING": "该你了"}


def test_block_commented_define_is_ignored():
    text = '/* #define UI_S_WAITING "旧值" */\n#define UI_S_WAITING "该你了"\n'
    assert _defines(text) == {"UI_S_WAITING": "该你了"}


def test_if_zero_block_is_ignored():
    """调试时被 #if 0 关掉的宏，编译器看不见，测试也不该当真。"""
    text = '#if 0\n#define UI_S_WAITING "旧值"\n#endif\n#define UI_S_WAITING "该你了"\n'
    assert _defines(text) == {"UI_S_WAITING": "该你了"}


def test_duplicate_define_is_rejected():
    """合并冲突留下两份定义时必须报错，而不是静默取最后一个。"""
    text = '#define UI_S_WAITING "甲"\n#define UI_S_WAITING "乙"\n'
    with pytest.raises(AssertionError, match="重复定义"):
        _defines(text)


def test_live_define_still_parsed():
    """剥离逻辑不得误伤正常定义。"""
    text = '#define UI_S_WAITING  "该你了"\n#define UI_S_IDLE "摸鱼中"\n'
    assert _defines(text) == {"UI_S_WAITING": "该你了", "UI_S_IDLE": "摸鱼中"}
