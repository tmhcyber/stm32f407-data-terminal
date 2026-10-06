#include <assert.h>
#include <stdio.h>
#include "bsp_can_rx_queue.h"

static uint32_t mask;
static unsigned barriers;
static unsigned locks;
static unsigned unlocks;

uint32_t __get_PRIMASK(void) { return mask; }
void __disable_irq(void) { mask = 1U; locks++; }
void __DMB(void) { assert(mask == 1U); barriers++; }
void __set_PRIMASK(uint32_t value)
{
  assert(mask == 1U);
  assert(barriers == 2U);
  barriers = 0U;
  mask = value;
  unlocks++;
}

static BSP_CAN_QueueFrame_t make_frame(uint32_t id)
{
  BSP_CAN_QueueFrame_t frame = {0};
  unsigned i;
  frame.id = id;
  frame.is_extended = (uint8_t)(id & 1U);
  frame.is_remote = (uint8_t)((id >> 1) & 1U);
  frame.dlc = (uint8_t)(id % 9U);
  for (i = 0U; i < 8U; i++) {
    frame.data[i] = (uint8_t)(id + i);
  }
  return frame;
}

static void expect_frame(BSP_CAN_QueueFrame_t actual, uint32_t id)
{
  BSP_CAN_QueueFrame_t expected = make_frame(id);
  unsigned i;
  assert(actual.id == expected.id);
  assert(actual.is_extended == expected.is_extended);
  assert(actual.is_remote == expected.is_remote);
  assert(actual.dlc == expected.dlc);
  for (i = 0U; i < 8U; i++) { assert(actual.data[i] == expected.data[i]); }
}

int main(void)
{
  BSP_CAN_QueueFrame_t out = make_frame(99U);
  BSP_CAN_QueueFrame_t input;
  uint32_t initial_mask;
  uint32_t round;

  for (initial_mask = 0U; initial_mask <= 1U; initial_mask++) {
    mask = initial_mask;
    BSP_CAN_QueueClear();
    assert(mask == initial_mask);
    out = make_frame(99U);
    assert(BSP_CAN_QueuePop(&out) == 0U);
    expect_frame(out, 99U);
    assert(mask == initial_mask);

    /* Repeated wraparound, full rejection and FIFO order: A B C, pop A, D. */
    for (round = 0U; round < 16U; round++) {
      uint32_t base = round * 4U;
      input = make_frame(base);
      assert(BSP_CAN_QueuePush(input) == 1U);
      input.id = 999U; /* Queue owns a copy, not a pointer to the caller. */
      assert(BSP_CAN_QueuePush(make_frame(base + 1U)) == 1U);
      assert(BSP_CAN_QueuePush(make_frame(base + 2U)) == 1U);
      assert(BSP_CAN_QueuePush(make_frame(base + 3U)) == 0U);
      assert(mask == initial_mask);
      assert(BSP_CAN_QueuePop(0) == 0U); /* Null must not consume A. */
      assert(BSP_CAN_QueuePop(&out) == 1U);
      expect_frame(out, base);
      assert(BSP_CAN_QueuePush(make_frame(base + 3U)) == 1U);
      assert(BSP_CAN_QueuePop(&out) == 1U);
      expect_frame(out, base + 1U);
      assert(BSP_CAN_QueuePop(&out) == 1U);
      expect_frame(out, base + 2U);
      assert(BSP_CAN_QueuePop(&out) == 1U);
      expect_frame(out, base + 3U);
      assert(BSP_CAN_QueuePop(&out) == 0U);
      expect_frame(out, base + 3U);
      assert(mask == initial_mask);
    }

    assert(BSP_CAN_QueuePush(make_frame(10U)) == 1U);
    assert(BSP_CAN_QueuePush(make_frame(11U)) == 1U);
    assert(BSP_CAN_QueuePop(&out) == 1U);
    BSP_CAN_QueueClear(); /* Drop pending B with nonzero indices. */
    assert(BSP_CAN_QueuePop(&out) == 0U);
    assert(BSP_CAN_QueuePush(make_frame(12U)) == 1U);
    assert(BSP_CAN_QueuePop(&out) == 1U);
    expect_frame(out, 12U);
    assert(mask == initial_mask);
    assert(locks == unlocks);
  }
  puts("CAN RX queue: FIFO/wrap/full/copy/clear/PRIMASK tests passed (fake IRQ boundary).");
  return 0;
}
