/* Execute the actual application's task bodies with a fake queue/UART.
 * This tests application logic, not the FreeRTOS scheduler or hardware. */
#include <setjmp.h>
#include <string.h>
#include "../Application/Src/app_queue_experiment.c"

static jmp_buf stopped;
static struct Sample sent[3];
static struct Sample mailbox;
static uint32_t fake_tick;
static unsigned sends, delays, receives, writes, created;
static unsigned report_delays;
static unsigned fail_queue, fail_task, panic_reason;
static char output[3][256];

static unsigned attempts, conversion_waits, sensor_reads;
static unsigned critical_depth, critical_entries, snapshot_mutated;
void TestEnterCritical(void)
{
    assert(critical_depth == 0);
    critical_depth = 1;
    ++critical_entries;
}
void TestExitCritical(void)
{
    assert(critical_depth == 1);
    critical_depth = 0;
    if (receives == 1 && !snapshot_mutated)
    {
        /* Model A running immediately after B copied its snapshot. */
        diagnostics.failure_count = 3;
        diagnostics.last_status = SHT30_STATUS_BUS_NACK;
        diagnostics.last_failure_stage = FAILURE_START;
        diagnostics.last_failure_status = SHT30_STATUS_BUS_NACK;
        snapshot_mutated = 1;
    }
}
BSP_I2C_Status_t BSP_I2C1_Init(void) { return BSP_I2C_STATUS_OK; }
BSP_I2C_Status_t BSP_I2C1_Write(uint8_t address, const uint8_t *data,
                              uint16_t length, uint32_t timeout)
{
    assert(critical_depth == 0);
    assert(address == 0x44 && length == 2 && timeout == 20);
    assert(data[0] == 0x24 && data[1] == 0);
    ++attempts;
    return attempts == 1 ? BSP_I2C_STATUS_NACK : BSP_I2C_STATUS_OK;
}
static uint8_t crc(const uint8_t *data)
{
    uint8_t value = 0xff;
    for (unsigned i = 0; i < 2; ++i) {
        value ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (uint8_t)((value & 0x80) ? (value << 1) ^ 0x31 : value << 1);
    }
    return value;
}
BSP_I2C_Status_t BSP_I2C1_Read(uint8_t address, uint8_t *data,
                             uint16_t length, uint32_t timeout)
{
    assert(critical_depth == 0);
    assert(address == 0x44 && length == 6 && timeout == 20);
    assert(conversion_waits == ++sensor_reads);
    data[0] = 0x66; data[1] = 0x66; data[2] = crc(data);
    data[3] = 0x99; data[4] = 0x99; data[5] = crc(data + 3);
    if (attempts == 2) data[2] ^= 1;
    return BSP_I2C_STATUS_OK;
}

QueueHandle_t xQueueCreate(UBaseType_t count, UBaseType_t size)
{
    assert(count == 1 && size == sizeof(struct Sample));
    return fail_queue ? NULL : (void *)1;
}

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned depth,
                      void *arg, UBaseType_t priority, TaskHandle_t *handle)
{
    (void)name;
    assert(arg == NULL);
    ++created;
    if (created == 1)
        assert(task == AcquisitionTask && depth == 256 && priority == 2);
    else
        assert(task == ReportingTask && depth == 512 && priority == 1);
    if (handle) *handle = (void *)2;
    return created == fail_task ? 0 : pdPASS;
}

BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *data)
{
    assert(critical_depth == 0);
    assert(queue == (void *)1 && sends < 3);
    sent[sends] = *(const struct Sample *)data;
    mailbox = *(const struct Sample *)data;
    ++sends;
    return pdPASS;
}

void vTaskDelay(TickType_t ticks)
{
    assert(critical_depth == 0);
    if (receives != 0)
    {
        assert(ticks == 3000);
        assert(writes == receives && writes == report_delays + 1);
        ++report_delays;
        return;
    }
    if (ticks == 16) { ++conversion_waits; fake_tick += ticks; return; }
    assert(ticks == 1000);
    assert(critical_entries == attempts);
    if (attempts == 1) {
        assert(sends == 0 && diagnostics.failure_count == 1);
        assert(diagnostics.last_status == SHT30_STATUS_BUS_NACK);
        assert(diagnostics.last_failure_stage == FAILURE_START);
        assert(diagnostics.last_failure_status == SHT30_STATUS_BUS_NACK);
    } else if (attempts == 2) {
        assert(sends == 0 && diagnostics.failure_count == 2);
        assert(diagnostics.last_status == SHT30_STATUS_TEMPERATURE_CRC_ERROR);
        assert(diagnostics.last_failure_stage == FAILURE_READ);
        assert(diagnostics.last_failure_status == SHT30_STATUS_TEMPERATURE_CRC_ERROR);
    } else {
        assert(diagnostics.failure_count == 2 && diagnostics.last_status == SHT30_STATUS_OK);
        assert(diagnostics.last_failure_stage == FAILURE_READ);
        assert(diagnostics.last_failure_status == SHT30_STATUS_TEMPERATURE_CRC_ERROR);
    }
    fake_tick += ticks;
    if (++delays == 5) longjmp(stopped, 1);
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *data, TickType_t wait)
{
    assert(critical_depth == 0);
    assert(queue == (void *)1 && wait == 2000);
    if (receives == 3) longjmp(stopped, 1);
    if (receives == 2) { ++receives; return 0; }
    if (receives == 1)
    {
        /* Model three more A publications while B was blocked. */
        mailbox.number = 5;
        mailbox.acquired_tick = 5000;
    }
    *(struct Sample *)data = mailbox;
    fake_tick = mailbox.acquired_tick + 9;
    ++receives;
    return pdPASS;
}

TickType_t xTaskGetTickCount(void) { return fake_tick; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle)
{
    return handle ? 180U : 280U;
}

HAL_StatusTypeDef BSP_Experiment_Write(const char *text, uint16_t length)
{
    assert(critical_depth == 0);
    assert(writes < 3 && length < sizeof(output[0]));
    memcpy(output[writes], text, length);
    output[writes][length] = '\0';
    ++writes;
    return writes == 1 ? HAL_ERROR : HAL_OK;
}

void Experiment_Panic(uint32_t reason)
{
    panic_reason = reason;
    longjmp(stopped, 1);
}

int main(void)
{
    App_QueueExperiment_Create();
    assert(created == 2);
    if (setjmp(stopped) == 0) AcquisitionTask(NULL);
    assert(sends == 3 && delays == 5 && g_acquired_count == 3);
    for (unsigned i = 0; i < 3; ++i)
        assert(sent[i].number == i && sent[i].temperature == 2500 && sent[i].humidity == 6000 && sent[i].acquired_tick == 2032 + i * 1016);
    assert(mailbox.number == 2);
    assert(diagnostics.failure_count == 2 && diagnostics.last_status == SHT30_STATUS_OK);
    assert(attempts == 5 && sensor_reads == 4 && conversion_waits == 4);
    puts("PASS: start NACK and read CRC failure do not publish; conversion blocks; recovery publishes valid consecutive sequences");

    if (setjmp(stopped) == 0) ReportingTask(NULL);
    assert(g_received_count == 2 && g_uart_completed_count == 2 && g_uart_error_count == 1);
    assert(report_delays == 3);
    assert(g_skipped_count == 4);
    assert(strstr(output[0], "RTOSS|seq=2|tick=4073|rx=1|skip=2|sample_tick=4064|age=9|temp=2500|hum=6000|txerr=0|"));
    assert(strstr(output[1], "RTOSS|seq=5|tick=5009|rx=2|skip=4|sample_tick=5000|age=9|temp=2500|hum=6000|txerr=1|"));
    assert(strstr(output[0], "|fail=2|last=0|fail_stage=2|fail_status=6\r\n"));
    assert(strstr(output[1], "|fail=3|last=2|fail_stage=1|fail_status=2\r\n"));
    assert(strstr(output[2], "state=no_new_data|fail=3|last=2|fail_stage=1|fail_status=2\r\n"));
    assert(critical_entries == 8 && snapshot_mutated == 1);
    puts("PASS: failure stage/reason retained through recovery; B snapshot survives later A update; timeout reports diagnostics");
    puts("PASS: B receives into its own message and distinguishes receive/UART success/error");

    created = 0; fail_queue = 1;
    if (setjmp(stopped) == 0) { App_QueueExperiment_Create(); assert(0); }
    assert(panic_reason == 3 && created == 0);
    puts("PASS: queue allocation failure prevents task creation");

    fail_queue = 0; fail_task = 1;
    if (setjmp(stopped) == 0) { App_QueueExperiment_Create(); assert(0); }
    assert(panic_reason == 4 && created == 1);
    puts("PASS: first task creation failure stops startup");
    return 0;
}
