# Agent 控制台 · 三端设计

状态：已确认，待实施
日期：2026-08-18

---

## 0. 现状与要解决的问题

仓库里有一份从设计对话导出的骨架：`server.py`、`index.html`、`tray.py`、
`build.bat`、`hooks-snippet.json`、`agent_display.ino`。骨架的状态机和视觉语言是对的，
但有四个缺陷让它跑不起来或不能用：

1. `server.py` 服务 `static/index.html` 并挂载 `StaticFiles(directory=BASE/"static")`，
   而 `index.html` 在仓库根、`static/` 不存在 —— **服务器起不来**。
2. `agent_display.ino` 轮询 `GET /api/tiny`，服务端**没有实现这个端点**。
3. `server.py` 监听 `0.0.0.0:8787` **零鉴权**。同网段任何人 `curl /api/stream` 即可拿到
   全部会话的 cwd 完整路径、正在执行的 Bash 命令原文、以及从 transcript 抓取的助手摘要。
4. `agent_display.ino` 的 `BUZZER_PIN 2` 与本板 I2C SCL 冲突（见 §5.1），且头注释把屏驱
   写成 RM67162（实际是 SH8501B0）。照此烧录会破坏 AXP2101 与触摸所在的 I2C 总线。

本设计一次性解决上述四点，并把三端做到可用。

### 目标

- 宿主端服务可启动、有鉴权、对外接口稳定
- 平板页面完整可用，文案统一
- ESP32 固件可编译、可配对、可显示四态
- 多宿主机场景下，设备只显示自己配对过的主机

### 非目标（本轮明确不做）

- 手机兜底推送（ntfy / Bark）
- PWA 安装与 Service Worker
- mkcert / HTTPS
- 触摸交互（硬件有 CHSC5816，本轮不用，仅保留驱动）
- CPU 温度指标（Windows 下 psutil 拿不到，已在前期决策中放弃）

---

## 1. 总体架构

```
每台宿主机各自运行一份服务，互不知情：

  Claude Code ──hooks(仅回环)──> server.py ──┬── GET /            页面(免令牌)
                                             ├── GET /api/stream  SSE   (令牌)
                                             ├── GET /api/tiny    纯文本 (令牌)
                                             ├── POST /api/pair   配对窗口内发令牌
                                             └── mDNS 广播 _agentdash._tcp
                                                   TXT: host / id / v

  ESP32 ──NVS 里存着 N 组 (host_id, token)──> 逐个轮询已配对主机
        显示当前选中的一台，其余有事时在右边缘出标记

  平板  ──URL 里带令牌，存 localStorage──> 单台主机的完整视图
```

信任边界是**令牌**：设备只能读取自己配对过的主机。同事的机器在 mDNS 里发现得到，
但拿不到令牌，因此不会出现在你的设备上；反之亦然。

---

## 2. 信任模型与隐私

### 2.1 令牌

- 首次启动生成 `secrets.token_hex(16)`（32 个十六进制字符），连同 `host_id` 持久化到
  `%APPDATA%\AgentDashboard\config.json`
- `host_id` = `uuid4().hex[:8]`，用于设备在 IP 变化后仍能认出同一台主机
- 校验用 `secrets.compare_digest`，避免时序侧信道
- 令牌可通过 `?k=<token>` 查询参数或 `X-Agent-Key` 请求头提供

配置文件结构：

```json
{
  "token": "7f3a9b2c1d4e5f60718293a4b5c6d7e8",
  "host_id": "a1b2c3d4",
  "tiny_detail": "tool"
}
```

### 2.2 端点与鉴权矩阵

| 端点 | 鉴权 | 来源限制 | 说明 |
|---|---|---|---|
| `GET /` | 无 | 任意 | 页面外壳；无令牌时页面自身显示"需要配对" |
| `GET /static/*` | 无 | 任意 | 静态资源 |
| `GET /api/stream` | 令牌 | 任意 | SSE，完整数据 |
| `GET /api/tiny` | 令牌 | 任意 | 纯文本，默认脱敏 |
| `POST /api/pair` | 无 | 任意 | **仅配对窗口开启的 60 秒内**返回令牌，否则 403 |
| `POST /hook/{event}` | 无 | **仅回环** | 非 `127.0.0.1` / `::1` 一律 403 |

`/hook/*` 限制回环是必须的：否则同网段任何人都能 POST 伪造会话污染面板，
甚至构造 `transcript_path` 诱导服务端读取任意文件。

### 2.3 配对流程

1. 用户在托盘菜单点击"配对新设备" → 服务端置 `PAIRING_UNTIL = now + 60`
2. 设备（ESP32 长按 GPIO21 / 平板打开托盘复制的 URL）向 `POST /api/pair` 请求
3. 窗口内则返回 `{"token": ..., "host_id": ..., "host": ...}`，窗口外返回 403
4. **每次配对成功弹一次托盘气泡**，写明配对方 IP —— 若有人趁窗口偷配，用户能立刻看见
5. 窗口 60 秒后自动关闭；期间允许多台设备配对（平板 + ESP32 一次搞定）

### 2.4 mDNS 广播内容

服务类型 `_agentdash._tcp.local.`，TXT 记录**只含**：

```
v=1
host=WORKSTATION
id=a1b2c3d4
```

不含任何会话名、路径、命令或计数。未配对方从广播中只能知道"这台机器上跑着一个 Agent 控制台"。

### 2.5 内容脱敏分级

`hook()` 分别记录工具名与参数，而不是拼好的字符串：

```python
s["tool"] = "Bash"
s["arg"]  = "git push origin main"
# 展示时组合
detail_full  = "Bash: git push origin main"
detail_short = "Bash"
```

| 终端 | detail | cwd |
|---|---|---|
| 平板 `/api/stream` | 完整 | 完整路径 |
| ESP32 `/api/tiny` | 仅工具名 | 不下发 |
| ESP32 `?d=full` | 完整 | 不下发 |

理由：平板是你凑近看的，ESP32 是桌上公开展示的，别人走过就能看见。

### 2.6 明确不防的

- 已配对设备被物理拿走 —— NVS 里的令牌可被读出。这是桌面小玩具，不做安全存储
- 同网段的被动流量嗅探 —— 明文 HTTP。要防需上 HTTPS，本轮非目标
- 宿主机本地的其他进程 —— 能读配置文件即能读令牌

---

## 3. 服务端 `server.py`

### 3.1 静态资源落地

- 新建 `static/`，`index.html` 移入
- 新增资源解析，兼容 PyInstaller `--noconsole` 打包后解压到 `sys._MEIPASS` 的情形：

```python
def resource_path(rel: str) -> Path:
    return Path(getattr(sys, "_MEIPASS", Path(__file__).parent)) / rel

STATIC = resource_path("static")
```

### 3.2 会话结构

`SESSIONS` 的 key 由 `session_id` 改为 `f"{HOST}:{session_id}"`，`HOST = socket.gethostname()`。
本轮虽是单机各跑，但字段现在就要有 —— ESP32 屏上要显示当前盯的是哪台。

```python
SESSIONS["WORKSTATION:abc123"] = {
    "id": "abc123", "host": "WORKSTATION",
    "name": ..., "cwd": ..., "state": ...,
    "tool": ..., "arg": ..., "phrase": ...,   # phrase = 非工具事件的固定文案
    "summary": ..., "transcript": ...,
    "started": ..., "last_event": ...,
}
```

### 3.3 状态机

沿用现有语义，仅把 detail 拆成 tool/arg：

状态词（"该你了"等）由 state 决定，见 §6；下表的第三列是 detail 那一行的内容，两者互不重叠。

| 事件 | state | detail 内容 |
|---|---|---|
| `session-start` | idle | 固定文案 |
| `prompt` | running | 固定文案 |
| `tool` | running | `tool` + `arg`（取 `command`/`file_path`/`pattern` 首个非空） |
| `notify` (permission) | **waiting** | hook 的 `message` |
| `notify` (idle) | idle | hook 的 `message` |
| `stop` | idle | transcript 尾部抓取的摘要 |
| `session-end` | — | 移除该会话 |

`snapshot()` 不变：`IDLE_TIMEOUT`(90s) 内无事件的 running 标记为 `stale`，
排序 waiting → running → idle → stale。排序是 `/api/tiny` 的前提 —— 取第一条天然就是最该显示的。

### 3.4 `/api/tiny` 格式

给 MCU 的纯文本，三行，不用 JSON：

```
1|3,1,2|WORKSTATION
恐龙公园初始化\tBash
12,41,3,8
```

- 第 1 行：`版本|waiting,running,idle|主机名`
- 第 2 行：`会话名\tdetail`（无会话时为空行）
- 第 3 行：`cpu,mem,gpu,net_kb`（无 GPU 时 gpu = `-1`）

按行切分再按分隔符切，比嵌套 `|` 好解析，且版本号留了前向兼容余地。
`?d=full` 时第 2 行的 detail 给完整参数。

### 3.5 mDNS 广播

用 `zeroconf` 注册 `_agentdash._tcp.local.`，TXT 内容见 §2.4。
需处理 Windows 多网卡：绑定到实际局域网接口，避免广播到 Hyper-V / WSL 虚拟网卡。
服务退出时注销。

### 3.6 依赖变更

新增 `zeroconf`；测试需 `pytest` + `httpx`。
当前机器已装 `psutil` / `Pillow` / `pyinstaller`，缺 `fastapi` / `uvicorn` /
`pystray` / `nvidia-ml-py` / `zeroconf`。

### 3.7 托盘 `tray.py`

- 菜单新增「配对新设备」，点击开启 60 秒窗口
- 「复制平板地址」改为复制带令牌的完整 URL：`http://<ip>:8787/?k=<token>`
- 配对成功时弹气泡，写明配对方 IP
- 气泡文案换成统一口语（§6）
- 保留 `log_config=None, access_log=False`（`--noconsole` 下 `sys.stdout` 为 `None`，
  uvicorn 默认 logging config 会崩）

---

## 4. 页面端 `static/index.html`

视觉语言不动 —— 顶部 6px 注意力条、左侧色条、四色语义都是对的。改三处：

1. 全部文案换成 §6 的口语版本
2. 会话卡片增加机器名一行（`host · cwd`）
3. 令牌处理：从 `?k=` 读取并存 `localStorage`；无令牌时不发起 SSE，
   显示"还没配对。在宿主机托盘里点『配对新设备』，然后从那儿复制地址过来。"

`http://192.168.x.x` 不是 secure context，`navigator.wakeLock` 与 `Notification` 直接不存在，
页面现有的静默降级逻辑（声音 + 震动 + 标题闪烁）保持不变。

---

## 5. ESP32 固件 `amo-display/`

### 5.1 硬件事实（来自 LilyGo-Display-IDF `main/product_pins.h`）

T-Display-AMOLED-Lite，ESP32-S3R8 / 16MB Flash / 8MB OPI PSRAM：

| 项 | 值 |
|---|---|
| 屏驱 | **SH8501B0**（初始化序列 `sh8501_cmd`），194×368 |
| QSPI | CS=4, SCK=5, D0=7, D1=10, D2=11, D3=12, RST=40, TE=6, 30MHz |
| I2C | SDA=1, SCL=2 |
| PMU | AXP2101（I2C），IRQ=3 |
| 触摸 | CHSC5816（I2C），IRQ=13, RST=14 |
| 按键 | GPIO0（BOOT，strapping，不用）、**GPIO21** |
| RGB LED | WS2812 ×1，GPIO18 |
| 蜂鸣器 | **无** |

`agent_display.ino` 的 `BUZZER_PIN 2` 正是 I2C SCL —— 必须去掉。

### 5.2 工程结构

从 `LilyGo-Display-IDF` 裁剪（其 `main/` 塞了 16 块板的驱动，只保留本板路径）：

```
amo-display/
├─ components/
│  ├─ lilygo_board/      QSPI 面板 + 初始化序列 + AXP2101 + WS2812
│  │                     （裁剪自官方，依赖 XPowersLib）
│  └─ agent_ui/          状态机 + LVGL 横屏布局 + 中文字库
│     └─ fonts/          生成的 .c 字库，签入仓库
├─ main/main.c           WiFi → mDNS 发现 → 轮询 → 按键
├─ sdkconfig.defaults    照搬官方 t-amoled-lite
├─ Kconfig.projbuild     WiFi 凭据 / 蜂鸣器 GPIO / detail 级别
└─ tools/gen_fonts.ps1   字库再生脚本
```

不使用官方 `setup.py`（它用 `os.system` + 交互式菜单，且仅在 Ubuntu 测过），
直接手工放 `sdkconfig.defaults` 并 `idf.py add-dependency "lvgl/lvgl^8.3.11"`。

### 5.3 显示布局（横屏 368×194，`setRotation(1)`）

**等待态** —— 唯一允许"喊叫"的状态：

```
┌────────┬────────────────────────────────┐
│████████│  该你了                        │  ← 28px 中文，琥珀
│████████│  恐龙公园初始化                │  ← 16px 中文，白
│███ ⚠ ██│  Bash                          │  ← 16px，暗琥珀
│████████│  ────────────────────          │
│████████│  · 机械时代本地化  干着呢      │  ← 12px 次条
└────────┴────────────────────────────────┘
   88px                            LAPTOP ⚠ ← 右边缘 peek 标记
```

亮度 255 + 色块呼吸；WS2812 琥珀色呼吸。

**运行态**：左侧 6px 青色竖条，右侧状态词 / 会话名 / detail / 次条。亮度 60，LED 灭。

**空闲态**：左侧 6px 暗条，左半大数字（会话数 48px）+「摸鱼中」，
右半 2×2 系统指标（cpu / mem / gpu / net，14px 暗灰）。亮度 20，LED 灭。
AMOLED 黑像素不通电，此态功耗接近熄屏。

**断连态**：慢速蓝 `没声儿了`。绝不静默伪装成正常。

### 5.4 交互

| 操作 | 行为 |
|---|---|
| GPIO21 短按 | 在已配对主机间循环切换 |
| GPIO21 长按 3s | 进入配对扫描：mDNS 查 `_agentdash._tcp`，向开着窗口的主机索取令牌 |

多主机"偷看"：后台每秒轮询**所有**已配对主机（数量小，S3 完全吃得住），
屏上显示当前选中的一台；若另一台已配对主机 waiting > 0，右边缘显示其主机名 + ⚠。
这样切换交互仍是手动的，但不会因为盯错机器而错过提醒。

### 5.5 告警（无蜂鸣器）

- WS2812 琥珀色呼吸 + 屏幕拉到 255
- **仅在状态跳变进入 waiting 时**触发一次，不在 waiting 持续期间反复
- 可选外接无源蜂鸣器：`CONFIG_AGENT_BUZZER_GPIO`，默认 `-1`（关闭），建议接 GPIO17（空闲脚）

### 5.6 中文字库

`npx lv_font_conv` 生成两份，产物 `.c` 签入仓库（构建机不需要 node）：

| 字库 | 字号 | 字符集 | 估算 |
|---|---|---|---|
| `font_status_28` | 28px | 状态词与提示用字，约 30 字 | ~30KB |
| `font_cjk_16` | 16px | GB2312 常用 3500 字 + ASCII | ~300KB |

底字用**思源黑体**（开源），不用系统微软雅黑 —— 嵌进固件分发时没有授权顾虑。
`tools/gen_fonts.ps1` 保存生成命令，便于改字号后重跑。

### 5.7 防烧屏

长期静态显示有 image retention 风险。空闲态几乎全黑天然规避大部分，
运行态每 60 秒整体偏移 1px（沿用骨架里的 `shift_task`）。

---

## 6. 三端统一文案

颜色只编码状态、不编码分类；文案同理，一套走三端。

| 状态 | 平板 / 托盘 | ESP32（窄，用短的） | 颜色 |
|---|---|---|---|
| waiting | 哥们儿，该你了 | 该你了 | `#FFB020` 琥珀 |
| running | 干着呢，别催 | 干着呢 | `#3FBFD8` 青 |
| idle | 摸鱼中，等你发话 | 摸鱼中 | `#4A5B78` 灰 |
| stale | 没声儿了，人呢？ | 没声儿了 | `#E2564A` 红 |

其他位置：

- 空列表：`一个会话都没有。随便找个项目敲 claude 就出来了。`
- 连接状态：`连上了` / `断了，正重连`
- 提醒按钮：`叫醒我` → 已开启后 `盯着呢`
- 未配对：`还没配对。在宿主机托盘里点『配对新设备』，然后从那儿复制地址过来。`
- 托盘悬浮：`3 个会话 · 摸鱼中` / `哥们儿，该你了：恐龙公园初始化`

---

## 7. 视觉与告警规范（跨端不变量）

改任意一条都必须三端同步：

1. **颜色只编码状态，不编码分类。** 四色在三块屏上含义完全一致
2. **waiting 是唯一允许"喊叫"的状态。** 其余保持安静；若运行态也在闪，等待就不再醒目
3. **`permission_prompt` 与 `idle_prompt` 必须分开。** 前者是等你批准（告警），
   后者是干完活等你说话（空闲）。混在一起会让告警变成狼来了
4. **顶部色条是设计核心而非装饰。** 平板 6px 横贯全宽，ESP32 3px；供两米外余光识别
5. **detail 是"当前在干嘛"，不是摘要。** 只有 `Stop` 之后才换成摘要文本
6. **断连绝不能渲染成正常。** 三端分别是 `断了，正重连` / `没声儿了` / `stale` 标红
7. **告警在状态跳变时触发一次，不在状态持续期间反复**

---

## 8. 测试策略

### 服务端（pytest + httpx TestClient）

- 状态机：各 hook 事件 → 期望的 state 与 tool/arg
- `session-end` 移除会话；`IDLE_TIMEOUT` 后 running → stale
- `snapshot()` 排序为 waiting → running → idle → stale
- 鉴权：`/api/*` 无令牌 401、错令牌 401、对令牌 200
- 来源限制：`/hook/*` 非回环 403
- 配对：窗口外 `POST /api/pair` 403；窗口内返回令牌；60 秒后自动失效
- `/api/tiny`：三行格式、无会话时的空行、gpu 缺失时为 `-1`、默认脱敏、`?d=full` 给完整参数

### ESP32

**只能验证到编译通过。** 板子当前未连接（串口仅有主板 COM1），
显示效果、触摸、PMU、配对流程都需要插上板子后由用户实机验证。

---

## 9. 落地顺序

1. 服务端：令牌 / 鉴权 / 回环限制 / `static/` / `/api/tiny` / mDNS + 测试
2. 页面端：移入 `static/`、令牌处理、文案、机器名字段
3. 托盘：配对菜单、带令牌的 URL、气泡文案
4. ESP32：先用现有 IDF 5.2.1 试编译官方仓库（十分钟），不成再升 5.3；
   然后裁剪工程、写 UI 与轮询、配对与按键
5. 字库生成并签入
6. 实机验证（需用户插板）

---

## 10. 已知风险

| 风险 | 处置 |
|---|---|
| IDF 5.2.1 能否编译官方仓库未知（官方要求 5.3.0） | 先花十分钟试；不成则升级到 5.3 |
| 官方仓库仅在 Ubuntu 22.04 测过，Windows 未测 | 不使用其 `setup.py`，手工配置 |
| 板子未连接，实机行为全部未验证 | 明确标注；等用户插板后再验 |
| zeroconf 在多网卡 Windows 上可能广播到虚拟网卡 | 显式绑定局域网接口 |
| 令牌明文存于 NVS 与配置文件 | 已在 §2.6 声明为不防范围 |
| 思源黑体需下载 | 生成产物签入仓库，仅首次需要网络 |
