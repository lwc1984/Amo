#pragma once
#include "view.h"

/* 建控件。必须在 tds3_lvgl_start() 之后调。 */
void ui_init(void);

/* 按视图刷新。幂等 —— 同一状态重复调用不重建控件，只改文字。
   每秒都会被调用，重建控件会让 LVGL 反复分配释放，也会让呼吸动画重新开始。 */
void ui_render(const view_t *v);
