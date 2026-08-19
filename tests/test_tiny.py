import sessions
import tiny


def make_snap(sess, metrics=None):
    return {"sessions": sess, "host": "WORKSTATION",
            "metrics": metrics or {"cpu": 12.4, "mem": 41.2, "gpu": 3,
                                   "net_up": 4096, "net_down": 4192}}


def sess(state, name, tool="", arg="", phrase=""):
    return {"state": state, "name": name, "tool": tool, "arg": arg, "phrase": phrase}


def test_three_lines_exactly():
    out = tiny.render_tiny(make_snap([]))
    assert len(out.split("\n")) == 3


def test_counts_and_host_on_first_line():
    s = [sess("waiting", "a"), sess("running", "b"), sess("running", "c"),
         sess("idle", "d"), sess("stale", "e")]
    line1 = tiny.render_tiny(make_snap(s)).split("\n")[0]
    assert line1 == "3|1,2,1,1,0|WORKSTATION"


def test_stale_is_counted_separately():
    """stale 不计进任何一个的话，固件只会看到 0,0,0，把失联渲染成「摸鱼中」。

    设计里那条「断连绝不能渲染成正常」正是靠这个计数落地的：
    第四种状态必须在协议里有自己的位置，不能靠推断。
    """
    line1 = tiny.render_tiny(make_snap([sess("stale", "失联了")])).split(chr(10))[0]
    assert line1 == "3|0,0,0,1,0|WORKSTATION"


def test_busy_is_counted_separately():
    """busy 混进 running 的话，板子分不出「干着呢」和「憋大招」。

    颜色上两者相同（都不需要你走过去），但文字和嘴型要分开 ——
    走近看时你需要知道它是刚开始还是已经憋了十分钟。
    """
    line1 = tiny.render_tiny(make_snap([sess("busy", "在跑大构建")])).split(chr(10))[0]
    assert line1 == "3|0,0,0,0,1|WORKSTATION"


def test_version_is_three():
    """每加一个计数位都要升版本号，老固件按旧位数解析会读错。

    tiny_parse 对不认识的版本整体拒绝而不是猜 —— 宁可显示「连不上」，
    也不要显示一个错的状态。
    """
    assert tiny.TINY_VERSION == 3
    assert tiny.render_tiny(make_snap([])).split("|")[0] == "3"


def test_top_session_is_the_first_one():
    """snapshot() 已按 waiting → running → idle 排过序，取第一条即可。"""
    s = [sess("waiting", "急事", tool="Bash", arg="rm -rf /tmp/x"),
         sess("running", "别的")]
    line2 = tiny.render_tiny(make_snap(s)).split("\n")[1]
    assert line2.startswith("急事\t")


def test_detail_is_redacted_by_default():
    s = [sess("waiting", "恐龙公园初始化", tool="Bash", arg="git push origin main")]
    line2 = tiny.render_tiny(make_snap(s)).split("\n")[1]
    assert line2 == "恐龙公园初始化\tBash"


def test_detail_full_when_requested():
    s = [sess("waiting", "恐龙公园初始化", tool="Bash", arg="git push origin main")]
    line2 = tiny.render_tiny(make_snap(s), full=True).split("\n")[1]
    assert line2 == "恐龙公园初始化\tBash: git push origin main"


def test_empty_second_line_when_no_sessions():
    assert tiny.render_tiny(make_snap([])).split("\n")[1] == ""


def test_idle_only_still_shows_that_session():
    s = [sess("idle", "闲着的", phrase="摸鱼中")]
    line2 = tiny.render_tiny(make_snap(s)).split("\n")[1]
    assert line2.startswith("闲着的\t")


def test_metrics_line_rounds():
    line3 = tiny.render_tiny(make_snap([])).split("\n")[2]
    assert line3 == "12,41,3,8"        # (4096+4192)/1024 = 8.09 -> 8


def test_gpu_absent_is_minus_one():
    snap = make_snap([], metrics={"cpu": 5, "mem": 5, "gpu": None,
                                  "net_up": 0, "net_down": 0})
    assert tiny.render_tiny(snap).split("\n")[2] == "5,5,-1,0"


def test_no_cwd_leaks_into_output():
    """桌上小屏别人走过就能看见，绝不下发路径。"""
    s = [dict(sess("running", "x", tool="Read", arg="D:/secret/plan.md"),
              cwd="D:/secret")]
    out = tiny.render_tiny(make_snap(s))
    assert "D:/secret" not in out
