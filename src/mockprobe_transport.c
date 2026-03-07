#include "mockprobe_transport.h"

#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/regs/uart.h"
#include "hardware/structs/uart.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "probe_config.h"

#include "mockprobe_uart_rx.pio.h"
#include "mockprobe_uart_tx.pio.h"

typedef enum {
    MOCKPROBE_BACKEND_HW_UART = 0,
    MOCKPROBE_BACKEND_PIO_UART
} mockprobe_backend_t;

typedef struct {
    mockprobe_backend_t backend;
    uint uart_id;
    uint tx_gpio;
    uint rx_gpio;
    uint32_t baud;
    uart_inst_t *hw_uart;
    PIO pio;
    int tx_sm;
    int rx_sm;
    int tx_offset;
    int rx_offset;
    bool pio_claimed;
    uint8_t rx_shadow[256];
    size_t rx_shadow_head;
    size_t rx_shadow_tail;
} mockprobe_transport_state_t;

static mockprobe_transport_state_t g_transport;

static size_t transport_rx_shadow_count(void) {
    return (g_transport.rx_shadow_head + sizeof(g_transport.rx_shadow) - g_transport.rx_shadow_tail) % sizeof(g_transport.rx_shadow);
}

static bool transport_rx_shadow_push(uint8_t value) {
    size_t next = (g_transport.rx_shadow_head + 1) % sizeof(g_transport.rx_shadow);
    if (next == g_transport.rx_shadow_tail) {
        return false;
    }
    g_transport.rx_shadow[g_transport.rx_shadow_head] = value;
    g_transport.rx_shadow_head = next;
    return true;
}

static bool transport_rx_shadow_pop(uint8_t *value) {
    if (g_transport.rx_shadow_head == g_transport.rx_shadow_tail) {
        return false;
    }
    *value = g_transport.rx_shadow[g_transport.rx_shadow_tail];
    g_transport.rx_shadow_tail = (g_transport.rx_shadow_tail + 1) % sizeof(g_transport.rx_shadow);
    return true;
}

static void transport_drain_pio_rx_to_shadow(void) {
    if (g_transport.backend != MOCKPROBE_BACKEND_PIO_UART) {
        return;
    }
    while (!pio_sm_is_rx_fifo_empty(g_transport.pio, (uint)g_transport.rx_sm)) {
        uint8_t value = (uint8_t)mockprobe_uart_rx_program_getc(g_transport.pio, (uint)g_transport.rx_sm);
        if (!transport_rx_shadow_push(value)) {
            break;
        }
    }
}

static const uint8_t UART0_TX_PINS[] = {0, 2, 12, 14, 16, 18, 28, 30, 32, 34, 44, 46};
static const uint8_t UART0_RX_PINS[] = {1, 3, 13, 15, 17, 19, 29, 31, 33, 35, 45, 47};
static const uint8_t UART1_TX_PINS[] = {4, 6, 8, 10, 20, 22, 24, 26, 36, 38, 40, 42};
static const uint8_t UART1_RX_PINS[] = {5, 7, 9, 11, 21, 23, 25, 27, 37, 39, 41, 43};

static bool uint_in_list(uint value, const uint8_t *list, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (list[i] == value) {
            return true;
        }
    }
    return false;
}

static bool transport_supports_hw_uart(uint uart_id, uint tx_gpio, uint rx_gpio) {
    if (uart_id == 0) {
        return uint_in_list(tx_gpio, UART0_TX_PINS, count_of(UART0_TX_PINS)) &&
               uint_in_list(rx_gpio, UART0_RX_PINS, count_of(UART0_RX_PINS));
    }
    if (uart_id == 1) {
        return uint_in_list(tx_gpio, UART1_TX_PINS, count_of(UART1_TX_PINS)) &&
               uint_in_list(rx_gpio, UART1_RX_PINS, count_of(UART1_RX_PINS));
    }
    return false;
}

static void transport_release_pio(void) {
    if (!g_transport.pio_claimed) {
        return;
    }
    pio_sm_set_enabled(g_transport.pio, (uint)g_transport.tx_sm, false);
    pio_sm_set_enabled(g_transport.pio, (uint)g_transport.rx_sm, false);
    pio_remove_program(g_transport.pio, &mockprobe_uart_tx_program, (uint)g_transport.tx_offset);
    pio_remove_program(g_transport.pio, &mockprobe_uart_rx_program, (uint)g_transport.rx_offset);
    pio_sm_unclaim(g_transport.pio, (uint)g_transport.tx_sm);
    pio_sm_unclaim(g_transport.pio, (uint)g_transport.rx_sm);
    g_transport.pio_claimed = false;
}

static void transport_reset_active_pins(void) {
    gpio_set_function(g_transport.tx_gpio, GPIO_FUNC_SIO);
    gpio_set_function(g_transport.rx_gpio, GPIO_FUNC_SIO);
    gpio_disable_pulls(g_transport.tx_gpio);
    gpio_disable_pulls(g_transport.rx_gpio);
}

static void transport_teardown(void) {
    if (g_transport.backend == MOCKPROBE_BACKEND_HW_UART && g_transport.hw_uart != NULL) {
        uart_deinit(g_transport.hw_uart);
    } else if (g_transport.backend == MOCKPROBE_BACKEND_PIO_UART) {
        transport_release_pio();
    }
    transport_reset_active_pins();
    g_transport.rx_shadow_head = 0;
    g_transport.rx_shadow_tail = 0;
}

static void transport_configure_hw_uart(uint uart_id, uint tx_gpio, uint rx_gpio, uint32_t baud) {
    g_transport.backend = MOCKPROBE_BACKEND_HW_UART;
    g_transport.uart_id = uart_id;
    g_transport.tx_gpio = tx_gpio;
    g_transport.rx_gpio = rx_gpio;
    g_transport.baud = baud;
    g_transport.hw_uart = uart_id == 0 ? uart0 : uart1;

    gpio_set_function(tx_gpio, UART_FUNCSEL_NUM(g_transport.hw_uart, tx_gpio));
    gpio_set_function(rx_gpio, UART_FUNCSEL_NUM(g_transport.hw_uart, rx_gpio));
    gpio_set_pulls(tx_gpio, true, false);
    gpio_set_pulls(rx_gpio, true, false);
    uart_init(g_transport.hw_uart, baud);
    uart_set_hw_flow(g_transport.hw_uart, false, false);
    uart_set_format(g_transport.hw_uart, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(g_transport.hw_uart, true);
}

static bool transport_configure_pio_uart(uint uart_id, uint tx_gpio, uint rx_gpio, uint32_t baud,
                                         char *detail, size_t detail_size) {
    (void)uart_id;

    PIO pio = pio1;
    int tx_sm = (int)pio_claim_unused_sm(pio, false);
    int rx_sm = (int)pio_claim_unused_sm(pio, false);
    if (tx_sm < 0 || rx_sm < 0) {
        if (tx_sm >= 0) {
            pio_sm_unclaim(pio, (uint)tx_sm);
        }
        snprintf(detail, detail_size, "CFG_UART no_pio_sm");
        return false;
    }

    uint tx_offset = pio_add_program(pio, &mockprobe_uart_tx_program);
    uint rx_offset = pio_add_program(pio, &mockprobe_uart_rx_program);

    g_transport.backend = MOCKPROBE_BACKEND_PIO_UART;
    g_transport.uart_id = uart_id;
    g_transport.tx_gpio = tx_gpio;
    g_transport.rx_gpio = rx_gpio;
    g_transport.baud = baud;
    g_transport.pio = pio;
    g_transport.tx_sm = tx_sm;
    g_transport.rx_sm = rx_sm;
    g_transport.tx_offset = (int)tx_offset;
    g_transport.rx_offset = (int)rx_offset;
    g_transport.pio_claimed = true;

    mockprobe_uart_tx_program_init(pio, (uint)tx_sm, tx_offset, tx_gpio, baud);
    mockprobe_uart_rx_program_init(pio, (uint)rx_sm, rx_offset, rx_gpio, baud);
    pio_sm_clear_fifos(pio, (uint)rx_sm);
    pio_sm_restart(pio, (uint)rx_sm);
    g_transport.rx_shadow_head = 0;
    g_transport.rx_shadow_tail = 0;
    return true;
}

void mockprobe_transport_init(void) {
    memset(&g_transport, 0, sizeof(g_transport));
    g_transport.tx_gpio = PROBE_UART_TX;
    g_transport.rx_gpio = PROBE_UART_RX;
    transport_configure_hw_uart((PROBE_UART_INTERFACE == uart0) ? 0u : 1u,
                                PROBE_UART_TX, PROBE_UART_RX, PROBE_UART_BAUDRATE);
}

bool mockprobe_transport_configure_uart(uint uart_id, uint tx_gpio, uint rx_gpio, uint32_t baud,
                                        mockprobe_uart_backend_preference_t preference,
                                        char *detail, size_t detail_size) {
    if (detail_size > 0) {
        detail[0] = '\0';
    }
    if (tx_gpio == rx_gpio || tx_gpio > 47 || rx_gpio > 47 || uart_id > 1 || baud == 0) {
        snprintf(detail, detail_size, "CFG_UART invalid_args");
        return false;
    }

    transport_teardown();

    if (preference != MOCKPROBE_UART_BACKEND_PIO && transport_supports_hw_uart(uart_id, tx_gpio, rx_gpio)) {
        transport_configure_hw_uart(uart_id, tx_gpio, rx_gpio, baud);
        snprintf(detail, detail_size, "CFG_UART hw");
        return true;
    }

    if (preference != MOCKPROBE_UART_BACKEND_HW &&
        transport_configure_pio_uart(uart_id, tx_gpio, rx_gpio, baud, detail, detail_size)) {
        snprintf(detail, detail_size, "CFG_UART pio");
        return true;
    }

    transport_configure_hw_uart((PROBE_UART_INTERFACE == uart0) ? 0u : 1u,
                                PROBE_UART_TX, PROBE_UART_RX, PROBE_UART_BAUDRATE);
    return false;
}

bool mockprobe_transport_set_loopback(bool enabled) {
    if (g_transport.backend != MOCKPROBE_BACKEND_HW_UART || g_transport.hw_uart == NULL) {
        return false;
    }
    uart_hw_t *hw = g_transport.hw_uart == uart0 ? uart0_hw : uart1_hw;
    if (enabled) {
        hw->cr |= UART_UARTCR_LBE_BITS;
    } else {
        hw->cr &= ~UART_UARTCR_LBE_BITS;
    }
    return true;
}

bool mockprobe_transport_get_loopback(void) {
    if (g_transport.backend != MOCKPROBE_BACKEND_HW_UART || g_transport.hw_uart == NULL) {
        return false;
    }
    uart_hw_t *hw = g_transport.hw_uart == uart0 ? uart0_hw : uart1_hw;
    return (hw->cr & UART_UARTCR_LBE_BITS) != 0;
}

void mockprobe_transport_set_baudrate(uint32_t baud) {
    char detail[32];
    (void)mockprobe_transport_configure_uart(g_transport.uart_id, g_transport.tx_gpio, g_transport.rx_gpio, baud,
                                             g_transport.backend == MOCKPROBE_BACKEND_HW_UART ? MOCKPROBE_UART_BACKEND_HW : MOCKPROBE_UART_BACKEND_PIO,
                                             detail, sizeof(detail));
}

void mockprobe_transport_set_format(uint data_bits, uint stop_bits, uart_parity_t parity) {
    if (g_transport.backend == MOCKPROBE_BACKEND_HW_UART && g_transport.hw_uart != NULL) {
        uart_set_format(g_transport.hw_uart, data_bits, stop_bits, parity);
    }
}

size_t mockprobe_transport_read(uint8_t *buffer, size_t max_len) {
    size_t count = 0;
    if (g_transport.backend == MOCKPROBE_BACKEND_PIO_UART) {
        transport_drain_pio_rx_to_shadow();
        while (count < max_len && transport_rx_shadow_pop(&buffer[count])) {
            ++count;
        }
        transport_drain_pio_rx_to_shadow();
        while (count < max_len && transport_rx_shadow_pop(&buffer[count])) {
            ++count;
        }
        return count;
    }
    if (g_transport.backend == MOCKPROBE_BACKEND_HW_UART) {
        while (count < max_len && uart_is_readable(g_transport.hw_uart)) {
            buffer[count++] = uart_getc(g_transport.hw_uart);
        }
    }
    return count;
}

void mockprobe_transport_write(const uint8_t *buffer, size_t length) {
    if (g_transport.backend == MOCKPROBE_BACKEND_HW_UART) {
        uart_write_blocking(g_transport.hw_uart, buffer, length);
    } else {
        uint32_t char_us = (1000000u * 10u) / (g_transport.baud ? g_transport.baud : 115200u);
        for (size_t i = 0; i < length; ++i) {
            mockprobe_uart_tx_program_putc(g_transport.pio, (uint)g_transport.tx_sm, (char)buffer[i]);
            sleep_us(char_us + 20);
            transport_drain_pio_rx_to_shadow();
        }
    }
}

void mockprobe_transport_write_byte(uint8_t byte) {
    mockprobe_transport_write(&byte, 1);
}

void mockprobe_transport_flush_rx(void) {
    if (g_transport.backend == MOCKPROBE_BACKEND_HW_UART) {
        while (uart_is_readable(g_transport.hw_uart)) {
            (void)uart_getc(g_transport.hw_uart);
        }
    } else {
        g_transport.rx_shadow_head = 0;
        g_transport.rx_shadow_tail = 0;
        while (!pio_sm_is_rx_fifo_empty(g_transport.pio, (uint)g_transport.rx_sm)) {
            (void)mockprobe_uart_rx_program_getc(g_transport.pio, (uint)g_transport.rx_sm);
        }
    }
}

bool mockprobe_transport_is_active_gpio(uint gpio) {
    return gpio == g_transport.tx_gpio || gpio == g_transport.rx_gpio;
}

const char *mockprobe_transport_backend_name(void) {
    return g_transport.backend == MOCKPROBE_BACKEND_HW_UART ? "hw" : "pio";
}

uint mockprobe_transport_uart_id(void) {
    return g_transport.uart_id;
}

uint mockprobe_transport_tx_gpio(void) {
    return g_transport.tx_gpio;
}

uint mockprobe_transport_rx_gpio(void) {
    return g_transport.rx_gpio;
}
