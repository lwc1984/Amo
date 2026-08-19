#include "buttons.h"

#include "driver/gpio.h"
#include "esp_timer.h"

#define BTN_GPIO      14
#define LONG_PRESS_US (3 * 1000 * 1000)

static int64_t s_down_at = 0;
static bool s_was_down = false;
static bool s_long_fired = false;

void buttons_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

btn_event_t buttons_poll(void)
{
    bool down = (gpio_get_level(BTN_GPIO) == 0);   /* 上拉，按下为低 */
    int64_t now = esp_timer_get_time();

    if (down && !s_was_down) {
        s_down_at = now;
        s_long_fired = false;
        s_was_down = true;
        return BTN_NONE;
    }

    /* 长按在到时的那一刻就报，不等松手 —— 配对要扫 mDNS，等松手才开始
       会让人以为没按上，然后反复长按。 */
    if (down && s_was_down && !s_long_fired && (now - s_down_at) >= LONG_PRESS_US) {
        s_long_fired = true;
        return BTN_LONG;
    }

    if (!down && s_was_down) {
        s_was_down = false;
        if (!s_long_fired) {
            return BTN_SHORT;
        }
    }
    return BTN_NONE;
}
