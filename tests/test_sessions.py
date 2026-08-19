import json

import phrases
import sessions


def ev(event, sid="s1", now=1000.0, **payload):
    payload.setdefault("session_id", sid)
    return sessions.apply_event(event, payload, now=now)


def test_key_includes_hostname():
    assert sessions.key("abc") == f"{sessions.HOST}:abc"


def test_session_start_is_idle():
    s = ev("session-start", cwd="D:/proj/Amo")
    assert s["state"] == "idle"
    assert s["phrase"] == phrases.SESSION_START
    assert s["host"] == sessions.HOST


def test_name_falls_back_to_cwd_dirname():
    s = ev("session-start", cwd="D:/proj/Amo")
    assert s["name"] == "Amo"


def test_session_title_wins_over_cwd():
    s = ev("session-start", cwd="D:/proj/Amo", session_title="恐龙公园初始化")
    assert s["name"] == "恐龙公园初始化"


def test_prompt_is_running():
    s = ev("prompt")
    assert s["state"] == "running"
    assert s["phrase"] == phrases.THINKING


def test_tool_stores_name_and_arg_separately():
    s = ev("tool", tool_name="Bash", tool_input={"command": "git push origin main"})
    assert s["state"] == "running"
    assert s["tool"] == "Bash"
    assert s["arg"] == "git push origin main"


def test_tool_arg_falls_back_through_fields():
    s = ev("tool", tool_name="Read", tool_input={"file_path": "D:/a/b.py"})
    assert s["arg"] == "D:/a/b.py"


def test_permission_notify_is_waiting():
    s = ev("notify", notification_type="permission_prompt", message="要跑这条命令吗")
    assert s["state"] == "waiting"
    assert s["phrase"] == "要跑这条命令吗"


def test_idle_notify_is_idle_not_waiting():
    """permission 与 idle 必须分开，混在一起会让告警变成狼来了。"""
    s = ev("notify", notification_type="idle_prompt", message="等着呢")
    assert s["state"] == "idle"


def test_notify_without_message_uses_default():
    s = ev("notify", notification_type="permission_prompt")
    assert s["phrase"] == phrases.WAITING_DEFAULT


def test_session_end_removes_session():
    ev("session-start")
    assert sessions.SESSIONS
    ev("session-end")
    assert not sessions.SESSIONS


def test_event_without_session_id_is_ignored():
    assert sessions.apply_event("prompt", {}) is None
    assert not sessions.SESSIONS


def test_tool_fields_cleared_on_non_tool_event():
    ev("tool", tool_name="Bash", tool_input={"command": "ls"})
    s = ev("prompt")
    assert s["tool"] == ""


def test_detail_full_vs_redacted():
    s = ev("tool", tool_name="Bash", tool_input={"command": "git push origin main"})
    assert sessions.detail(s, full=True) == "Bash: git push origin main"
    assert sessions.detail(s, full=False) == "Bash"


def test_detail_uses_phrase_when_no_tool():
    s = ev("prompt")
    assert sessions.detail(s, full=False) == phrases.THINKING


def test_stop_uses_transcript_summary(tmp_path):
    t = tmp_path / "t.jsonl"
    t.write_text(json.dumps({
        "type": "assistant",
        "message": {"content": [{"type": "text", "text": "改完了，测试全绿"}]},
    }) + "\n", encoding="utf-8")

    ev("session-start", transcript_path=str(t))
    s = ev("stop", transcript_path=str(t))
    assert s["phrase"] == "改完了，测试全绿"


def test_stop_without_transcript_falls_back():
    s = ev("stop")
    assert s["phrase"] == phrases.DONE


def test_tail_summary_missing_file_is_empty():
    assert sessions.tail_summary("D:/nope/nothing.jsonl") == ""
    assert sessions.tail_summary(None) == ""


def test_running_goes_busy_after_quiet_timeout():
    """安静一会儿不等于出事 —— 长时间构建就是最正常不过的事。

    PostToolUse 要等命令结束才触发，所以一次十分钟的构建期间，会话看起来
    和卡死一模一样。把这段时间标成「憋大招」而不是「没声儿了」，
    是为了让真正的失联仍然值得警觉。
    """
    ev("prompt", now=1000.0)
    snap = sessions.snapshot({}, now=1000.0 + sessions.QUIET_TIMEOUT + 1)
    assert snap["sessions"][0]["state"] == "busy"


def test_running_goes_stale_only_after_a_long_silence():
    ev("prompt", now=1000.0)
    snap = sessions.snapshot({}, now=1000.0 + sessions.STALE_TIMEOUT + 1)
    assert snap["sessions"][0]["state"] == "stale"


def test_busy_window_is_generous_enough_for_a_real_build():
    """阈值不是随手定的：十分钟的构建不该被判成失联。"""
    assert sessions.STALE_TIMEOUT >= 600
    assert sessions.QUIET_TIMEOUT < sessions.STALE_TIMEOUT


def test_idle_does_not_go_busy():
    """只有 running 会进入这两态；空闲本来就没事件。"""
    ev("session-start", now=1000.0)
    snap = sessions.snapshot({}, now=1000.0 + sessions.STALE_TIMEOUT + 1)
    assert snap["sessions"][0]["state"] == "idle"





def test_snapshot_sorts_waiting_first():
    ev("session-start", sid="a", cwd="D:/a")
    ev("prompt", sid="b", cwd="D:/b")
    ev("notify", sid="c", cwd="D:/c", notification_type="permission_prompt")

    states = [s["state"] for s in sessions.snapshot({}, now=1000.0)["sessions"]]
    assert states == ["waiting", "running", "idle"]


def test_snapshot_carries_host_and_full_detail():
    ev("tool", tool_name="Bash", tool_input={"command": "ls -la"})
    snap = sessions.snapshot({"cpu": 12}, now=1000.0)

    assert snap["host"] == sessions.HOST
    assert snap["metrics"] == {"cpu": 12}
    assert snap["sessions"][0]["detail"] == "Bash: ls -la"


def test_detail_collapses_newlines_in_arg():
    """多行脚本会把卡片撑爆，参数必须压成一行。"""
    s = ev("tool", tool_name="PowerShell",
           tool_input={"command": 'Set-Location "D:/x"\n$t = 1\n"done"'})
    assert "\n" not in sessions.detail(s, full=True)
    assert sessions.detail(s, full=True) == 'PowerShell: Set-Location "D:/x" $t = 1 "done"'


def test_detail_truncates_long_arg():
    s = ev("tool", tool_name="Bash", tool_input={"command": "x" * 200})
    out = sessions.detail(s, full=True)
    assert out.endswith("…")
    assert len(out) == len("Bash: ") + sessions.ARG_MAX + 1


def test_detail_short_arg_is_not_truncated():
    s = ev("tool", tool_name="Bash", tool_input={"command": "git status"})
    assert sessions.detail(s, full=True) == "Bash: git status"


def test_redacted_detail_unaffected_by_truncation():
    s = ev("tool", tool_name="Bash", tool_input={"command": "x" * 200})
    assert sessions.detail(s, full=False) == "Bash"
