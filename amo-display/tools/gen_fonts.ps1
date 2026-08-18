# 生成 LVGL 中文字库。改字号或加字后重跑，产物签入仓库，构建机无需 node。
#
# 底字用思源黑体 Source Han Sans SC Regular（开源，SIL Open Font License 1.1），
# 不用系统微软雅黑 —— 嵌进固件分发时没有授权顾虑。
# 下载来源：https://github.com/adobe-fonts/source-han-sans/releases
# 许可证全文：https://github.com/adobe-fonts/source-han-sans/blob/release/LICENSE.txt
#
$ErrorActionPreference = "Stop"

# npm 默认把缓存放在 %LOCALAPPDATA%（通常在系统盘）。本机系统盘曾只剩 16MB，
# 会让 npx 直接失败。所以：系统盘不足 1GB 时才改道到仓库旁边，否则用默认值 ——
# 硬编码到某个开发者的具体盘符/目录会让脚本在别的机器、别的 clone 上直接炸掉。
$sysDriveLetter = $env:SystemDrive.Substring(0, 1)
$freeGB = (Get-PSDrive -Name $sysDriveLetter).Free / 1GB
if ($freeGB -lt 1) {
    $env:npm_config_cache = Join-Path $PSScriptRoot "..\..\.npm-cache"
    New-Item -ItemType Directory -Force -Path $env:npm_config_cache | Out-Null
    Write-Host ("系统盘仅剩 {0:N2} GB，npm 缓存改道到 {1}" -f $freeGB, $env:npm_config_cache)
}

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

# 缺字兜底占位符。LVGL 对 cmap 里没有的码点什么都不画（没有方框、没有日志），
# 会话名里的繁体字/生僻姓氏/emoji 会变成一段看不见的空隙。规范的替换字符是
# U+FFFD，但思源黑体 SC 没有这个字形（用 fontTools 的 getBestCmap() 核实过）；
# 用 U+25A1（□ 白方块）代替 —— 思源黑体 SC 有这个字形。UI 层的实际替换逻辑
# 不在本脚本范围内，这里只保证字库里有一个能渲染出来的占位符可用。
$fallbackGlyph = [char]0x25A1
$cjkSymbols = $cjkSymbols + $fallbackGlyph
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
