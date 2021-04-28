/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Jaroslav Kysela <perex@perex.cz>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *
 * Protocol link: https://www.sump.org/projects/analyzer/protocol
 *
 */

#include <pico/stdlib.h>
#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/structs/bus_ctrl.h"

#include "tusb.h"
#include "led.h"

#include "picoprobe_config.h"
#include "cdc_sump.h"

#if false
#define sump_irq_debug(format,args...) picoprobe_debug(format, ## args)
#else
#define sump_irq_debug(format,...) ((void)0)
#endif

#define CDC_INTF		1

#define SAMPLING_DIVIDER	4	// minimal sysclk sampling divider

#define SAMPLING_GPIO_FIRST	6
#define SAMPLING_GPIO_LAST	21
#define SAMPLING_BITS		(SAMPLING_GPIO_LAST-SAMPLING_GPIO_FIRST+1)
#define SAMPLING_BYTES		((SAMPLING_BITS+7)/8)
#define SAMPLING_GPIO_MASK	(((1 << SAMPLING_BITS) - 1) << SAMPLING_GPIO_FIRST)

#define SAMPLING_GPIO_TEST	22

#if SAMPLING_BITS != 8 && SAMPLING_BITS != 16
#error "Correct sampling width (8 or 16 bits)"
#endif

#define SAMPLING_PIO		pio1
#define SAMPLING_PIO_SM		0u

#define SAMPLING_DMA_IRQ	DMA_IRQ_1
#define sump_dma_set_irq_channel_mask_enabled dma_set_irq1_channel_mask_enabled
#define sump_dma_ints		(dma_hw->ints1)

#define SUMP_SAMPLE_MASK	((1<<SAMPLING_BITS)-1)
#define SUMP_BYTE0_OR		((~SUMP_SAMPLE_MASK) & 0xff)
#define SUMP_BYTE1_OR		((~SUMP_SAMPLE_MASK >> 8) & 0xff)

#define SUMP_DMA_CH_FIRST	0
#define SUMP_DMA_CH_LAST	7
#define SUMP_DMA_CHANNELS	(SUMP_DMA_CH_LAST-SUMP_DMA_CH_FIRST+1)
#define SUMP_DMA_MASK		(((1<<SUMP_DMA_CHANNELS)-1) << SUMP_DMA_CH_FIRST)

#if PICO_NO_FLASH
#define SUMP_MEMORY_SIZE	102400	// 100kB
#else
#define SUMP_MEMORY_SIZE	204800	// 200kB
#endif
#define SUMP_MAX_CHUNK_SIZE	4096

#if (SUMP_MEMORY_SIZE % SUMP_MAX_CHUNK_SIZE) != 0
#error "Invalid maximal chunk size!"
#endif

#if (SUMP_MEMORY_SIZE / SUMP_MAX_CHUNK_SIZE) < SUMP_DMA_CHANNELS
#error "DMA buffer and DMA channels out of sync!"
#endif

#define SUMP_STATE_CONFIG	0
#define SUMP_STATE_INIT		1
#define SUMP_STATE_TRIGGER	2
#define SUMP_STATE_SAMPLING	3
#define SUMP_STATE_DUMP		4
#define SUMP_STATE_ERROR	5

#define ONE_MHZ			1000000u

struct _trigger {
    uint32_t mask;
    uint32_t value;
    uint16_t delay;
    uint8_t  channel;
    uint8_t  level;
    bool     serial;
    bool     start;
};

static struct _sump {

    /* internal states */
    bool     cdc_connected;
    uint8_t  cmd[5];		// command
    uint8_t  cmd_pos;		// command buffer position
    uint8_t  state;		// SUMP_STATE_*
    uint8_t  width;		// in bytes, 1 = 8 bits, 2 = 16 bits
    uint8_t  trigger_index;
    uint32_t pio_prog_offset;
    uint32_t read_start;
    uint64_t timestamp_start;

    /* protocol config */
    uint32_t divider;		// clock divider
    uint32_t read_count;
    uint32_t delay_count;
    uint32_t flags;
    struct _trigger trigger[4];

    /* DMA buffer */
    uint32_t chunk_size;	// in bytes
    uint32_t dma_start;
    uint32_t dma_count;
    uint32_t dma_curr_idx;	// current DMA channel (index)
    uint32_t dma_pos;
    uint32_t next_count;
    uint8_t  buffer[SUMP_MEMORY_SIZE];

} sump;

#define AS_16P(a) (*(uint16_t *)(a))

static void
picoprobe_debug_hexa(uint8_t *buf, uint32_t len)
{
    uint32_t l;
    for (l = 0; len > 0; len--, l++) {
        if (l != 0)
            putchar(':');
        printf("%02x", *buf++);
    }
}

static uint8_t *
sump_add_metas(uint8_t * buf, uint8_t tag, const char *str)
{
    *buf++ = tag;
    while (*str)
	*buf++ = (uint8_t)(*str++);
    *buf++ = '\0';
    return buf;
}

static uint8_t *
sump_add_meta1(uint8_t * buf, uint8_t tag, uint8_t val)
{
    buf[0] = tag;
    buf[1] = val;
    return buf + 2;
}

static uint8_t *
sump_add_meta4(uint8_t * buf, uint8_t tag, uint32_t val)
{
    buf[0] = tag;
    // this is a bit weird, but libsigrok decodes Big-Endian words here
    // the commands use Little-Endian
#if false
    buf[1] = val;
    buf[2] = val >> 8;
    buf[3] = val >> 16;
    buf[4] = val >> 24;
#else
    buf[1] = val >> 24;
    buf[2] = val >> 16;
    buf[3] = val >> 8;
    buf[4] = val;
#endif
    return buf + 5;
}

static void
sump_do_meta(void)
{
    char cpu[32];
    uint8_t buf[128], *ptr = buf, *wptr = buf;
    uint32_t sysclk;

    sysclk = clock_get_hz(clk_sys) / SAMPLING_DIVIDER;
    sprintf(cpu, "RP2040 %uMhz", sysclk / ONE_MHZ);
    ptr = sump_add_metas(ptr, SUMP_META_NAME, "Picoprobe Logic Analyzer v1");
    ptr = sump_add_metas(ptr, SUMP_META_FPGA_VERSION, "No FPGA :-( PIO+DMA!");
    ptr = sump_add_metas(ptr, SUMP_META_CPU_VERSION, cpu);
    ptr = sump_add_meta4(ptr, SUMP_META_SAMPLE_RATE, sysclk);
    ptr = sump_add_meta4(ptr, SUMP_META_SAMPLE_RAM, SUMP_MEMORY_SIZE);
    ptr = sump_add_meta1(ptr, SUMP_META_PROBES_B, SAMPLING_BITS);
    ptr = sump_add_meta1(ptr, SUMP_META_PROTOCOL_B, 2);
    *ptr++ = SUMP_META_END;
    while (wptr != ptr)
        wptr += tud_cdc_n_write(CDC_INTF, wptr, ptr - wptr);
    tud_cdc_n_write_flush(CDC_INTF);
}

static void
sump_do_id(void)
{
    tud_cdc_n_write_str(CDC_INTF, "1ALS");
    tud_cdc_n_write_flush(CDC_INTF);
}

static uint32_t
sump_calc_sysclk_divider()
{
    uint32_t divider = sump.divider, v;
    const uint32_t common_divisor = 4;

    if (divider > 65535)
        divider = 65535;
    // return the fractional part in lowest byte (8 bits)
    if (sump.flags & SUMP_FLAG1_DDR) {
        // 125Mhz support
        divider *= 128 / common_divisor;
    } else {
        divider *= 256 / common_divisor;
    }
    v = clock_get_hz(clk_sys);
    assert((v % ONE_MHZ) == 0);
    // conversion from 100Mhz to sysclk
    v = ((v / ONE_MHZ) * divider) / ((100 / common_divisor) * SAMPLING_DIVIDER);
    v *= sump.width;
    if (v > 65535 * 256)
        v = 65535 * 256;
    else if (v <= 255)
        v = 256;
    picoprobe_debug("%s(): %u %u -> %u (%.4f)\n", __func__,
                    clock_get_hz(clk_sys), sump.divider, v, (float)v / 256.0);
    return v;
}

static void
sump_pio_program(void)
{
    uint16_t prog[] = {
        pio_encode_in(pio_pins, 8),
        pio_encode_in(pio_pins, 16)
    };
    struct pio_program program = {
        .instructions = prog,
        .length = count_of(prog),
        .origin = -1
    };
    picoprobe_debug("%s(): 0x%04x 0x%04x len=%u\n", __func__, prog[0], prog[1], program.length);
    sump.pio_prog_offset = pio_add_program(SAMPLING_PIO, &program);
}

static void
sump_pio_init(void)
{
    pio_sm_config c;
    uint off, gpio = SAMPLING_GPIO_FIRST, divider;

#if SAMPLING_BITS > 8
    if (sump.width == 1 && (sump.flags & SUMP_FLAG1_GR0_DISABLE) != 0)
        gpio += 8;
#endif
    // loop the IN instruction forewer (8-bit and 16-bit version)
    c = pio_get_default_sm_config();
    sm_config_set_in_pins(&c, gpio);
    off = sump.pio_prog_offset + (sump.width - 1);
    sm_config_set_wrap(&c, off, off);
    divider = sump_calc_sysclk_divider();
    sm_config_set_clkdiv_int_frac(&c, divider >> 8, divider & 0xff);
    sm_config_set_in_shift(&c, true, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(SAMPLING_PIO, SAMPLING_PIO_SM, off, &c);
    picoprobe_debug("%s(): pc=0x%02x [0x%02x], gpio=%u\n", __func__,
                    off, sump.pio_prog_offset, gpio);
}

static uint32_t
sump_pwm_slice_init(uint gpio, uint clock, bool swap_levels)
{
    uint32_t clksys = clock_get_hz(clk_sys), clkdiv, slice, tmp;
    uint16_t top = 5, level_a = 1, level_b = 4;

    // correction for low speed PWM
    while ((clksys / clock / top) & ~0xff) {
        top *= 1000;
        level_a *= 1000;
        level_b *= 1000;
    }
    clkdiv = clksys / clock / top;
    // pwm setup
    slice = pwm_gpio_to_slice_num(gpio);
    gpio_set_function(gpio, GPIO_FUNC_PWM);
    gpio_set_function(gpio + 1, GPIO_FUNC_PWM);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, top - 1);
    pwm_config_set_clkdiv_int(&c, clkdiv);
    pwm_init(slice, &c, false);
    if (swap_levels) {
        uint16_t tmp = level_a;
        level_a = level_b;
        level_b = tmp;
    }
    pwm_set_both_levels(slice, level_a, level_b);
    picoprobe_debug("%s(): gpio=%u clkdiv=%u top=%u level=%u/%u freq=%.4fMhz (req %.4fMhz)\n",
                    __func__, gpio, clkdiv, top, level_a, level_b,
                    (float)clksys / (float)clkdiv / (float)top / 1000000.0,
                    (float)clock / 1000000.0);
    return 1u << slice;
}

static uint32_t
sump_calib_init(void)
{
    uint32_t clksys = clock_get_hz(clk_sys), clkdiv, slice;
    const uint32_t clock = 5 * ONE_MHZ;
    const uint16_t top = 10, level_a = 5;

    // set 5Mhz PWM on test pin

    // should not go beyond 255!
    clkdiv = clksys / clock / top;

    // pwm setup
    slice = pwm_gpio_to_slice_num(SAMPLING_GPIO_TEST);
    gpio_set_function(SAMPLING_GPIO_TEST, GPIO_FUNC_PWM);
    pwm_config c = pwm_get_default_config();
    pwm_config_set_wrap(&c, top - 1);
    pwm_config_set_clkdiv_int(&c, clkdiv);
    pwm_init(slice, &c, false);
    pwm_set_both_levels(slice, level_a, level_a);
    picoprobe_debug("%s(): gpio=%u clkdiv=%u top=%u level=%u/%u freq=%.4fMhz (req %.4fMhz)\n",
                    __func__, SAMPLING_GPIO_TEST, clkdiv, top, level_a, level_a,
                    (float)clksys / (float)clkdiv / (float)top / 1000000.0,
                    (float)clock / 1000000.0);
    return 1u << slice;
}

static uint32_t
sump_test_init(void)
{
    // Initialize test PWMs
    const uint32_t gpio = SAMPLING_GPIO_FIRST;
    uint32_t mask;
    // 10Mhz PWM
    mask = sump_pwm_slice_init(gpio, 10000000, false);
    // 1Mhz PWM
    mask |= sump_pwm_slice_init(gpio + 2, 1000000, false);
    // 1kHz PWM
    mask |= sump_pwm_slice_init(gpio + 4, 1000, false);
#if SAMPLING_BITS > 8
    // 1kHz PWM (second byte)
    mask |= sump_pwm_slice_init(gpio + 8, 1000, true);
#endif
    return mask;
}

static void
sump_test_done(void)
{
    const uint32_t gpio = SAMPLING_GPIO_FIRST;
    uint32_t i;

    pwm_set_enabled(pwm_gpio_to_slice_num(gpio), false);
    pwm_set_enabled(pwm_gpio_to_slice_num(gpio + 2), false);
    pwm_set_enabled(pwm_gpio_to_slice_num(gpio + 4), false);
#if SAMPLING_BITS > 8
    pwm_set_enabled(pwm_gpio_to_slice_num(gpio + 8), false);
#endif
    for (i = SAMPLING_GPIO_FIRST; i <= SAMPLING_GPIO_LAST; i++)
        gpio_set_function(i, GPIO_FUNC_NULL);
    // test pin
    pwm_set_enabled(SAMPLING_GPIO_TEST, false);
}

static void
sump_set_chunk_size(void)
{
    uint32_t clk_hz;

    clk_hz = clock_get_hz(clk_sys) / (sump_calc_sysclk_divider() / 256);
    // the goal is to transfer around 125 DMA chunks per second
    // for slow sampling rates
    sump.chunk_size = 1;
    while (clk_hz > 125 && sump.chunk_size < SUMP_MAX_CHUNK_SIZE) {
        sump.chunk_size *= 2;
        clk_hz /= 2;
    }
    picoprobe_debug("%s(): 0x%04x\n", __func__, sump.chunk_size);
}

static void
sump_dma_program(uint ch, uint32_t pos)
{
    dma_channel_config cfg = dma_channel_get_default_config(SUMP_DMA_CH_FIRST + ch);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, pio_get_dreq(SAMPLING_PIO, SAMPLING_PIO_SM, false));
    channel_config_set_chain_to(&cfg, SUMP_DMA_CH_FIRST + ((ch + 1) % SUMP_DMA_CHANNELS));
    channel_config_set_transfer_data_size(&cfg, sump.width == 1 ? DMA_SIZE_8 : DMA_SIZE_16);
    dma_channel_configure(SUMP_DMA_CH_FIRST + ch, &cfg,
                          sump.buffer + pos,
                          &SAMPLING_PIO->rxf[SAMPLING_PIO_SM],
                          sump.chunk_size / sump.width,
                          false);
    picoprobe_debug("%s() %u: w=0x%08x r=0x%08x t=0x%08x -> %u\n", __func__,
                    SUMP_DMA_CH_FIRST + ch,
                    sump.buffer + pos,
                    &SAMPLING_PIO->rxf[SAMPLING_PIO_SM],
                    sump.chunk_size / sump.width,
                    SUMP_DMA_CH_FIRST + ((ch + 1) % SUMP_DMA_CHANNELS));
}

static void
sump_dma_init(uint8_t state)
{
    uint32_t i, count, dma_transfer_size, pwm_mask = 0, irq_state;
    uint8_t *dma_start;

    sump.dma_start = 0;
    sump.dma_pos = 0;
    sump.dma_curr_idx = 0;

    picoprobe_debug("%s(): read=0x%08x delay=0x%08x divider=%u\n", __func__,
                    sump.read_count, sump.delay_count, sump.divider);

    count = sump.read_count;
    if (count > SUMP_MEMORY_SIZE)
	count = SUMP_MEMORY_SIZE;
    sump.dma_count = count;
    if (sump.read_count <= sump.delay_count)
	sump.next_count = sump.read_count;
    else
	sump.next_count = sump.read_count - sump.delay_count;
    sump.next_count *= sump.width;
    sump.read_start = 0;

    picoprobe_debug("%s(): buffer = 0x%08x, dma_count=0x%08x next_count=0x%08x\n", __func__,
                    sump.buffer, sump.dma_count, sump.next_count);

    sump_pio_init();

    pwm_mask = sump_calib_init();
    if (sump.flags & SUMP_FLAG1_EXT_TEST) {
        pwm_mask |= sump_test_init();
    } else {
        sump_test_done();
    }

    // limit chunk size for slow sampling
    sump_set_chunk_size();

    for (i = 0; i < SUMP_DMA_CHANNELS; i++)
        sump_dma_program(i, i * sump.chunk_size);

    // let's go
    irq_state = save_and_disable_interrupts();
    pio_sm_set_enabled(SAMPLING_PIO, SAMPLING_PIO_SM, true);
    if (pwm_mask)
        pwm_set_mask_enabled(pwm_mask);
    dma_channel_start(SUMP_DMA_CH_FIRST);
    irq_set_enabled(SAMPLING_DMA_IRQ, true);
    sump.timestamp_start = time_us_64();
    restore_interrupts(irq_state);

    sump.state = state;
}

static void *
sump_analyze_trigger8(void *ptr, uint32_t size)
{
    uint8_t *src = ptr;
    uint8_t v;
    uint8_t tmask = sump.trigger[sump.trigger_index].mask;
    uint8_t tvalue = sump.trigger[sump.trigger_index].value;
    uint32_t count = sump.chunk_size;

    for (count = sump.chunk_size; count > 0; count--) {
	v = *src++;
	if ((v & tmask) != tvalue)
	    continue;
__next:
	if (sump.trigger[sump.trigger_index].start)
	    return src;
        sump.trigger_index++;
        tmask = sump.trigger[sump.trigger_index].mask;
        tvalue = sump.trigger[sump.trigger_index].value;
        if (tmask == 0 && tvalue == 0)
            goto __next;
    }
    return NULL;
}

static void *
sump_analyze_trigger16(void *ptr, uint32_t size)
{
    uint16_t *src = ptr;
    uint16_t v;
    uint16_t tmask = sump.trigger[0].mask;
    uint16_t tvalue = sump.trigger[0].value;
    uint32_t count = sump.chunk_size;

    for (count = sump.chunk_size / 2; count > 0; count--) {
	v = *src++;
	if ((v & tmask) != tvalue)
	    continue;
__next:
	if (sump.trigger[sump.trigger_index].start)
	    return src;
        sump.trigger_index++;
        tmask = sump.trigger[sump.trigger_index].mask;
        tvalue = sump.trigger[sump.trigger_index].value;
        if (tmask == 0 && tvalue == 0)
            goto __next;
    }
    return NULL;
}

static void *
sump_analyze_trigger(void *ptr, uint32_t size)
{
    if (sump.width == 1)
	return sump_analyze_trigger8(ptr, size);
    else
	return sump_analyze_trigger16(ptr, size);
}

static void
sump_dma_done(void)
{
    uint64_t us;

    pio_sm_set_enabled(SAMPLING_PIO, SAMPLING_PIO_SM, false);
    irq_set_enabled(SAMPLING_DMA_IRQ, false);
    us = time_us_64() - sump.timestamp_start;
    picoprobe_debug("%s(): sampling time = %llu.%llu\n", __func__, us / 1000000ull, us % 1000000ull);
    sump.state = SUMP_STATE_DUMP;
}

static int
sump_dma_next(uint32_t pos)
{
    uint32_t tmp, delay_bytes;
    uint8_t *ptr;

    if (sump.state != SUMP_STATE_TRIGGER) {
        sump_dma_done();
        return 0;
    }

    // waiting for the trigger samples
    ptr = sump_analyze_trigger(sump.buffer + pos, sump.chunk_size);
    if (ptr == NULL) {
        // call this routine again right after next chunk
        return sump.chunk_size;
    }

    sump.state = SUMP_STATE_SAMPLING;

    // calculate read start
    pos = ptr - sump.buffer;
    tmp = (sump.read_count - sump.delay_count) * sump.width;
    sump.read_start = (pos - tmp) % SUMP_MEMORY_SIZE;

    // calculate the samples after trigger
    tmp = sump.chunk_size - (pos % sump.chunk_size);
    delay_bytes = sump.delay_count * sump.width;
    if (tmp >= delay_bytes) {
        sump_dma_done();
        return 0;
    }
    return delay_bytes - tmp;
}

static void
sump_dma_chain_to_self(uint ch)
{
    dma_channel_config cfg;

    ch += SUMP_DMA_CH_FIRST;
    cfg = dma_get_channel_config(ch);
    channel_config_set_chain_to(&cfg, ch);
    dma_channel_set_config(ch, &cfg, false);
}

void __isr
sump_dma_irq_handler(void)
{
    uint32_t ch, mask, loop = 0;

__retry:
    ch = SUMP_DMA_CH_FIRST + sump.dma_curr_idx;
    mask = 1u << ch;
    if ((sump_dma_ints & mask) == 0)
        return;
    // acknowledge interrupt
    sump_dma_ints = mask;

    // reprogram the current DMA channel to the tail
    mask = SUMP_DMA_CHANNELS * sump.chunk_size;
    dma_channel_set_write_addr(ch, sump.buffer + (sump.dma_pos + mask) % SUMP_MEMORY_SIZE, false);
    sump_irq_debug("%s(): %u: w=0x%08x, state=%u\n", __func__, ch, sump.buffer + (sump.dma_pos + mask) % SUMP_MEMORY_SIZE, sump.state);

    if (sump.next_count <= sump.chunk_size) {
	sump.next_count = sump_dma_next(sump.dma_pos);
        if (sump.state == SUMP_STATE_DUMP)
            return;
    } else {
	sump.next_count -= sump.chunk_size;
    }
    sump_irq_debug("%s(): next=0x%x\n", __func__, sump.next_count);

    sump.dma_curr_idx = (sump.dma_curr_idx + 1) % SUMP_DMA_CHANNELS;
    sump.dma_pos += sump.chunk_size;
    sump.dma_pos %= SUMP_MEMORY_SIZE;

    if (sump.state == SUMP_STATE_SAMPLING &&
        sump.next_count >= sump.chunk_size &&
        sump.next_count < SUMP_DMA_CHANNELS * sump.chunk_size) {
        // set the last DMA segment to correct size to avoid overwrites
        mask = sump.next_count / sump.chunk_size;
        if ((sump.next_count % sump.chunk_size) == 0) {
            ch = (mask + sump.dma_curr_idx - 1) % SUMP_DMA_CHANNELS;
            sump_dma_chain_to_self(ch);
            ch = (ch + 1) % SUMP_DMA_CHANNELS;
        } else {
            ch = (mask + sump.dma_curr_idx) % SUMP_DMA_CHANNELS;
            dma_channel_set_trans_count(ch + SUMP_DMA_CH_FIRST, (sump.next_count % sump.chunk_size) / sump.width, false);
        }
        sump_irq_debug("%s(): %u: t=0x%08x\n", __func__, ch + SUMP_DMA_CH_FIRST, (sump.next_count % sump.chunk_size) / sump.width);
        // break chain, reset unused DMA chunks
        // clear all chains for high-speed DMAs
        mask = SUMP_DMA_CHANNELS - ((sump.next_count + sump.chunk_size - 1) / sump.chunk_size);
        while (mask > 0) {
            sump_dma_chain_to_self(ch);
            sump_irq_debug("%s(): %u -> %u\n", __func__, ch + SUMP_DMA_CH_FIRST, ch + SUMP_DMA_CH_FIRST);
            ch = (ch + 1) % SUMP_DMA_CHANNELS;
            mask--;
        }
    }

    // are we slow?
    if (++loop == SUMP_DMA_CHANNELS) {
        sump_dma_done();
        sump.state = SUMP_STATE_ERROR;
        return;
    }

    goto __retry;
}

static void
sump_do_run(void)
{
    uint8_t state;
    uint32_t i, tmask = 0;
    bool tstart = false;

    if (sump.width == 0) {
        // invalid config, dump something nice
        sump.state = SUMP_STATE_DUMP;
	return;
    }

    for (i = 0; i < count_of(sump.trigger); i++) {
        tstart |= sump.trigger[i].start;
        tmask |= sump.trigger[i].mask;
    }
    if (tstart && tmask) {
	state = SUMP_STATE_TRIGGER;
	sump.trigger_index = 0;
    } else {
        state = SUMP_STATE_SAMPLING;
    }

    sump_dma_init(state);
}

static void
sump_do_finish(void)
{
    if (sump.state == SUMP_STATE_TRIGGER || sump.state == SUMP_STATE_SAMPLING) {
        sump.state = SUMP_STATE_DUMP;
        sump_dma_done();
        return;
    }
}

static void
sump_do_stop(void)
{
    uint32_t i;

    if (sump.state == SUMP_STATE_INIT)
        return;
    // IRQ and PIO fast stop
    irq_set_enabled(SAMPLING_DMA_IRQ, false);
    pio_sm_set_enabled(SAMPLING_PIO, SAMPLING_PIO_SM, false);
    // DMA abort
    for (i = SUMP_DMA_CH_FIRST; i <= SUMP_DMA_CH_LAST; i++)
        dma_channel_abort(i);
    // IRQ status cleanup
    sump_dma_ints = SUMP_DMA_MASK;
    // PIO cleanup
    pio_sm_clear_fifos(SAMPLING_PIO, SAMPLING_PIO_SM);
    pio_sm_restart(SAMPLING_PIO, SAMPLING_PIO_SM);
    // test
    sump_test_done();
    // protocol state
    sump.state = SUMP_STATE_INIT;
}

static void
sump_do_reset(void)
{
    uint32_t i;

    sump_do_stop();
    memset(&sump.trigger, 0, sizeof(sump.trigger));
}

static void
sump_set_flags(uint32_t flags)
{
    uint8_t width;

    sump.flags = flags;
    width = 2;
    if (flags & SUMP_FLAG1_GR0_DISABLE)
	width--;
    if (flags & SUMP_FLAG1_GR1_DISABLE)
	width--;
    // we don't support 24-bit or 32-bit capture - sorry
    if ((flags & SUMP_FLAG1_GR2_DISABLE) == 0)
	width = 0;
    if ((flags & SUMP_FLAG1_GR3_DISABLE) == 0)
	width = 0;
    picoprobe_debug("%s(): sample %u bytes\n", __func__, width);
    sump.width = width;
}

static void
sump_update_counts(uint32_t val)
{
    /*
     * This just sets up how many samples there should be before
     * and after the trigger fires. The read_count is total samples
     * to return and delay_count number of samples after
     * the trigger.
     *
     * This sets the buffer splits like 0/100, 25/75, 50/50
     * for example if read_count == delay_count then we should
     * return all samples starting from the trigger point.
     * If delay_count < read_count we return
     * (read_count - delay_count) of samples from before
     * the trigger fired.
     */
    uint32_t read_count = ((val & 0xffff) + 1) * 4;
    uint32_t delay_count = ((val >> 16) + 1) * 4;
    if (delay_count > read_count)
        read_count = delay_count;
    sump.read_count = read_count;
    sump.delay_count = delay_count;
}

static void
sump_set_trigger_mask(uint trig, uint32_t val)
{
    struct _trigger *t = &sump.trigger[trig];
    t->mask = val;
    picoprobe_debug("%s(): idx=%u val=0x%08x\n", __func__, trig, val);
}

static void
sump_set_trigger_value(uint trig, uint32_t val)
{
    struct _trigger *t = &sump.trigger[trig];
    t->value = val;
    picoprobe_debug("%s(): idx=%u val=0x%08x\n", __func__, trig, val);
}

static void
sump_set_trigger_config(uint trig, uint32_t val)
{
    struct _trigger *t = &sump.trigger[trig];
    t->start = (val & 0x08000000) != 0;
    t->serial = (val & 0x02000000) != 0;
    t->channel = ((val >> 20) & 0x0f) | ((val >> (24 - 4)) & 0x10);
    t->level = (val >> 16) & 3;
    t->delay = val & 0xffff;
    picoprobe_debug("%s(): idx=%u val=0x%08x (start=%u serial=%u channel=%u level=%u delay=%u)\n",
                    __func__, trig, val, t->start, t->serial, t->channel, t->level, t->delay);
}

static void
sump_rx_short(uint8_t cmd)
{
    picoprobe_debug("%s(): 0x%02x\n", __func__, cmd);
    switch (cmd) {
    case SUMP_CMD_RESET:
	sump_do_reset();
	break;
    case SUMP_CMD_ARM:
	sump_do_run();
	break;
    case SUMP_CMD_ID:
	sump_do_id();
	break;
    case SUMP_CMD_META:
	sump_do_meta();
	break;
    case SUMP_CMD_FINISH:
	sump_do_finish();
	break;
    case SUMP_CMD_QUERY_INPUT:
	break;
    case SUMP_CMD_ADVANCED_ARM:
	sump_do_run();
	break;
    default:
	break;
    }
}

static void
sump_rx_long(uint8_t * cmd)
{
    uint32_t val;

    val = cmd[1] | (cmd[2] << 8) | (cmd[3] << 16) | (cmd[4] << 24);
    picoprobe_debug("%s(): [0x%02x] 0x%08x\n", __func__, cmd[0], val);
    switch (cmd[0]) {
    case SUMP_CMD_SET_SAMPLE_RATE:
	sump_do_stop();
	sump.divider = val + 1;
	break;
    case SUMP_CMD_SET_COUNTS:
	sump_do_stop();
	sump_update_counts(val);
	break;
    case SUMP_CMD_SET_FLAGS:
	sump_do_stop();
	sump_set_flags(val);
	break;
    case SUMP_CMD_SET_ADV_TRG_SELECT:
    case SUMP_CMD_SET_ADV_TRG_DATA:
	break;			/* not implemented */

    case SUMP_CMD_SET_BTRG0_MASK:
    case SUMP_CMD_SET_BTRG1_MASK:
    case SUMP_CMD_SET_BTRG2_MASK:
    case SUMP_CMD_SET_BTRG3_MASK:
	sump_set_trigger_mask((cmd[0] - SUMP_CMD_SET_BTRG0_MASK) / 3, val);
	break;

    case SUMP_CMD_SET_BTRG0_VALUE:
    case SUMP_CMD_SET_BTRG1_VALUE:
    case SUMP_CMD_SET_BTRG2_VALUE:
    case SUMP_CMD_SET_BTRG3_VALUE:
	sump_set_trigger_value((cmd[0] - SUMP_CMD_SET_BTRG0_VALUE) / 3, val);
	break;

    case SUMP_CMD_SET_BTRG0_CONFIG:
    case SUMP_CMD_SET_BTRG1_CONFIG:
    case SUMP_CMD_SET_BTRG2_CONFIG:
    case SUMP_CMD_SET_BTRG3_CONFIG:
	sump_set_trigger_config((cmd[0] - SUMP_CMD_SET_BTRG0_CONFIG) / 3, val);
	break;
    default:
	return;
    }
}

void
sump_rx(uint8_t *buf, uint count)
{
    if (count == 0)
	return;
#if false
    picoprobe_debug("%s(): ", __func__);
    picoprobe_debug_hexa(buf, count);
    picoprobe_debug("\n");
#endif
    while (count-- > 0) {
        sump.cmd[sump.cmd_pos++] = *buf++;
        if (SUMP_CMD_IS_SHORT(sump.cmd[0])) {
            sump_rx_short(sump.cmd[0]);
            sump.cmd_pos = 0;
        } else if (sump.cmd_pos >= 5) {
            sump_rx_long(sump.cmd);
            sump.cmd_pos = 0;
        }
    }
}

static uint
sump_tx_empty(uint8_t *buf, uint len)
{
    uint32_t i, count;
    uint8_t a, b;

    count = sump.read_count;
    //picoprobe_debug("%s: count=%u\n", __func__, count);
    a = 0x55;
    if (sump.flags & SUMP_FLAG1_ENABLE_RLE) {
        count += count & 1; // align up
        if (sump.width == 1) {
            for (i = 0; i < len && count > 0; count -= 2, i += 2) {
                *buf++ = 0x81;	// RLE mark + two samples
                *buf++ = a;
                a ^= 0xff;
            }
            if (i > sump.read_count)
                sump.read_count = 0;
            else
                sump.read_count -= i;
        } else if (sump.width == 2) {
            for (i = 0; i < len && count > 0; count -= 2, i += 4) {
                *buf++ = 0x01;	// two samples
                *buf++ = 0x80;	// RLE mark + two samples
                *buf++ = a;
                *buf++ = a;
                a ^= 0xff;
            }
            if (i / 2 > sump.read_count)
                sump.read_count = 0;
            else
                sump.read_count -= i / 2;
        } else {
            return 0;
        }
    } else {
        if (sump.width == 1) {
            for (i = 0; i < len && count > 0; count--, i++) {
                *buf++ = a;
                a ^= 0xff;
            }
            sump.read_count -= i;
        } else if (sump.width == 2) {
            for (i = 0; i < len && count > 0; count--, i += 2) {
                *buf++ = a;
                *buf++ = a;
                a ^= 0xff;
            }
            sump.read_count -= i / 2;
        } else {
            return 0;
        }
    }
    //picoprobe_debug("%s: ret=%u\n", __func__, i);
    return i;
}

static uint
sump_tx8(uint8_t *buf, uint len)
{
    uint32_t i, count;
    uint8_t *ptr;

    count = sump.read_count;
    //picoprobe_debug("%s: count=%u, start=%u\n", __func__, count);
    ptr = sump.buffer + (sump.read_start + count) % SUMP_MEMORY_SIZE;
    if (sump.flags & SUMP_FLAG1_ENABLE_RLE) {
        uint8_t b, rle_last = 0x80, rle_count = 0;
        for (i = 0; i + 1 < len && count > 0; count--) {
            if (ptr == sump.buffer)
                ptr = sump.buffer + SUMP_MEMORY_SIZE;
            b = *(--ptr) & 0x7f;
            if (b != rle_last) {
                if (rle_count > 0) {
                    *((uint16_t *)buf) = (rle_count - 1) | 0x80 | ((uint16_t)rle_last << 8);
                    buf += 2;
                    i += 2;
                    sump.read_count -= rle_count;
                }
                rle_last = b;
                rle_count = 1;
                continue;
            }
            if (++rle_count == 0x80) {
                *((uint16_t *)buf) = (rle_count - 1) | 0x80 | ((uint16_t)rle_last << 8);
                buf += 2;
                i += 2;
                sump.read_count -= rle_count;
                rle_count = 0;
            }
        }
    } else {
        for (i = 0; i < len && count > 0; i++, count--) {
            if (ptr == sump.buffer)
                ptr = sump.buffer + SUMP_MEMORY_SIZE;
            *buf++ = *(--ptr);
        }
        sump.read_count -= i;
    }
    //picoprobe_debug("%s: ret=%u\n", __func__, i);
    return i;
}

static uint
sump_tx16(uint8_t *buf, uint len)
{
    uint32_t i, count;
    volatile uint8_t *ptr;

    count = sump.read_count;
    //picoprobe_debug("%s: count=%u, start=%u\n", __func__, count, sump.read_count);
    ptr = sump.buffer + (sump.read_start + count * 2) % SUMP_MEMORY_SIZE;
    if (sump.flags & SUMP_FLAG1_ENABLE_RLE) {
        uint16_t b, rle_last = 0x8000, rle_count = 0;
        for (i = 0; i + 3 < len && count > 0; count--) {
            if (ptr == sump.buffer)
                ptr = sump.buffer + SUMP_MEMORY_SIZE;
            ptr -= 2;
            b = *((uint16_t *)ptr) & 0x7fff;
            if (b != rle_last) {
                if (rle_count > 0) {
                    *((uint32_t *)buf) = (rle_count - 1) | 0x8000 | ((uint32_t)rle_last << 16);
                    buf += 4;
                    i += 4;
                    sump.read_count -= rle_count;
                }
                rle_last = b;
                rle_count = 1;
                continue;
            }
            if (++rle_count == 0x8000) {
                *((uint32_t *)buf) = (rle_count - 1) | 0x8000 | ((uint32_t)rle_last << 16);
                buf += 4;
                i += 4;
                sump.read_count -= rle_count;
                rle_count = 0;
            }
        }
    } else {
        for (i = 0; i + 1 < len && count > 0; i += 2, count--) {
            if (ptr == sump.buffer)
                ptr = sump.buffer + SUMP_MEMORY_SIZE;
            ptr -= 2;
            *((uint16_t *)buf) = *((uint16_t *)ptr);
            buf += 2;
        }
        sump.read_count -= i / 2;
    }
    //picoprobe_debug("%s: ret=%u\n", __func__, i);
    return i;
}

static uint
sump_fill_tx(uint8_t *buf, uint len)
{
    uint ret;

    assert((len & 3) == 0);
    if (sump.read_count == 0) {
        sump.state = SUMP_STATE_CONFIG;
        return 0;
    }
    if (sump.state == SUMP_STATE_DUMP) {
        if (sump.width == 1) {
            ret = sump_tx8(buf, len);
        } else if (sump.width == 2) {
            ret = sump_tx16(buf, len);
        } else {
            // invalid
            ret = sump_tx_empty(buf, len);
        }
    } else {
        // invalid or error
        ret = sump_tx_empty(buf, len);
    }
    if (ret == 0)
        sump.state = SUMP_STATE_CONFIG;
    return ret;
}

static void
cdc_sump_init_connect(void)
{
    uint32_t pio_off;

    pio_off = sump.pio_prog_offset;
    memset(&sump, 0, sizeof(sump));
    sump.pio_prog_offset = pio_off;
    sump.width = 1;
    sump.divider = 1000;		// a safe value
    sump.read_count = 256;
    sump.delay_count = 256;

    picoprobe_debug("%s(): memory buffer %u bytes\n", __func__, SUMP_MEMORY_SIZE);
}

void
cdc_sump_init(void)
{
    uint i;

    // claim DMA channels
    dma_claim_mask(SUMP_DMA_MASK);

    // claim PIO state machine and add program
    pio_claim_sm_mask(SAMPLING_PIO, 1u << SAMPLING_PIO_SM);
    sump_pio_program();

    // high bus priority to the DMA
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    // GPIO init
    gpio_set_dir_in_masked(SAMPLING_GPIO_MASK);
    gpio_put_masked(SAMPLING_GPIO_MASK, 0);
    for (i = SAMPLING_GPIO_FIRST; i <= SAMPLING_GPIO_LAST; i++) {
        gpio_set_function(i, GPIO_FUNC_NULL);
        gpio_set_pulls(i, false, false);
    }

    // test GPIO pin
    gpio_set_dir(SAMPLING_GPIO_TEST, true);
    gpio_put(SAMPLING_GPIO_TEST, true);
    gpio_set_function(SAMPLING_GPIO_TEST, GPIO_FUNC_PWM);

    // set exclusive interrupt handler
    irq_set_enabled(SAMPLING_DMA_IRQ, false);
    irq_set_exclusive_handler(SAMPLING_DMA_IRQ, sump_dma_irq_handler);
    sump_dma_set_irq_channel_mask_enabled(SUMP_DMA_MASK, true);

    cdc_sump_init_connect();

    picoprobe_debug("%s()\n", __func__);
}

#define MAX_UART_PKT 64
void
cdc_sump_task(void)
{
    uint8_t buf[MAX_UART_PKT];

    if (tud_cdc_n_connected(CDC_INTF)) {
        if (!sump.cdc_connected) {
            cdc_sump_init_connect();
            sump.cdc_connected = true;
        }
        if (sump.state == SUMP_STATE_DUMP || sump.state == SUMP_STATE_ERROR) {
            if (tud_cdc_n_write_available(CDC_INTF) >= sizeof(buf)) {
                uint tx_len = sump_fill_tx(buf, sizeof(buf));
                tud_cdc_n_write(CDC_INTF, buf, tx_len);
                tud_cdc_n_write_flush(CDC_INTF);
            }
        }
	if (tud_cdc_n_available(CDC_INTF)) {
	    uint cmd_len = tud_cdc_n_read(CDC_INTF, buf, sizeof(buf));
	    sump_rx(buf, cmd_len);
	}
	if (sump.state == SUMP_STATE_TRIGGER || sump.state == SUMP_STATE_SAMPLING)
            led_signal_activity(1);
    } else if (!sump.cdc_connected) {
        sump.cdc_connected = false;
        sump_do_reset();
    }
}

void
cdc_sump_line_coding(cdc_line_coding_t const *line_coding)
{
    picoprobe_info("Sump new baud rate %d\n", line_coding->bit_rate);
}
