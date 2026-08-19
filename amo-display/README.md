# ESP32 端

## 构建环境

本机 IDF 源码与工具链分处两盘，必须同时设置：

```powershell
$env:IDF_PATH       = "C:\Users\CC\esp\v5.2\esp-idf"          # v5.2.3-378-g97bf63adde
$env:IDF_TOOLS_PATH = "D:\Lvwenchao\geek\esp\Espressif\frameworks\esp-idf-v5.2.1\tools"
& "$env:IDF_PATH\export.ps1"
```

IDF 组件管理器缓存固定在 `%LOCALAPPDATA%\Espressif\ComponentManager`，
无法改盘符，所以 **C 盘需要至少 3GB 可用空间**，否则 lvgl 下载会失败。

`idf.py --version` 打印的是 `v1.0.3` —— 那是 idf.py 工具自身的版本，不是 IDF 版本。
要确认 IDF 版本用 `git -C $env:IDF_PATH describe --tags`。

## 主机侧测试台

`host_test/` 里的纯逻辑测试不依赖 IDF，用主机 gcc 编译成可执行文件直接跑：

```bash
cd amo-display/host_test && make          # 需要 gcc 与 GNU make 在 PATH 上
```

本机用 w64devkit（gcc 16.2.0 + GNU Make 4.4.1，装在 `D:\tools\w64devkit`，
`bin` 已加入用户级 PATH）。**注意 gcc 需要该 bin 目录在 PATH 上才找得到汇编器 `as`**，
用绝对路径直接调用 gcc 会报 `cannot execute 'as'`。

IDF 工具链里的 `xtensa-esp32s3-elf-gcc` 是交叉编译器，产物不能在主机上跑，
不能拿来编测试台。

## 官方仓库编译探针结论

**结论：现有的 IDF v5.2.3 可以编译官方仓库，不必升级到 5.3。**

`Xinyuan-LilyGO/LilyGo-Display-IDF`（`b1a1cc5`，含子模块）在 v5.2.3 下
`idf.py build` 通过，1426 个目标，`Project build complete`，exit 0。
产物 `lilygo_display_project.bin` 875,712 字节（855 KB），
默认 app 分区 1,048,576 字节，余 16%。

### 唯一的拦路石

官方 README 声明依赖 v5.3.0。开箱编译在 configure 阶段即失败：

```
CMake Error at .../tools/cmake/build.cmake:288 (message):
  Failed to resolve component 'esp_driver_gpio'.
```

来源是子模块 `components/SensorLib/CMakeLists.txt:10`：

```cmake
REQUIRES esp_timer esp_driver_gpio driver
```

`esp_driver_gpio` 是 IDF **5.3 才从单体 `driver` 组件里拆出来的**，5.2 中不存在。
而同一行已经 `REQUIRES driver`，在 5.2 下它就包含 GPIO —— 所以去掉
`esp_driver_gpio` 一个词，整个仓库即可编过。已实测。

这条 REQUIRES 是全仓库唯一的 5.3 依赖点：不涉及任何 5.3 独有的 C API，
因此"官方声明依赖 5.3"属于**它们在 5.3 上测的**，而非用了 5.3 专有能力。

### 对本项目的影响

我们不 fork 官方仓库，而是把用得到的板级代码裁进自己的 `components/lilygo_board`
组件（见计划 Task 3），那份 `CMakeLists.txt` 由我们自己写，天然不会带上这一行。
若后续为触摸功能引入 SensorLib，记得它的 REQUIRES 需要同样处理。

### 复现该探针

```powershell
cd D:\Lvwenchao\geek\esp-probe
git clone --recurse-submodules --depth 1 `
  https://github.com/Xinyuan-LilyGO/LilyGo-Display-IDF.git idf-ref
cd idf-ref
Copy-Item sdkconfig.defaults.t-amoled-lite sdkconfig.defaults -Force
# 5.2 下必须先去掉这个 5.3 才有的组件
(Get-Content components\SensorLib\CMakeLists.txt) `
  -replace 'REQUIRES esp_timer esp_driver_gpio driver', 'REQUIRES esp_timer driver' `
  | Set-Content components\SensorLib\CMakeLists.txt
idf.py set-target esp32s3
idf.py add-dependency "lvgl/lvgl^8.3.11"      # 实际解析到 8.4.0
idf.py reconfigure                             # 不能省，CMake 的组件列表首次 configure 即冻结
idf.py build
```

`--recurse-submodules` 不能省：`components/XPowersLib` 与 `components/SensorLib`
是子模块，缺了会报 `XPowersAXP2101.tpp: No such file or directory`，
看起来像编译器问题，其实是文件根本不在。

**残留的 `build` 目录会卡住 `fullclean`**：若上次构建是半途失败的，
`idf.py fullclean` 会以 "doesn't seem to be a CMake build directory" 拒绝执行，
连带 `set-target` 一起失败（它把 fullclean 作为前置依赖）。手动
`Remove-Item build -Recurse -Force` 即可。
