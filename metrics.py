"""系统指标采集。

采集在独立守护线程里跑；psutil.cpu_percent(interval=1.0) 自带 1 秒阻塞，
那句话就是循环节拍，别换成非阻塞版本再加 sleep。
"""
import os
import threading
import time
from collections import deque

import psutil

HISTORY = 60          # 保留 60 个采样点画 sparkline

_metrics = {"cpu": 0, "mem": 0, "disk": 0, "gpu": None, "vram": None,
            "net_up": 0, "net_down": 0,
            "hist": {k: [] for k in ("cpu", "mem", "gpu", "net")}}

_started = False
_lock = threading.Lock()

try:
    import pynvml
    pynvml.nvmlInit()
    _gpu = pynvml.nvmlDeviceGetHandleByIndex(0)
except Exception:
    _gpu = None


def current() -> dict:
    return dict(_metrics)


def _sample(prev_net, prev_t: float, now: float, counters) -> dict:
    dt = max(now - prev_t, 0.001)
    return {
        "net_up": round((counters.bytes_sent - prev_net.bytes_sent) / dt),
        "net_down": round((counters.bytes_recv - prev_net.bytes_recv) / dt),
    }


def _read_gpu():
    if not _gpu:
        return None, None
    try:
        util = pynvml.nvmlDeviceGetUtilizationRates(_gpu).gpu
        mem = pynvml.nvmlDeviceGetMemoryInfo(_gpu)
        return util, round(mem.used / mem.total * 100)
    except Exception:
        return None, None


def _collect_loop():
    prev_net = psutil.net_io_counters()
    prev_t = time.time()
    hist = {k: deque(maxlen=HISTORY) for k in ("cpu", "mem", "gpu", "net")}

    while True:
        cpu = psutil.cpu_percent(interval=1.0)      # 这一句就是节拍
        now = time.time()
        counters = psutil.net_io_counters()
        rates = _sample(prev_net, prev_t, now, counters)
        prev_net, prev_t = counters, now

        mem = psutil.virtual_memory().percent
        disk = psutil.disk_usage(os.environ.get("SystemDrive", "C:") + "\\").percent
        gpu, vram = _read_gpu()

        hist["cpu"].append(cpu)
        hist["mem"].append(mem)
        hist["gpu"].append(gpu or 0)
        hist["net"].append(round((rates["net_up"] + rates["net_down"]) / 1024))

        _metrics.update(cpu=cpu, mem=mem, disk=disk, gpu=gpu, vram=vram,
                        hist={k: list(v) for k, v in hist.items()}, **rates)


def start_collector() -> None:
    global _started
    with _lock:
        if _started:
            return
        _started = True
    threading.Thread(target=_collect_loop, daemon=True).start()
