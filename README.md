# Agent 控制台（Windows 宿主机）

## 1. 装依赖并启动

```powershell
pip install fastapi "uvicorn[standard]" psutil nvidia-ml-py
python server.py
```

监听 `0.0.0.0:8787`。宿主机本地先打开 <http://localhost:8787> 确认能看到指标。

## 2. 放行防火墙（平板才连得上）

管理员 PowerShell 里跑一次：

```powershell
netsh advfirewall firewall add rule name="AgentDashboard" dir=in action=allow protocol=TCP localport=8787
```

然后 `ipconfig` 看局域网 IP，平板浏览器打开 `http://<IP>:8787`。

## 3. 接上 Claude Code

把 `hooks-snippet.json` 里的 `hooks` 字段合并进 `%USERPROFILE%\.claude\settings.json`。
注意是**合并**，不是覆盖 —— 如果文件里已有 `hooks`，把各事件数组拼进去。

改完后在 Claude Code 里用 `/hooks` 确认已加载。新开一个会话，控制台里应该立刻出现卡片。

**服务没启动时 hook 会失败但不会阻断 Claude Code** —— 这里只用了 `PostToolUse` / `Stop` /
`Notification` 这类事后事件，即使返回错误也无法回滚已发生的动作。刻意没用 `PreToolUse`，
避免仪表盘挂掉时拖慢或干扰工具调用。

## 4. 平板端

打开页面后点一次右上角 **开启提醒**。这一次点击做三件事：解锁音频（浏览器要求用户手势）、
申请通知权限、申请 Wake Lock。

**关于熄屏**：`http://192.168.x.x` 不是 secure context，`navigator.wakeLock` 和
`Notification` 在上面直接不可用，页面会静默降级（只剩声音+震动+视觉）。两个解法选一个：

- **省事**：平板 设置 → 开发者选项 → 打开「不锁定屏幕」，插着电就永不熄屏。
- **彻底**：宿主机上用 `mkcert` 给局域网 IP 签证书，平板装一次根 CA，改成 HTTPS。
  之后 Wake Lock、系统通知、PWA 安装到桌面全部可用。

## 5. 后续可以加的

- **手机兜底推送**：在 `hook()` 的 `waiting` 分支里加一行 POST 到 ntfy.sh / Bark，
  人不在平板前也能收到。
- **多机汇总**：`SESSIONS` 的 key 换成 `f"{hostname}:{session_id}"`，多台机器指向同一个服务。
- **其他 Agent**：`/hook/{event}` 这套协议是自定义的，别的 Agent 只要能发 webhook
  就能复用同一套 UI，不用改前端。
