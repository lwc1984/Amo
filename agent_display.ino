/*
 * Agent 状态显示器 — LilyGo T-Display AMOLED Lite (194x368, RM67162, QSPI)
 * 依赖: LilyGo-AMOLED-Series (自带 LVGL 8.x)
 * platformio.ini 里 board = lilygo-t-display-amoled-lite
 */
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <WiFi.h>
#include <HTTPClient.h>

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-pass"
#define ENDPOINT  "http://192.168.1.100:8787/api/tiny"

#define BUZZER_PIN 2          // 无源蜂鸣器，按实际接线改

// 与平板端共用同一套语义色
#define C_RUN    lv_color_hex(0x3FBFD8)
#define C_WAIT   lv_color_hex(0xFFB020)
#define C_WAIT_D lv_color_hex(0x412402)
#define C_DIM    lv_color_hex(0x2A3648)

enum State { ST_UNKNOWN, ST_IDLE, ST_RUNNING, ST_WAITING };

LilyGo_Class amoled;
static State cur = ST_UNKNOWN;

static lv_obj_t *banner, *topbar, *big, *label_state, *label_name, *label_detail, *foot;
static lv_anim_t breathe;

// ── UI 构建 ────────────────────────────────────────────────
static void build_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // 顶部 3px 语义条：与平板端顶部的注意力条同一套语言
    topbar = lv_obj_create(scr);
    lv_obj_set_size(topbar, 194, 3);
    lv_obj_set_pos(topbar, 0, 0);
    lv_obj_set_style_border_width(topbar, 0, 0);
    lv_obj_set_style_radius(topbar, 0, 0);

    // 等待态的琥珀色块，平时隐藏
    banner = lv_obj_create(scr);
    lv_obj_set_size(banner, 194, 112);
    lv_obj_set_pos(banner, 0, 0);
    lv_obj_set_style_bg_color(banner, C_WAIT, 0);
    lv_obj_set_style_border_width(banner, 0, 0);
    lv_obj_set_style_radius(banner, 0, 0);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *icon = lv_label_create(banner);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, C_WAIT_D, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_center(icon);

    // 空闲态的大数字
    big = lv_label_create(scr);
    lv_obj_set_style_text_font(big, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(big, C_DIM, 0);
    lv_obj_align(big, LV_ALIGN_CENTER, 0, -20);

    label_state = lv_label_create(scr);
    lv_obj_set_style_text_font(label_state, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(label_state, 3, 0);

    label_name = lv_label_create(scr);
    lv_obj_set_width(label_name, 166);
    lv_label_set_long_mode(label_name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label_name, &lv_font_montserrat_18, 0);

    label_detail = lv_label_create(scr);
    lv_obj_set_width(label_detail, 166);
    lv_label_set_long_mode(label_detail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label_detail, &lv_font_montserrat_12, 0);

    foot = lv_label_create(scr);
    lv_obj_set_style_text_font(foot, &lv_font_montserrat_12, 0);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_LEFT, 12, -10);
}

// ── 呼吸动画：只有等待态才跑 ───────────────────────────────
static void breathe_cb(void *obj, int32_t v) {
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, v, 0);
    amoled.setBrightness(120 + v / 2);      // 亮度跟着一起呼吸
}

static void set_state(State s, const char *name, const char *detail, int n_idle) {
    if (s == cur && s != ST_WAITING) return;

    lv_anim_del(banner, breathe_cb);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(big, LV_OBJ_FLAG_HIDDEN);

    switch (s) {
    case ST_IDLE:
        amoled.setBrightness(20);                       // 近乎熄屏，AMOLED 黑像素不耗电
        lv_obj_set_style_bg_color(topbar, lv_color_hex(0x14202E), 0);
        lv_obj_clear_flag(big, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(big, "%d", n_idle);
        lv_obj_align(label_state, LV_ALIGN_CENTER, 0, 24);
        lv_label_set_text(label_state, "IDLE");
        lv_obj_set_style_text_color(label_state, C_DIM, 0);
        lv_label_set_text(label_name, "");
        lv_label_set_text(label_detail, "");
        break;

    case ST_RUNNING:
        amoled.setBrightness(60);
        lv_obj_set_style_bg_color(topbar, C_RUN, 0);
        lv_obj_align(label_state, LV_ALIGN_TOP_LEFT, 14, 34);
        lv_label_set_text(label_state, "RUNNING");
        lv_obj_set_style_text_color(label_state, lv_color_hex(0x2E7E90), 0);
        lv_obj_align(label_name, LV_ALIGN_TOP_LEFT, 14, 62);
        lv_obj_set_style_text_color(label_name, lv_color_hex(0x8FD9E8), 0);
        lv_label_set_text(label_name, name);
        lv_obj_align(label_detail, LV_ALIGN_TOP_LEFT, 14, 96);
        lv_obj_set_style_text_color(label_detail, lv_color_hex(0x3D6B78), 0);
        lv_label_set_text(label_detail, detail);
        break;

    case ST_WAITING:
        amoled.setBrightness(255);
        lv_obj_set_style_bg_color(topbar, C_WAIT, 0);
        lv_obj_clear_flag(banner, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(label_state, LV_ALIGN_TOP_LEFT, 14, 132);
        lv_label_set_text(label_state, "NEEDS YOU");
        lv_obj_set_style_text_color(label_state, C_WAIT, 0);
        lv_obj_align(label_name, LV_ALIGN_TOP_LEFT, 14, 168);
        lv_obj_set_style_text_color(label_name, lv_color_white(), 0);
        lv_label_set_text(label_name, name);
        lv_obj_align(label_detail, LV_ALIGN_TOP_LEFT, 14, 210);
        lv_obj_set_style_text_color(label_detail, lv_color_hex(0x8A6A2A), 0);
        lv_label_set_text(label_detail, detail);

        lv_anim_init(&breathe);
        lv_anim_set_var(&breathe, banner);
        lv_anim_set_exec_cb(&breathe, breathe_cb);
        lv_anim_set_values(&breathe, 110, 255);
        lv_anim_set_time(&breathe, 700);
        lv_anim_set_playback_time(&breathe, 700);
        lv_anim_set_repeat_count(&breathe, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&breathe);

        if (cur != ST_WAITING) {                        // 只在状态跳变时响，不要一直叫
            tone(BUZZER_PIN, 1480, 120);
            delay(160);
            tone(BUZZER_PIN, 1975, 180);
        }
        break;

    default:                                            // 断连：慢速蓝，绝不伪装成正常
        amoled.setBrightness(30);
        lv_obj_set_style_bg_color(topbar, lv_color_hex(0x1E3A5F), 0);
        lv_obj_align(label_state, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(label_state, "NO LINK");
        lv_obj_set_style_text_color(label_state, lv_color_hex(0x3E6FA0), 0);
        lv_label_set_text(label_name, "");
        lv_label_set_text(label_detail, "");
        break;
    }
    cur = s;
}

// ── 轮询 ───────────────────────────────────────────────────
static void poll_task(void *) {
    static char name[48], detail[64];
    for (;;) {
        bool ok = false;
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            http.setTimeout(3000);
            http.begin(ENDPOINT);
            if (http.GET() == 200) {
                String body = http.getString();
                int w = 0, r = 0, i = 0;
                sscanf(body.c_str(), "%d,%d,%d", &w, &r, &i);
                int bar = body.indexOf('|');
                String rest = bar >= 0 ? body.substring(bar + 1) : "";
                rest.trim();

                int sep = rest.indexOf('\t');
                snprintf(name, sizeof(name), "%s",
                         sep > 0 ? rest.substring(0, sep).c_str() : rest.c_str());
                snprintf(detail, sizeof(detail), "%s",
                         sep > 0 ? rest.substring(sep + 1).c_str() : "");

                if (w > 0)      set_state(ST_WAITING, name, detail, 0);
                else if (r > 0) set_state(ST_RUNNING, name, detail, 0);
                else            set_state(ST_IDLE, "", "", i);
                ok = true;
            }
            http.end();
        }
        if (!ok) set_state(ST_UNKNOWN, "", "", 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── 防烧屏：每分钟整体偏移 1px ─────────────────────────────
static void shift_task(void *) {
    int8_t d = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        d = (d + 1) % 3;
        lv_obj_set_y(lv_scr_act(), d - 1);
    }
}

void setup() {
    amoled.begin();
    amoled.setRotation(0);
    beginLvglHelper(amoled);
    pinMode(BUZZER_PIN, OUTPUT);

    build_ui();
    set_state(ST_UNKNOWN, "", "", 0);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    xTaskCreate(poll_task, "poll", 6144, NULL, 4, NULL);
    xTaskCreate(shift_task, "shift", 2048, NULL, 1, NULL);
}

void loop() {
    lv_timer_handler();
    delay(5);
}
