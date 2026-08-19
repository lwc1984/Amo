/* 小怪物的嘴。
 *
 * 这块屏嵌在 3D 打印的 Claude 像素风小怪物的嘴部开口里（约 40x23mm），所以它
 * 不是一块"显示器"，而是一张脸。设计前提因此变了：
 *
 *   - 2 米外能读到的只有**嘴型和颜色**，文字必须走近看。这与 spec §7 里
 *     「6px 色条是核心，从 2 米读」是同一条原则，只是载体从色条换成了嘴。
 *   - 状态词不再单独显示 —— 嘴已经在表达这件事，再写一遍"干着呢"是重复。
 *     口腔里那两行是"它正在说的话"：会话名与 detail。
 *
 * 设计铁律（spec §7）在本文件的落地：
 *   - 颜色只编码状态：牙齿色取自同一张色表，与托盘/网页逐值一致。
 *   - waiting 是唯一允许"喊叫"的：只有它张大嘴 + 快速呼吸。别的状态再动，
 *     waiting 就不显眼了。
 *   - 断连绝不能渲染成正常：stale 完全静止（不动本身就是信号），
 *     nolink 用自己的深蓝和慢闪，都不复用 idle 的样子。
 */
#include "ui.h"

#include <stdio.h>

#include "lvgl.h"
#include "ui_strings.h"

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

/* 5 颗大方牙。少而大比多而细更像漫画里的怪物 —— 8 颗 34px 的看着更接近
   梳子或栅栏，牙缝多了反而削弱"这是一张嘴"的第一印象。
   间距 64，牙宽 54，牙缝 10，两侧各留 5 的余量。
   方块状不加圆角：小怪物本体是像素风的，圆角会破坏那个味道。 */
#define TOOTH_N     5
#define TOOTH_PITCH (W / TOOTH_N)
#define TOOTH_W     54
#define TOOTH_X0    5

/* 牙齿伸进屏幕的长度范围。上限 46 是算出来的：口腔至少要留下
   46..124 共 78 像素，才放得下两行 16px 的字加行距。 */
#define OPEN_MIN 12
#define OPEN_MAX 46

static lv_obj_t *s_top[TOOTH_N];
static lv_obj_t *s_bot[TOOTH_N];
static lv_obj_t *s_name;
static lv_obj_t *s_detail;
static lv_obj_t *s_peek;

static int s_last_state = -1;

/* stale 的参差牙形。固定图案而不是随机 —— 每帧都变会看成"在动"，
   而 stale 恰恰要靠"不动"来表达出了问题。 */
static const int STALE_JITTER[TOOTH_N] = {0, 11, -6, 14, -4};

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

/* 按开合度重排牙齿。v 是牙齿伸入的长度。 */
static void set_open(void *unused, int32_t v)
{
    (void)unused;
    if (v < 2) {
        v = 2;
    }
    for (int i = 0; i < TOOTH_N; i++) {
        int h = (int)v;
        if (s_last_state == VS_STALE) {
            h += STALE_JITTER[i];
            if (h < 4) {
                h = 4;
            }
        }
        lv_obj_set_height(s_top[i], h);
        lv_obj_set_height(s_bot[i], h);
        lv_obj_set_y(s_bot[i], H - h);
    }
}

static void make_tooth(lv_obj_t **slot, int idx, bool top)
{
    lv_obj_t *o = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 0, 0);          /* 像素风：不要圆角 */
    lv_obj_set_width(o, TOOTH_W);
    lv_obj_set_x(o, TOOTH_X0 + idx * TOOTH_PITCH);
    lv_obj_set_y(o, top ? 0 : H - 20);
    lv_obj_set_height(o, 20);
    *slot = o;
}

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);   /* 口腔内部 */
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);

    for (int i = 0; i < TOOTH_N; i++) {
        make_tooth(&s_top[i], i, true);
        make_tooth(&s_bot[i], i, false);
    }

    /* 口腔里的两行 —— 小怪物"正在说的话"。 */
    s_name = lv_label_create(scr);
    lv_obj_set_style_text_font(s_name, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_name, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_name, W - 24);
    lv_obj_set_pos(s_name, 12, 62);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_name, "");

    s_detail = lv_label_create(scr);
    lv_obj_set_style_text_font(s_detail, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_detail, lv_color_hex(0x8899AA), 0);
    lv_obj_set_style_text_align(s_detail, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_detail, W - 24);
    lv_obj_set_pos(s_detail, 12, 88);
    lv_label_set_long_mode(s_detail, LV_LABEL_LONG_DOT);
    lv_label_set_text(s_detail, "");

    /* 偷看标记：盯着这台时别台有人在等，也要能看见。
       放左上角牙缝下方，不抢口腔中央。 */
    s_peek = lv_label_create(scr);
    lv_obj_set_style_text_font(s_peek, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_peek, C_WAIT, 0);
    lv_obj_set_pos(s_peek, 12, 118);
    lv_label_set_text(s_peek, "");

    set_open(NULL, 30);
}

/* 每种状态一套动画参数。数值是"这张脸在说什么"的一部分，
   不是随便调的手感 —— 见文件头的设计说明。 */
static void animate_for(view_state_t st)
{
    lv_anim_del(NULL, set_open);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, set_open);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);

    switch (st) {
    case VS_RUNNING:
        /* 咀嚼：半开，中等速度开合。它在干活。 */
        lv_anim_set_values(&a, 24, 36);
        lv_anim_set_time(&a, 380);
        lv_anim_set_playback_time(&a, 380);
        lv_anim_start(&a);
        break;

    case VS_WAITING:
        /* 张到最大，快速开合。唯一允许喊叫的状态。
           幅度要和 running 拉开 —— 余光扫过时，形状比颜色先被认出来，
           两者只差颜色的话，等于把这条信号压回单一维度。 */
        lv_anim_set_values(&a, OPEN_MIN, OPEN_MIN + 6);
        lv_anim_set_time(&a, 260);
        lv_anim_set_playback_time(&a, 260);
        lv_anim_start(&a);
        break;

    case VS_IDLE:
        /* 几乎闭上，极慢地起伏 —— 像在打盹。 */
        lv_anim_set_values(&a, OPEN_MAX - 4, OPEN_MAX);
        lv_anim_set_time(&a, 2200);
        lv_anim_set_playback_time(&a, 2200);
        lv_anim_start(&a);
        break;

    case VS_STALE:
        /* 完全不动。参差的牙 + 静止，比任何动画都更像"出事了"。 */
        set_open(NULL, 32);
        break;

    default:
        /* 连不上：一条缝，慢闪。绝不复用 idle 的样子。 */
        lv_anim_set_values(&a, OPEN_MAX, OPEN_MAX - 6);
        lv_anim_set_time(&a, 1400);
        lv_anim_set_playback_time(&a, 1400);
        lv_anim_start(&a);
        break;
    }
}

void ui_render(const view_t *v)
{
    if (!v) {
        return;
    }

    /* 只有状态变了才换色重启动画。每秒重启会让呼吸永远停在第一下，
       看起来就是"一直在闪同一帧"。 */
    if ((int)v->state != s_last_state) {
        s_last_state = (int)v->state;

        lv_color_t c = color_of(v->state);
        for (int i = 0; i < TOOTH_N; i++) {
            lv_obj_set_style_bg_color(s_top[i], c, 0);
            lv_obj_set_style_bg_color(s_bot[i], c, 0);
        }
        animate_for(v->state);
    }

    if (v->state == VS_NOLINK) {
        lv_label_set_text(s_name, UI_S_NOLINK);
        lv_label_set_text(s_detail, "");
        lv_label_set_text(s_peek, "");
        return;
    }

    lv_label_set_text(s_name, v->name[0] ? v->name : UI_S_NOHOST);
    lv_label_set_text(s_detail, v->detail);

    if (v->peer_needs_you) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s %s", v->peer_name, UI_S_WAITING);
        lv_label_set_text(s_peek, buf);
    } else {
        lv_label_set_text(s_peek, "");
    }
}
