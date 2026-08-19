/* 横屏 320x170 的显示布局。
 *
 * 设计铁律（spec §7）：颜色只编码状态，不编码分类；waiting 是唯一允许"喊叫"
 * 的状态；断连绝不能渲染成正常。这三条在本文件里分别对应：色值表、只有
 * waiting 才启动呼吸动画、VS_NOLINK 有自己的颜色和文案而不是复用 idle。
 */
#include "ui.h"

#include <stdio.h>

#include "lvgl.h"
#include "ui_strings.h"

LV_FONT_DECLARE(lv_font_status_28);
LV_FONT_DECLARE(lv_font_cjk_16);

/* 与 tray.py:COLORS、static/index.html 的 CSS 变量逐值一致。
   改任何一个都必须四处同步改 —— 这是"颜色只编码状态"的落地方式。 */
#define C_RUN    lv_color_hex(0x3FBFD8)
#define C_WAIT   lv_color_hex(0xFFB020)
#define C_IDLE   lv_color_hex(0x4A5B78)
#define C_STALE  lv_color_hex(0xE2564A)
#define C_NOLINK lv_color_hex(0x1E3A5F)

#define W 320
#define H 170
#define BAR_W 76           /* 左侧色块宽度，约占 1/4 */
#define PAD   12

static lv_obj_t *s_bar;
static lv_obj_t *s_state;
static lv_obj_t *s_name;
static lv_obj_t *s_detail;
static lv_obj_t *s_host;
static lv_obj_t *s_peek;

static int s_last_state = -1;
static bool s_breathing = false;

static void breathe_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

static lv_color_t color_of(view_state_t st)
{
    switch (st) {
    case VS_WAITING: return C_WAIT;
    case VS_RUNNING: return C_RUN;
    case VS_STALE:   return C_STALE;
    case VS_IDLE:    return C_IDLE;
    default:         return C_NOLINK;
    }
}

static const char *label_of(view_state_t st)
{
    switch (st) {
    case VS_WAITING: return UI_S_WAITING;
    case VS_RUNNING: return UI_S_RUNNING;
    case VS_STALE:   return UI_S_STALE;
    case VS_IDLE:    return UI_S_IDLE;
    default:         return UI_S_NOLINK;
    }
}

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* 左侧色块 —— 从 2 米外能读到的就是它，文字是走近才看的。 */
    s_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, BAR_W, H);
    lv_obj_set_pos(s_bar, 0, 0);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_state = lv_label_create(scr);
    lv_obj_set_style_text_font(s_state, &lv_font_status_28, 0);
    lv_obj_set_pos(s_state, BAR_W + PAD, 14);
    lv_label_set_text(s_state, UI_S_NOHOST);

    s_name = lv_label_create(scr);
    lv_obj_set_style_text_font(s_name, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_name, lv_color_white(), 0);
    lv_obj_set_pos(s_name, BAR_W + PAD, 58);
    lv_obj_set_width(s_name, W - BAR_W - PAD * 2);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_name, "");

    s_detail = lv_label_create(scr);
    lv_obj_set_style_text_font(s_detail, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_detail, lv_color_hex(0x8899AA), 0);
    lv_obj_set_pos(s_detail, BAR_W + PAD, 84);
    lv_obj_set_width(s_detail, W - BAR_W - PAD * 2);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_detail, "");

    s_host = lv_label_create(scr);
    lv_obj_set_style_text_font(s_host, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_host, lv_color_hex(0x4A5670), 0);
    lv_obj_set_pos(s_host, BAR_W + PAD, H - 26);
    lv_label_set_text(s_host, "");

    /* 偷看标记：盯着这台时，别台有人在等也要能看见。
       放右下角，不抢主区域，但余光能扫到。 */
    s_peek = lv_label_create(scr);
    lv_obj_set_style_text_font(s_peek, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_peek, C_WAIT, 0);
    lv_obj_set_style_text_align(s_peek, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_peek, W - BAR_W - PAD * 2);
    lv_obj_set_pos(s_peek, BAR_W + PAD, H - 26);
    lv_label_set_text(s_peek, "");

}

void ui_render(const view_t *v)
{
    if (!v) {
        return;
    }

    /* 只有状态变了才动色块与动画。每秒重建会让呼吸从头开始，
       看起来就是"一直在闪第一下"。 */
    if ((int)v->state != s_last_state) {
        s_last_state = (int)v->state;

        lv_anim_del(s_bar, breathe_cb);
        lv_obj_set_style_bg_color(s_bar, color_of(v->state), 0);
        lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
        s_breathing = false;

        lv_obj_set_style_text_color(s_state, color_of(v->state), 0);
        lv_label_set_text(s_state, label_of(v->state));

        /* waiting 是唯一允许"喊叫"的状态。运行态也闪的话，
           等待就不再显眼了 —— 那才是真正需要你走过去的时刻。 */
        if (v->state == VS_WAITING) {
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_bar);
            lv_anim_set_exec_cb(&a, breathe_cb);
            lv_anim_set_values(&a, LV_OPA_40, LV_OPA_COVER);
            lv_anim_set_time(&a, 550);
            lv_anim_set_playback_time(&a, 550);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&a);
            s_breathing = true;
        }
    }

    if (v->state == VS_NOLINK) {
        lv_label_set_text(s_name, "");
        lv_label_set_text(s_detail, "");
        lv_label_set_text(s_host, "");
        lv_label_set_text(s_peek, "");
        return;
    }

    lv_label_set_text(s_name, v->name[0] ? v->name : UI_S_NOHOST);
    lv_label_set_text(s_detail, v->detail);
    lv_label_set_text(s_host, v->host);

    if (v->peer_needs_you) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s", v->peer_name, UI_S_WAITING);
        lv_label_set_text(s_peek, buf);
    } else {
        lv_label_set_text(s_peek, "");
    }
}
