#include <stdio.h>
#include <stdlib.h>

#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>

#include "probe_config.h"
#include "ws2812.pio.h"

#define RED_MASK   0x01
#define GREEN_MASK 0x02
#define BLUE_MASK  0x04
struct _ws2812 {
	uint initted;
	uint offset;
	uint led;
};

static struct _ws2812 ws2812 = {0};

#ifndef SM_WS2812
#define SM_WS2812 2
#endif

#ifndef WS2812_PIN
#define WS2812_PIN 16
#endif

static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio1, SM_WS2812, pixel_grb << 8u);
}

void put_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    uint32_t mask = (green << 16) | (red << 8) | (blue << 0);
    put_pixel(mask);
}

void refresh_led(void)
{
    uint val = 0;
    if (ws2812.led & RED_MASK)
      val |= 10 << 8;
    if (ws2812.led & GREEN_MASK)
      val |= 10 << 16;
    if (ws2812.led & BLUE_MASK)
      val |= 10;

    put_pixel(val);
}

void put_redLED(bool on)
{
    if (on)
      ws2812.led |= RED_MASK;
    else
      ws2812.led &= ~RED_MASK;
}

void put_greenLED(bool on)
{
    if (on)
      ws2812.led |= GREEN_MASK;
    else
      ws2812.led &= ~GREEN_MASK;
}

void put_blueLED(bool on)
{
    if (on)
      ws2812.led |= BLUE_MASK;
    else
      ws2812.led &= ~BLUE_MASK;
}

void ws2812_init(void)
{
    if (ws2812.initted)
	return;

    ws2812.initted = 1;
    ws2812.led = 0;
    ws2812.offset = pio_add_program(pio1, &ws2812_program);
    ws2812_program_init(pio1, SM_WS2812, ws2812.offset, WS2812_PIN, 800000, false);
}

void ws2812_deinit(void)
{
    if (!ws2812.initted)
	return;

    pio_sm_set_enabled(pio1, SM_WS2812, 0);
    pio_remove_program(pio1, &ws2812_program, ws2812.offset);
    ws2812.initted = 0;
}
