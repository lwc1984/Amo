# 生成 LVGL 中文字库。改字号或加字后重跑，产物签入仓库，构建机无需 node。
#
# 底字用思源黑体 Source Han Sans SC Regular（开源，SIL Open Font License 1.1），
# 不用系统微软雅黑 —— 嵌进固件分发时没有授权顾虑。
# 下载来源：https://github.com/adobe-fonts/source-han-sans/releases
# 许可证全文：https://github.com/adobe-fonts/source-han-sans/blob/release/LICENSE.txt
#
# C 盘空间紧张（实测常年 <100MB 可用），npm 默认缓存在
# %LOCALAPPDATA%\npm-cache（C 盘）会导致 npx 安装失败，
# 所以这里强制把缓存重定向到 D 盘再跑。
$ErrorActionPreference = "Stop"

$env:npm_config_cache = "D:\Lvwenchao\geek\npm-cache"
New-Item -ItemType Directory -Force -Path $env:npm_config_cache | Out-Null

$fontDir = Join-Path $PSScriptRoot "..\components\agent_ui\fonts"
$ttf = Join-Path $PSScriptRoot "SourceHanSansSC-Regular.otf"

if (-not (Test-Path $ttf)) {
    Write-Host "请先下载思源黑体到 $ttf"
    Write-Host "https://github.com/adobe-fonts/source-han-sans/releases"
    exit 1
}

New-Item -ItemType Directory -Force -Path $fontDir | Out-Null

# 状态词用到的字：只有十几个，大字号也很小
$statusChars = "该你了干着呢摸鱼中没声儿连不上配对了找着还"

# 会话名可能是任意中文。完整 0x4E00-0x9FA5（~20900 字）在 4bpp 下会产出
# 数 MB 的 .c 文件，超出 3M app 分区的余量（固件本体约占 1M）。
# 改用 GB2312 一级常用字（3755 字，覆盖日常人名/地名/术语的绝大多数）
# 作为可复现的字符子集，体积落在 300-400KB 量级。
# 字表来源：GB 2312-1980 一级汉字表（按拼音排列，收录于本仓库同目录
# gb2312_level1.txt，一行一个字，UTF-8 无 BOM）。
$gb2312Level1Path = Join-Path $PSScriptRoot "gb2312_level1.txt"
$cjkSymbols = (Get-Content -Raw -Encoding UTF8 $gb2312Level1Path) -replace "\s", ""
$asciiRange = "0x20-0x7F"

Write-Host "生成 font_status_28（状态词专用，小字符集大字号）..."
npx lv_font_conv --font $ttf --size 28 --bpp 4 --format lvgl `
    --symbols $statusChars `
    --lv-font-name lv_font_status_28 `
    -o (Join-Path $fontDir "font_status_28.c")

Write-Host "生成 font_cjk_16（会话名与 detail，GB2312 一级字表 + ASCII）..."
npx lv_font_conv --font $ttf --size 16 --bpp 4 --format lvgl `
    --range "$asciiRange" --symbols $cjkSymbols `
    --lv-font-name lv_font_cjk_16 `
    -o (Join-Path $fontDir "font_cjk_16.c")

Write-Host "完成。产物已写入 $fontDir，记得一并提交。"
