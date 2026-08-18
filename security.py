"""令牌校验、来源判定、配对窗口。

信任边界就是令牌：设备只能读取自己配对过的主机。同事的机器在 mDNS 里
发现得到，但拿不到令牌，因此不会出现在你的设备上。
"""
import secrets
import time
from typing import Callable

LOOPBACK = {"127.0.0.1", "::1", "localhost"}


def check_token(provided: str | None, expected: str) -> bool:
    if not provided or len(provided) != len(expected):
        return False
    return secrets.compare_digest(provided, expected)


def is_loopback(host: str | None) -> bool:
    return host in LOOPBACK if host else False


class PairingWindow:
    """托盘点"配对新设备"后开启的一段有限时间窗口。

    窗口内允许多台设备配对（平板 + ESP32 一次搞定），但每次成功都回调
    一次通知 —— 若有人趁窗口偷配，用户能立刻看见。
    """

    def __init__(self, on_pair: Callable[[str], None] | None = None):
        self.until = 0.0
        self.on_pair = on_pair

    def open(self, seconds: int = 60, now: float | None = None) -> None:
        self.until = (time.time() if now is None else now) + seconds

    def is_open(self, now: float | None = None) -> bool:
        return (time.time() if now is None else now) < self.until

    def close(self) -> None:
        self.until = 0.0

    def record(self, peer: str) -> None:
        if self.on_pair:
            self.on_pair(peer)
