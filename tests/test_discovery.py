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
