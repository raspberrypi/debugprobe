#include <pico/stdlib.h>

#include "tusb.h"

#include "picoprobe_config.h"

void cdc_uart_init(void) {
    gpio_set_function(PICOPROBE_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PICOPROBE_UART_RX, GPIO_FUNC_UART);
    uart_init(uart1, 115200);
}

#define MAX_UART_PKT 64
void cdc_task(void) {
    uint8_t rx_buf[MAX_UART_PKT];
    uint8_t tx_buf[MAX_UART_PKT];

    // Consume uart fifo regardless even if not connected
    uint rx_len = 0;
    while(uart_is_readable(uart1) && (rx_len < MAX_UART_PKT)) {
        rx_buf[rx_len++] = uart_getc(uart1);
    }

    if (tud_cdc_connected()) {
        // Do we have anything to display on the host's terminal?
        if (rx_len) {
            for (uint i = 0; i < rx_len; i++) {
                tud_cdc_write_char(rx_buf[i]);
            }
            tud_cdc_write_flush();
        }

        if (tud_cdc_available()) {
            // Is there any data from the host for us to tx
            uint tx_len = tud_cdc_read(tx_buf, sizeof(tx_buf));
            uart_write_blocking(uart1, tx_buf, tx_len);
        }
    }
}

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding) {
    picoprobe_info("New baud rate %d\n", line_coding->bit_rate);
    uart_init(uart1, line_coding->bit_rate);
}