"""配对二维码。

二维码里带着令牌，所以这个接口必须只对回环开放。页面外壳本身是不带令牌
就发给整个局域网的 —— 若二维码画在所有人都能打开的页面上，同网段任何设备
扫一下就绕过了 60 秒配对窗口，信任模型等于不存在。
"""
import pytest
from fastapi.testclient import TestClient

import server


@pytest.fixture
def loopback():
    return TestClient(server.app, client=("127.0.0.1", 50000))


@pytest.fixture
def lan():
    """局域网来源。平板打开页面时就是这一种。"""
    return TestClient(server.app, client=("192.168.1.77", 50000))


# ---- 地址构造 ----

def test_pair_url_carries_ip_port_and_token():
    url = server.pair_url("192.168.1.100")
    assert url == f"http://192.168.1.100:{server.PORT}/?k={server.CFG.token}"


def test_pair_url_without_arg_uses_local_ip(monkeypatch):
    import discovery
    monkeypatch.setattr(discovery, "local_ip", lambda: "10.0.0.5")
    assert server.pair_url().startswith("http://10.0.0.5:")


# ---- 接口 ----

def test_qr_from_loopback_returns_svg_and_url(loopback):
    """二维码和明文地址一起给。

    页面是从 localhost 打开的，自己不知道宿主的局域网 IP，所以手抄兜底那一行
    只能由服务端给。
    """
    r = loopback.get("/api/qr")
    assert r.status_code == 200
    body = r.json()
    assert "<svg" in body["svg"]
    assert body["url"] == server.pair_url()


def test_qr_from_lan_is_403(lan):
    """最重要的一条：二维码不能发给局域网，否则令牌就公开了。"""
    assert lan.get("/api/qr").status_code == 403


def test_qr_from_lan_stays_403_even_with_token(lan):
    """带对令牌也不行 —— 已配对设备没有替别人发放令牌的权限。"""
    assert lan.get(f"/api/qr?k={server.CFG.token}").status_code == 403


def test_qr_url_is_the_thing_the_tablet_should_open(loopback):
    """明文地址必须带令牌 —— 它就是给人照着敲的那一行，不带令牌等于废的。"""
    url = loopback.get("/api/qr").json()["url"]
    assert server.CFG.token in url
    assert f":{server.PORT}/" in url


def test_qr_svg_has_no_xml_prolog(loopback):
    """SVG 要用 innerHTML 塞进 HTML 文档，XML 前言在那里只会变成一个伪注释节点。"""
    svg = loopback.get("/api/qr").json()["svg"]
    assert not svg.lstrip().startswith("<?xml")
    assert svg.lstrip().startswith("<svg")
