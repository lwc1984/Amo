# Task 4 生成的中文字库。此刻还没有 UI 代码引用它们，但编进来能验证两件事：
# .c 能不能过编译、lv_font_t 的结构体布局与当前 LVGL 版本是否匹配 ——
# 这正是 Task 4 报告里列为"未验证"的那两项（当时没有可编译的工程）。
idf_component_register(
    SRCS "fonts/font_status_28.c" "fonts/font_cjk_16.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl
)

# lv_font_conv 产出的文件写的是 #ifdef LV_LVGL_H_INCLUDE_SIMPLE / "lvgl.h"
# / #else / "lvgl/lvgl.h"。IDF 组件管理器把 lvgl 装成 managed_components/lvgl__lvgl，
# 目录名不是 lvgl，所以 else 分支的 "lvgl/lvgl.h" 解析不到。定义这个宏走 simple 分支
# —— 这是 LVGL 官方给的机制，比改生成出来的文件好：重新跑 gen_fonts.ps1 不会丢。
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
