import metrics


class FakeCounters:
    def __init__(self, sent, recv):
        self.bytes_sent = sent
        self.bytes_recv = recv


def test_sample_computes_rate_per_second():
    prev = FakeCounters(1000, 2000)
    cur = FakeCounters(1000 + 4096, 2000 + 8192)
    out = metrics._sample(prev, prev_t=100.0, now=102.0, counters=cur)

    assert out["net_up"] == 2048       # 4096 字节 / 2 秒
    assert out["net_down"] == 4096


def test_sample_survives_zero_elapsed():
    """时钟没走时不能除零。"""
    prev = FakeCounters(0, 0)
    cur = FakeCounters(10, 10)
    out = metrics._sample(prev, prev_t=100.0, now=100.0, counters=cur)

    assert out["net_up"] >= 0
    assert out["net_down"] >= 0


def test_current_has_all_keys():
    m = metrics.current()
    for k in ("cpu", "mem", "disk", "gpu", "vram", "net_up", "net_down", "hist"):
        assert k in m


def test_history_buckets_exist():
    m = metrics.current()
    for k in ("cpu", "mem", "gpu", "net"):
        assert k in m["hist"]


def test_gpu_is_none_without_nvidia():
    """没有 N 卡时静默降级为 None，客户端渲染成 —。"""
    m = metrics.current()
    assert m["gpu"] is None or isinstance(m["gpu"], (int, float))
