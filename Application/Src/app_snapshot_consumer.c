#include "app_snapshot_consumer.h"

static uint32_t App_SnapshotConsumer_GetComparablePointCount(
  const App_DataSnapshot_t *previous,
  const App_DataSnapshot_t *current)
{
  uint32_t point_count;

  point_count = previous->point_count;
  if (current->point_count < point_count)
  {
    point_count = current->point_count;
  }
  if (point_count > APP_DATA_POINT_COUNT)
  {
    point_count = APP_DATA_POINT_COUNT;
  }

  return point_count;
}

static App_SnapshotEventFlags_t App_SnapshotConsumer_Classify(
  const App_DataSnapshot_t *previous,
  const App_DataSnapshot_t *current)
{
  App_SnapshotEventFlags_t flags;
  uint32_t index;
  uint32_t point_count;

  flags = APP_SNAPSHOT_EVENT_SNAPSHOT_CHANGED;
  point_count = App_SnapshotConsumer_GetComparablePointCount(previous,
                                                               current);

  for (index = 0U; index < point_count; ++index)
  {
    const App_DataPoint_t *old_point;
    const App_DataPoint_t *new_point;

    old_point = &previous->points[index];
    new_point = &current->points[index];

    if (old_point->sequence != new_point->sequence)
    {
      flags |= APP_SNAPSHOT_EVENT_NEW_VALID_DATA;
    }

    if (old_point->quality != new_point->quality)
    {
      flags |= APP_SNAPSHOT_EVENT_QUALITY_CHANGED;
    }

    if ((old_point->last_status != new_point->last_status) ||
        (old_point->status_timestamp_ms !=
         new_point->status_timestamp_ms))
    {
      flags |= APP_SNAPSHOT_EVENT_STATUS_CHANGED;
    }

    if ((old_point->quality != APP_DATA_QUALITY_OFFLINE) &&
        (new_point->quality == APP_DATA_QUALITY_OFFLINE))
    {
      flags |= APP_SNAPSHOT_EVENT_SOURCE_OFFLINE;
    }

    if ((old_point->quality == APP_DATA_QUALITY_OFFLINE) &&
        (new_point->quality == APP_DATA_QUALITY_VALID) &&
        (old_point->sequence != new_point->sequence))
    {
      flags |= APP_SNAPSHOT_EVENT_SOURCE_RECOVERED;
    }
  }

  return flags;
}

static uint32_t App_SnapshotConsumer_GetSkippedCount(
  uint32_t previous_version,
  uint32_t current_version)
{
  uint32_t version_delta;
  uint32_t update_count;

  version_delta = current_version - previous_version;
  update_count = version_delta / 2U;
  if (update_count <= 1U)
  {
    return 0U;
  }

  return update_count - 1U;
}

void App_SnapshotConsumer_Init(App_SnapshotConsumer_t *consumer)
{
  if (consumer == 0)
  {
    return;
  }

  consumer->has_previous_snapshot = 0U;
  consumer->total_skipped_count = 0U;
}

App_DataResult_t App_SnapshotConsumer_Process(
  App_SnapshotConsumer_t *consumer,
  App_SnapshotEvent_t *event,
  uint8_t *event_ready)
{
  App_DataSnapshot_t current_snapshot;
  App_DataResult_t result;
  uint32_t skipped_count;

  if ((consumer == 0) || (event == 0) || (event_ready == 0))
  {
    return APP_DATA_RESULT_INVALID_ARGUMENT;
  }

  *event_ready = 0U;
  result = App_DataService_GetSnapshot(&current_snapshot);
  if (result != APP_DATA_RESULT_OK)
  {
    return result;
  }

  if (consumer->has_previous_snapshot == 0U)
  {
    event->flags = APP_SNAPSHOT_EVENT_INITIAL_SNAPSHOT;
    event->skipped_count = 0U;
    event->snapshot = current_snapshot;
    consumer->previous_snapshot = current_snapshot;
    consumer->has_previous_snapshot = 1U;
    *event_ready = 1U;
    return APP_DATA_RESULT_OK;
  }

  if (consumer->previous_snapshot.snapshot_version ==
      current_snapshot.snapshot_version)
  {
    return APP_DATA_RESULT_OK;
  }

  skipped_count = App_SnapshotConsumer_GetSkippedCount(
    consumer->previous_snapshot.snapshot_version,
    current_snapshot.snapshot_version);

  event->flags = App_SnapshotConsumer_Classify(
    &consumer->previous_snapshot,
    &current_snapshot);
  if (skipped_count != 0U)
  {
    event->flags |= APP_SNAPSHOT_EVENT_VERSION_GAP;
  }
  event->skipped_count = skipped_count;
  event->snapshot = current_snapshot;

  consumer->previous_snapshot = current_snapshot;
  consumer->total_skipped_count += skipped_count;
  *event_ready = 1U;
  return APP_DATA_RESULT_OK;
}
