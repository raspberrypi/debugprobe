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

#ifdef TARGET_BOARD_PICO_W
    #include <pico/cyw43_arch.h>
#endif
#if OPT_PROBE_DEBUG_OUT_RTT
    #include "pico/stdio/driver.h"
#endif

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#if 0
    // in the released SDK 1.5.1 with TinyUSB 0.15.0 the include path was "bsp/board.h", so we solve it with a hack.
    #include "bsp/board_api.h"
#else
    extern void board_init(void);
#endif
#include "tusb.h"

#include "picoprobe_config.h"
#include "probe.h"
#if OPT_PROBE_DEBUG_OUT_CDC
    #include "cdc/cdc_debug.h"
#endif
#if OPT_TARGET_UART
    #include "cdc/cdc_uart.h"
#endif
#if OPT_CDC_SYSVIEW
    #include "cdc/cdc_sysview.h"
#endif
#if OPT_CMSIS_DAPV2
    #include "cmsis-dap/dap_server.h"
#endif
#include "get_config.h"
#include "led.h"
#include "sw_lock.h"

#include "DAP_config.h"
#include "DAP.h"

#include "target_board.h"    // DAPLink

#include "minIni/minIni.h"

#if OPT_MSC
    #include "msc/msc_utils.h"
#endif
#if defined(INCLUDE_RTT_CONSOLE)
    #include "rtt_io.h"
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

#if OPT_PROBE_DEBUG_OUT_RTT
    #include "RTT/SEGGER_RTT.h"
#endif


#ifdef NDEBUG
    #define BUILD_TYPE "release build"
#else
    #define BUILD_TYPE "debug build"
#endif

#if PICO_RP2350
    #define PROBE_MCU "rp2350"
#else
    #define PROBE_MCU "rp2040"
#endif



// maximum number of expected FreeRTOS task (used for uxTaskGetSystemState())
#define TASK_MAX_CNT                15

// prios are critical and determine throughput
#define LED_TASK_PRIO               (tskIDLE_PRIORITY + 30)       // simple task which may interrupt everything else for periodic blinking
#define TUD_TASK_PRIO               (tskIDLE_PRIORITY + 28)       // high prio for TinyUSB (must be higher then lwIP!)
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



void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
#if OPT_TARGET_UART
    if (itf == CDC_UART_N) {
        cdc_uart_line_state_cb(dtr, rts);
    }
#endif
#if OPT_PROBE_DEBUG_OUT_CDC
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
#if OPT_PROBE_DEBUG_OUT_CDC
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
#if OPT_PROBE_DEBUG_OUT_CDC
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

            all_delta_tick_sum_us /= configNUMBER_OF_CORES;
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
    if (ini_getbool(MININI_SECTION, MININI_VAR_RTT, true, MININI_FILENAME))
    {
        rtt_console_init(RTT_CONSOLE_TASK_PRIO);
    }
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
    dap_server_init(DAPV2_TASK_PRIO);
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



#if OPT_PROBE_DEBUG_OUT_RTT
    static void stdio_rtt_out_chars(const char *buf, int length)
    {
        SEGGER_RTT_Write(0, buf, length);
    }   // stdio_rtt_out_chars


    stdio_driver_t stdio_rtt = {
        .out_chars = stdio_rtt_out_chars,
    #if PICO_STDIO_ENABLE_CRLF_SUPPORT
        .crlf_enabled = false
    #endif
    };
#endif



int main(void)
{
    board_init();
    ini_init();                              // for debugging this must be moved below cdc_debug_init()

    // set CPU frequency according to configuration
    probe_set_cpu_freq_khz( 1000 * ini_getl(MININI_SECTION, MININI_VAR_FCPU, PROBE_CPU_CLOCK_MHZ, MININI_FILENAME) );

    get_config_init();

    // initialize stdio and should be done before anything else (that does printf())
#if OPT_PROBE_DEBUG_OUT_CDC
    cdc_debug_init(CDC_DEBUG_TASK_PRIO);
#endif
#if OPT_PROBE_DEBUG_OUT_UART
    setup_default_uart();
#endif
#if OPT_PROBE_DEBUG_OUT_RTT
    stdio_set_driver_enabled(&stdio_rtt, true);
#endif

    sw_lock_init();

    DAP_Setup();

    // now we can "print"
    picoprobe_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");
    picoprobe_info("                     Welcome to Yet Another Picoprobe v" PICOPROBE_VERSION_STRING "-" GIT_HASH "\n");
    picoprobe_info("Features:\n");
    picoprobe_info(" %s\n", CONFIG_FEATURES());
    picoprobe_info("Probe HW:\n");
    picoprobe_info("  %s (" PROBE_MCU ") @ %uMHz (%s core)\n", CONFIG_BOARD(), (unsigned)((probe_get_cpu_freq_khz() + 500) / 1000),
                                               (configNUMBER_OF_CORES > 1) ? "dual" : "single");
#if OPT_NET
    picoprobe_info("IP:\n");
    picoprobe_info("  192.168.%d.1\n", (int)ini_getl(MININI_SECTION, MININI_VAR_NET, OPT_NET_192_168, MININI_FILENAME));
#endif
    picoprobe_info("Compiler:\n");
#if defined(__clang__)
    picoprobe_info("  clang %d.%d.%d - " BUILD_TYPE "\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    picoprobe_info("  gcc %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    picoprobe_info("  UNKNOWN\n");
#endif
    picoprobe_info("PICO-SDK:\n");
    picoprobe_info("  " PICO_SDK_VERSION_STRING "\n");
    picoprobe_info("++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n");

    // it seems that TinyUSB does not like affinity setting in its thread, so the affinity of the USB thread is corrected in the task itself
    xTaskCreate(usb_thread, "TinyUSB Main", 4096, NULL, TUD_TASK_PRIO, &tud_taskhandle);
    vTaskStartScheduler();

    return 0;
}



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
