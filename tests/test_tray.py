import tray


def test_tablet_url_carries_token():
    url = tray.tablet_url("192.168.1.100", 8787, "7f3a9b2c")
    assert url == "http://192.168.1.100:8787/?k=7f3a9b2c"


def test_overall_state_waiting_wins():
    ss = [{"state": "idle"}, {"state": "running"}, {"state": "waiting"}]
    assert tray.overall_state(ss) == "waiting"


def test_overall_state_stale_beats_running():
    ss = [{"state": "running"}, {"state": "stale"}]
    assert tray.overall_state(ss) == "stale"


def test_overall_state_running_beats_idle():
    assert tray.overall_state([{"state": "idle"}, {"state": "running"}]) == "running"


def test_overall_state_empty_is_off():
    assert tray.overall_state([]) == "off"


def test_colors_match_spec():
    assert tray.COLORS["waiting"] == (255, 176, 32)     # #FFB020
    assert tray.COLORS["running"] == (63, 191, 216)     # #3FBFD8
    assert tray.COLORS["stale"] == (226, 86, 74)        # #E2564A


def test_tray_empty_label_is_chinese():
    import phrases
    assert phrases.TRAY_EMPTY
    assert not phrases.TRAY_EMPTY.isascii()


# ── 配对窗口的可见反馈 ────────────────────────────────────────
# 点了「配对新设备」屏幕上什么都不发生，是这一组测试要防的缺陷。

def test_pairing_label_when_closed_invites():
    import phrases
    import security
    w = security.PairingWindow()
    assert tray.pairing_label(w, now=1000.0) == phrases.PAIRING_MENU


def test_pairing_label_when_open_shows_remaining():
    """窗口开着时菜单要显示还剩多久 —— 否则用户无从判断自己是否还来得及。"""
    import phrases
    import security
    w = security.PairingWindow()
    w.open(seconds=60, now=1000.0)

    assert tray.pairing_label(w, now=1000.0) == f"{phrases.PAIRING_OPEN}  剩 60 秒"
    assert tray.pairing_label(w, now=1045.0) == f"{phrases.PAIRING_OPEN}  剩 15 秒"


def test_pairing_label_reverts_after_expiry():
    import phrases
    import security
    w = security.PairingWindow()
    w.open(seconds=60, now=1000.0)
    assert tray.pairing_label(w, now=1061.0) == phrases.PAIRING_MENU


def test_pairing_label_never_shows_negative():
    import security
    w = security.PairingWindow()
    w.open(seconds=60, now=1000.0)
    assert "剩 0 秒" in tray.pairing_label(w, now=1059.9)
