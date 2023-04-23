/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 a-pushkin on GitHub
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

#ifndef LED_H
#define LED_H


#include <stdint.h>


typedef enum _led_state {
    LS_TARGET_FOUND,          // there is a target
    LS_NO_TARGET,             // no target found
    LS_RTT_CB_FOUND,          // found an RTT control block on target
    LS_RTT_RX_DATA,           // RTT data received from target
    LS_UART_RX_DATA,          // UART data received from target
    LS_UART_TX_DATA,          // UART data transmitted to target
    LS_MSC_CONNECTED,         // MSC connected
    LS_MSC_DISCONNECTED,      // MSC disconnected
    LS_DAPV1_CONNECTED,       // DAPV1 connected
    LS_DAPV1_DISCONNECTED,    // DAPV1 disconnected
    LS_DAPV2_CONNECTED,       // DAPV2 connected
    LS_DAPV2_DISCONNECTED,    // DAPV2 disconnected
    LS_SIGROK_WAIT,           // sigrok waits for trigger
    LS_SIGROK_RUNNING,        // sigrok is running
    LS_SIGROK_STOPPED,        // sigrok stopped
} led_state_t;

void led_init(uint32_t task_prio);
void led_state(led_state_t state);

#endif
