#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mb_rtu.h"
#include "app_modbus_registers.h"

static MB_Exception_t Read(void *context,uint16_t start,uint16_t count,
  uint16_t *registers,uint16_t capacity)
{
  return App_ModbusRegisters_Read((const App_ModbusSample_t *)context,start,count,registers,capacity);
}
int main(int argc,char **argv)
{
  uint8_t request[256],response[256];
  uint16_t i,length,result;
  App_ModbusSample_t sample={25340,60120,1};
  if (argc!=5 || strlen(argv[1])>512 || strlen(argv[1])%2) return 2;
  sample.temperature_milli=(int32_t)strtol(argv[2],0,10);
  sample.humidity_milli=(int32_t)strtol(argv[3],0,10);
  sample.quality=(uint16_t)strtoul(argv[4],0,10);
  length=(uint16_t)(strlen(argv[1])/2);
  for(i=0;i<length;++i) {
    unsigned byte;
    if(sscanf(argv[1]+2*i,"%2x",&byte)!=1) return 2;
    request[i]=(uint8_t)byte;
  }
  result=MB_RTU_Handle(request,length,1,Read,&sample,response,sizeof(response));
  for(i=0;i<result;++i) printf("%02x",response[i]);
  puts("");
  return 0;
}
