# Modbus RTU 温湿度从站

PC 主站经 RS485 读取 STM32 已有的 SHT30 快照，将采集、数据质量和标准工业串行协议连接成完整业务流程。[实板结果与波形](../evidence/modbus/README.md)提供原始数据。

## 配置与寄存器

| 项目 | 配置 |
| --- | --- |
| 从站地址 | 1；单节点实验 |
| 串口 | USART2，115200 bit/s，8E1 |
| 引脚 | PA2 TX、PA3 RX、PC0 方向；高发送、低接收 |
| 功能码 | 0x04 读输入寄存器 |
| 定时 | TIM2 1 MHz；t1.5=750 μs，t3.5=1750 μs |
| 数据字节序 | 寄存器高字节先发；CRC低字节先发 |

| 协议地址 | 界面常用编号 | 内容 | 编码 |
| --- | --- | --- | --- |
| 0 | 30001 | 温度 | int16补码，0.01 ℃ |
| 1 | 30002 | 湿度 | uint16，0.01 %RH |
| 2 | 30003 | 质量 | 0未取得样本，1有效，2陈旧，3离线 |

每次请求从 DataService 取得同一份稳定快照。milli单位除以10转成百分之一单位，按C99向零截断，与CAN使用相同定点语义；陈旧/离线保留最近一次样本并更新质量。首次采样前返回三个零，客户端依据质量0显示不可用。

```text
请求：01 04 00 00 00 03 B0 0B
      地址 功能 起始0 数量3  CRC

25.340 ℃、60.120 %RH、有效时：
应答：01 04 06 09 E6 17 7C 00 01 6C 70
      地址 功能 长度 温度  湿度  质量  CRC
```

30001为主站界面编号，请求地址字段使用0。CRC16初值0xFFFF、反射多项式0xA001。

## 分层与接收时序

| 层 | 职责与源码 |
| --- | --- |
| Middlewares | [RTU核心](../Middlewares/Modbus/Src/mb_rtu.c)：CRC、地址/功能分派、异常与帧状态机 |
| Application | [寄存器映射](../Application/Src/app_modbus_registers.c)：范围、编码、质量；[服务](../Application/Src/app_modbus_service.c)：DataService快照与收发调度 |
| BSP | [RS485](../BSP/Src/bsp_rs485.c)：UART中断、字节队列、方向/TC；[TIM2](../BSP/Src/bsp_modbus_time.c)：微秒时间戳 |

RX ISR采集字节结束时间，主循环消费队列并识别帧。静默达到1750 μs后完成帧；帧内间隔超过750 μs进入丢弃/重同步。高于19200 bit/s采用官方串行规范建议的固定时间；字节完成时间扣除一个8E1字符时间后计算线间静默。

仅在接收队列/错误邮箱为空且无硬件待收事件时，用当前时间结束帧。积压期间新请求替代旧未应答帧并计数；主站按请求—应答或超时顺序访问。TX缓冲保持至TC，TC回调将PC0恢复接收。发送截止预算10 ms，主循环依据真实TC事件时间判断完成。

数量0或大于125返回异常0x03；访问超出0～2返回0x02；正确寻址的不支持功能返回0x01。CRC错误、其他地址、广播读取静默丢弃。接口/快照无效返回0x04。合法子区间支持只读湿度与质量等组合。

## 构建与主站读取

按[接线与构建](BUILD.md)设置J40跳帽、A/B和共地；改线前断电。演示入口固定选择 Modbus＋诊断宏：

```powershell
$env:KEIL_UVISION = 'F:/Keil_v5/UV4/uVision.com'
.\Tools\build_modbus_demo.ps1
python -m pip install "pymodbus==3.11.4" "pyserial==3.5"
python Tools/pc_modbus_read.py --port COMx --rounds 600 --interval-ms 100 --output tmp/modbus-new
```

构建输出在新的 `tmp/modbus-build-*/1/`；下载对应AXF/HEX并RUN。通用主站设置从站1、115200/8E1、功能04、起址0、数量3。工具每100 ms顺序请求，超时500 ms、重试0；保存收发字节、寄存器、质量、往返时间与统计。每次采集使用新的输出目录。

默认宏 `APP_RS485_MODBUS_ENABLED=0` 为旧PING/PONG（115200 8N1）；置1启用Modbus。`APP_DIAGNOSTICS_ENABLED=1` 在USART1启用[诊断上行](DIAGNOSTICS.md)，保持遥测优先。

## 验证与协议依据

主机检查运行实际生产C逻辑，覆盖负温度、寄存器边界、数量、CRC、异常和750/1750 μs边界；PyModbus独立编解码核对68个场景。实板包含600次正常读取、11个请求场景、320次地址NACK恢复及10组方向波形，详见[证据](../evidence/modbus/README.md)。

- [Modbus Application Protocol V1.1b3](https://www.modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf)：4.2、6.4、7节。
- [Modbus Serial Line V1.02](https://www.modbus.org/docs/Modbus_over_serial_line_V1_02.pdf)：2.2、2.5节。
- STM32F407 RM0090定时器与时钟树；F4 HAL字长包含校验位，8E1使用 `UART_WORDLENGTH_9B + UART_PARITY_EVEN`。
