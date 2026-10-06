#include "app_queue_experiment.h"
#include "bsp_experiment.h"
#include "bsp_i2c.h"
#include "sht30.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stdio.h>

static QueueHandle_t sample_queue;
static TaskHandle_t acquisition_handle;
static SHT30_t sensor;
enum FailureStage
{
    FAILURE_NONE = 0,
    FAILURE_START,
    FAILURE_READ
};

struct SensorDiagnostics
{
    uint32_t failure_count;
    SHT30_Status_t last_status;
    enum FailureStage last_failure_stage;
    SHT30_Status_t last_failure_status;
};

/* A writes and B copies under the same short critical-section discipline.
 * Only these two tasks access this record; no ISR reads or writes it. */
static struct SensorDiagnostics diagnostics = {
    0, SHT30_STATUS_OK, FAILURE_NONE, SHT30_STATUS_OK
};
volatile uint32_t g_acquired_count;
volatile uint32_t g_skipped_count;
volatile uint32_t g_received_count;
volatile uint32_t g_uart_completed_count;
volatile uint32_t g_uart_error_count;

static SHT30_BusStatus_t MapBusStatus(BSP_I2C_Status_t status)
{
    switch (status)
    {
    case BSP_I2C_STATUS_OK: return SHT30_BUS_STATUS_OK;
    case BSP_I2C_STATUS_INVALID_ARGUMENT: return SHT30_BUS_STATUS_INVALID_ARGUMENT;
    case BSP_I2C_STATUS_NACK: return SHT30_BUS_STATUS_NACK;
    case BSP_I2C_STATUS_TIMEOUT: return SHT30_BUS_STATUS_TIMEOUT;
    case BSP_I2C_STATUS_BUSY: return SHT30_BUS_STATUS_BUSY;
    default: return SHT30_BUS_STATUS_ERROR;
    }
}

static SHT30_BusStatus_t SensorWrite(void *context, uint8_t address,
    const uint8_t *data, uint16_t length, uint32_t timeout)
{
    (void)context;
    return MapBusStatus(BSP_I2C1_Write(address, data, length, timeout));
}

static SHT30_BusStatus_t SensorRead(void *context, uint8_t address,
    uint8_t *data, uint16_t length, uint32_t timeout)
{
    (void)context;
    return MapBusStatus(BSP_I2C1_Read(address, data, length, timeout));
}

/* Core sequence written by the learner in chat; integration/diagnostics by Codex. */
static void AcquisitionTask(void *argument)
{
    uint32_t next_number = 0;
    struct Sample sample = {0};
    SHT30_Measurement_t measurement;
    SHT30_Status_t status;
    enum FailureStage current_failure_stage;
    (void)argument;

    while (1)
    {
        current_failure_stage = FAILURE_NONE;
        status = SHT30_StartHighRepeatabilityMeasurement(&sensor);
        if (status == SHT30_STATUS_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(SHT30_HIGH_REPEATABILITY_WAIT_MS) + 1);
            status = SHT30_ReadMeasurement(&sensor, &measurement);
            if (status == SHT30_STATUS_OK)
            {
                sample.number = next_number;
                next_number++;
                sample.temperature = (int16_t)(measurement.temperature_milli_c / 10);
                sample.humidity = (uint16_t)(measurement.humidity_milli_percent / 10);
                sample.acquired_tick = xTaskGetTickCount();
                g_acquired_count = next_number;
            }
            else
            {
                current_failure_stage = FAILURE_READ;
            }
        }
        else
        {
            current_failure_stage = FAILURE_START;
        }

        /* Learner's update logic: count once and retain the last failure pair. */
        taskENTER_CRITICAL();
        diagnostics.last_status = status;
        if (status != SHT30_STATUS_OK)
        {
            diagnostics.failure_count++;
            diagnostics.last_failure_stage = current_failure_stage;
            diagnostics.last_failure_status = status;
        }
        taskEXIT_CRITICAL();

        /* Publish after recording this attempt; never call queue/HAL in a
         * diagnostic critical section. These are still separate snapshots. */
        if (status == SHT30_STATUS_OK) xQueueOverwrite(sample_queue, &sample);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void ReportingTask(void *argument)
{
    struct Sample received = {0};
    uint32_t received_count = 0;
    uint32_t next_expected_number = 0;
    uint32_t skipped_count = 0;
    uint32_t received_tick;
    struct SensorDiagnostics snapshot;
    int length;
    char line[256];
    (void)argument;

    while (1)
    {
        if (xQueueReceive(sample_queue, &received, pdMS_TO_TICKS(2000)) == pdPASS)
        {
            received_count++;
            g_received_count = received_count;
            received_tick = xTaskGetTickCount();
            /* Single producer, monotonically increasing uint32 sequence.
             * Count gaps up to THIS received message, including initial gaps.
             * Pending overwrites beyond it aren't counted until a later receive. */
            skipped_count += received.number - next_expected_number;
            next_expected_number = received.number + 1U;
            g_skipped_count = skipped_count;
            taskENTER_CRITICAL();
            snapshot = diagnostics;
            taskEXIT_CRITICAL();

            length = snprintf(line, sizeof(line),
                "RTOSS|seq=%lu|tick=%lu|rx=%lu|skip=%lu|sample_tick=%lu|age=%lu|temp=%d|hum=%u"
                "|txerr=%lu|stack_a=%lu|stack_b=%lu|fail=%lu|last=%lu|fail_stage=%u|fail_status=%u\r\n",
                (unsigned long)received.number,
                (unsigned long)received_tick,
                (unsigned long)received_count,
                (unsigned long)skipped_count,
                (unsigned long)received.acquired_tick,
                (unsigned long)(received_tick - received.acquired_tick),
                (int)received.temperature, (unsigned int)received.humidity,
                (unsigned long)g_uart_error_count,
                (unsigned long)uxTaskGetStackHighWaterMark(acquisition_handle),
                (unsigned long)uxTaskGetStackHighWaterMark(NULL),
                (unsigned long)snapshot.failure_count,
                (unsigned long)snapshot.last_status,
                (unsigned int)snapshot.last_failure_stage,
                (unsigned int)snapshot.last_failure_status);
        }
        else
        {
            taskENTER_CRITICAL();
            snapshot = diagnostics;
            taskEXIT_CRITICAL();
            length = snprintf(line, sizeof(line),
                "SHTSTAT|tick=%lu|state=no_new_data|fail=%lu|last=%lu|fail_stage=%u|fail_status=%u\r\n",
                (unsigned long)xTaskGetTickCount(),
                (unsigned long)snapshot.failure_count,
                (unsigned long)snapshot.last_status,
                (unsigned int)snapshot.last_failure_stage,
                (unsigned int)snapshot.last_failure_status);
        }
        if ((length <= 0) || (length >= (int)sizeof(line))) Experiment_Panic(7U);
        if (BSP_Experiment_Write(line, (uint16_t)length) == HAL_OK)
            g_uart_completed_count++;
        else
            g_uart_error_count++;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void App_QueueExperiment_Create(void)
{
    SHT30_Bus_t bus = {SensorWrite, SensorRead, NULL, 20U};
    if (BSP_I2C1_Init() != BSP_I2C_STATUS_OK) Experiment_Panic(16U);
    if (SHT30_Init(&sensor, &bus, SHT30_ADDRESS_DEFAULT_7BIT) != SHT30_STATUS_OK)
        Experiment_Panic(17U);
    sample_queue = xQueueCreate(1, sizeof(struct Sample));
    if (sample_queue == NULL) Experiment_Panic(3U);
    if (xTaskCreate(AcquisitionTask, "Acquire", 256, NULL, 2,
                    &acquisition_handle) != pdPASS) Experiment_Panic(4U);
    /* 512 words because B now has snprintf and a 256-byte text buffer.
     * A/B stack high-water marks are reported for later board validation. */
    if (xTaskCreate(ReportingTask, "Report", 512, NULL, 1,
                    NULL) != pdPASS) Experiment_Panic(5U);
}
