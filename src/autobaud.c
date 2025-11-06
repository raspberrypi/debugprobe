#include <pico/stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/clocks.h>

#include "autobaud.h"
#include "probe_config.h"
#include "autobaud.pio.h"

// DMA buffer size
#define BUF_SIZE 1024

// Size of hash table for sample occurrence counts
#define HASH_TBL_SIZE 500

// Minimum sample occurrence ratio to consider a baud rate value valid
#define MIN_FREQUENCY 0.05f

// PIO clock frequency in Hz
#define PIO_CLOCK_FREQUENCY 125000000

// DMA IRQ for autobaud
#define DMA_AUTOBAUD_IRQ 0

// Priority for DMA IRQ handler
#define DMA_AUTOBAUD_IRQ_PRIORITY PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY

typedef struct {
    int key;
    int count;
} Entry;

typedef struct {
    Entry *entries;
    size_t size;
} HashTable;

// PIO instance
static PIO pio;
// PIO state machine
static int sm = -1;
// PIO program offset
static int offset = -1;
// UART RX GPIO 1 pin
static const uint rx_pin = PROBE_UART_RX;

// Frequency hash table to store samples occurance
static HashTable *freq_table;

// Estimated baud rate and validity
static float baud;
static float validity;

// shortest bit duration in PIO cycles
static uint32_t min_cycles_count = UINT32_MAX;
// longest bit duration in PIO cycles
static uint32_t max_cycles_count = 0;

static uint32_t total_samples;  // total samples seen
static uint32_t bit_time_sum;   // sum of 1-bit times
static uint32_t bit_time_count; // total 1-bit times
static uint32_t outlier_count;  // total of 1-bit times outliers

// DMA channels to read RX PIO line
static int ctrl_chan = -1;
static int data_chan = -1;

// DMA ring buffer storing PIO RX FIFO data
static uint32_t rx_buffer[BUF_SIZE] __attribute__((aligned(4096)));

// DMA control channel reads this value to reload transfer count
static const uint32_t dma_reload_count = BUF_SIZE;

static uintptr_t last_write_addr = (uintptr_t)rx_buffer;
static uintptr_t curr_write_addr = (uintptr_t)rx_buffer;

volatile bool autobaud_running = false;
volatile bool autobaud_stopped = true;

// Queue to hold new baud rate estimates
QueueHandle_t baudQueue;

TaskHandle_t autobaud_taskhandle;


uint32_t hash(uint32_t x, size_t size) {
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = ((x >> 16) ^ x) * 0x45d9f3bu;
    x = (x >> 16) ^ x;
    return x % size;
}

HashTable *create_table(size_t size) {
    HashTable *table = malloc(sizeof(HashTable));
    if (!table) return NULL;
    table->size = size;
    table->entries = calloc(size, sizeof(Entry));
    if (!table->entries) {
        free(table);
        return NULL;
    }
    return table;
}

void insert(HashTable *table, int key) {
    uint32_t idx = hash(key, table->size);
    while (table->entries[idx].key != 0) {
        if (table->entries[idx].key == key) {
            table->entries[idx].count++;
            return;
        }
        idx = (idx + 1) % table->size;
    }
    table->entries[idx].key = key;
    table->entries[idx].count = 1;
}

int get_count(HashTable *table, int key) {
    uint32_t idx = hash(key, table->size);
    while (table->entries[idx].key != 0) {
        if (table->entries[idx].key == key) {
            return table->entries[idx].count;
        }
        idx = (idx + 1) % table->size;
    }
    return 0;
}

void free_table(HashTable *table) {
    free(table->entries);
    free(table);
}

void __isr dma_handler() {
    // Clear DMA interrupt
    if ((data_chan >= 0) && dma_irqn_get_channel_status(DMA_AUTOBAUD_IRQ, data_chan)) {
        dma_irqn_acknowledge_channel(DMA_AUTOBAUD_IRQ, data_chan);
    }
}

bool dma_configure(PIO pio, uint sm) {
    // Configure two DMA channels for continuous RX FIFO monitoring:
    // - data_chan: Continuously reads from PIO RX FIFO into circular buffer
    // - ctrl_chan: Triggers data_chan to restart when it completes a transfer
    ctrl_chan = dma_claim_unused_channel(true);
    if (ctrl_chan < 0) return false;
    data_chan = dma_claim_unused_channel(true);
    if (data_chan < 0) return false;

    dma_channel_config data_cfg = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&data_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&data_cfg, false);
    channel_config_set_write_increment(&data_cfg, true);
    channel_config_set_dreq(&data_cfg, pio_get_dreq(pio, sm, false)); // Trigger when PIO RX FIFO has data to read
    channel_config_set_chain_to(&data_cfg, ctrl_chan); // Chain to control channel when transfer completes
    channel_config_set_ring(&data_cfg, true, 12); // Ring buffer size 2^12 = 4096 bytes (1024 uint32_t)

    dma_channel_configure(
        data_chan,
        &data_cfg,
        rx_buffer,
        &pio->rxf[sm],
        BUF_SIZE,
        false
    );

    dma_channel_config ctrl_cfg = dma_channel_get_default_config(ctrl_chan);
    channel_config_set_transfer_data_size(&ctrl_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&ctrl_cfg, false);
    channel_config_set_write_increment(&ctrl_cfg, false);

    dma_channel_configure(
        ctrl_chan,
        &ctrl_cfg,
        &(dma_hw->ch[data_chan].al1_transfer_count_trig), // Destination: data channel's transfer count register
        &dma_reload_count,                               // Source: reload transfer count value (BUF_SIZE)
        1,
        false
    );

    // Enable DMA interrupt on data channel completion
    irq_add_shared_handler(dma_get_irq_num(DMA_AUTOBAUD_IRQ), dma_handler, DMA_AUTOBAUD_IRQ_PRIORITY);
    irq_set_enabled(dma_get_irq_num(DMA_AUTOBAUD_IRQ), true);
    dma_irqn_set_channel_enabled(DMA_AUTOBAUD_IRQ, data_chan, true);

    dma_channel_start(data_chan);
    return true;
}

// Compare rounded integer parts of baud rates
// Tolerance of 0.5% in detected versus set
inline bool baud_changed(float new_baud, float baud) {
    uint32_t hi = (uint32_t)(baud * 1.005f);
    uint32_t lo = (uint32_t)(baud * 0.995f);
    uint32_t new = (uint32_t)new_baud;

    return (new > hi || new < lo);
}

// Processes new DMA samples read from the PIO RX FIFO. Each DMA sample
// represents a timestamp (or cycle count) between edges on the input signal.
// Accumulates these durations, filters noise, and estimates baud rate.
uint estimate_baud_rate() {
    uint old_progress = total_samples;
    // Get current DMA write address
    curr_write_addr = dma_hw->ch[data_chan].write_addr;
    // Convert absolute addresses to buffer indices
    size_t curr_index = ((curr_write_addr) - (uintptr_t)rx_buffer) / sizeof(rx_buffer[0]);
    size_t last_index = (last_write_addr - (uintptr_t)rx_buffer) / sizeof(rx_buffer[0]);

    for (size_t i = last_index; i != curr_index; i = (i + 1) % BUF_SIZE) {
        uint32_t raw = rx_buffer[i];
        uint32_t curr_cycles_count = (UINT32_MAX - raw) * 2;
        insert(freq_table, curr_cycles_count);

        total_samples++;
        if (curr_cycles_count > max_cycles_count)
            max_cycles_count = curr_cycles_count;
        float freq = (float) get_count(freq_table, curr_cycles_count) / (float) total_samples;
        // if sample is seen at least 5% of all samples,
        // it is assumed it's not a noisy value
        if (freq < MIN_FREQUENCY) continue;
        if (curr_cycles_count < min_cycles_count) {
            min_cycles_count = curr_cycles_count;
            bit_time_sum = 0;
            bit_time_count = 0;
            outlier_count = 0;
            continue;
        }
        // If current duration is within +10% of min_cycles, treat it as a "1-bit period"
        if ((curr_cycles_count - min_cycles_count) < ((float)min_cycles_count * 0.1f)) {
            bit_time_sum += curr_cycles_count;
            bit_time_count++;
            // 1-bit period should not be less than 1/9th of the longest period
            if (curr_cycles_count < (max_cycles_count / 9))
                outlier_count++;
            // Calculate baud from average of 1-bit times
            float avg_bit_time = (float) bit_time_sum / (float) bit_time_count;
            float new_baud = PIO_CLOCK_FREQUENCY / avg_bit_time;
            // If baud has changed, send updated baud information to cdc_thread
            if (baud_changed(new_baud, baud)) {
                float completeness = 1.0f - expf(-(float) total_samples / 40.0f);
                float noise_ratio = (float) outlier_count / (float) bit_time_count;
                float consistency = 1.0f - fminf(noise_ratio * 2.0f, 1.0f);
                float validity = completeness * consistency;
                if (validity > 0.6f) {
                    baud = new_baud;
                    BaudInfo_t new_baud_info;
                    new_baud_info.baud = (uint32_t)roundf(baud);
                    new_baud_info.validity = validity;
                    xQueueOverwrite(baudQueue, &new_baud_info);
                }
            }
        }
    }
    last_write_addr = curr_write_addr;
    return total_samples - old_progress;
}

void autobaud_deinit() {
    // Disable DMA IRQ and channels
    if (data_chan >= 0) {
        dma_irqn_set_channel_enabled(DMA_AUTOBAUD_IRQ, data_chan, false);
        dma_irqn_acknowledge_channel(DMA_AUTOBAUD_IRQ, data_chan);
        dma_channel_unclaim(data_chan);
        data_chan = -1;
    }
    if (ctrl_chan >= 0) {
        dma_channel_unclaim(ctrl_chan);
        ctrl_chan = -1;
    }
    irq_remove_handler(dma_get_irq_num(DMA_AUTOBAUD_IRQ), dma_handler);
    if (!irq_has_shared_handler(dma_get_irq_num(DMA_AUTOBAUD_IRQ))) {
        irq_set_enabled(dma_get_irq_num(DMA_AUTOBAUD_IRQ), false);
    }

    // Remove PIO program
    if (pio && (sm >= 0)) {
        pio_sm_set_enabled(pio, sm, false);
        pio_sm_unclaim(pio, sm);
    }
    if (pio && (offset >= 0)) {
        pio_remove_program(pio, &autobaud_program, offset);
    }

    if (freq_table) {
        free_table(freq_table);
        freq_table = NULL;
    }
    if (baudQueue) {
        vQueueDelete(baudQueue);
        baudQueue = NULL;
    }

    // Reset state
    pio = NULL;
    sm = offset = -1;
    baud = validity = 0.0f;
    min_cycles_count = UINT32_MAX;
    max_cycles_count = 0;
    total_samples = bit_time_sum = bit_time_count = outlier_count = 0;
    last_write_addr = (uintptr_t)rx_buffer;
    curr_write_addr = (uintptr_t)rx_buffer;
}

bool autobaud_init() {
    pio = pio0;
    // Claim a free PIO state machine
    sm = pio_claim_unused_sm(pio, true);
    if (sm < 0)
        return false;
    // Add PIO program
    offset = pio_add_program(pio, &autobaud_program);
    if (offset < 0) {
        autobaud_deinit();
        return false;
    }
    float div = (float)clock_get_hz(clk_sys) / PIO_CLOCK_FREQUENCY;
    autobaud_program_init(pio, sm, offset, rx_pin, div);
    pio_sm_set_enabled(pio, sm, true);

    // Create hash table to keep count of sample occurance
    freq_table = create_table(HASH_TBL_SIZE);
    if (!freq_table) {
        autobaud_deinit();
        return false;
    }
    // Create queue to send baud information to cdc_thread
    baudQueue = xQueueCreate(1, sizeof(BaudInfo_t));
    if (!baudQueue) {
        autobaud_deinit();
        return false;
    }
    // Set up DMA to continuously write PIO RX data into RAM
    if (!dma_configure(pio, sm)) {
        autobaud_deinit();
        return false;
    }
    return true;
}

void autobaud_start() {
    xTaskNotify(autobaud_taskhandle, AUTOBAUD_CMD_START, eSetValueWithOverwrite);
}

void autobaud_wait_stop() {
    while (!autobaud_stopped)
        xTaskNotify(autobaud_taskhandle, AUTOBAUD_CMD_STOP, eSetValueWithOverwrite);
}

// FreeRTOS thread running if MAGIC_BAUD was set by host
void autobaud_thread(void * param) {
    TickType_t wake = xTaskGetTickCount();
    uint32_t cmd = AUTOBAUD_CMD_NONE;
    uint processed;

    while (true) {
        if (!autobaud_running) {
            // Idle state: thread is blocked here until start command is received
            xTaskNotifyWait(0, 0xFFFFFFFFu, &cmd, portMAX_DELAY);
            if (cmd == AUTOBAUD_CMD_START) {
                if (autobaud_init()) {
                    autobaud_running = true;
                    autobaud_stopped = false;
                }
            }
        } else {
            // Check if host requested autobaud termination
            if (xTaskNotifyWait(0, 0xFFFFFFFFu, &cmd, 0) == pdTRUE) {
                if (cmd == AUTOBAUD_CMD_STOP) {
                    autobaud_running = false;
                    autobaud_deinit();
                    autobaud_stopped = true;
                    continue;
                }
            }
            processed = estimate_baud_rate();
            if (!processed)
                xTaskDelayUntil(&wake, 1);
        }
    }
}