import asyncio
import json

import pytest
from fastapi.testclient import TestClient

import server


@pytest.fixture
def client():
    # TestClient 默认把 client.host 设成 "testclient"，不在 LOOPBACK 里，
    # 会让 /hook/* 全部 403。显式伪装成回环来源。
    return TestClient(server.app, client=("127.0.0.1", 50000))


@pytest.fixture
def tok():
    return server.CFG.token


def hook(client, event, **payload):
    payload.setdefault("session_id", "s1")
    return client.post(f"/hook/{event}", json=payload)


# ---- 静态资源 ----

def test_static_dir_exists():
    assert server.STATIC.is_dir()
    assert (server.STATIC / "index.html").is_file()


def test_index_served_without_token(client):
    assert client.get("/").status_code == 200


# ---- 鉴权 ----

def test_stream_without_token_is_401(client):
    assert client.get("/api/stream").status_code == 401


def test_stream_with_wrong_token_is_401(client):
    assert client.get("/api/stream?k=deadbeef").status_code == 401


def test_tiny_without_token_is_401(client):
    assert client.get("/api/tiny").status_code == 401


def test_tiny_with_token_is_200(client, tok):
    assert client.get(f"/api/tiny?k={tok}").status_code == 200


def test_token_accepted_via_header(client, tok):
    r = client.get("/api/tiny", headers={"X-Agent-Key": tok})
    assert r.status_code == 200


# ---- hook 来源限制 ----

def test_hook_from_loopback_is_accepted(client):
    assert hook(client, "session-start", cwd="D:/proj/Amo").status_code == 200


def test_hook_rejects_non_loopback_client(client, monkeypatch):
    monkeypatch.setattr(server.security, "is_loopback", lambda h: False)
    assert hook(client, "prompt").status_code == 403


# ---- 端到端 ----

def test_hook_then_tiny_reflects_state(client, tok):
    hook(client, "session-start", cwd="D:/proj/Amo")
    hook(client, "notify", notification_type="permission_prompt", message="批准吗")

    body = client.get(f"/api/tiny?k={tok}").text
    line1, line2, _ = body.split("\n")

    assert line1.startswith("1|1,0,0|")
    assert line2.startswith("Amo\t")


def test_tiny_full_flag_gives_arguments(client, tok):
    hook(client, "tool", cwd="D:/proj/Amo",
         tool_name="Bash", tool_input={"command": "git push origin main"})

    redacted = client.get(f"/api/tiny?k={tok}").text.split("\n")[1]
    full = client.get(f"/api/tiny?k={tok}&d=full").text.split("\n")[1]

    assert redacted == "Amo\tBash"
    assert full == "Amo\tBash: git push origin main"


def test_tiny_query_can_force_redaction_over_config(client, tok, monkeypatch):
    """配置设成 full 时，调用方仍能用 ?d=tool 要求脱敏。"""
    monkeypatch.setattr(server.CFG, "tiny_detail", "full")
    hook(client, "tool", cwd="D:/proj/Amo",
         tool_name="Bash", tool_input={"command": "git push origin main"})

    assert client.get(f"/api/tiny?k={tok}&d=tool").text.split("\n")[1] == "Amo\tBash"


def test_tiny_config_full_applies_when_no_query(client, tok, monkeypatch):
    monkeypatch.setattr(server.CFG, "tiny_detail", "full")
    hook(client, "tool", cwd="D:/proj/Amo",
         tool_name="Bash", tool_input={"command": "git push origin main"})

    line2 = client.get(f"/api/tiny?k={tok}").text.split("\n")[1]
    assert line2 == "Amo\tBash: git push origin main"


# ---- 配对 ----

def test_pair_closed_by_default_is_403(client):
    server.PAIRING.close()
    assert client.post("/api/pair").status_code == 403


def test_pair_inside_window_returns_token(client, tok):
    server.PAIRING.open(seconds=60)
    try:
        r = client.post("/api/pair")
        assert r.status_code == 200
        assert r.json()["token"] == tok
        assert r.json()["host_id"] == server.CFG.host_id
    finally:
        server.PAIRING.close()


def test_pair_after_close_is_403_again(client):
    server.PAIRING.open(seconds=60)
    server.PAIRING.close()
    assert client.post("/api/pair").status_code == 403


# ---- 新增：流 / 回环 / 配对开窗 / 通知回调 / 脱敏端到端 / hook kind ----

def test_stream_generator_yields_a_valid_frame(client, tok):
    """/api/stream 的生成器此前从没被任何测试执行过。
    经 TestClient 流式读会挂在无限循环上，所以直接驱动生成器取第一帧。"""
    hook(client, "session-start", cwd="D:/proj/Amo")

    async def first_frame():
        resp = await server.stream()
        return await resp.body_iterator.__anext__()

    frame = asyncio.run(first_frame())

    assert frame.startswith("data: ")
    assert frame.endswith("\n\n")
    snap = json.loads(frame[6:].strip())
    assert snap["host"] == server.sessions.HOST
    assert snap["sessions"][0]["name"] == "Amo"


def test_hook_from_real_lan_address_is_403():
    """用真实的非回环客户端地址，而不是 monkeypatch —— 后者对着一个
    信任可伪造请求头的实现同样会通过。"""
    lan = TestClient(server.app, client=("192.168.1.55", 5000))
    assert lan.post("/hook/prompt", json={"session_id": "s1"}).status_code == 403


def test_pair_open_is_loopback_only():
    lan = TestClient(server.app, client=("192.168.1.55", 5000))
    assert lan.post("/api/pair/open").status_code == 403


def test_pair_open_then_pair_succeeds(client, tok):
    server.PAIRING.close()
    assert client.post("/api/pair").status_code == 403
    assert client.post("/api/pair/open").status_code == 200
    try:
        r = client.post("/api/pair")
        assert r.status_code == 200
        assert r.json()["token"] == tok
        assert r.json()["host"] == server.sessions.HOST
    finally:
        server.PAIRING.close()


def test_pair_fires_on_pair_callback(client, monkeypatch):
    """有人趁窗口偷配时，宿主必须收到通知 —— 这是该机制唯一的安全网。"""
    seen = []
    monkeypatch.setattr(server.PAIRING, "on_pair", seen.append)
    server.PAIRING.open(seconds=60)
    try:
        client.post("/api/pair")
        assert len(seen) == 1
    finally:
        server.PAIRING.close()


def test_tiny_never_leaks_cwd_end_to_end(client, tok):
    """保证是"cwd 字段不下发"，不是"任何路径都不出现" ——
    d=full 本来就会给出工具参数，参数里可能自带路径。"""
    hook(client, "tool", cwd="D:/secret/plan",
         tool_name="Bash", tool_input={"command": "echo hi"})

    redacted = client.get(f"/api/tiny?k={tok}").text
    full = client.get(f"/api/tiny?k={tok}&d=full").text

    assert "D:/secret" not in redacted
    assert "D:/secret" not in full          # cwd 任何模式下都不下发
    assert "echo hi" not in redacted        # 默认脱敏到工具名
    assert "echo hi" in full                # d=full 才给参数


def test_hook_kind_query_sets_notification_type(client, tok):
    """hooks-snippet 用 ?kind= 传 matcher 类型，服务端必须真的读它。"""
    hook(client, "session-start", cwd="D:/proj/Amo")
    client.post("/hook/notify?kind=permission",
                json={"session_id": "s1", "cwd": "D:/proj/Amo", "message": "OK"})
    assert client.get(f"/api/tiny?k={tok}").text.split("\n")[0].startswith("1|1,0,0|")
