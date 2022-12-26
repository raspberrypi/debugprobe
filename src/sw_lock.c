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

#include "FreeRTOS.h"
#include "semphr.h"

#include "picoprobe_config.h"
#include "sw_lock.h"


static SemaphoreHandle_t      sema_lock;


bool sw_lock(const char *who, bool wait_some_ms)
{
    BaseType_t r;

    r = xSemaphoreTake(sema_lock, wait_some_ms ? pdMS_TO_TICKS(200) : 0);
    picoprobe_debug("sw_lock('%s', %d) = %ld\n", who, wait_some_ms, r);
    return (r == pdTRUE) ? true : false;
}   // sw_lock



void sw_unlock(const char *who)
{
    BaseType_t r;

    r = xSemaphoreGive(sema_lock);
    picoprobe_debug("sw_unlock('%s') = %ld\n", who, r);
}   // sw_unlock



void sw_unlock_request(uint32_t prio)
{

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
