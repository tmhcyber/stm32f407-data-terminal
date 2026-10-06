#ifndef APP_QUEUE_EXPERIMENT_H
#define APP_QUEUE_EXPERIMENT_H

#include <stdint.h>

struct Sample
{
    uint32_t number;
    int16_t temperature;
    uint16_t humidity;
    uint32_t acquired_tick;
};

void App_QueueExperiment_Create(void);

/* Watch-window observations, not a general concurrent snapshot interface. */
extern volatile uint32_t g_acquired_count;
extern volatile uint32_t g_skipped_count;
extern volatile uint32_t g_received_count;
extern volatile uint32_t g_uart_completed_count;
extern volatile uint32_t g_uart_error_count;

#endif
