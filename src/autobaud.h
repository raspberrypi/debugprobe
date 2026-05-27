#ifndef AUTOBAUD_H
#define AUTOBAUD_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#define MAGIC_BAUD 9728 // 0x2600

typedef enum {
    AUTOBAUD_CMD_NONE = 0,
    AUTOBAUD_CMD_START = 1,
    AUTOBAUD_CMD_STOP = 2,
} autobaud_cmd_t;

typedef struct {
    uint32_t baud;  // Estimated baud rate
    float validity; // Validity of the estimated baud rate
} BaudInfo_t;

extern volatile bool autobaud_running;
extern volatile bool autobaud_stopped;
extern QueueHandle_t baudQueue;
extern TaskHandle_t autobaud_taskhandle;

void autobaud_start(void);
void autobaud_wait_stop(void);
void autobaud_thread(void * param);

#endif