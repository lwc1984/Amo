"""令牌与主机标识的持久化。

令牌是设备能否读取本机会话数据的唯一凭据；host_id 让设备在本机 IP
变化之后仍能认出是同一台主机。
"""
import json
import os
import secrets
import uuid
from dataclasses import asdict, dataclass
from pathlib import Path

APP_DIR_NAME = "AgentDashboard"


def config_dir() -> Path:
    base = os.environ.get("APPDATA") or str(Path.home())
    return Path(base) / APP_DIR_NAME


@dataclass
class Config:
    token: str
    host_id: str
    tiny_detail: str = "tool"          # "tool" 脱敏到工具名 / "full" 给完整参数


def load_config(path: Path | None = None) -> Config:
    p = path or (config_dir() / "config.json")
    if p.exists():
        raw = json.loads(p.read_text("utf-8"))
        return Config(raw["token"], raw["host_id"], raw.get("tiny_detail", "tool"))

    cfg = Config(token=secrets.token_hex(16), host_id=uuid.uuid4().hex[:8])
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(json.dumps(asdict(cfg), indent=2), "utf-8")
    return cfg
