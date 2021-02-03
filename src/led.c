#include <pico/stdlib.h>
#include <stdint.h>

#include "picoprobe_config.h"

#define LED_COUNT_SHIFT 14
#define LED_COUNT_MAX 5 * (1 << LED_COUNT_SHIFT)

static uint32_t led_count;

void led_init(void) {
    led_count = 0;

    gpio_init(PICOPROBE_LED);
    gpio_set_dir(PICOPROBE_LED, GPIO_OUT);
    gpio_put(PICOPROBE_LED, 1);
}



void led_task(void) {
    if (led_count != 0) {
        --led_count;
        gpio_put(PICOPROBE_LED, !((led_count >> LED_COUNT_SHIFT) & 1));
    }
}

void led_signal_activity(uint total_bits) {
    if (led_count == 0) {
        gpio_put(PICOPROBE_LED, 0);
    }

    if (led_count < LED_COUNT_MAX) {
        led_count += total_bits;
    }
}
