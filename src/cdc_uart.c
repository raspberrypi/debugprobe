/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
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
 */

#include <pico/stdlib.h>
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "task.h"
#include "event_groups.h"

#include "tusb.h"

#include "picoprobe_config.h"
#include "led.h"


#define STREAM_UART_SIZE      1024
#define STREAM_UART_TRIGGER   32

static TaskHandle_t           task_uart = NULL;
static StreamBufferHandle_t   stream_uart;

static bool was_connected = false;


#define EV_TX_COMPLETE        0x01
#define EV_STREAM             0x02
#define EV_RX                 0x04

/// event flags
static EventGroupHandle_t     events;



void cdc_thread(void *ptr)
{
    static uint8_t cdc_tx_buf[CFG_TUD_CDC_TX_BUFSIZE];

    for (;;) {
        size_t cdc_rx_chars;

        if ( !was_connected) {
            // wait here some time (until my terminal program is ready)
            was_connected = true;
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        cdc_rx_chars = tud_cdc_n_available(CDC_UART_N);
        if (cdc_rx_chars == 0  &&  xStreamBufferIsEmpty(stream_uart)) {
            // -> nothing left to do: sleep for a long time
            tud_cdc_n_write_flush(CDC_UART_N);
            xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
        }
        else if (cdc_rx_chars != 0) {
            // wait a short period if there are characters host -> probe -> target
            xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(1));
        }
        else {
            // wait until transmission via USB has finished
            xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        }

        if ( !xStreamBufferIsEmpty(stream_uart)) {
            //
            // transmit characters target -> picoprobe -> host
            //
            size_t cnt;
            size_t max_cnt;

            max_cnt = tud_cdc_n_write_available(CDC_UART_N);
            if (max_cnt != 0) {
                max_cnt = MIN(sizeof(cdc_tx_buf), max_cnt);
                cnt = xStreamBufferReceive(stream_uart, cdc_tx_buf, max_cnt, pdMS_TO_TICKS(500));
                if (cnt != 0) {
                    tud_cdc_n_write(CDC_UART_N, cdc_tx_buf, cnt);
                }
            }
        }
        else {
            tud_cdc_n_write_flush(CDC_UART_N);
        }

        //
        // transmit characters host -> probe -> target
        // (actually don't know if this works, but note that in worst case this loop is taken just every 50ms.
        //  So this is not for high throughput)
        //
        cdc_rx_chars = tud_cdc_n_available(CDC_UART_N);
        if (cdc_rx_chars > 0  &&  uart_is_writable(PICOPROBE_UART_INTERFACE)) {
            uint8_t ch;
            size_t tx_len;

            tx_len = tud_cdc_n_read(CDC_UART_N, &ch, 1);
            uart_write_blocking(PICOPROBE_UART_INTERFACE, &ch, tx_len);
        }
    }
}   // cdc_thread



//
// CDC bitrate updates are reflected on \a PICOPROBE_UART_INTERFACE
//
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding)
{
    if (itf == CDC_UART_N) {
        vTaskSuspend(task_uart);
        tud_cdc_n_write_clear(CDC_UART_N);
        tud_cdc_n_read_flush(CDC_UART_N);
        uart_set_baudrate(PICOPROBE_UART_INTERFACE, line_coding->bit_rate);
        vTaskResume(task_uart);
    }
}   // tud_cdc_line_coding_cb



void cdc_uart_line_state_cb(bool dtr, bool rts)
{
    /* CDC drivers use linestate as a bodge to activate/deactivate the interface.
     * Resume our UART polling on activate, stop on deactivate */
    if (!dtr  &&  !rts) {
        vTaskSuspend(task_uart);
        tud_cdc_n_write_clear(CDC_UART_N);
        was_connected = false;
    }
    else {
        vTaskResume(task_uart);
    }
}   // cdc_uart_line_state_cb



void cdc_uart_tx_complete_cb(void)
{
    xEventGroupSetBits(events, EV_TX_COMPLETE);
}   // cdc_uart_tx_complete_cb



void cdc_uart_rx_cb()
{
    xEventGroupSetBits(events, EV_RX);
}   // cdc_uart_rx_cb



void on_uart_rx(void)
{
    uint8_t buf[40];
    uint32_t cnt;
    uint8_t ch;

    cnt = 0;
    while (uart_is_readable(PICOPROBE_UART_INTERFACE)) {
        ch = (uint8_t)uart_getc(PICOPROBE_UART_INTERFACE);
        if (cnt < sizeof(buf)) {
            buf[cnt++] = ch;
        }
    }

    if (cnt != 0) {
        led_state(LS_UART_DATA);
        xStreamBufferSendFromISR(stream_uart, buf, cnt, NULL);
    }

    {
        BaseType_t task_woken, res;

        task_woken = pdFALSE;
        res = xEventGroupSetBitsFromISR(events, EV_STREAM, &task_woken);
        if (res != pdFAIL) {
            portYIELD_FROM_ISR(task_woken);
        }
    }
}   // on_uart_rx



void cdc_uart_write(const uint8_t *buf, uint32_t cnt)
{
    xStreamBufferSend(stream_uart, buf, cnt, pdMS_TO_TICKS(100));
    xEventGroupSetBits(events, EV_STREAM);
}   // cdc_uart_write



void cdc_uart_init(uint32_t task_prio)
{
    events = xEventGroupCreate();

    stream_uart = xStreamBufferCreate(STREAM_UART_SIZE, STREAM_UART_TRIGGER);
    if (stream_uart == NULL) {
        picoprobe_error("cdc_uart_init: cannot create stream_uart\n");
    }

    gpio_set_function(PICOPROBE_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICOPROBE_UART_RX, GPIO_FUNC_UART);
    gpio_set_pulls(PICOPROBE_UART_TX, 1, 0);
    gpio_set_pulls(PICOPROBE_UART_RX, 1, 0);

    uart_init(PICOPROBE_UART_INTERFACE, PICOPROBE_UART_BAUDRATE);
    uart_set_format(PICOPROBE_UART_INTERFACE, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(PICOPROBE_UART_INTERFACE, true);

    int UART_IRQ = (PICOPROBE_UART_INTERFACE == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(PICOPROBE_UART_INTERFACE, true, false);

    /* UART needs to preempt USB as if we don't, characters get lost */
    xTaskCreate(cdc_thread, "CDC_UART", configMINIMAL_STACK_SIZE, NULL, task_prio, &task_uart);
    cdc_uart_line_state_cb(false, false);
}   // cdc_uart_init
