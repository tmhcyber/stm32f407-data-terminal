#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "app_can_protocol.h"
#include "app_can_service.h"
/* Production data service included to inject snapshot contention/corruption. */
#include "../../Application/Src/app_data_service.c"

static uint8_t request = 1U;
static uint8_t out[8];
static void publish(int32_t t, int32_t h)
{
  App_DataValueUpdate_t values[2] = {
    {APP_DATA_SOURCE_SHT30, APP_DATA_POINT_TEMPERATURE, 0, -3, APP_DATA_UNIT_DEGREE_CELSIUS},
    {APP_DATA_SOURCE_SHT30, APP_DATA_POINT_HUMIDITY, 0, -3, APP_DATA_UNIT_PERCENT_RH}
  };
  values[0].raw_value = t; values[1].raw_value = h;
  assert(App_DataService_PublishValues(values, 2U, 100U) == APP_DATA_RESULT_OK);
}
static uint8_t handle(void)
{ return App_CAN_HandleRequest(0x123U, 0U, 0U, 1U, &request, out); }
int main(void)
{
  uint32_t i;
  const uint8_t valid[] = {1U, 0xFDU, 0xF3U, 0x17U, 0xA2U};
  assert(CAN_RequestIsValid(0x123U, 0U, 0U, 1U, &request));
  assert(!CAN_RequestIsValid(0x124U, 0U, 0U, 1U, &request));
  assert(!CAN_RequestIsValid(0x123U, 1U, 0U, 1U, &request));
  assert(!CAN_RequestIsValid(0x123U, 0U, 1U, 1U, &request));
  assert(!CAN_RequestIsValid(0x123U, 0U, 0U, 0U, 0));
  assert(!CAN_RequestIsValid(0x123U, 0U, 0U, 1U, 0));
  for (i = 2U; i <= 8U; ++i)
    assert(!CAN_RequestIsValid(0x123U, 0U, 0U, (uint8_t)i, &request));
  request = 2U; assert(!CAN_RequestIsValid(0x123U, 0U, 0U, 1U, &request)); request = 1U;
  assert(!CAN_EncodeResponse(1U, 0, 0U, 0, 8U));
  memset(out, 0xAA, sizeof(out));
  for (i = 0U; i < 5U; ++i) {
    assert(!CAN_EncodeResponse(1U, 0, 0U, out, i));
    assert(out[0] == 0xAAU && out[4] == 0xAAU);
  }
  assert(!CAN_EncodeResponse(4U, 0, 0U, out, 8U));
  assert(!CAN_EncodeResponse(1U, 0, 10001U, out, 8U));
  assert(CAN_EncodeResponse(1U, -525, 6050U, out, 5U));
  assert(memcmp(out, valid, 5U) == 0 && out[5] == 0xAAU);
  assert(CAN_EncodeResponse(0U, -525, 6050U, out, 8U));
  for (i = 0U; i < 5U; ++i) assert(out[i] == 0U);
  assert(CAN_EncodeResponse(1U, INT16_MIN, 10000U, out, 8U));
  assert(out[1] == 0x80U && out[2] == 0U && out[3] == 0x27U && out[4] == 0x10U);
  assert(CAN_EncodeResponse(1U, INT16_MAX, 0U, out, 8U));
  assert(out[1] == 0x7FU && out[2] == 0xFFU);
  assert(CAN_EncodeResponse(1U, -1, 0U, out, 8U));
  assert(out[1] == 0xFFU && out[2] == 0xFFU);
  assert(handle() == 0U); /* data service not initialized */
  App_DataService_Init(); assert(handle() == 5U);
  for (i = 0U; i < 5U; ++i) assert(out[i] == 0U);
  assert(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,
    APP_DATA_QUALITY_OFFLINE, APP_DATA_STATUS_NACK, 1U) == APP_DATA_RESULT_OK);
  assert(handle() == 5U && out[0] == 0U); /* no historical measurement */
  publish(-5259, 60509); assert(handle() == 5U && memcmp(out, valid, 5U) == 0);
  for (i = 2U; i <= 3U; ++i) {
    assert(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,
      (App_DataQuality_t)i, APP_DATA_STATUS_NACK, 200U) == APP_DATA_RESULT_OK);
    assert(handle() == 5U && out[0] == i && memcmp(out + 1, valid + 1, 4U) == 0);
  }
  publish(25300, 60500); assert(handle() == 5U && out[1] == 9U && out[2] == 0xE2U);
  ++s_snapshot_version; assert(handle() == 0U); ++s_snapshot_version;
  s_points[0].sequence++; assert(handle() == 0U); s_points[0].sequence--;
  s_points[0].scale = -2; assert(handle() == 0U); s_points[0].scale = -3;
  publish(327671, 0); assert(handle() == 0U);
  publish(-327681, 0); assert(handle() == 0U);
  publish(0, -1); assert(handle() == 0U);
  publish(0, 100001); assert(handle() == 0U);
  publish(-9, 9); assert(handle() == 5U && out[1] == 0U && out[2] == 0U && out[4] == 0U);
  puts("PASS: CAN user protocol and production snapshot mapping: bounds, signed encoding, invalid requests, no-data/stale/offline, truncation, inconsistent/busy snapshot.");
  return 0;
}
