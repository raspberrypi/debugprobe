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
#include "tusb_config.h"
#include "dap_util.h"


#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_OPENOCD_CUSTOM)
    #define THREADED 0                        // threaded is here not implemented
#else
    #define THREADED 1
#endif


// UART1 for picoprobe to target device

#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V1)
    static uint8_t TxDataBuffer[CFG_TUD_HID_EP_BUFSIZE * DAP_PACKET_COUNT];
#elif (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
    static uint8_t TxDataBuffer[8192]; //DAP_PACKET_SIZE * DAP_PACKET_COUNT];    // TODO correct!?
    static uint8_t RxDataBuffer[8192]; //DAP_PACKET_SIZE * DAP_PACKET_COUNT];
    static TaskHandle_t dap_taskhandle;
#endif


#if (THREADED != 0)
    static TaskHandle_t tud_taskhandle;

    #define UART_TASK_PRIO (tskIDLE_PRIORITY + 1)
    #define TUD_TASK_PRIO  (tskIDLE_PRIORITY + 2)
    #define DAP_TASK_PRIO  (tskIDLE_PRIORITY + 3)
#endif



void usb_thread(void *ptr)
{
    do {
        tud_task();
        // Trivial delay to save power
        vTaskDelay(5);
    } while (1);
}



#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
void dap_thread(void *ptr)
{
    uint32_t rx_len;
    uint32_t resp_len;

    rx_len = 0;
    for (;;)
    {
        if (tud_vendor_available()) 
        {
            rx_len += tud_vendor_read(RxDataBuffer + rx_len, sizeof(RxDataBuffer));
        }
        else
        {
            // Trivial delay to save power
            vTaskDelay(1);
        }

        if (rx_len != 0  &&  rx_len >= DAP_Check_ExecuteCommand(RxDataBuffer, rx_len))
        {
            resp_len = DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
            tud_vendor_write(TxDataBuffer, resp_len & 0xffff);
            tud_vendor_flush();

            if (rx_len < (resp_len >> 16))
            {
                picoprobe_error("   !!!!!!!! request (%u) was not long enough for interpretation (%u)\n", rx_len, resp_len >> 16);
                rx_len = 0;
            }
            else if (rx_len == (resp_len >> 16))
            {
                rx_len = 0;
            }
            else
            {
                memmove(RxDataBuffer, RxDataBuffer + (resp_len >> 16), rx_len - (resp_len >> 16));
                rx_len -= (resp_len >> 16);
            }
        }
    }
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

            resp_len = DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
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

    DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
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
