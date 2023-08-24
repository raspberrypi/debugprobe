/**
 * Copyright (c) 2023 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TUSB_EDPT_HANDLER_H
#define TUSB_EDPT_HANDLER_H

#include "tusb.h"

#include "device/usbd_pvt.h"
#include "DAP_config.h"

#define PICOPROBE_INTERFACE_SUBCLASS 0x00
#define PICOPROBE_INTERFACE_PROTOCOL 0x00

typedef struct {
    uint8_t data[DAP_PACKET_COUNT][DAP_PACKET_SIZE];  
    volatile uint32_t packet_wr_idx;
    volatile uint32_t packet_rd_idx;
    volatile bool wasEmpty;
    volatile bool wasFull;
} buffer_t;

extern TaskHandle_t dap_taskhandle, tud_taskhandle;

/* Main DAP loop */
void dap_thread(void *ptr);

/* Endpoint Handling */
void picoprobe_edpt_init(void);
uint16_t picoprobe_edpt_open(uint8_t __unused rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len);
bool picoprobe_edpt_control_xfer_cb(uint8_t __unused rhport, uint8_t stage,  tusb_control_request_t const *request);
bool picoprobe_edpt_xfer_cb(uint8_t __unused rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);

/* Helper Functions */
bool buffer_full(buffer_t *buffer);
bool buffer_empty(buffer_t *buffer);

#endif