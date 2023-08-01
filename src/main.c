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
#include "timers.h"
#include "event_groups.h"

#include <pico/stdlib.h>
#ifdef TARGET_BOARD_PICO_W
    #include <pico/cyw43_arch.h>
#endif

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "bsp/board.h"
#include "tusb.h"

#include "picoprobe_config.h"
#include "probe.h"
#if OPT_PROBE_DEBUG_OUT
    #include "cdc/cdc_debug.h"
#endif
#if OPT_TARGET_UART
    #include "cdc/cdc_uart.h"
#endif
#if OPT_CDC_SYSVIEW
    #include "cdc/cdc_sysview.h"
#endif
#include "dap_util.h"
#include "get_config.h"
#include "led.h"
#include "DAP_config.h"
#include "DAP.h"
#include "sw_lock.h"

#include "target_board.h"    // DAPLink

#include "minIni/minIni.h"

#if OPT_MSC
    #include "msc/msc_utils.h"
#endif
#if defined(INCLUDE_RTT_CONSOLE)
    #include "rtt_console.h"
#endif
#if OPT_SIGROK
    #include "pico-sigrok/cdc_sigrok.h"
    #include "pico-sigrok/sigrok.h"
#endif
#if OPT_NET
    #include "net/net_glue.h"
    #if OPT_NET_ECHO_SERVER
        #include "net/net_echo.h"
    #endif
    #if OPT_NET_IPERF_SERVER
        #include "lwip/apps/lwiperf.h"
    #endif
    #if OPT_NET_SYSVIEW_SERVER
        #include "net/net_sysview.h"
    #endif
#endif


#define TASK_MAX_CNT                15


/*
 * The following is part of a hack to make DAP_PACKET_COUNT a variable.
 * CMSIS-DAPv2 has better performance with 2 packets while
 * CMSIS-DAPv1 only works with one packet, at least with openocd which throws a
 *     "CMSIS-DAP transfer count mismatch: expected 12, got 8" on flashing.
 * The correct packet count has to be set on connection.
 *
 * More notes: pyocd works with large packets only, if the packet count is one.
 * Additionally pyocd is instable if packet count > 1.  Valid for pyocd 0.34.3.
 */
#define _DAP_PACKET_COUNT_OPENOCD   2
#define _DAP_PACKET_SIZE_OPENOCD    CFG_TUD_VENDOR_RX_BUFSIZE
#define _DAP_PACKET_COUNT_PYOCD     1
#define _DAP_PACKET_SIZE_PYOCD      1024                                   // pyocd does not like packets > 128 if COUNT != 1
#define _DAP_PACKET_COUNT_UNKNOWN   1
#define _DAP_PACKET_SIZE_UNKNOWN    64

#define _DAP_PACKET_COUNT_HID       1
#define _DAP_PACKET_SIZE_HID        64

uint8_t  dap_packet_count = _DAP_PACKET_COUNT_UNKNOWN;
uint16_t dap_packet_size  = _DAP_PACKET_SIZE_UNKNOWN;

static uint8_t TxDataBuffer[_DAP_PACKET_COUNT_OPENOCD * CFG_TUD_VENDOR_RX_BUFSIZE];     // maximum required size
static uint8_t RxDataBuffer[_DAP_PACKET_COUNT_OPENOCD * CFG_TUD_VENDOR_RX_BUFSIZE];     // maximum required size


// prios are critical and determine throughput
#define LED_TASK_PRIO               (tskIDLE_PRIORITY + 30)       // simple task which may interrupt everything else for periodic blinking
#define TUD_TASK_PRIO               (tskIDLE_PRIORITY + 28)       // high prio for TinyUSB
//#define TCPIP_THREAD_PRIO           (27)                        // defined in lwipopts.h
#define CDC_DEBUG_TASK_PRIO         (tskIDLE_PRIORITY + 26)       // probe debugging output (CDC)
#define PRINT_STATUS_TASK_PRIO      (tskIDLE_PRIORITY + 24)       // high prio to get status output transferred in (almost) any case
#define SIGROK_TASK_PRIO            (tskIDLE_PRIORITY + 9)        // Sigrok digital/analog signals (does nothing at the moment)
#define MSC_WRITER_THREAD_PRIO      (tskIDLE_PRIORITY + 8)        // this is only running on writing UF2 files
#define SYSVIEW_TASK_PRIO           (tskIDLE_PRIORITY + 6)        // target -> host via SysView (CDC)
#define UART_TASK_PRIO              (tskIDLE_PRIORITY + 5)        // target -> host via UART (CDC)
#define DAPV2_TASK_PRIO             (tskIDLE_PRIORITY + 3)        // DAPv2 execution
#define RTT_CONSOLE_TASK_PRIO       (tskIDLE_PRIORITY + 1)        // target -> host via RTT, ATTENTION: this task can fully load the CPU depending on target RTT output

static TaskHandle_t tud_taskhandle;
static TaskHandle_t dap_taskhandle;
static EventGroupHandle_t dap_events;



void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
#if OPT_TARGET_UART
    if (itf == CDC_UART_N) {
        cdc_uart_line_state_cb(dtr, rts);
    }
#endif
#if OPT_PROBE_DEBUG_OUT
    if (itf == CDC_DEBUG_N) {
        cdc_debug_line_state_cb(dtr, rts);
    }
#endif
#if OPT_SIGROK
    if (itf == CDC_SIGROK_N) {
        cdc_sigrok_line_state_cb(dtr, rts);
    }
#endif
#if OPT_CDC_SYSVIEW
    if (itf == CDC_SYSVIEW_N) {
        cdc_sysview_line_state_cb(dtr, rts);
    }
#endif
}   // tud_cdc_line_state_cb



#if CFG_TUD_CDC
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* line_coding)
{
#if OPT_TARGET_UART
    if (itf == CDC_UART_N) {
        cdc_uart_line_coding_cb(line_coding);
    }
#endif
}   // tud_cdc_line_coding_cb
#endif



void tud_cdc_rx_cb(uint8_t itf)
{
#if OPT_SIGROK
    if (itf == CDC_SIGROK_N) {
        cdc_sigrok_rx_cb();
    }
#endif
#if OPT_TARGET_UART
    if (itf == CDC_UART_N) {
        cdc_uart_rx_cb();
    }
#endif
#if OPT_PROBE_DEBUG_OUT
    if (itf == CDC_DEBUG_N) {
        cdc_debug_rx_cb();
    }
#endif
#if OPT_CDC_SYSVIEW
    if (itf == CDC_SYSVIEW_N) {
        cdc_sysview_rx_cb();
    }
#endif
}   // tud_cdc_rx_cb



void tud_cdc_tx_complete_cb(uint8_t itf)
{
#if OPT_SIGROK
    if (itf == CDC_SIGROK_N) {
        cdc_sigrok_tx_complete_cb();
    }
#endif
#if OPT_TARGET_UART
    if (itf == CDC_UART_N) {
        cdc_uart_tx_complete_cb();
    }
#endif
#if OPT_PROBE_DEBUG_OUT
    if (itf == CDC_DEBUG_N) {
        cdc_debug_tx_complete_cb();
    }
#endif
#if OPT_CDC_SYSVIEW
    if (itf == CDC_SYSVIEW_N) {
        cdc_sysview_tx_complete_cb();
    }
#endif
}   // tud_cdc_tx_complete_cb



#if OPT_CMSIS_DAPV2
void tud_vendor_rx_cb(uint8_t itf)
{
    if (itf == 0) {
        xEventGroupSetBits(dap_events, 0x01);
    }
}   // tud_vendor_rx_cb
#endif



#if OPT_CMSIS_DAPV2
/**
 * CMSIS-DAP task.
 * Receive DAP requests, execute them via DAP_ExecuteCommand() and transmit the response.
 *
 * Problem zones:
 * - connect / disconnect: pyOCD does not send permanently requests if in gdbserver mode, OpenOCD does.
 *   As a consequence "disconnect" has to be detected via the command stream.  If the tool on host side
 *   fails without a disconnect, the SWD connection is not freed (for MSC or RTT).  To recover from this
 *   situation either reset the probe or issue something like "pyocd reset -t rp2040"
 * - fingerprinting the host tool: this is for optimization of the OpenOCD connection, because OpenOCD
 *   can handle big DAP packets and thus transfer is faster.
 * - ID_DAP_Disconnect / ID_DAP_Info / ID_DAP_HostStatus leads to an SWD disconnect if there is no other
 *   command following within 1s.  This is required, because "pyocd list" leads to tool detection without
 *   connect/disconnect and thus otherwise tool detection would be stuck to "pyocd" for the next connection.
 */
void dap_task(void *ptr)
{
    bool swd_connected = false;
    bool swd_disconnect_requested = false;
    uint32_t last_request_us = 0;
    uint32_t rx_len = 0;
    daptool_t tool = E_DAPTOOL_UNKNOWN;

    dap_packet_count = _DAP_PACKET_COUNT_UNKNOWN;
    dap_packet_size  = _DAP_PACKET_SIZE_UNKNOWN;
    for (;;) {
        // disconnect after 1s without data
        if (swd_disconnect_requested  &&  time_us_32() - last_request_us > 1000000) {
            if (swd_connected) {
                swd_connected = false;
                picoprobe_info("=================================== DAPv2 disconnect target\n");
                led_state(LS_DAPV2_DISCONNECTED);
                sw_unlock("DAPv2");
            }
            swd_disconnect_requested = false;
            dap_packet_count = _DAP_PACKET_COUNT_UNKNOWN;
            dap_packet_size  = _DAP_PACKET_SIZE_UNKNOWN;
            tool = DAP_FingerprintTool(NULL, 0);
        }

        xEventGroupWaitBits(dap_events, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(100));  // TODO "pyocd reset -f 500000" does otherwise not disconnect

        if (tud_vendor_available())
        {
            rx_len += tud_vendor_read(RxDataBuffer + rx_len, sizeof(RxDataBuffer));

            if (rx_len != 0)
            {
                uint32_t request_len;

                request_len = DAP_GetCommandLength(RxDataBuffer, rx_len);
                if (rx_len >= request_len)
                {
                    last_request_us = time_us_32();
//                    picoprobe_info("<<<(%lx) %d %d\n", request_len, RxDataBuffer[0], RxDataBuffer[1]);

                    //
                    // try to find out which tool is connecting
                    //
                    if (tool == E_DAPTOOL_UNKNOWN) {
                        tool = DAP_FingerprintTool(RxDataBuffer, request_len);
                        if (tool == E_DAPTOOL_OPENOCD) {
                            dap_packet_count = _DAP_PACKET_COUNT_OPENOCD;
                            dap_packet_size  = _DAP_PACKET_SIZE_OPENOCD;
                        }
                        else if (tool == E_DAPTOOL_PYOCD) {
                            dap_packet_count = _DAP_PACKET_COUNT_PYOCD;
                            dap_packet_size  = _DAP_PACKET_SIZE_PYOCD;
                        }
                    }

                    //
                    // initiate SWD connect / disconnect
                    //
                    if ( !swd_connected  &&  RxDataBuffer[0] != ID_DAP_Info) {
                        if (sw_lock("DAPv2", true)) {
                            swd_connected = true;
                            picoprobe_info("=================================== DAPv2 connect target, host %s\n",
                                    (tool == E_DAPTOOL_OPENOCD) ? "OpenOCD with two big buffers" :
                                     ((tool == E_DAPTOOL_PYOCD) ? "pyOCD with single big buffer" : "UNKNOWN"));
                            led_state(LS_DAPV2_CONNECTED);
                        }
                    }
                    if (RxDataBuffer[0] == ID_DAP_Disconnect  ||  RxDataBuffer[0] == ID_DAP_Info  ||  RxDataBuffer[0] == ID_DAP_HostStatus) {
                        swd_disconnect_requested = true;
                    }
                    else {
                        swd_disconnect_requested = false;
                    }

                    //
                    // execute request and send back response
                    //
                    if (swd_connected  ||  DAP_OfflineCommand(RxDataBuffer))
                    {
                        uint32_t resp_len;

                        resp_len = DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
//                        picoprobe_info(">>>(%lx) %d %d %d %d\n", resp_len, TxDataBuffer[0], TxDataBuffer[1], TxDataBuffer[2], TxDataBuffer[3]);

                        tud_vendor_write(TxDataBuffer, resp_len & 0xffff);
                        tud_vendor_flush();

                        if (request_len != (resp_len >> 16))
                        {
                            // there is a bug in CMSIS-DAP, see https://github.com/ARM-software/CMSIS_5/pull/1503
                            // but we trust our own length calculation
                            picoprobe_error("   !!!!!!!! request (%lu) and executed length (%lu) differ\n", request_len, resp_len >> 16);
                        }

                        if (rx_len == request_len)
                        {
                            rx_len = 0;
                        }
                        else
                        {
                            memmove(RxDataBuffer, RxDataBuffer + request_len, rx_len - request_len);
                            rx_len -= request_len;
                        }
                    }
                }
            }
        }
    }
}   // dap_task
#endif



#if configGENERATE_RUN_TIME_STATS
static uint32_t tusb_count;
static TimerHandle_t timer_task_stat;
static EventGroupHandle_t events_task_stat;

char task_state(eTaskState state)
{
    static const char state_ch[] = "RrBSDI";
    return state_ch[state];
}   // task_state


void trigger_task_stat(TimerHandle_t xTimer)
{
    xEventGroupSetBits(events_task_stat, 0x01);
}   // trigger_task_stat


void print_task_stat(void *ptr)
{
    uint32_t prev_tusb_count = 0;
    HeapStats_t heap_status;
    TaskStatus_t task_status[TASK_MAX_CNT];
    uint32_t total_run_time;

    vTaskDelay(pdMS_TO_TICKS(5000));

    timer_task_stat = xTimerCreate("task stat", pdMS_TO_TICKS(10000), pdTRUE, NULL, trigger_task_stat);   // just for fun: exact period of 10s
    events_task_stat = xEventGroupCreate();

    xTimerReset(timer_task_stat, 0);

    for (;;) {
        printf("---------------------------------------\n");

#if LWIP_STATS
        {
            extern void stats_display(void);
            stats_display();
            printf("---------------------------------------\n");
        }
#endif

        printf("TinyUSB counter : %lu\n", tusb_count - prev_tusb_count);
        prev_tusb_count = tusb_count;
        vPortGetHeapStats( &heap_status);
        printf("curr heap free  : %d\n", heap_status.xAvailableHeapSpaceInBytes);
        printf("min heap free   : %d\n", heap_status.xMinimumEverFreeBytesRemaining);

        printf("number of tasks : %lu\n", uxTaskGetNumberOfTasks());
        if (uxTaskGetNumberOfTasks() > TASK_MAX_CNT) {
            printf("!!!!!!!!!!!!!!! redefine TASK_MAX_CNT to see task state\n");
        }
        else {
            // this part is critical concerning overflow because the numbers are getting quickly very big (us timer resolution)
            static uint32_t prev_tick_us[TASK_MAX_CNT+1];
            static uint32_t sum_tick_ms[TASK_MAX_CNT+1];
            static uint32_t total_sum_tick_ms;
            uint32_t cnt;
            uint32_t all_delta_tick_sum_us;
            uint32_t permille_sum;
            uint32_t permille_total_sum;

            cnt = uxTaskGetSystemState(task_status, TASK_MAX_CNT, &total_run_time);
            all_delta_tick_sum_us = 0;
            for (uint32_t n = 0;  n < cnt;  ++n) {
                uint32_t task_ndx = task_status[n].xTaskNumber;
                uint32_t ticks_us;

                assert(task_ndx < TASK_MAX_CNT + 1);
                ticks_us = task_status[n].ulRunTimeCounter - prev_tick_us[task_ndx];
                all_delta_tick_sum_us += ticks_us;
                sum_tick_ms[task_ndx] += (ticks_us + 500) / 1000;
            }
            printf("uptime [s]      : %lu\n", clock() / CLOCKS_PER_SEC);
            printf("delta tick sum  : %lu\n", all_delta_tick_sum_us);

            printf("NUM PRI  S/AM  CPU  TOT STACK  NAME\n");
            printf("---------------------------------------\n");

            all_delta_tick_sum_us /= configNUM_CORES;
            total_sum_tick_ms += (all_delta_tick_sum_us + 500) / 1000;

            permille_sum = 0;
            permille_total_sum = 0;
            for (uint32_t n = 0;  n < cnt;  ++n) {
                uint32_t permille;
                uint32_t permille_total;
                uint32_t curr_tick;
                uint32_t delta_tick;
                uint32_t task_ndx = task_status[n].xTaskNumber;

                curr_tick = task_status[n].ulRunTimeCounter;
                delta_tick = curr_tick - prev_tick_us[task_ndx];

                permille = (delta_tick + all_delta_tick_sum_us / 2000) / (all_delta_tick_sum_us / 1000);
                permille_total = (sum_tick_ms[task_ndx] + total_sum_tick_ms / 2000) / (total_sum_tick_ms / 1000);
                permille_sum += permille;
                permille_total_sum += permille_total;

#if defined(configUSE_CORE_AFFINITY)  &&  configUSE_CORE_AFFINITY != 0
                printf("%3lu  %2lu  %c/%2d %4lu %4lu %5lu  %s\n",
                               task_status[n].xTaskNumber,
                               task_status[n].uxCurrentPriority,
                               task_state(task_status[n].eCurrentState), (int)task_status[n].uxCoreAffinityMask,
                               permille, permille_total,
                               task_status[n].usStackHighWaterMark,
                               task_status[n].pcTaskName);
#else
                printf("%3lu  %2lu  %c/%2d %4lu %4lu %5lu  %s\n",
                               task_status[n].xTaskNumber,
                               task_status[n].uxCurrentPriority,
                               task_state(task_status[n].eCurrentState), 1,
                               permille, permille_total,
                               task_status[n].usStackHighWaterMark,
                               task_status[n].pcTaskName);
#endif

                prev_tick_us[task_ndx] = curr_tick;
            }
            printf("---------------------------------------\n");
            printf("              %4lu %4lu\n", permille_sum, permille_total_sum);
        }
        printf("---------------------------------------\n");

        xEventGroupWaitBits(events_task_stat, 0x01, pdTRUE, pdFALSE, pdMS_TO_TICKS(60000));
    }
}   // print_task_stat
#endif



void usb_thread(void *ptr)
{
#ifdef TARGET_BOARD_PICO_W
    if (cyw43_arch_init()) {
        printf("failed to initialize WiFi\n");
    }
#endif

    led_init(LED_TASK_PRIO);

#if 0  &&  defined(configUSE_CORE_AFFINITY)  &&  configUSE_CORE_AFFINITY != 0
    vTaskCoreAffinitySet(tud_taskhandle, 1);
#endif

    // do a first initialization, dynamic target detection is done in rtt_console
    if (g_board_info.prerun_board_config != NULL) {
        g_board_info.prerun_board_config();
    }

#if OPT_TARGET_UART
    cdc_uart_init(UART_TASK_PRIO);
#endif

#if OPT_CDC_SYSVIEW
    cdc_sysview_init(SYSVIEW_TASK_PRIO);
#endif

#if OPT_MSC
    msc_init(MSC_WRITER_THREAD_PRIO);
#endif

#if defined(INCLUDE_RTT_CONSOLE)
    rtt_console_init(RTT_CONSOLE_TASK_PRIO);
#endif

#if OPT_SIGROK
    sigrok_init(SIGROK_TASK_PRIO);
#endif

#if OPT_NET
    net_glue_init();

    #if OPT_NET_SYSVIEW_SERVER
        net_sysview_init();
    #endif
    #if OPT_NET_ECHO_SERVER
        net_echo_init();
    #endif
    #if OPT_NET_IPERF_SERVER
        // test with: iperf -c 192.168.10.1 -e -i 1 -l 1024
        lwiperf_start_tcp_server_default(NULL, NULL);
    #endif
#endif

#if OPT_CMSIS_DAPV2
    xTaskCreate(dap_task, "CMSIS-DAPv2", configMINIMAL_STACK_SIZE, NULL, DAPV2_TASK_PRIO, &dap_taskhandle);
#endif

#if configGENERATE_RUN_TIME_STATS
    {
        TaskHandle_t task_stat_handle;
        xTaskCreate(print_task_stat, "Print Task Stat", configMINIMAL_STACK_SIZE, NULL, PRINT_STATUS_TASK_PRIO, &task_stat_handle);
    }
#endif

#if defined(configUSE_CORE_AFFINITY)  &&  configUSE_CORE_AFFINITY != 0
    //
    // This is the only place to set task affinity.
    // TODO ATTENTION core affinity
    // Currently only "RTT-From" is running on an extra core (and "RTT-From" is a real hack).  This is because
    // if RTT is running on a different thread than tinyusb/lwip (not sure), the probe is crashing very
    // fast on SystemView events in net_sysview_send()
    //
    {
        TaskStatus_t task_status[TASK_MAX_CNT];
        uint32_t cnt;

        //picoprobe_info("Assign tasks to certain cores\n");
        cnt = uxTaskGetSystemState(task_status, TASK_MAX_CNT, NULL);
        if (cnt >= TASK_MAX_CNT) {
            picoprobe_error("TASK_MAX_CNT must be re-adjusted\n");
        }

        for (uint32_t n = 0;  n < cnt;  ++n) {
            if (    strcmp(task_status[n].pcTaskName, "IDLE1") == 0
                ||  strcmp(task_status[n].pcTaskName, "RTT-From") == 0
                ||  strcmp(task_status[n].pcTaskName, "RTT-IO-Dont-Do-That") == 0
            ) {
                // set it to core 1
                vTaskCoreAffinitySet(task_status[n].xHandle, 1 << 1);
            }
            else {
                // set it to core 0
                vTaskCoreAffinitySet(task_status[n].xHandle, 1 << 0);
            }
        }
    }
#endif

    tusb_init();
    for (;;) {
#if configGENERATE_RUN_TIME_STATS
        ++tusb_count;
#endif
        tud_task();             // the FreeRTOS version goes into blocking state if its event queue is empty
    }
}   // usb_thread



int main(void)
{
    board_init();
    ini_init();                              // for debugging this must be moved below cdc_debug_init()

    // set CPU frequency according to configuration
    probe_set_cpu_freq_khz( 1000 * ini_getl(MININI_SECTION, "f_cpu", PROBE_CPU_CLOCK_MHZ, MININI_FILENAME) );

    get_config_init();

    // initialize stdio and should be done before anything else (that does printf())
#if OPT_PROBE_DEBUG_OUT
    cdc_debug_init(CDC_DEBUG_TASK_PRIO);
#endif

    sw_lock_init();

    DAP_Setup();

    // now we can "print"
    picoprobe_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
    picoprobe_info("                     Welcome to Yet Another Picoprobe v" PICOPROBE_VERSION_STRING "-" GIT_HASH "\n");
    picoprobe_info("Features:\n");
    picoprobe_info(" %s\n", CONFIG_FEATURES());
    picoprobe_info("Probe HW:\n");
    picoprobe_info("  %s @ %luMHz\n", CONFIG_BOARD(), (probe_get_cpu_freq_khz() + 500) / 1000);
#if OPT_NET
    picoprobe_info("IP:\n");
    picoprobe_info("  192.168.%ld.1\n", ini_getl(MININI_SECTION, "net", OPT_NET_192_168, MININI_FILENAME));
#endif
    picoprobe_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

    dap_events = xEventGroupCreate();

    // it seems that TinyUSB does not like affinity setting in its thread, so the affinity of the USB thread is corrected in the task itself
    xTaskCreate(usb_thread, "TinyUSB Main", 4096, NULL, TUD_TASK_PRIO, &tud_taskhandle);
    vTaskStartScheduler();

    return 0;
}



#if OPT_CMSIS_DAPV1
static bool hid_swd_connected;
static bool hid_swd_disconnect_requested;
static TimerHandle_t     timer_hid_disconnect = NULL;
static void             *timer_hid_disconnect_id;


static void hid_disconnect(TimerHandle_t xTimer)
{
    if (hid_swd_disconnect_requested  &&  hid_swd_connected) {
        hid_swd_connected = false;
        picoprobe_info("=================================== DAPv1 disconnect target\n");
        led_state(LS_DAPV1_DISCONNECTED);
        sw_unlock("DAPv1");
    }
}   // hid_disconnect



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

    if (timer_hid_disconnect == NULL) {
        timer_hid_disconnect = xTimerCreate("timer_hid_disconnect", pdMS_TO_TICKS(1000), pdFALSE, timer_hid_disconnect_id,
                                            hid_disconnect);
        if (timer_hid_disconnect == NULL) {
            picoprobe_error("tud_hid_set_report_cb: cannot create timer_hid_disconnect\n");
        }
    }
    else {
        xTimerReset(timer_hid_disconnect, pdMS_TO_TICKS(1000));
    }

    //
    // initiate SWD connect / disconnect
    //
    if ( !hid_swd_connected  &&  RxDataBuffer[0] != ID_DAP_Info) {
        if (sw_lock("DAPv1", true)) {
            hid_swd_connected = true;
            picoprobe_info("=================================== DAPv1 connect target\n");
            led_state(LS_DAPV1_CONNECTED);
        }
    }
    if (RxDataBuffer[0] == ID_DAP_Disconnect  ||  RxDataBuffer[0] == ID_DAP_Info  ||  RxDataBuffer[0] == ID_DAP_HostStatus) {
        hid_swd_disconnect_requested = true;

        // this is the minimum version which should always work
        dap_packet_count = _DAP_PACKET_COUNT_HID;
        dap_packet_size  = _DAP_PACKET_SIZE_HID;
    }
    else {
        hid_swd_disconnect_requested = false;
    }

    //
    // execute request and send back response
    //
    if (hid_swd_connected  ||  DAP_OfflineCommand(RxDataBuffer)) {
#if 0
        // heavy debug output, set dap_packet_count=2 to stumble into the bug
        uint32_t request_len = DAP_GetCommandLength(RxDataBuffer, bufsize);
        picoprobe_info("< ");
        for (int i = 0;  i < bufsize;  ++i) {
            picoprobe_info_out(" %02x", RxDataBuffer[i]);
            if (i == request_len - 1) {
                picoprobe_info_out(" !!!!");
            }
        }
        picoprobe_info_out("\n");
        vTaskDelay(pdMS_TO_TICKS(30));
        uint32_t res = DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
        picoprobe_info("> %lu %lu\n", res >> 16, res & 0xffff);
#else
        DAP_ExecuteCommand(RxDataBuffer, TxDataBuffer);
#endif
        tud_hid_report(0, TxDataBuffer, response_size);
    }
}   // tud_hid_set_report_cb
#endif



#if OPT_CMSIS_DAPV2
extern uint8_t const desc_ms_os_20[];

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
    // nothing to with DATA & ACK stage
    if (stage != CONTROL_STAGE_SETUP)
        return true;

    switch (request->bmRequestType_bit.type) {
        case TUSB_REQ_TYPE_VENDOR:
            switch (request->bRequest) {
                case 1:
                    if (request->wIndex == 7) {
                        // Get Microsoft OS 2.0 compatible descriptor
                        uint16_t total_len;
                        memcpy(&total_len, desc_ms_os_20 + 8, 2);

                        return tud_control_xfer(rhport, request, (void*) desc_ms_os_20, total_len);
                    }
                    else {
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
}   // tud_vendor_control_xfer_cb
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
