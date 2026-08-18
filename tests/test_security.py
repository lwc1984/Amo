import security


def test_correct_token_passes():
    assert security.check_token("abc123", "abc123") is True


def test_wrong_token_fails():
    assert security.check_token("nope", "abc123") is False


def test_missing_token_fails():
    assert security.check_token(None, "abc123") is False
    assert security.check_token("", "abc123") is False


def test_length_mismatch_does_not_raise():
    """compare_digest 对不等长输入会抛，必须先挡住。"""
    assert security.check_token("short", "muchlongertoken") is False


def test_loopback_detection():
    assert security.is_loopback("127.0.0.1") is True
    assert security.is_loopback("::1") is True
    assert security.is_loopback("192.168.1.50") is False
    assert security.is_loopback(None) is False


def test_pairing_window_closed_by_default():
    w = security.PairingWindow()
    assert w.is_open(now=1000.0) is False


def test_pairing_window_opens_for_60s():
    w = security.PairingWindow()
    w.open(seconds=60, now=1000.0)

    assert w.is_open(now=1000.0) is True
    assert w.is_open(now=1059.0) is True
    assert w.is_open(now=1061.0) is False


def test_pairing_window_can_be_closed_early():
    w = security.PairingWindow()
    w.open(seconds=60, now=1000.0)
    w.close()
    assert w.is_open(now=1001.0) is False


def test_record_fires_callback_with_peer():
    seen = []
    w = security.PairingWindow(on_pair=seen.append)
    w.open(seconds=60, now=1000.0)

    assert w.record("192.168.1.77", now=1000.0) is True
    assert seen == ["192.168.1.77"]


def test_record_outside_window_does_not_fire():
    """窗口没开时既不回调也不返回真 —— 纵深防御的第二层。"""
    seen = []
    w = security.PairingWindow(on_pair=seen.append)

    assert w.record("192.168.1.77", now=1000.0) is False
    assert seen == []


def test_record_without_callback_does_not_raise():
    w = security.PairingWindow()
    w.open(seconds=60, now=1000.0)

    assert w.record("192.168.1.77", now=1000.0) is True
