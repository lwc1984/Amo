# Agent 控制台（Windows 宿主机）

## 1. 装依赖并启动

```powershell
pip install -r requirements.txt
python server.py
```

监听 `0.0.0.0:8787`。宿主机本地先打开 <http://localhost:8787> 确认能看到指标。

## 2. 放行防火墙（平板才连得上）

管理员 PowerShell 里跑一次：

```powershell
netsh advfirewall firewall add rule name="AgentDashboard" dir=in action=allow protocol=TCP localport=8787
```

然后 `ipconfig` 看局域网 IP —— 光拿这个 IP 打开页面是看不到数据的：除 `/api/pair`
外，`/api/*` 都要令牌，见下方「配对设备」一节。

## 3. 接上 Claude Code

把 `hooks-snippet.json` 里的 `hooks` 字段合并进 `%USERPROFILE%\.claude\settings.json`。
注意是**合并**，不是覆盖 —— 如果文件里已有 `hooks`，把各事件数组拼进去。

改完后在 Claude Code 里用 `/hooks` 确认已加载。新开一个会话，控制台里应该立刻出现卡片。

**服务没启动时 hook 会失败但不会阻断 Claude Code** —— 这里只用了 `PostToolUse` / `Stop` /
`Notification` 这类事后事件，即使返回错误也无法回滚已发生的动作。刻意没用 `PreToolUse`，
避免仪表盘挂掉时拖慢或干扰工具调用。

## 配对设备

服务首次启动会在 `%APPDATA%\AgentDashboard\config.json` 生成一个令牌。
除 `/api/pair` 外，`/api/*` 都需要它，所以直接打开 `http://<IP>:8787` 是看不到数据的；
`/api/pair` 只在配对窗口开启的 60 秒内响应，不需要令牌。

- **平板**：在宿主机本地打开 <http://localhost:8787>，点右上角「配对平板」，用平板扫码。
  打开一次令牌就存进 localStorage，之后直接访问 IP 即可。
  二维码接口只对回环开放 —— 平板上打开这个页面看不到那个按钮，因为二维码里带着令牌，
  画给整个局域网看等于把令牌公开。托盘菜单的「复制平板地址」也还在，手敲用。
- **ESP32**：托盘菜单点「配对新设备（60 秒）」，然后长按板子上的 **GPIO14** 三秒。
  开机时按住 GPIO14 两秒可清空已配对记录（换宿主机或令牌失效时用）。

`/hook/*` 只接受本机来源，所以 `hooks-snippet.json` 里的地址必须是 `127.0.0.1`，
换成局域网 IP 会被 403。

## 4. 平板端

打开页面后点一次右上角 **叫醒我**（点过之后按钮变成 **盯着呢**）。这一次点击做三件事：
解锁音频（浏览器要求用户手势）、申请通知权限、申请 Wake Lock。**顶部的注意力条和标题闪烁
不受这次点击影响** —— 只要有会话进入等待状态就会亮，不用先点按钮；点按钮解锁的是声音、
震动和系统通知这三样。

**关于熄屏**：`http://192.168.x.x` 不是 secure context，`navigator.wakeLock` 和
`Notification` 在上面直接不可用，页面会静默降级（只剩声音+震动+视觉）。两个解法选一个：

- **省事**：平板 设置 → 开发者选项 → 打开「不锁定屏幕」，插着电就永不熄屏。
- **彻底**：宿主机上用 `mkcert` 给局域网 IP 签证书，平板装一次根 CA，改成 HTTPS。
  之后 Wake Lock、系统通知、PWA 安装到桌面全部可用。

## 5. ESP32 桌面显示器

固件在 `amo-display/`，构建与烧录见 `amo-display/README.md`。

板子是 **LilyGo T-Display S3**，嵌在 3D 打印的 Claude 像素风小怪物的嘴部开口里 ——
所以那块屏不是"显示器"而是一张脸：六种状态各有嘴型，颜色与本页面、托盘逐值一致。
设计见 `docs/superpowers/specs/2026-08-18-agent-dashboard-design.md` §5。

## 6. 后续可以加的

- **手机兜底推送**：在 `hook()` 的 `waiting` 分支里加一行 POST 到 ntfy.sh / Bark，
  人不在平板前也能收到。
- **多机场景不是汇总，是各跑各的**：每台宿主机各自起一个服务、各自广播 mDNS，
  平板 / ESP32 分别去配对想看的那几台，不做单一服务聚合多机数据。
- **其他 Agent**：`/hook/{event}` 这套协议是自定义的，别的 Agent 只要能发 webhook
  就能复用同一套 UI，不用改前端。
