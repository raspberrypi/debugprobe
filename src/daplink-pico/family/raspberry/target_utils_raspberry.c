/* CMSIS-DAP Interface Firmware
 * Copyright (c) 2024 Hardy Griech
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdio.h>
#include "DAP_config.h"
#include "target_family.h"
#include "swd_host.h"

#include "target_utils_raspberry.h"

#include "FreeRTOS.h"
#include "task.h"


#define DBG_Addr     (0xe000edf0)
#include "debug_cm.h"


//----------------------------------------------------------------------------------------------------------------------
//
// some utility functions
//


bool target_core_is_halted(void)
{
    uint32_t value;

    if ( !swd_read_word(DBG_HCSR, &value))
        return false;
    if (value & S_HALT)
        return true;
    return false;
}   // target_core_is_halted



bool target_core_halt(void)
{
    if ( !swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_MASKINTS | C_HALT)) {
        return false;
    }

    while ( !target_core_is_halted())
        ;
    return true;
}   // target_core_halt



bool target_core_unhalt(void)
{
    if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN)) {
        return false;
    }
    return true;
}   // target_core_unhalt



bool target_core_unhalt_with_masked_ints(void)
{
    if (!swd_write_word(DBG_HCSR, DBGKEY | C_DEBUGEN | C_MASKINTS)) {
        return false;
    }
    return true;
}   // target_core_unhalt_with_masked_ints
