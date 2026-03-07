#ifndef MOCKPROBE_TRANSPORT_H
#define MOCKPROBE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/uart.h"

typedef enum {
    MOCKPROBE_UART_BACKEND_AUTO = 0,
    MOCKPROBE_UART_BACKEND_HW,
    MOCKPROBE_UART_BACKEND_PIO,
} mockprobe_uart_backend_preference_t;

void mockprobe_transport_init(void);
bool mockprobe_transport_configure_uart(uint uart_id, uint tx_gpio, uint rx_gpio, uint32_t baud,
                                        mockprobe_uart_backend_preference_t preference,
                                        char *detail, size_t detail_size);
void mockprobe_transport_set_baudrate(uint32_t baud);
void mockprobe_transport_set_format(uint data_bits, uint stop_bits, uart_parity_t parity);
bool mockprobe_transport_set_loopback(bool enabled);
bool mockprobe_transport_get_loopback(void);
size_t mockprobe_transport_read(uint8_t *buffer, size_t max_len);
void mockprobe_transport_write(const uint8_t *buffer, size_t length);
void mockprobe_transport_write_byte(uint8_t byte);
void mockprobe_transport_flush_rx(void);
bool mockprobe_transport_is_active_gpio(uint gpio);
const char *mockprobe_transport_backend_name(void);
uint mockprobe_transport_uart_id(void);
uint mockprobe_transport_tx_gpio(void);
uint mockprobe_transport_rx_gpio(void);

#endif
