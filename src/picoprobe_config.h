/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Raspberry Pi (Trading) Ltd.
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

#ifndef PICOPROBE_H_
#define PICOPROBE_H_

#include "FreeRTOS.h"
#include "task.h"

#if false
#define picoprobe_info(format,args...) \
do { \
	vTaskSuspendAll(); \
	printf(format, ## args); \
	xTaskResumeAll(); \
} while (0)
#else
#define picoprobe_info(format,...) ((void)0)
#endif


#if false
#define picoprobe_debug(format,args...) \
do { \
	vTaskSuspendAll(); \
	printf(format, ## args); \
	xTaskResumeAll(); \
} while (0)
#else
#define picoprobe_debug(format,...) ((void)0)
#endif

#if false
#define picoprobe_dump(format,args...)\
do { \
	vTaskSuspendAll(); \
	printf(format, ## args); \
	xTaskResumeAll(); \
} while (0)
#else
#define picoprobe_dump(format,...) ((void)0)
#endif

// TODO tie this up with PICO_BOARD defines in the main SDK

#ifndef DEBUGPROBE

#if CDC_UARTS < 1 || CDC_UARTS > 2
#error "PICOPROBE only supports one or two UARTs"
#endif
#include "board_pico_config.h"

#else /* DEBUGPROBE */

#if CDC_UARTS != 1
#error "DEBUGPROBE only supports one UART"
#endif
#include "board_debugprobe_config.h"

#endif /* DEBUGPROBE */
//#include "board_example_config.h"


#define PROTO_DAP_V1 1
#define PROTO_DAP_V2 2

// Interface config
#ifndef PICOPROBE_DEBUG_PROTOCOL
#define PICOPROBE_DEBUG_PROTOCOL PROTO_DAP_V2
#endif

#endif
