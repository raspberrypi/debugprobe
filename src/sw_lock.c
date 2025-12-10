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

#include <stdio.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "picoprobe_config.h"
#include "sw_lock.h"


static SemaphoreHandle_t  sema_lock;
static volatile bool      lock_requested;


bool sw_lock(E_SWLOCK who)
/**
 * Lock SW access.  Idea is, that DAP/MSC access is exclusive, while the RTT console has a low priority and
 * can be superseded by the former.  But RTT console tries to have SW as long as possible.
 * To allow priority to DAP/MSC, the RTT console has to query sw_unlock_requested() periodically.
 *
 * \param who    name of the caller, just for debugging
 * \return  true -> got the lock.  Note that false might only happen if \a who != \a E_SWLOCK_RTT is true
 */
{
    BaseType_t r;

    if (who == E_SWLOCK_RTT) {
        // RTT console: wait until SW is free
        r = xSemaphoreTake(sema_lock, portMAX_DELAY);
    }
    else {
        // wait just a short period and try to supersede RTT console
        lock_requested = true;
        picoprobe_debug("sw_lock(%d)...\n", who);
        r = xSemaphoreTake(sema_lock, pdMS_TO_TICKS(1000));
        lock_requested = false;
    }
    picoprobe_debug("sw_lock(%d) = %ld\n", who, r);
    return (r == pdTRUE) ? true : false;
}   // sw_lock



void sw_unlock(E_SWLOCK who)
/**
 * Free SW access.
 */
{
    BaseType_t r;

    r = xSemaphoreGive(sema_lock);
    (void)r;  // suppress warning from compiler
    picoprobe_debug("sw_unlock(%d) = %ld\n", who, r);
}   // sw_unlock



bool sw_unlock_requested(void)
/**
 * Indicate if DAP/MSC want to lock SW.
 */
{
    return lock_requested;
}   // sw_unlock_request



void sw_lock_init(void)
{
    picoprobe_debug("sw_lock_init\n");
    sema_lock = xSemaphoreCreateBinary();    // don't know why, but xSemaphoreCreateMutex() leads to hang on ...Take()
    if (sema_lock == NULL) {
        panic("sw_lock_init: cannot create sema_lock\n");
    }
    xSemaphoreGive(sema_lock);
}   // sw_lock_init
