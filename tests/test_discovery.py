import pytest

import discovery


def test_service_type_matches_spec():
    assert discovery.SERVICE == "_agentdash._tcp.local."


def test_txt_contains_only_allowed_keys():
    """TXT 绝不能带任何会话内容 —— 未配对方只该知道"这儿有台控制台"。"""
    props = discovery.txt_properties("a1b2c3d4", "WORKSTATION")
    assert set(props) == {"v", "host", "id"}


def test_txt_values():
    props = discovery.txt_properties("a1b2c3d4", "WORKSTATION")
    assert props["v"] == "1"
    assert props["host"] == "WORKSTATION"
    assert props["id"] == "a1b2c3d4"


def test_txt_carries_no_token():
    props = discovery.txt_properties("a1b2c3d4", "WORKSTATION")
    joined = "".join(str(v) for v in props.values())
    assert "token" not in joined.lower()


def test_local_ip_is_v4_dotted():
    ip = discovery.local_ip()
    parts = ip.split(".")
    assert len(parts) == 4
    assert all(p.isdigit() for p in parts)


def test_refresh_is_noop_when_ip_unchanged(monkeypatch):
    b = discovery.Broadcast(8787, "a1b2c3d4", "HOST")
    b.ip = "192.168.1.5"
    calls = []
    monkeypatch.setattr(b, "start", lambda ip=None: calls.append(("start", ip)))
    monkeypatch.setattr(b, "stop", lambda: calls.append(("stop",)))

    assert b.refresh(ip="192.168.1.5") is False
    assert calls == []


def test_refresh_reregisters_when_ip_changed(monkeypatch):
    """IP 变了必须先注销再重新注册，否则广播里还是旧地址。"""
    b = discovery.Broadcast(8787, "a1b2c3d4", "HOST")
    b.ip = "192.168.1.5"
    calls = []
    monkeypatch.setattr(b, "start", lambda ip=None: calls.append(("start", ip)))
    monkeypatch.setattr(b, "stop", lambda: calls.append(("stop",)))

    assert b.refresh(ip="192.168.1.9") is True
    assert calls == [("stop",), ("start", "192.168.1.9")]


def test_start_closes_socket_when_registration_fails(monkeypatch):
    """注册失败时组播 socket 必须收回，否则进程余生都漏着它。"""
    closed = []

    class FakeZeroconf:
        def __init__(self, interfaces=None):
            pass

        def register_service(self, info):
            raise OSError("name collision")

        def close(self):
            closed.append(True)

    monkeypatch.setattr(discovery, "Zeroconf", FakeZeroconf)
    b = discovery.Broadcast(8787, "a1b2c3d4", "HOST")

    with pytest.raises(OSError):
        b.start(ip="192.168.1.5")

    assert closed == [True]
    assert b._zc is None
