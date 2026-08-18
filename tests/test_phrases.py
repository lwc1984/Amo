import phrases

STATES = ("waiting", "running", "idle", "stale")


def test_every_state_has_both_labels():
    for st in STATES:
        assert phrases.STATE_LABEL[st]
        assert phrases.STATE_LABEL_SHORT[st]


def test_short_labels_fit_narrow_screen():
    """ESP32 横屏状态词区宽度有限，短文案不得超过 4 个字。"""
    for st in STATES:
        assert len(phrases.STATE_LABEL_SHORT[st]) <= 4, st


def test_waiting_copy_matches_spec():
    assert phrases.STATE_LABEL["waiting"] == "哥们儿，该你了"
    assert phrases.STATE_LABEL_SHORT["waiting"] == "该你了"


def test_no_english_state_labels_remain():
    """告警文案必须是中文口语，不能残留骨架里的英文。"""
    for st in STATES:
        assert not phrases.STATE_LABEL[st].isascii()
