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
