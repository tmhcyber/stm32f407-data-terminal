#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mb_rtu.h"
#include "app_modbus_registers.h"
#include "app_modbus_service.h"
#include "app_data_service.h"
#include "bsp_rs485.h"
#include "bsp_modbus_time.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static const uint8_t read_all[] = {1,4,0,0,0,3,0xB0,0x0B};
static const uint8_t known_reply[] = {1,4,6,0x09,0xE6,0x17,0x7C,0,1,0x6C,0x70};
static App_ModbusSample_t sample = {25340,60120,1};
static unsigned callbacks;

static MB_Exception_t Map(void *context, uint16_t start, uint16_t count,
  uint16_t *values, uint16_t capacity)
{
  ++callbacks;
  return App_ModbusRegisters_Read((const App_ModbusSample_t *)context,
                                  start, count, values, capacity);
}
static void Crc(uint8_t *bytes, uint16_t length)
{
  uint16_t crc = MB_Crc16(bytes, (uint16_t)(length - 2U));
  bytes[length-2U] = (uint8_t)crc;
  bytes[length-1U] = (uint8_t)(crc >> 8U);
}
static void Request(uint8_t *bytes, uint8_t address, uint8_t function,
  uint16_t start, uint16_t count)
{
  bytes[0]=address; bytes[1]=function;
  bytes[2]=(uint8_t)(start>>8U); bytes[3]=(uint8_t)start;
  bytes[4]=(uint8_t)(count>>8U); bytes[5]=(uint8_t)count; Crc(bytes,8);
}
static uint16_t Handle(const uint8_t *bytes, uint16_t length, uint8_t *out)
{
  return MB_RTU_Handle(bytes,length,1,Map,&sample,out,256);
}
static void ProtocolAndRegisters(void)
{
  uint8_t request[256], out[256];
  uint16_t values[3], start, count;
  uint32_t q;
  CHECK(MB_Crc16((const uint8_t *)"123456789",9)==0x4B37U);
  CHECK(MB_Crc16(read_all,6)==0x0BB0U);
  CHECK(Handle(read_all,8,out)==sizeof(known_reply));
  CHECK(memcmp(out,known_reply,sizeof(known_reply))==0);
  for (start=0;start<5;++start) for (count=0;count<6;++count)
  {
    MB_Exception_t result;
    values[0]=values[1]=values[2]=0xAA55U;
    result=App_ModbusRegisters_Read(&sample,start,count,values,3);
    if (count==0) CHECK(result==MB_EX_ILLEGAL_VALUE);
    else if (start>=3 || count>3-start) CHECK(result==MB_EX_ILLEGAL_ADDRESS);
    else CHECK(result==MB_EX_NONE);
    if (result!=MB_EX_NONE) CHECK(values[0]==0xAA55U && values[2]==0xAA55U);
  }
  CHECK(App_ModbusRegisters_Read(&sample,65535,2,values,3)==MB_EX_ILLEGAL_ADDRESS);
  CHECK(App_ModbusRegisters_Read(&sample,0,126,values,3)==MB_EX_ILLEGAL_VALUE);
  CHECK(App_ModbusRegisters_Read(0,0,1,values,3)==MB_EX_DEVICE_FAILURE);
  CHECK(App_ModbusRegisters_Read(&sample,0,1,0,3)==MB_EX_DEVICE_FAILURE);
  CHECK(App_ModbusRegisters_Read(&sample,0,3,values,2)==MB_EX_DEVICE_FAILURE);
  for (q=0;q<4;++q)
  {
    sample.quality=(uint16_t)q;
    sample.temperature_milli=-12345;
    CHECK(App_ModbusRegisters_Read(&sample,0,3,values,3)==MB_EX_NONE);
    CHECK(values[0]==(q ? (uint16_t)(int16_t)-1234 : 0));
    CHECK(values[1]==(q ? 6012 : 0)); CHECK(values[2]==q);
  }
  sample.quality=4;
  CHECK(App_ModbusRegisters_Read(&sample,0,3,values,3)==MB_EX_DEVICE_FAILURE);
  sample.quality=1;
  sample.temperature_milli=-327680;
  CHECK(App_ModbusRegisters_Read(&sample,0,1,values,3)==MB_EX_NONE && values[0]==0x8000U);
  sample.temperature_milli=327670; sample.humidity_milli=100000;
  CHECK(App_ModbusRegisters_Read(&sample,0,3,values,3)==MB_EX_NONE && values[0]==0x7FFF && values[1]==10000);
  sample.temperature_milli=327671;
  CHECK(App_ModbusRegisters_Read(&sample,0,3,values,3)==MB_EX_DEVICE_FAILURE);
  sample.temperature_milli=-327681;
  CHECK(App_ModbusRegisters_Read(&sample,0,3,values,3)==MB_EX_DEVICE_FAILURE);
  sample.temperature_milli=0; sample.humidity_milli=-1;
  CHECK(App_ModbusRegisters_Read(&sample,0,3,values,3)==MB_EX_DEVICE_FAILURE);
  sample.humidity_milli=100001;
  CHECK(App_ModbusRegisters_Read(&sample,0,3,values,3)==MB_EX_DEVICE_FAILURE);
  sample.temperature_milli=25340; sample.humidity_milli=60120;

  Request(request,1,4,2,2); CHECK(Handle(request,8,out)==5 && out[2]==2);
  Request(request,1,4,0,0); CHECK(Handle(request,8,out)==5 && out[2]==3);
  Request(request,1,4,0,126); CHECK(Handle(request,8,out)==5 && out[2]==3);
  Request(request,1,3,0,3); CHECK(Handle(request,8,out)==5 && out[1]==0x83 && out[2]==1);
  callbacks=0;
  Request(request,0,4,0,3); CHECK(Handle(request,8,out)==0);
  Request(request,2,4,0,3); CHECK(Handle(request,8,out)==0);
  memcpy(request,read_all,8); request[7]^=1; CHECK(Handle(request,8,out)==0);
  CHECK(callbacks==0);
  memcpy(request,read_all,8); request[8]=0; Crc(request,9); CHECK(Handle(request,9,out)==0);
  CHECK(Handle(read_all,3,out)==0);
  CHECK(MB_RTU_Handle(read_all,8,1,Map,&sample,out,10)==0);
  CHECK(MB_RTU_Handle(read_all,8,1,0,0,out,256)==5 && out[2]==4);
  CHECK(MB_RTU_Handle(0,8,1,Map,&sample,out,256)==0);
}

static uint32_t Feed(MB_RTU_Receiver_t *rx, uint32_t first, unsigned length, uint32_t delta)
{
  unsigned i;
  for (i=0;i<length;++i) MB_RTU_RxByte(rx,read_all[i%8],first+i*delta);
  return first+(length-1U)*delta;
}
static void Framing(void)
{
  MB_RTU_Receiver_t rx={0};
  uint32_t last;
  MB_RTU_RxReset(&rx,0);
  CHECK(MB_RTU_RxTake(&rx,1845)==0 && rx.discard);
  CHECK(MB_RTU_RxTake(&rx,1846)==0 && !rx.discard);
  last=Feed(&rx,3000,8,96);
  CHECK(MB_RTU_RxTake(&rx,last-1)==0); /* stale now must not underflow */
  CHECK(MB_RTU_RxTake(&rx,last+1845)==0);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==8);
  CHECK(memcmp(rx.bytes,read_all,8)==0);
  CHECK(MB_RTU_RxTake(&rx,last+10000)==0);
  /* At t1.5, a character may still be in flight: do not discard early. */
  MB_RTU_RxReset(&rx,0); last=Feed(&rx,3000,2,846);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==2);
  MB_RTU_RxReset(&rx,0); last=Feed(&rx,3000,2,847);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==0 && rx.gap_errors==1);
  MB_RTU_RxReset(&rx,0); last=Feed(&rx,3000,2,1845);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==0);
  MB_RTU_RxReset(&rx,0); last=Feed(&rx,3000,2,1846);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==1);
  MB_RTU_RxReset(&rx,0); last=Feed(&rx,3000,257,96);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==0 && rx.overlengths==1);
  /* Delayed servicing must not merge frames or answer an older request. */
  MB_RTU_RxReset(&rx,0); last=Feed(&rx,3000,8,96);
  last=Feed(&rx,last+2000,8,96);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==8 && rx.superseded>=1);
  MB_RTU_RxReset(&rx,0xFFFFF000U);
  last=Feed(&rx,0xFFFFFF00U,8,96);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==8); /* TIM2 wrap */
  MB_RTU_RxReset(&rx,0); CHECK(MB_RTU_RxTake(&rx,2000)==0);
  last=Feed(&rx,0x90000000U,8,96);
  CHECK(MB_RTU_RxTake(&rx,last+1846)==8); /* long idle */
}

/* Actual application service and DataService, fake only the hardware boundary. */
static BSP_RS485_RxByteEvent_t queue[512];
static unsigned head,tail,write_count,abort_count;
static uint32_t now;
static BSP_RS485_TxEvent_t tx_event;
static BSP_RS485_RxFaultEvent_t rx_fault;
static BSP_RS485_Status_t init_status,write_status;
static uint8_t transmitted[256];
static uint16_t transmitted_length;
static uint8_t busy_line;
BSP_RS485_Status_t BSP_RS485_Init(void) { return init_status; }
uint32_t BSP_ModbusTime_Micros(void) { return now; }
BSP_RS485_Status_t BSP_RS485_WriteAsync(const uint8_t *p,uint16_t n)
{ ++write_count; memcpy(transmitted,p,n); transmitted_length=n; return write_status; }
BSP_RS485_Status_t BSP_RS485_AbortTransmit(void) { ++abort_count; return BSP_RS485_STATUS_OK; }
uint8_t BSP_RS485_TakeRxByte(BSP_RS485_RxByteEvent_t *event)
{ if(head==tail)return 0; *event=queue[tail++]; return 1; }
uint8_t BSP_RS485_FlushRx(void) { unsigned n=head-tail; tail=head; return (uint8_t)n; }
BSP_RS485_TxEvent_t BSP_RS485_TakeTxEvent(void)
{ BSP_RS485_TxEvent_t e=tx_event; tx_event.type=BSP_RS485_TX_EVENT_NONE; return e; }
BSP_RS485_RxFaultEvent_t BSP_RS485_TakeRxFaultEvent(void)
{ BSP_RS485_RxFaultEvent_t e=rx_fault; memset(&rx_fault,0,sizeof(rx_fault)); return e; }
uint8_t BSP_RS485_GetQuietTime(uint32_t *time)
{ if(head!=tail || busy_line)return 0; *time=now; return 1; }

static void Publish(int32_t temperature)
{
  App_DataValueUpdate_t points[2]={
    {APP_DATA_SOURCE_SHT30,APP_DATA_POINT_TEMPERATURE,0,APP_DATA_SCALE_MILLI,APP_DATA_UNIT_DEGREE_CELSIUS},
    {APP_DATA_SOURCE_SHT30,APP_DATA_POINT_HUMIDITY,60120,APP_DATA_SCALE_MILLI,APP_DATA_UNIT_PERCENT_RH}};
  points[0].raw_value=temperature;
  CHECK(App_DataService_PublishValues(points,2,now/1000)==APP_DATA_RESULT_OK);
}
static void ResetService(void)
{
  now=0; head=tail=write_count=abort_count=0; busy_line=0;
  memset(&tx_event,0,sizeof(tx_event)); memset(&rx_fault,0,sizeof(rx_fault));
  init_status=write_status=BSP_RS485_STATUS_OK;
  CHECK(App_ModbusService_Init()); App_DataService_Init();
}
static void Enqueue(const uint8_t *request,uint32_t first)
{
  unsigned i;
  for(i=0;i<8;++i) {
    queue[head].byte=request[i]; queue[head].timestamp_us=first+i*96;
    queue[head++].timestamp_ms=(first+i*96)/1000;
  }
  now=first+7*96;
}
static void Deliver(const uint8_t *request,uint32_t first)
{
  unsigned previous=write_count;
  Enqueue(request,first); App_ModbusService_Process(); CHECK(write_count==previous);
  now+=MB_RTU_RX_END_US; App_ModbusService_Process();
}
static void Complete(uint32_t at)
{ tx_event.type=BSP_RS485_TX_EVENT_COMPLETE; tx_event.timestamp_us=at; now=at; App_ModbusService_Process(); }
static void Service(void)
{
  uint8_t bad[8]; unsigned i;
  ResetService(); Publish(25340); Deliver(read_all,3000);
  CHECK(write_count==1 && transmitted_length==11);
  CHECK(memcmp(transmitted,known_reply,11)==0);
  Complete(now+1100); CHECK(g_app_modbus_report.responses_completed==1);
  CHECK(App_DataService_UpdateSourceQuality(APP_DATA_SOURCE_SHT30,APP_DATA_QUALITY_OFFLINE,APP_DATA_STATUS_NACK,20)==APP_DATA_RESULT_OK);
  Deliver(read_all,now+3000); CHECK(write_count==2 && transmitted[8]==3 && transmitted[4]==0xE6);
  Complete(now+1100); Publish(-12345); Deliver(read_all,now+3000);
  CHECK(transmitted[3]==0xFB && transmitted[4]==0x2E && transmitted[8]==1);
  ResetService(); Deliver(read_all,3000);
  CHECK(transmitted[3]==0 && transmitted[4]==0 && transmitted[8]==0);
  ResetService(); memcpy(bad,read_all,8); bad[7]^=1;
  Deliver(bad,3000); CHECK(write_count==0 && g_app_modbus_report.silent_frames==1);
  Publish(25340); Deliver(read_all,now+3000); CHECK(write_count==1);
  ResetService(); Publish(25340); Deliver(read_all,3000);
  now+=10000; App_ModbusService_Process(); CHECK(abort_count==1 && g_app_modbus_report.tx_timeouts==1);
  Deliver(read_all,now+3000); CHECK(write_count==2);
  /* Completion consumed late; preserve the next request queued after TC. */
  ResetService(); Publish(25340); Deliver(read_all,3000);
  tx_event.type=BSP_RS485_TX_EVENT_COMPLETE; tx_event.timestamp_us=now+1100;
  Enqueue(read_all,tx_event.timestamp_us+3000); now+=20000;
  App_ModbusService_Process();
  CHECK(g_app_modbus_report.responses_completed==1 && write_count==2 && abort_count==0);
  ResetService(); Publish(25340); Enqueue(read_all,3000);
  rx_fault.overflow_count=1; App_ModbusService_Process(); now+=5000; App_ModbusService_Process();
  CHECK(write_count==0 && g_app_modbus_report.rx_faults==1);
  Deliver(read_all,now+3000); CHECK(write_count==1);
  ResetService(); Publish(25340); Enqueue(read_all,3000);
  busy_line=1; now+=3000; App_ModbusService_Process(); CHECK(write_count==0);
  busy_line=0; App_ModbusService_Process(); CHECK(write_count==1);
  ResetService(); Publish(25340); write_status=BSP_RS485_STATUS_BUSY;
  Deliver(read_all,3000); CHECK(g_app_modbus_report.tx_errors==1 && !g_app_modbus_report.transmitting);
  /* A partial new publication cannot be combined with the other old value. */
  ResetService(); Publish(25340);
  {
    App_DataValueUpdate_t partial={APP_DATA_SOURCE_SHT30,APP_DATA_POINT_TEMPERATURE,30000,APP_DATA_SCALE_MILLI,APP_DATA_UNIT_DEGREE_CELSIUS};
    CHECK(App_DataService_PublishValues(&partial,1,1)==APP_DATA_RESULT_OK);
    Deliver(read_all,3000);
    CHECK(transmitted_length==5 && transmitted[2]==4 && g_app_modbus_report.snapshot_errors==1);
  }
  ResetService(); for(i=0;i<100;++i) { queue[head].byte=0xFF; queue[head++].timestamp_us=3000+i*96; }
  now=20000; App_ModbusService_Process(); CHECK(tail==64 && write_count==0);
  App_ModbusService_Process(); CHECK(tail==100 && write_count==0);
  init_status=BSP_RS485_STATUS_ERROR;
  CHECK(!App_ModbusService_Init()); App_ModbusService_Process();
}
int main(void)
{
  ProtocolAndRegisters(); Framing(); Service();
  printf("Modbus protocol/register/framing/service: %u checks passed\n",checks);
  return 0;
}
