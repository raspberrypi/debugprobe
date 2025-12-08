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

/**
 * Target (debug) input/output via CDC to host.
 *
 * Target -> Probe -> Host
 * -----------------------
 * * target -> probe
 *   * UART: interrupt handler on_uart_rx() to cdc_uart_put_into_stream()
 *   * RTT: (rtt_console) to cdc_uart_write()
 *   * UART/RTT data is written into \a stream_uart via
 * * probe -> host: cdc_thread()
 *   * data is fetched from \a stream_uart and then put into a CDC
 *     in cdc_thread()
 *
 * Host -> Probe -> Target
 * -----------------------
 * * host -> probe
 *   * data is received from CDC in cdc_thread()
 * * probe -> target
 *   * data is first tried to be transmitted via RTT
 *   * if that was not successful (no RTT_CB), data is transmitted via UART to the target
 */

#include <stdio.h>
#include <pico/stdlib.h>
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "task.h"
#include "event_groups.h"

#include "tusb.h"

#include "picoprobe_config.h"
#include "led.h"
#include "rtt_io.h"


#define STREAM_UART_SIZE      4096
#define STREAM_UART_TRIGGER   32

static TaskHandle_t           task_uart = NULL;
static StreamBufferHandle_t   stream_uart;

static volatile bool m_connected = false;


#define EV_TX_COMPLETE        0x01
#define EV_STREAM             0x02
#define EV_RX                 0x04

/// event flags
static EventGroupHandle_t     events;



static void cdc_thread(void *ptr)
{
    static uint8_t cdc_tx_buf[CFG_TUD_CDC_TX_BUFSIZE];

    for (;;) {
        uint32_t cdc_rx_chars;

        if ( !m_connected) {
            // wait here until connected (and until my terminal program is ready)
            while ( !m_connected) {
                xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
            }
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
            // waiting is done below
        }
        else {
            // wait until transmission via USB has finished
            xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
        }

        //
        // probe -> host
        //
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
        // host -> probe -> target
        // -----------------------
        // Characters are transferred bytewise to keep delays into the other direction low.
        // So this is not a high throughput solution...
        //
        cdc_rx_chars = tud_cdc_n_available(CDC_UART_N);
        if (cdc_rx_chars != 0) {
            if (rtt_console_cb_exists()) {
                //
                // -> data is going thru RTT
                //
                uint8_t ch;
                uint32_t tx_len;

                tx_len = tud_cdc_n_read(CDC_UART_N, &ch, sizeof(ch));
                if (tx_len != 0) {
                    rtt_console_send_byte(ch);
                }
            }
            else {
                //
                // -> data is going thru UART
                //    assure that the UART has free buffer, otherwise wait (for a short moment)
                //
                if ( !uart_is_writable(PICOPROBE_UART_INTERFACE)) {
                    xEventGroupWaitBits(events, EV_TX_COMPLETE | EV_STREAM | EV_RX, pdTRUE, pdFALSE, pdMS_TO_TICKS(1));
                }
                else {
                    uint8_t ch;
                    uint32_t tx_len;

                    tx_len = tud_cdc_n_read(CDC_UART_N, &ch, sizeof(ch));
                    if (tx_len != 0) {
                        led_state(LS_UART_TX_DATA);
                        uart_write_blocking(PICOPROBE_UART_INTERFACE, &ch, tx_len);
                    }
                }
            }
        }
    }
}   // cdc_thread



void cdc_uart_line_coding_cb(cdc_line_coding_t const* line_coding)
/**
 * CDC bitrate updates are reflected on \a PICOPROBE_UART_INTERFACE
 */
{
    uart_set_baudrate(PICOPROBE_UART_INTERFACE, line_coding->bit_rate);
}   // cdc_uart_line_coding_cb



void cdc_uart_line_state_cb(bool dtr, bool rts)
/**
 * Flush tinyusb buffers on connect/disconnect.
 * This seems to be necessary to survive e.g. a restart of the host (Linux)
 */
{
    tud_cdc_n_write_clear(CDC_UART_N);
    tud_cdc_n_read_flush(CDC_UART_N);
    m_connected = (dtr  ||  rts);
    xEventGroupSetBits(events, EV_TX_COMPLETE);
}   // cdc_uart_line_state_cb



void cdc_uart_tx_complete_cb(void)
{
    xEventGroupSetBits(events, EV_TX_COMPLETE);
}   // cdc_uart_tx_complete_cb



void cdc_uart_rx_cb()
{
    xEventGroupSetBits(events, EV_RX);
}   // cdc_uart_rx_cb



static uint32_t cdc_uart_put_into_stream(const void *data, size_t len, bool in_isr)
/**
 * Write into stream.  If not connected use the stream as a FIFO and drop old content.
 */
{
    uint32_t r = 0;

    if ( !m_connected) {
        size_t available = xStreamBufferSpacesAvailable(stream_uart);
        for (;;) {
            uint8_t dummy[64];
            size_t n;

            if (available >= len) {
                break;
            }
            if (in_isr) {
                n = xStreamBufferReceiveFromISR(stream_uart, dummy, sizeof(dummy), NULL);
            }
            else {
                n = xStreamBufferReceive(stream_uart, dummy, sizeof(dummy), 0);
            }
            if (n <= 0) {
                break;
            }
            available += n;
        }
    }
    if (in_isr) {
        r = xStreamBufferSendFromISR(stream_uart, data, len, NULL);
    }
    else {
        r = xStreamBufferSend(stream_uart, data, len, 0);     // drop characters in worst case
    }

    return r;
}   // cdc_uart_put_into_stream



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
        led_state(LS_UART_RX_DATA);
        cdc_uart_put_into_stream(buf, cnt, true);
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



uint32_t cdc_uart_write(const uint8_t *buf, uint32_t cnt)
/**
 * Send characters from console RTT channel into stream.
 *
 * \param buf  pointer to the buffer to be sent, if NULL then remaining space in stream is returned
 * \param cnt  number of bytes to be sent
 * \return if \buf is NULL the remaining space in stream is returned, otherwise the number of bytes
 */
{
    uint32_t r = 0;

    if (buf == NULL) {
        r = xStreamBufferSpacesAvailable(stream_uart);
    }
    else {
        r = cdc_uart_put_into_stream(buf, cnt, false);
        xEventGroupSetBits(events, EV_STREAM);
    }

    return r;
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
    xTaskCreate(cdc_thread, "CDC-TargetUart", configMINIMAL_STACK_SIZE, NULL, task_prio, &task_uart);
    cdc_uart_line_state_cb(false, false);
}   // cdc_uart_init
