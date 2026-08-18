import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))


@pytest.fixture(autouse=True)
def clean_sessions():
    """每个用例都从空会话表开始，避免用例间互相污染。

    延迟到 fixture 内部再 import：Task 0 阶段 sessions.py 还不存在，
    顶层 import 会让整个套件收集失败。只容忍"sessions 本身不存在"，
    sessions.py 内部的导入错误必须原样上抛，否则会被误当成"还没写"。
    """
    try:
        import sessions
    except ModuleNotFoundError as e:
        if e.name != "sessions":
            raise
        yield
        return
    sessions.SESSIONS.clear()
    yield
    sessions.SESSIONS.clear()
