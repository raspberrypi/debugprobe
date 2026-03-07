#include "mockprobe_mode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "probe_config.h"
#include "tusb.h"
#include "mockprobe_transport.h"

#define MOCKPROBE_MAX_PINS 16
#define MOCKPROBE_MAX_NAME 16
#define MOCKPROBE_UART_RX_BUF 128

typedef struct {
    bool used;
    char name[MOCKPROBE_MAX_NAME];
    uint gpio;
    bool is_output;
} mockprobe_pin_t;

static mockprobe_pin_t g_pins[MOCKPROBE_MAX_PINS];
static uint8_t g_uart_rx_buf[MOCKPROBE_UART_RX_BUF];
static size_t g_uart_rx_len;

static void mockprobe_write_line(const char *prefix, const char *detail) {
    tud_cdc_write_str(prefix);
    if (detail != NULL && detail[0] != '\0') {
        tud_cdc_write_str(" ");
        tud_cdc_write_str(detail);
    }
    tud_cdc_write_str("\r\n");
    tud_cdc_write_flush();
}

static void mockprobe_ok(const char *detail) {
    mockprobe_write_line("OK", detail);
}

static void mockprobe_err(const char *detail) {
    mockprobe_write_line("ERR", detail);
}

static void mockprobe_evt(const char *topic, const char *detail) {
    tud_cdc_write_str("EVT ");
    tud_cdc_write_str(topic);
    if (detail != NULL && detail[0] != '\0') {
        tud_cdc_write_str(" ");
        tud_cdc_write_str(detail);
    }
    tud_cdc_write_str("\r\n");
    tud_cdc_write_flush();
}

static bool next_token(char **cursor, char **out) {
    char *p = *cursor;
    while (*p == ' ') {
        ++p;
    }
    if (*p == '\0') {
        return false;
    }
    char *start = p;
    while (*p != '\0' && *p != ' ') {
        ++p;
    }
    if (*p != '\0') {
        *p++ = '\0';
    }
    *cursor = p;
    *out = start;
    return true;
}

static bool json_unescape_inplace(char *text) {
    size_t length = strlen(text);
    if (length < 2 || text[0] != '"' || text[length - 1] != '"') {
        return false;
    }

    char *src = text + 1;
    char *dst = text;
    char *end = text + length - 1;
    while (src < end) {
        if (*src == '\\') {
            ++src;
            if (src >= end) {
                return false;
            }
            switch (*src) {
            case 'n': *dst++ = '\n'; break;
            case 'r': *dst++ = '\r'; break;
            case 't': *dst++ = '\t'; break;
            case '\\': *dst++ = '\\'; break;
            case '"': *dst++ = '"'; break;
            default: return false;
            }
            ++src;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
    return true;
}

static bool gpio_reserved(uint gpio) {
    if (gpio == PROBE_PIN_RESET || gpio == PROBE_PIN_SWCLK || gpio == PROBE_PIN_SWDIO) {
        return true;
    }
#ifdef PROBE_USB_CONNECTED_LED
    if (gpio == PROBE_USB_CONNECTED_LED) {
        return true;
    }
#endif
#ifdef PROBE_UART_RTS
    if (gpio == PROBE_UART_RTS) {
        return true;
    }
#endif
#ifdef PROBE_UART_DTR
    if (gpio == PROBE_UART_DTR) {
        return true;
    }
#endif
#ifdef PROBE_UART_CTS
    if (gpio == PROBE_UART_CTS) {
        return true;
    }
#endif
    return false;
}

static mockprobe_pin_t *find_pin(const char *name) {
    for (size_t i = 0; i < MOCKPROBE_MAX_PINS; ++i) {
        if (g_pins[i].used && strcmp(g_pins[i].name, name) == 0) {
            return &g_pins[i];
        }
    }
    return NULL;
}

static mockprobe_pin_t *alloc_pin(const char *name) {
    mockprobe_pin_t *slot = find_pin(name);
    if (slot != NULL) {
        return slot;
    }
    for (size_t i = 0; i < MOCKPROBE_MAX_PINS; ++i) {
        if (!g_pins[i].used) {
            memset(&g_pins[i], 0, sizeof(g_pins[i]));
            g_pins[i].used = true;
            strncpy(g_pins[i].name, name, MOCKPROBE_MAX_NAME - 1);
            return &g_pins[i];
        }
    }
    return NULL;
}

static void uart_poll_rx(void) {
    while (g_uart_rx_len < sizeof(g_uart_rx_buf) - 1) {
        size_t read = mockprobe_transport_read(&g_uart_rx_buf[g_uart_rx_len], 1);
        if (read == 0) {
            break;
        }
        g_uart_rx_len += read;
        g_uart_rx_buf[g_uart_rx_len] = '\0';
    }
}

static bool handle_cfg_pin(char *args) {
    char *name;
    char *gpio_text;
    char *dir_text;
    if (!next_token(&args, &name) || !next_token(&args, &gpio_text) || !next_token(&args, &dir_text)) {
        mockprobe_err("CFG_PIN syntax");
        return true;
    }

    uint gpio = (uint)strtoul(gpio_text, NULL, 10);
    if (gpio_reserved(gpio) || mockprobe_transport_is_active_gpio(gpio)) {
        mockprobe_err("CFG_PIN reserved_gpio");
        return true;
    }

    mockprobe_pin_t *slot = alloc_pin(name);
    if (slot == NULL) {
        mockprobe_err("CFG_PIN no_slots");
        return true;
    }

    slot->gpio = gpio;
    slot->is_output = strcmp(dir_text, "out") == 0;

    gpio_init(slot->gpio);
    gpio_set_dir(slot->gpio, slot->is_output ? GPIO_OUT : GPIO_IN);
    mockprobe_ok("CFG_PIN");
    return true;
}

static bool handle_pin_set(char *args) {
    char *name;
    char *value_text;
    if (!next_token(&args, &name) || !next_token(&args, &value_text)) {
        mockprobe_err("PIN_SET syntax");
        return true;
    }

    mockprobe_pin_t *slot = find_pin(name);
    if (slot == NULL || !slot->is_output) {
        mockprobe_err("PIN_SET unknown_pin");
        return true;
    }

    gpio_put(slot->gpio, (uint)(strtoul(value_text, NULL, 10) ? 1 : 0));
    mockprobe_ok("PIN_SET");
    return true;
}

static bool handle_delay(char *args) {
    sleep_ms((uint32_t)strtoul(args, NULL, 10));
    mockprobe_ok("DELAY");
    return true;
}

static bool handle_cfg_uart(char *args) {
    char *uart_id_text;
    char *tx_text;
    char *rx_text;
    char *baud_text;
    char *backend_text = NULL;
    if (!next_token(&args, &uart_id_text) || !next_token(&args, &tx_text) ||
        !next_token(&args, &rx_text) || !next_token(&args, &baud_text)) {
        mockprobe_err("CFG_UART syntax");
        return true;
    }
    (void)next_token(&args, &backend_text);

    int uart_id = (int)strtol(uart_id_text, NULL, 10);
    uint tx_gpio = (uint)strtoul(tx_text, NULL, 10);
    uint rx_gpio = (uint)strtoul(rx_text, NULL, 10);
    uint baud = (uint)strtoul(baud_text, NULL, 10);
    mockprobe_uart_backend_preference_t preference = MOCKPROBE_UART_BACKEND_AUTO;
    if (backend_text != NULL) {
        if (strcmp(backend_text, "auto") == 0) {
            preference = MOCKPROBE_UART_BACKEND_AUTO;
        } else if (strcmp(backend_text, "hw") == 0) {
            preference = MOCKPROBE_UART_BACKEND_HW;
        } else if (strcmp(backend_text, "pio") == 0) {
            preference = MOCKPROBE_UART_BACKEND_PIO;
        } else {
            mockprobe_err("CFG_UART backend");
            return true;
        }
    }
    if (gpio_reserved(tx_gpio) || gpio_reserved(rx_gpio)) {
        mockprobe_err("CFG_UART reserved_gpio");
        return true;
    }

    char detail[32];
    if (!mockprobe_transport_configure_uart((uint)uart_id, tx_gpio, rx_gpio, baud, preference, detail, sizeof(detail))) {
        mockprobe_err(detail);
        return true;
    }

    g_uart_rx_len = 0;
    g_uart_rx_buf[0] = '\0';
    mockprobe_ok(detail);
    return true;
}

static bool handle_uart_expect(char *args) {
    char *timeout_text;
    if (!next_token(&args, &timeout_text)) {
        mockprobe_err("UART_EXPECT syntax");
        return true;
    }

    while (*args == ' ') {
        ++args;
    }
    if (!json_unescape_inplace(args)) {
        mockprobe_err("UART_EXPECT pattern");
        return true;
    }

    uint32_t timeout_ms = (uint32_t)strtoul(timeout_text, NULL, 10);
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        uart_poll_rx();
        if (strstr((char *)g_uart_rx_buf, args) != NULL) {
            mockprobe_evt("UART_RX", (char *)g_uart_rx_buf);
            g_uart_rx_len = 0;
            g_uart_rx_buf[0] = '\0';
            mockprobe_ok("UART_EXPECT");
            return true;
        }
        sleep_ms(5);
    }

    mockprobe_err("UART_EXPECT timeout");
    return true;
}

static bool handle_uart_reply(char *args) {
    if (!json_unescape_inplace(args)) {
        mockprobe_err("UART_REPLY data");
        return true;
    }
    mockprobe_transport_write((const uint8_t *)args, strlen(args));
    mockprobe_ok("UART_REPLY");
    return true;
}

static bool handle_uart_loopback(char *args) {
    uint enable = (uint)strtoul(args, NULL, 10);
    if (!mockprobe_transport_set_loopback(enable != 0)) {
        mockprobe_err("UART_LOOPBACK unsupported");
        return true;
    }
    mockprobe_ok(enable ? "UART_LOOPBACK on" : "UART_LOOPBACK off");
    return true;
}

static bool handle_bootsel(void) {
    mockprobe_ok("BOOTSEL");
    sleep_ms(50);
    reset_usb_boot(0, 0);
    return true;
}

static bool handle_uart_peek(char *args) {
    uint32_t timeout_ms = (uint32_t)strtoul(args, NULL, 10);
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    uint8_t buf[64];
    while (!time_reached(deadline)) {
        size_t n = mockprobe_transport_read(buf, sizeof(buf) - 1);
        if (n != 0) {
            buf[n] = '\0';
            mockprobe_evt("UART_RX", (const char *)buf);
            mockprobe_ok("UART_PEEK");
            return true;
        }
        sleep_ms(2);
    }
    mockprobe_err("UART_PEEK timeout");
    return true;
}

static bool handle_uart_selftest(char *args) {
    while (*args == ' ') {
        ++args;
    }
    if (!json_unescape_inplace(args)) {
        mockprobe_err("UART_SELFTEST data");
        return true;
    }
    mockprobe_transport_flush_rx();
    mockprobe_transport_write((const uint8_t *)args, strlen(args));
    absolute_time_t deadline = make_timeout_time_ms(300);
    uint8_t buf[64];
    size_t total = 0;
    while (!time_reached(deadline) && total < sizeof(buf) - 1) {
        size_t n = mockprobe_transport_read(&buf[total], sizeof(buf) - 1 - total);
        if (n != 0) {
            total += n;
            buf[total] = '\0';
            if (strstr((const char *)buf, args) != NULL) {
                mockprobe_evt("UART_RX", (const char *)buf);
                mockprobe_ok("UART_SELFTEST");
                return true;
            }
        }
        sleep_ms(2);
    }
    mockprobe_err("UART_SELFTEST timeout");
    return true;
}

static bool handle_uart_info(void) {
    char detail[96];
    snprintf(detail, sizeof(detail), "UART_INFO %s id=%u tx=%u rx=%u loop=%u",
             mockprobe_transport_backend_name(),
             mockprobe_transport_uart_id(),
             mockprobe_transport_tx_gpio(),
             mockprobe_transport_rx_gpio(),
             mockprobe_transport_get_loopback() ? 1u : 0u);
    mockprobe_ok(detail);
    return true;
}

static bool handle_uart_stress(char *args) {
    uint32_t total_bytes = (uint32_t)strtoul(args, NULL, 10);
    if (total_bytes == 0) {
        mockprobe_err("UART_STRESS size");
        return true;
    }

    mockprobe_transport_flush_rx();

    uint32_t progress_step = 65536;
    uint8_t tx_buf[256];
    uint8_t rx_buf[256];
    uint32_t processed = 0;
    while (processed < total_bytes) {
        uint32_t chunk = total_bytes - processed;
        if (chunk > sizeof(tx_buf)) {
            chunk = sizeof(tx_buf);
        }
        for (uint32_t i = 0; i < chunk; ++i) {
            tx_buf[i] = (uint8_t)((processed + i) & 0xffu);
        }
        mockprobe_transport_write(tx_buf, chunk);

        uint32_t received = 0;
        absolute_time_t chunk_deadline = delayed_by_ms(get_absolute_time(), 200);
        while (received < chunk && !time_reached(chunk_deadline)) {
            size_t n = mockprobe_transport_read(&rx_buf[received], chunk - received);
            if (n == 0) {
                tight_loop_contents();
                continue;
            }
            received += (uint32_t)n;
        }
        if (received != chunk) {
            mockprobe_err("UART_STRESS timeout");
            return true;
        }
        for (uint32_t i = 0; i < chunk; ++i) {
            if (rx_buf[i] != tx_buf[i]) {
                char detail[64];
                snprintf(detail, sizeof(detail), "UART_STRESS mismatch tx=%u rx=%u idx=%lu",
                         tx_buf[i], rx_buf[i], (unsigned long)(processed + i));
                mockprobe_err(detail);
                return true;
            }
        }

        processed += chunk;
        if (processed % progress_step == 0) {
            char detail[48];
            snprintf(detail, sizeof(detail), "UART_STRESS %lu/%lu",
                     (unsigned long)processed, (unsigned long)total_bytes);
            mockprobe_evt("LOG", detail);
        }
    }

    char detail[48];
    snprintf(detail, sizeof(detail), "UART_STRESS bytes=%lu", (unsigned long)total_bytes);
    mockprobe_ok(detail);
    return true;
}

void mockprobe_mode_init(void) {
    memset(g_pins, 0, sizeof(g_pins));
    g_uart_rx_len = 0;
    g_uart_rx_buf[0] = '\0';
}

bool mockprobe_handle_command_line(char *line) {
    char *command;
    char *args = line;
    if (!next_token(&args, &command)) {
        return false;
    }

    if (strcmp(command, "HELLO") == 0) {
        mockprobe_ok("HELLO mockprobe");
        return true;
    }
    if (strcmp(command, "CFG_PIN") == 0) {
        return handle_cfg_pin(args);
    }
    if (strcmp(command, "PIN_SET") == 0) {
        return handle_pin_set(args);
    }
    if (strcmp(command, "DELAY") == 0) {
        return handle_delay(args);
    }
    if (strcmp(command, "CFG_UART") == 0) {
        return handle_cfg_uart(args);
    }
    if (strcmp(command, "UART_EXPECT") == 0) {
        return handle_uart_expect(args);
    }
    if (strcmp(command, "UART_REPLY") == 0) {
        return handle_uart_reply(args);
    }
    if (strcmp(command, "UART_LOOPBACK") == 0) {
        return handle_uart_loopback(args);
    }
    if (strcmp(command, "BOOTSEL") == 0) {
        return handle_bootsel();
    }
    if (strcmp(command, "UART_PEEK") == 0) {
        return handle_uart_peek(args);
    }
    if (strcmp(command, "UART_SELFTEST") == 0) {
        return handle_uart_selftest(args);
    }
    if (strcmp(command, "UART_INFO") == 0) {
        return handle_uart_info();
    }
    if (strcmp(command, "UART_STRESS") == 0) {
        return handle_uart_stress(args);
    }
    if (strcmp(command, "RUN") == 0) {
        mockprobe_ok("RUN");
        return true;
    }

    mockprobe_err("unknown_command");
    return true;
}
