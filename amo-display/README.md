# ESP32 端 —— LilyGo T-Display S3

ST7789 LCD 170×320，8 位 I8080 并口，无 PMU、无触摸、无 RGB 灯。

这块屏嵌在 3D 打印的 Claude 像素风小怪物的嘴部开口里（约 40×23mm），
所以 UI 不是一块"显示器"而是一张脸：五种状态各有嘴型，详见 spec §5.3。
硬件事实与三个软件坑的权威记录在
`docs/superpowers/specs/2026-08-18-agent-dashboard-design.md` §5.1。

> **型号修订**：本项目最初误判为 T-Display-AMOLED-Lite（SH8501 / QSPI / 194×368 /
> AXP2101），据此写的板级代码在实物上完全跑不起来。2026-08-19 实测确认是
> T-Display S3。若你在别处看到本项目提及 AMOLED、194×368、AXP2101、WS2812 或
> GPIO21，那都是修订前的残留。

## 构建

本机 IDF 源码与工具链分处两盘，必须同时设置：

```powershell
$env:IDF_PATH       = "C:\Users\CC\esp\v5.2\esp-idf"          # v5.2.3-378-g97bf63adde
$env:IDF_TOOLS_PATH = "D:\Lvwenchao\geek\esp\Espressif\frameworks\esp-idf-v5.2.1\tools"
& "$env:IDF_PATH\export.ps1"

cd amo-display
idf.py set-target esp32s3
idf.py build
```

IDF 组件管理器缓存固定在 `%LOCALAPPDATA%\Espressif\ComponentManager`，
无法改盘符，所以 **C 盘需要至少 3GB 可用空间**，否则 lvgl 下载会失败。

`idf.py --version` 打印的是 `v1.0.3` —— 那是 idf.py 工具自身的版本，不是 IDF 版本。
要确认 IDF 版本用 `git -C $env:IDF_PATH describe --tags`。

## 烧录与串口

```powershell
idf.py -p COM26 flash
```

**烧完必须拔插一次 USB。** 本板用芯片内置的 USB-Serial-JTAG，`idf.py flash` 收尾
那次经 RTS 的复位经常把芯片留在下载模式，表现为烧录成功但串口一个字节都不吐、
`idf.py monitor` 显示 `boot:0x2 (DOWNLOAD(USB/UART0))`。

也可以用脚本自己复位：保持 DTR 为低（IO0 高 = 正常启动）、脉冲 RTS（EN），
然后**关闭端口等 USB 重新枚举再重连** —— 复位芯片会把内置的 USB 设备一起复位，
攥着的句柄当场失效，这是"复位后读不到任何输出"的原因。

## 两个必须踩过才知道的构建坑

### `sdkconfig.defaults` 与 `partitions.csv` 必须是纯 ASCII

这两个文件由 IDF 的 Python 工具（`kconfgen`、`gen_esp32part.py`）读取，而它们用
**系统 locale 编码**打开文件 —— 中文 Windows 上就是 GBK。往里写 UTF-8 中文注释
会让构建在 configure 阶段直接炸：

```
UnicodeDecodeError: 'gbk' codec can't decode byte 0xb9 in position 10
CMake Error at .../kconfig.cmake:209 (message): Failed to run kconfgen
```

报错完全不提"你的注释是中文"，很容易被当成 IDF 装坏了。所以这两个文件里的注释
写英文，项目其余部分照旧用中文。

### 残留的 `build` 目录会卡住 `fullclean`

若上次构建是半途失败的，`idf.py fullclean` 会以 "doesn't seem to be a CMake build
directory" 拒绝执行，**并连带 `set-target` 一起失败**（后者把 fullclean 作为前置依赖）。
手动 `Remove-Item build -Recurse -Force` 即可。

## 看不到屏幕时怎么验证 UI

`tds3_dump_frame()` 把 LVGL 刷过的影子缓冲（PSRAM）以 base64 的 RGB565 吐到串口，
主机侧还原成 PNG。开发时人不在板子旁边、或者根本看不到屏幕时，这让
"UI 长什么样"从一个只能靠肉眼的问题变成**可自动检查**的问题。

```powershell
idf.py menuconfig     # Agent Display -> 开机后 dump 一帧屏幕内容到串口
```

配套的主机脚本在开发时用过两个版本：抓一帧（`grab.py`）和抓多帧（`grab_all.py`），
都是复位板子、读串口、找 `FRAME_BEGIN ... FRAME_END`、解 base64、拼 PNG。
它们没有签入仓库 —— 逻辑只有几十行，且高度依赖具体串口号。

它当场抓出过三个只看日志绝对发现不了的问题：颜色双重对调、中文一个字都不显示
（字库是压缩的但 `LV_USE_FONT_COMPRESSED` 没开，LVGL 静默不画）、标点全是缺字方框。

## UI 演示模式

```powershell
idf.py menuconfig     # Agent Display -> UI 演示模式：轮流展示五种状态
```

忽略真实会话，轮流展示五种状态。**idle 与 nolink 在真实环境里几乎凑不出来** ——
只要你还在敲代码就不会有 idle，nolink 要等真断网 —— 但它们同样需要被看过一眼。
配合上面的 dump 可以一次抓齐五张图。给人看这只小怪物时也用得上。

## 主机侧测试台

`host_test/` 里的纯逻辑测试不依赖 IDF，用主机 gcc 编译成可执行文件直接跑。
板子不在手边时这是唯一能验证的部分。

```bash
cd amo-display/host_test && make          # 需要 gcc 与 GNU make 在 PATH 上
```

本机用 w64devkit（gcc 16.2.0 + GNU Make 4.4.1，装在 `D:\tools\w64devkit`，
`bin` 已加入用户级 PATH）。**注意 gcc 需要该 bin 目录在 PATH 上才找得到汇编器 `as`**，
用绝对路径直接调用 gcc 会报 `cannot execute 'as'`。

IDF 工具链里的 `xtensa-esp32s3-elf-gcc` 是交叉编译器，产物不能在主机上跑，
不能拿来编测试台。

## 历史：LilyGo-Display-IDF 编译探针

修订前曾验证过 `Xinyuan-LilyGO/LilyGo-Display-IDF` 能否用现有的 IDF 5.2.3 编译，
结论是**可以**：官方 README 声明依赖 v5.3.0，但唯一的拦路石是子模块
`components/SensorLib/CMakeLists.txt:10` 一行过时的 `REQUIRES esp_driver_gpio`
（该组件 5.3 才从单体 `driver` 中拆出，而同一行已有 `driver`，5.2 下它就包含 GPIO）。
去掉那个词，整仓 1426 个目标全部编过。

**这个结论现在没有用了** —— 我们不再依赖那个仓库：ST7789 + i80 是 ESP-IDF 内置
支持，板级层没有任何 vendored 代码。记在这里只是为了避免有人重走一遍。

顺带一提，那个仓库的 T-Display S3 demo（`main/display_s3.c`）仍然是有价值的参考：
`components/tds3_board/` 里的厂商初始化表就是从它那里抄的。
