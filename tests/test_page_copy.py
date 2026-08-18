"""页面 JS 无法 import phrases.py，只能靠这两条测试挡住三端文案漂移。"""
import re
from pathlib import Path

import phrases

PAGE = Path(__file__).resolve().parent.parent / "static" / "index.html"


def test_page_state_labels_match_phrases():
    html = PAGE.read_text(encoding="utf-8")
    for state, label in phrases.STATE_LABEL.items():
        pattern = rf"{state}\s*:\s*['\"]{re.escape(label)}['\"]"
        assert re.search(pattern, html), f"{state} 的文案与 phrases.STATE_LABEL 不一致"


def test_page_carries_shared_strings():
    html = PAGE.read_text(encoding="utf-8")
    for s in (phrases.EMPTY_LIST, phrases.CONN_ON, phrases.CONN_OFF,
              phrases.ARM_BUTTON, phrases.ARMED_BUTTON, phrases.UNPAIRED):
        assert s in html, f"页面缺少共享文案: {s}"
