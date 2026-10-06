#include "bsp_can_rx_queue.h"
#include "stm32f4xx_hal.h"

static BSP_CAN_QueueFrame_t s_queue[BSP_CAN_RX_QUEUE_SLOTS];
static volatile uint8_t s_write_index;
static volatile uint8_t s_read_index;

static uint8_t Queue_NextIndex(uint8_t index)
{
  if (index < (BSP_CAN_RX_QUEUE_SLOTS - 1U)) {
    index++;
    return index;
  }
  return 0U;
}

static uint32_t Queue_Enter(void)
{
  uint32_t saved_primask = __get_PRIMASK();
  __disable_irq();
  /* CMSIS ArmCC DMB includes compiler scheduling barriers as well. Keep the
   * slot copy and index access inside the short masked region. */
  __DMB();
  return saved_primask;
}

static void Queue_Leave(uint32_t saved_primask)
{
  __DMB();
  __set_PRIMASK(saved_primask);
}

uint8_t BSP_CAN_QueuePush(BSP_CAN_QueueFrame_t frame)
{
  uint32_t saved_primask = Queue_Enter();
  uint8_t next = Queue_NextIndex(s_write_index);

  if (next == s_read_index) {
    Queue_Leave(saved_primask);
    return 0U;
  }

  s_queue[s_write_index] = frame;
  s_write_index = next;
  Queue_Leave(saved_primask);
  return 1U;
}

uint8_t BSP_CAN_QueuePop(BSP_CAN_QueueFrame_t *frame)
{
  uint32_t saved_primask;
  if (frame == 0) {
    return 0U;
  }

  saved_primask = Queue_Enter();
  if (s_write_index == s_read_index) {
    Queue_Leave(saved_primask);
    return 0U;
  }

  *frame = s_queue[s_read_index];
  s_read_index = Queue_NextIndex(s_read_index);
  Queue_Leave(saved_primask);
  return 1U;
}

void BSP_CAN_QueueClear(void)
{
  uint32_t saved_primask = Queue_Enter();
  s_read_index = 0U;
  s_write_index = 0U;
  Queue_Leave(saved_primask);
}
