import json

import config


def test_generates_and_persists(tmp_path):
    p = tmp_path / "config.json"
    cfg = config.load_config(p)

    assert len(cfg.token) == 32
    assert int(cfg.token, 16) >= 0          # 必须是合法十六进制
    assert len(cfg.host_id) == 8
    assert cfg.tiny_detail == "tool"
    assert p.exists()


def test_reuses_existing(tmp_path):
    p = tmp_path / "config.json"
    first = config.load_config(p)
    second = config.load_config(p)

    assert first.token == second.token
    assert first.host_id == second.host_id


def test_tokens_differ_between_hosts(tmp_path):
    a = config.load_config(tmp_path / "a.json")
    b = config.load_config(tmp_path / "b.json")

    assert a.token != b.token
    assert a.host_id != b.host_id


def test_written_file_is_readable_json(tmp_path):
    p = tmp_path / "config.json"
    cfg = config.load_config(p)
    raw = json.loads(p.read_text("utf-8"))

    assert raw["token"] == cfg.token
    assert raw["host_id"] == cfg.host_id


def test_config_dir_under_appdata(monkeypatch, tmp_path):
    monkeypatch.setenv("APPDATA", str(tmp_path))
    assert config.config_dir() == tmp_path / "AgentDashboard"
