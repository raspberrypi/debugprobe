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

#include "picoprobe_config.h"
#include "probe.h"
#include "cdc_uart.h"
#include "get_serial.h"
#include "led.h"
#include "tusb_edpt_handler.h"
#include "DAP.h"

// UART0 for Picoprobe debug
// UART1 for picoprobe to target device

static uint8_t TxDataBuffer[CFG_TUD_HID_EP_BUFSIZE];
static uint8_t RxDataBuffer[CFG_TUD_HID_EP_BUFSIZE];

#define THREADED 1

#define UART_TASK_PRIO (tskIDLE_PRIORITY + 3)
#define TUD_TASK_PRIO  (tskIDLE_PRIORITY + 2)
#define DAP_TASK_PRIO  (tskIDLE_PRIORITY + 1)

TaskHandle_t dap_taskhandle, tud_taskhandle;

void usb_thread(void *ptr)
{
    TickType_t wake;
    wake = xTaskGetTickCount();
    do {
        tud_task();
#ifdef PICOPROBE_USB_CONNECTED_LED
        if (!gpio_get(PICOPROBE_USB_CONNECTED_LED) && tud_ready())
            gpio_put(PICOPROBE_USB_CONNECTED_LED, 1);
        else
            gpio_put(PICOPROBE_USB_CONNECTED_LED, 0);
#endif
        // Go to sleep for up to a tick if nothing to do
        if (!tud_task_event_ready())
            xTaskDelayUntil(&wake, 1);
    } while (1);
}

// Workaround API change in 0.13
#if (TUSB_VERSION_MAJOR == 0) && (TUSB_VERSION_MINOR <= 12)
#define tud_vendor_flush(x) ((void)0)
#endif

int main(void) {

    board_init();
    usb_serial_init();
    cdc_uart_init();
    tusb_init();

    DAP_Setup();
    stdio_uart_init();

    led_init();

    picoprobe_info("Welcome to Picoprobe!\n");

    if (THREADED) {
        /* UART needs to preempt USB as if we don't, characters get lost */
        xTaskCreate(cdc_thread, "UART", configMINIMAL_STACK_SIZE, NULL, UART_TASK_PRIO, &uart_taskhandle);
        xTaskCreate(usb_thread, "TUD", configMINIMAL_STACK_SIZE, NULL, TUD_TASK_PRIO, &tud_taskhandle);
        /* Lowest priority thread is debug - need to shuffle buffers before we can toggle swd... */
        xTaskCreate(dap_thread, "DAP", configMINIMAL_STACK_SIZE, NULL, DAP_TASK_PRIO, &dap_taskhandle);
        vTaskStartScheduler();
    }

    while (!THREADED) {
        tud_task();
        cdc_task();

#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
        if (tud_vendor_available()) {
            uint32_t resp_len;
            tud_vendor_read(RxDataBuffer, sizeof(RxDataBuffer));
            resp_len = DAP_ProcessCommand(RxDataBuffer, TxDataBuffer);
            tud_vendor_write(TxDataBuffer, resp_len);
        }
#endif
    }

    return 0;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) itf;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* RxDataBuffer, uint16_t bufsize)
{
  uint32_t response_size = TU_MIN(CFG_TUD_HID_EP_BUFSIZE, bufsize);

  // This doesn't use multiple report and report ID
  (void) itf;
  (void) report_id;
  (void) report_type;

  DAP_ProcessCommand(RxDataBuffer, TxDataBuffer);

  tud_hid_report(0, TxDataBuffer, response_size);
}

#if (PICOPROBE_DEBUG_PROTOCOL == PROTO_DAP_V2)
extern uint8_t const desc_ms_os_20[];

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  // nothing to with DATA & ACK stage
  if (stage != CONTROL_STAGE_SETUP) return true;

  switch (request->bmRequestType_bit.type)
  {
    case TUSB_REQ_TYPE_VENDOR:
      switch (request->bRequest)
      {
        case 1:
          if ( request->wIndex == 7 )
          {
            // Get Microsoft OS 2.0 compatible descriptor
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20+8, 2);

            return tud_control_xfer(rhport, request, (void*) desc_ms_os_20, total_len);
          }else
          {
            return false;
          }

        default: break;
      }
    break;
    default: break;
  }

  // stall unknown request
  return false;
}
#endif

void vApplicationTickHook (void)
{
};

void vApplicationStackOverflowHook(TaskHandle_t Task, char *pcTaskName)
{
  panic("stack overflow (not the helpful kind) for %s\n", *pcTaskName);
}

void vApplicationMallocFailedHook(void)
{
  panic("Malloc Failed\n");
};
