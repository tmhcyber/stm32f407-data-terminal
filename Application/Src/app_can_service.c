#include "app_can_service.h"
#include "app_can_protocol.h"
#include "app_data_service.h"

volatile App_CAN_Report_t g_app_can_report;

uint8_t App_CAN_HandleRequest(uint32_t id, uint8_t extended,
    uint8_t remote, uint8_t dlc, const uint8_t *data, uint8_t *response)
{
  App_DataSnapshot_t snapshot;
  const App_DataPoint_t *temperature = 0, *humidity = 0;
  uint32_t i;
  uint8_t quality = 0U;
  int32_t t = 0, h = 0;
  if (!CAN_RequestIsValid(id, extended, remote, dlc, data)) {
    ++g_app_can_report.invalid_requests; return 0U;
  }
  ++g_app_can_report.valid_requests;
  if ((response == 0) ||
      (App_DataService_GetSnapshot(&snapshot) != APP_DATA_RESULT_OK) ||
      (snapshot.point_count != APP_DATA_POINT_COUNT)) goto snapshot_error;
  for (i = 0U; i < snapshot.point_count; ++i) {
    const App_DataPoint_t *p = &snapshot.points[i];
    if (p->source_id != APP_DATA_SOURCE_SHT30) continue;
    if (p->point_id == APP_DATA_POINT_TEMPERATURE) temperature = p;
    if (p->point_id == APP_DATA_POINT_HUMIDITY) humidity = p;
  }
  if ((temperature == 0) || (humidity == 0) ||
      (temperature->scale != APP_DATA_SCALE_MILLI) ||
      (humidity->scale != APP_DATA_SCALE_MILLI) ||
      (temperature->unit != APP_DATA_UNIT_DEGREE_CELSIUS) ||
      (humidity->unit != APP_DATA_UNIT_PERCENT_RH)) goto snapshot_error;

  /* sequence 0 means no successful sample, even after an offline update. */
  if ((temperature->sequence != 0U) && (humidity->sequence != 0U)) {
    if ((temperature->sequence != humidity->sequence) ||
        (temperature->quality != humidity->quality)) goto snapshot_error;
    switch (temperature->quality) {
      case APP_DATA_QUALITY_NOT_AVAILABLE: quality = 0U; break;
      case APP_DATA_QUALITY_VALID: quality = 1U; break;
      case APP_DATA_QUALITY_STALE: quality = 2U; break;
      case APP_DATA_QUALITY_OFFLINE: quality = 3U; break;
      default: goto snapshot_error;
    }
    if (quality != 0U) {
      /* Validate before narrowing. C99 division truncates toward zero. */
      if ((temperature->raw_value < -327680) ||
          (temperature->raw_value > 327670) ||
          (humidity->raw_value < 0) || (humidity->raw_value > 100000))
        goto snapshot_error;
      t = temperature->raw_value / 10;
      h = humidity->raw_value / 10;
    }
  }
  if (!CAN_EncodeResponse(quality, (int16_t)t, (uint16_t)h, response, 8U))
    goto snapshot_error;
  g_app_can_report.last_quality = quality;
  g_app_can_report.last_temperature = (int16_t)t;
  g_app_can_report.last_humidity = (uint16_t)h;
  ++g_app_can_report.encoded_count;
  return 5U;

snapshot_error:
  ++g_app_can_report.snapshot_errors;
  return 0U; /* Never label an inconsistent/failed snapshot as valid. */
}
