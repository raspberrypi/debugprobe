/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
 * Copyright (c) 2021 Peter Lawrence
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

#include "FreeRTOS.h"
#include "task.h"

#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"

#include "tusb.h"
#include "time.h"
#include "cdc_uart.h"
#include "get_serial.h"
#include "led.h"
#include "DAP.h"
#include "DAP_config.h"


#define DAP_DEBUG

#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_OPENOCD_CUSTOM)
    #define THREADED 0                        // threaded is here not implemented
#else
    #define THREADED 1
#endif


// UART1 for picoprobe to target device

#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V1)
    static uint8_t TxDataBuffer[CFG_TUD_HID_EP_BUFSIZE * DAP_PACKET_COUNT];
#elif (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
    static uint8_t TxDataBuffer[DAP_PACKET_SIZE * DAP_PACKET_COUNT];    // TODO correct!?
    static uint8_t RxDataBuffer[DAP_PACKET_SIZE * DAP_PACKET_COUNT];
    static TaskHandle_t dap_taskhandle;
#endif


#if (THREADED != 0)
    static TaskHandle_t tud_taskhandle;

    #define UART_TASK_PRIO (tskIDLE_PRIORITY + 1)
    #define TUD_TASK_PRIO  (tskIDLE_PRIORITY + 2)
    #define DAP_TASK_PRIO  (tskIDLE_PRIORITY + 3)
#endif



#ifdef DAP_DEBUG
static uint32_t DAP_ProcessCommandDebug(char *prefix, const uint8_t *request, uint32_t req_len, uint8_t *response)
{
    uint32_t resp_len;
    bool echo = false;

    switch (request[0])
    {
        case 0x00:
            // ID_DAP_Info
            picoprobe_info("%s_exec ID_DAP_Info_00(%d), len %lu\n", prefix, request[1], req_len);
            echo = true;
            break;

        case 0x02:
            // ID_DAP_Connect
            picoprobe_info("%s_exec ID_DAP_Connect_02(%d), len %lu\n", prefix, request[1], req_len);
            echo = true;
            break;

        case 0x03:
            // ID_DAP_Disconnect
            picoprobe_info("%s_exec ID_DAP_Disconnect_03\n", prefix);
            echo = true;
            break;

        case 0x05:
            // ID_DAP_Transfer, appears very very often, so suppress it
            picoprobe_info("%s_exec ID_DAP_Transfer_05(%d)...\n", prefix, request[1]);
            break;

        case 0x06:
            // ID_DAP_TransferBlock
            picoprobe_info("%s_exec ID_DAP_TransferBlock_06, %02x %02x %02x %02x\n", prefix, request[1], request[2], request[3], request[4]);
            break;

        case 0x11:
            // ID_DAP_SWJ_Clock
            picoprobe_info("%s_exec ID_DAP_SWJ_Clock_11(%lu)\n", prefix, 1UL*request[1] + 256UL*request[2] + 65536UL*request[3] + 1048576UL*request[4]);
            echo = true;
            break;

        case 0x12:
            // ID_DAP_SWJ_Sequence
            picoprobe_info("%s_exec ID_DAP_SWJ_Sequence_12(%d)\n", prefix, request[1]);
            echo = true;
            break;

        case 0x1d:
            // ID_DAP_SWD_Sequence
            picoprobe_info("%s_exec ID_DAP_SWD_Sequence_1d(%d), len %lu\n", prefix, request[1], req_len);
            echo = true;
            break;

        default:
            picoprobe_info("---------%s_Exec cmd %02x, len %lu\n", prefix, request[0], req_len);
            echo = true;
            break;
    }

    resp_len = DAP_ProcessCommand(request, response);
    if (echo)
    {
        picoprobe_info("   %s_response, len 0x%lx: ", prefix, resp_len);
        for (uint32_t u = 0;  u < (resp_len & 0xffff);  ++u)
            picoprobe_info(" %02x", response[u]);
        picoprobe_info("\n");
    }
    return resp_len;
}   // DAP_ProcessCommandDebug
#endif



void usb_thread(void *ptr)
{
    do {
        tud_task();
        // Trivial delay to save power
        vTaskDelay(1);
    } while (1);
}



#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
void dap_thread(void *ptr)
/**
 * used only if DAPv2?
 */
// TODO this code is actually doing nothing (never called).  Everything seems to be handled 
{
#if 0
    uint32_t ret;
    int32_t packet_len;
    uint16_t req_len, resp_len;
    uint8_t *rx_ptr;

    do {
        while (tud_vendor_available()) {
            rx_ptr = &RxDataBuffer[0];
            packet_len = tud_vendor_read(rx_ptr, sizeof(RxDataBuffer));
            picoprobe_debug("Got chunk %u\n", packet_len);
            do {
#ifdef DAP_DEBUG
                ret = DAP_ProcessCommandDebug("1", RxDataBuffer, packet_len, TxDataBuffer);
#else
                ret = DAP_ProcessCommand(rx_ptr, TxDataBuffer);
#endif
                resp_len = ret & 0xffff;
                req_len = ret >> 16;
                tud_vendor_write(TxDataBuffer, resp_len);
                tud_vendor_flush();
                rx_ptr += req_len;
                packet_len -= req_len;
                picoprobe_debug("Packet decode remaining %u req %u resp %u\n",
                                packet_len, req_len, resp_len);
            } while (packet_len > 0);
        }
        // Trivial delay to save power
        vTaskDelay(1);
    } while (1);
#else
   for (;;)
   {
        if (tud_vendor_available()) 
        {
            uint32_t req_len;
            uint32_t resp_len;

            req_len = tud_vendor_read(RxDataBuffer, sizeof(RxDataBuffer));
            resp_len = DAP_ProcessCommandDebug("1", RxDataBuffer, req_len, TxDataBuffer);
            tud_vendor_write(TxDataBuffer, resp_len & 0xffff);
            tud_vendor_flush();
        }
        else
        {
            // Trivial delay to save power
            vTaskDelay(2);
        }
   }
#endif
}
#endif



int main(void) 
{
    board_init();
    usb_serial_init();
    cdc_uart_init();
    tusb_init();
#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_OPENOCD_CUSTOM)
    probe_gpio_init();
    probe_init();
#else
    DAP_Setup();
#endif
    led_init();

    picoprobe_info("------------------------------------------");
#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_OPENOCD_CUSTOM)
    picoprobe_info("Welcome to Picoprobe! (CUSTOM)\n");
#elif (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V1)
    picoprobe_info("Welcome to Picoprobe! (DAP_V1)\n");
#elif (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
    picoprobe_info("Welcome to Picoprobe! (DAP_V2)\n");
#else
    picoprobe_info("Welcome to Picoprobe! (UNKNOWN)\n");
#endif

#if (THREADED != 0)
    /* UART needs to preempt USB as if we don't, characters get lost */
    xTaskCreate(cdc_thread, "UART", configMINIMAL_STACK_SIZE, NULL, UART_TASK_PRIO, &uart_taskhandle);
    xTaskCreate(usb_thread, "TUD", configMINIMAL_STACK_SIZE, NULL, TUD_TASK_PRIO, &tud_taskhandle);
    /* Lowest priority thread is debug - need to shuffle buffers before we can toggle swd... */
#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
    xTaskCreate(dap_thread, "DAP", configMINIMAL_STACK_SIZE, NULL, DAP_TASK_PRIO, &dap_taskhandle);
#endif
    vTaskStartScheduler();
#endif

#if (THREADED == 0)
    for (;;) {
        tud_task();
        cdc_task();
#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_OPENOCD_CUSTOM)
        probe_task();
        led_task();
#elif (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
        if (tud_vendor_available()) {
            uint32_t resp_len;

#ifdef DAP_DEBUG
            uint32_t req_len;

            req_len = tud_vendor_read(RxDataBuffer, sizeof(RxDataBuffer));
            resp_len = DAP_ProcessCommandDebug("2", RxDataBuffer, req_len, TxDataBuffer);
#else
            resp_len = DAP_ProcessCommand(RxDataBuffer, TxDataBuffer);
#endif
            tud_vendor_write(TxDataBuffer, resp_len & 0xffff);
            tud_vendor_flush();
        }
#endif
    }
#endif

    return 0;
}



#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V1)
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    // TODO not Implemented
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}
#endif



#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V1)
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *RxDataBuffer, uint16_t bufsize)
{
    uint32_t response_size = TU_MIN(CFG_TUD_HID_EP_BUFSIZE, bufsize);

    // This doesn't use multiple report and report ID
    (void)itf;
    (void)report_id;
    (void)report_type;

#ifdef DAP_DEBUG
    DAP_ProcessCommandDebug("hid", RxDataBuffer, bufsize, TxDataBuffer);
#else
    DAP_ProcessCommand(RxDataBuffer, TxDataBuffer);
#endif
    tud_hid_report(0, TxDataBuffer, response_size);
}
#endif



#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)

extern uint8_t const desc_ms_os_20[];

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request)
{
    // nothing to with DATA & ACK stage
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    switch (request->bmRequestType_bit.type)
    {
        case TUSB_REQ_TYPE_VENDOR:
            switch (request->bRequest)
            {
                case 1:   // VENDOR_REQUEST_WEBUSB
                    if (request->wIndex == 7)
                    {
                        // Get Microsoft OS 2.0 compatible descriptor
                        uint16_t total_len;
                        memcpy(&total_len, desc_ms_os_20 + 8, 2);

                        return tud_control_xfer(rhport, request, (void *)desc_ms_os_20, total_len);
                    }
                    else
                    {
                        return false;
                    }

                default:
                    break;
            }
            break;

        default:
            break;
    }

    // stall unknown request
    return false;
}
#endif



void vApplicationTickHook (void)
{
}



void vApplicationStackOverflowHook(TaskHandle_t Task, char *pcTaskName)
{
    panic("stack overflow (not the helpful kind) for %s\n", *pcTaskName);
}



void vApplicationMallocFailedHook(void)
{
    panic("Malloc Failed\n");
}
