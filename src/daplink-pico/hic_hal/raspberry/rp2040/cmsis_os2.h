#ifndef _CMSIS_OS2_H
#define _CMSIS_OS2_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif


static inline void osDelay(uint32_t ticks)
{
    vTaskDelay(pdMS_TO_TICKS(ticks));
}


#ifdef __cplusplus
}
#endif

#endif
