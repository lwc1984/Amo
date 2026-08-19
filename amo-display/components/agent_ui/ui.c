/* 小怪物的嘴。
 *
 * 这块屏嵌在 3D 打印的 Claude 像素风小怪物的嘴部开口里（约 40x23mm），所以它
 * 不是一块"显示器"，而是一张脸。设计前提因此变了：2 米外能读到的只有嘴型和
 * 颜色，文字必须走近看 —— 与 spec §7 的「6px 色条是核心，从 2 米读」同一条
 * 原则，只是载体从色条换成了嘴。
 *
 * 状态词保留。嘴型确实也在表达状态，但嘴型要学、文字不用学：第一次看见参差的
 * 红牙，没人知道那叫「没声儿了」。文字是那个锚点，学会之后才轮到嘴型在余光里
 * 独立工作。
 *
 * 两套排布：
 *   张嘴态（干活/等你/失联）—— 上下两排牙，文字在口腔里
 *   闭嘴态（摸鱼/连不上）  —— 一条唇线，文字在唇线上下
 * 嘴闭上了就不该还看见牙，所以闭嘴态换成唇线而不是"两排快碰上的牙"。
 *
 * 设计铁律（spec §7）在本文件的落地：
 *   - 颜色只编码状态：色值与托盘/网页逐值一致。
 *   - waiting 是唯一允许"喊叫"的：只有它张到最大 + 快速开合。
 *   - 断连绝不能渲染成正常：stale 完全静止（不动本身就是信号），
 *     nolink 是断开的虚线（不是起伏的波浪），都不复用 idle 的样子。
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
/* 计划里定的是 0x1E3A5F，实测在 40x23mm 上暗到几乎看不见 —— 而「断连绝不能
   渲染成正常」的前提是它得先被看见。提亮到同色系的 0x3A6EA5，仍是冷蓝，
   不与 run 的青蓝混淆。 */
#define C_NOLINK lv_color_hex(0x3A6EA5)

#define W 320
#define H 170

/* 5 颗大方牙。少而大更像漫画里的怪物 —— 8 颗细牙看着接近梳子或栅栏，
   牙缝多了反而削弱"这是一张嘴"的第一印象。
   方块不加圆角：小怪物本体是像素风的，圆角会破坏那个味道。 */
#define TOOTH_N     5
#define TOOTH_PITCH (W / TOOTH_N)
#define TOOTH_W     54
#define TOOTH_X0    5

/* 牙齿伸入长度。上限 46 是算出来的：口腔要留下 46..124 共 78 像素，
   才放得下 28px 状态词加两行 16px 的字。 */
#define OPEN_MIN 12
#define OPEN_MAX 46

/* 闭嘴态的唇线：城墙状的直角波，从左往右慢速滚动。
 *
 * 不用正弦：像素风里本来就不该有曲线，城垛的直角才是这套视觉语言里的"波浪"。
 * 32 段各 10 像素、彼此相接不留缝（城墙是连续的），每 3 段一个高度，
 * 于是 60 像素一个周期。相位按整段推进 —— 一格一格地挪，正是像素动画的样子。 */
#define WAVE_N     32
#define WAVE_PITCH (W / WAVE_N)
#define WAVE_W     WAVE_PITCH
/* 方块高度必须 >= 2*WAVE_AMP，否则高低两排接不上，看着是两排虚线而不是
   一道连续的城墙。22 > 20，留 2 像素重叠保证没有缝。 */
#define WAVE_H     22
#define WAVE_AMP   10
#define WAVE_GROUP 3
#define WAVE_MID   ((H - WAVE_H) / 2)

static lv_obj_t *s_top[TOOTH_N];
static lv_obj_t *s_bot[TOOTH_N];
static lv_obj_t *s_wave[WAVE_N];
static lv_obj_t *s_state;
static lv_obj_t *s_name;
static lv_obj_t *s_detail;
static lv_obj_t *s_peek;
static lv_obj_t *s_count;

static int s_last_state = -1;

/* stale 的参差牙形。固定图案而不是随机 —— 每帧都变会看成"在动"，
   而 stale 恰恰要靠"不动"来表达出了问题。 */
static const int STALE_JITTER[TOOTH_N] = {0, 11, -6, 14, -4};

static bool is_closed_mouth(view_state_t st)
{
    return st == VS_IDLE || st == VS_NOLINK;
}

static lv_color_t color_of(view_state_t st)
{
    switch (st) {
    case VS_WAITING: return C_WAIT;
    case VS_RUNNING: return C_RUN;
    /* busy 与 running 同色。颜色要回答的是"要不要走过去"，两者答案相同：
       不用管它。给它第五种颜色只会让 2 米外那道信号变模糊。 */
    case VS_BUSY:    return C_RUN;
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
    case VS_BUSY:    return UI_S_BUSY;
    case VS_STALE:   return UI_S_STALE;
    case VS_IDLE:    return UI_S_IDLE;
    default:         return UI_S_NOLINK;
    }
}

/* 张嘴幅度。v 是牙齿伸入的长度。 */
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

/* nolink 的慢闪。stale 是冻住不动，nolink 是还在重试 —— 两者都不正常，
   但不正常的方式不同，视觉上必须能分开。 */
static void set_wave_opa(void *unused, int32_t opa)
{
    (void)unused;
    for (int i = 0; i < WAVE_N; i++) {
        lv_obj_set_style_bg_opa(s_wave[i], (lv_opa_t)opa, 0);
    }
}

/* 城垛相位推进。phase 以"段"为单位，一个周期 2*WAVE_GROUP 段。
   flat 为真时压平成一条直线（连不上用），此时相位无意义。 */
static void set_wave_at(int32_t phase, bool flat)
{
    for (int i = 0; i < WAVE_N; i++) {
        int dy = 0;
        if (!flat) {
            int idx = (i + (int)phase) / WAVE_GROUP;
            dy = (idx % 2) ? WAVE_AMP : -WAVE_AMP;
        }
        lv_obj_set_y(s_wave[i], WAVE_MID + dy);
    }
}

static void set_wave(void *unused, int32_t phase)
{
    (void)unused;
    set_wave_at(phase, false);
}

static lv_obj_t *make_block(lv_obj_t *scr, int w, int h, int x, int y)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    return o;
}

static lv_obj_t *make_label(lv_obj_t *scr, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(scr);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, W - 24);
    lv_obj_set_x(l, 12);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, "");
    return l;
}

void ui_init(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);   /* 口腔内部 */
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);

    for (int i = 0; i < TOOTH_N; i++) {
        s_top[i] = make_block(scr, TOOTH_W, 20, TOOTH_X0 + i * TOOTH_PITCH, 0);
        s_bot[i] = make_block(scr, TOOTH_W, 20, TOOTH_X0 + i * TOOTH_PITCH, H - 20);
    }
    for (int i = 0; i < WAVE_N; i++) {
        s_wave[i] = make_block(scr, WAVE_W, WAVE_H, i * WAVE_PITCH + 1, WAVE_MID);
        lv_obj_add_flag(s_wave[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_state  = make_label(scr, &lv_font_status_28, C_IDLE);
    s_name   = make_label(scr, &lv_font_cjk_16, lv_color_white());
    s_detail = make_label(scr, &lv_font_cjk_16, lv_color_hex(0x8899AA));

    /* 会话总数。板子只显示一条会话（宿主端按状态优先级挑出来的那条），
       但五个会话里只看见最要紧那条，会让人以为就这一个。
       放在状态词那一行的右端：状态词居中，右侧本来就是空的；
       原先放右下角会被下排牙齿压住，牙齿越长压得越多。 */
    s_count = lv_label_create(scr);
    lv_obj_set_style_text_font(s_count, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_count, lv_color_hex(0x5A6880), 0);
    lv_obj_set_style_text_align(s_count, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(s_count, 84);
    lv_obj_set_x(s_count, W - 92);
    lv_label_set_text(s_count, "");

    /* 偷看标记：盯着这台时别台有人在等，也要能看见。左下角，不抢中央。 */
    s_peek = lv_label_create(scr);
    lv_obj_set_style_text_font(s_peek, &lv_font_cjk_16, 0);
    lv_obj_set_style_text_color(s_peek, C_WAIT, 0);
    lv_obj_set_pos(s_peek, 10, H - 22);
    lv_label_set_text(s_peek, "");

    set_open(NULL, 30);
}

/* 两套排布。张嘴态文字挤在口腔里，闭嘴态文字分在唇线上下 ——
   唇线在正中，文字压上去会糊成一团。 */
static void layout_for(view_state_t st)
{
    if (is_closed_mouth(st)) {
        lv_obj_set_y(s_state, 18);
        lv_obj_set_y(s_count, 26);
        lv_obj_set_y(s_name, 126);
        lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);   /* 闭着嘴，没什么在说 */
    } else {
        lv_obj_set_y(s_state, 40);
        lv_obj_set_y(s_count, 48);
        lv_obj_set_y(s_name, 80);
        lv_obj_set_y(s_detail, 102);
        lv_obj_clear_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < TOOTH_N; i++) {
        if (is_closed_mouth(st)) {
            lv_obj_add_flag(s_top[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_bot[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_top[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_bot[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (int i = 0; i < WAVE_N; i++) {
        if (is_closed_mouth(st)) {
            lv_obj_clear_flag(s_wave[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wave[i], LV_OBJ_FLAG_HIDDEN);
        }
        /* 连不上：按组隐藏，成为断开的虚线（32 段太密，隔一段藏一段会糊成一片）。
           与 idle 的连续城垛区分开 —— 同样是闭嘴，一个在打盹，一个是断了。 */
        if (st == VS_NOLINK && ((i / WAVE_GROUP) % 2)) {
            lv_obj_add_flag(s_wave[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* 每种状态一套动画参数。数值是"这张脸在说什么"的一部分，不是随便调的手感。 */
static void animate_for(view_state_t st)
{
    lv_anim_del(NULL, set_open);
    lv_anim_del(NULL, set_wave);
    lv_anim_del(NULL, set_wave_opa);
    set_wave_opa(NULL, LV_OPA_COVER);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);

    switch (st) {
    case VS_RUNNING:
        /* 咀嚼：半开，中速开合。它在干活。 */
        lv_anim_set_exec_cb(&a, set_open);
        lv_anim_set_values(&a, 24, 36);
        lv_anim_set_time(&a, 380);
        lv_anim_set_playback_time(&a, 380);
        lv_anim_start(&a);
        break;

    case VS_BUSY:
        /* 慢而深的咀嚼。颜色和 running 一样，靠节奏区分 ——
           一口咬得久，像在啃硬东西。1.1 秒一开合，是 running 的三倍。 */
        lv_anim_set_exec_cb(&a, set_open);
        lv_anim_set_values(&a, 20, 40);
        lv_anim_set_time(&a, 1100);
        lv_anim_set_playback_time(&a, 1100);
        lv_anim_start(&a);
        break;

    case VS_WAITING:
        /* 张到最大，快速开合。唯一允许喊叫的状态。
           幅度要和 running 拉开 —— 余光扫过时形状比颜色先被认出来，
           两者只差颜色等于把这条信号压回单一维度。 */
        lv_anim_set_exec_cb(&a, set_open);
        lv_anim_set_values(&a, OPEN_MIN, OPEN_MIN + 6);
        lv_anim_set_time(&a, 260);
        lv_anim_set_playback_time(&a, 260);
        lv_anim_start(&a);
        break;

    case VS_IDLE:
        /* 城垛从左往右滚。一个周期 6 段、走完 2.4 秒，一格 400 毫秒 ——
           慢到不会吸引注意，又刚好能看出它在动。不设 playback：
           来回摆动看着像犹豫，单向滚动才像"在慢慢晃悠"。 */
        lv_anim_set_exec_cb(&a, set_wave);
        lv_anim_set_values(&a, 0, WAVE_GROUP * 2 - 1);
        lv_anim_set_time(&a, 2400);
        lv_anim_start(&a);
        break;

    case VS_STALE:
        /* 完全不动。参差的牙 + 静止，比任何动画都更像"出事了"。 */
        set_open(NULL, 32);
        break;

    default:
        /* 连不上：断开的虚线压平不起伏，靠慢闪表示"还在重试"。
           节奏刻意比 waiting 慢一个数量级 —— 它不该抢走注意力，
           但也不能像 stale 那样完全冻住。 */
        set_wave_at(0, true);
        lv_anim_set_exec_cb(&a, set_wave_opa);
        lv_anim_set_values(&a, LV_OPA_30, LV_OPA_COVER);
        lv_anim_set_time(&a, 1500);
        lv_anim_set_playback_time(&a, 1500);
        lv_anim_start(&a);
        break;
    }
}

void ui_render(const view_t *v)
{
    if (!v) {
        return;
    }

    /* 只有状态变了才换色重排重启动画。每秒重启会让呼吸永远停在第一下，
       看起来就是"一直在闪同一帧"。 */
    if ((int)v->state != s_last_state) {
        s_last_state = (int)v->state;

        lv_color_t c = color_of(v->state);
        for (int i = 0; i < TOOTH_N; i++) {
            lv_obj_set_style_bg_color(s_top[i], c, 0);
            lv_obj_set_style_bg_color(s_bot[i], c, 0);
        }
        for (int i = 0; i < WAVE_N; i++) {
            lv_obj_set_style_bg_color(s_wave[i], c, 0);
        }
        lv_obj_set_style_text_color(s_state, c, 0);
        lv_label_set_text(s_state, label_of(v->state));

        layout_for(v->state);
        animate_for(v->state);
    }

    if (v->state == VS_NOLINK) {
        /* name 通常为空，但"配对已清空"这类一次性提示会借用它 ——
           那条消息必须显示出来，否则清掉配对这件事就静默发生了。 */
        lv_label_set_text(s_name, v->name);
        lv_label_set_text(s_detail, "");
        lv_label_set_text(s_peek, "");
        lv_label_set_text(s_count, "");
        return;
    }

    /* 只有一条会话时不显示计数 —— 「1 个会话」是废话，占地方还分散注意力。 */
    if (v->total > 1) {
        char cnt[24];
        snprintf(cnt, sizeof(cnt), "%d 个会话", v->total);
        lv_label_set_text(s_count, cnt);
    } else {
        lv_label_set_text(s_count, "");
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
