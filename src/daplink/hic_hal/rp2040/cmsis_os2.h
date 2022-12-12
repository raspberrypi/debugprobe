#ifndef _CMSIS_OS2_H
#define _CMSIS_OS2_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


void osDelay(uint32_t ticks);

extern int cdc_debug_printf(const char* format, ...) __attribute__ ((format (printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif
