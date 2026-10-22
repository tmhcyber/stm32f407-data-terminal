# 构建与演示入口

仓库提供两个Keil构建目标、接线配置、PC观察工具和主机检查入口。最新通信实测已完成RS485一小时36000轮与CAN1800次，完整结果见[验证证据](EVIDENCE.md)。编译产物由本机工具链生成。

## 环境

- Keil MDK，Arm Compiler 5.06 update 7 build 960（ARMCC 5）；目标 STM32F407ZGTx，C99。其他编译器需核对启动、链接与语言选项。
- STM32F4xx Device Pack / CMSIS 环境：RTOS验证环境使用STM32F4xx 2.15.0 pack；按本机环境安装。
- PC Python 3；串口观察需 pyserial，`python -m pip install pyserial`。相关解析器测试使用标准库 unittest，RTOS 测试使用 pytest。
- 固件逻辑主机测试使用Clang；实板另验证物理链路、采集与应用响应。

## 两个目标

1. 裸机：打开 `MDK-ARM/IndustrialTerminal.uvprojx`，选择 `IndustrialTerminal`。当前入口 `Core/Src/main.c` 的 `APP_CAN_EXTERNAL_TEST=1` 使用外部 CAN / HSE 25 MHz；0 为原静默回环入口。初始化只有一个 CAN2 所有者。
2. RTOS：打开 `Experiments/FreeRTOS_Queue/MDK-ARM/FreeRTOS_Queue.uvprojx`，选择 `FreeRTOS_Queue`。使用仓库共享BSP、SHT30、HAL/CMSIS与内核。

可在 uVision 中 Rebuild，要求检查本次日志的错误与警告。裸机输出位于根 `Output/`；RTOS 输出位于实验的 `Output/`。若首次构建提示输出目录不存在，创建目标目录后重试。工程不包含作者 `.uvoptx` / `.uvguix` 用户调试状态，DAP 的型号与 SWD 设置须在本机选择。

也可在根目录执行 RTOS 构建 helper（展示副本将原硬编码 Keil 路径改为参数）：

```powershell
.\Experiments\FreeRTOS_Queue\Tools\build.ps1 -KeilPath '本机路径\UV4\uVision.com'
```

## 已记录的接线

[硬件组成与接线实物](../evidence/hardware-setup.jpg)：STM32F407ZG开发板、SHT30、UTS-T02、UTC-T01及fireDAP。

| 链路 | 连接 / 参数 |
| --- | --- |
| SHT30 | 3V3/GND，PB8=SCL、PB9=SDA；I2C1 100 kHz，地址 0x44 |
| USART1 上行 | PA9 经 J80 到板载 CH340；115200 8N1；实际 COM 口需枚举 |
| RS485 | USART2 PA2=TX、PA3=RX；PC0 高发送、低接收；115200 8N1 |
| CAN2 | PB13=TX、PB12=RX；J13 CANH/CANL，对端共地；500 kbit/s |

开发板 J40：1-2 提供 CAN/485_3V3；RS485 3-5 / 4-6 连接 PA2/PA3；5-7 / 6-8 的 RS232 路径断开。改线和跳帽先断开所有供电。不要让信号线连接到未供电收发器而 MCU 已上电。实际板型不同须按其原理图核对，不能照搬跳帽。

CAN目标拓扑为两端各120Ω。本项目实测采用现有短线连接，USB-CAN终端状态记录见具体实验配置；换线或扩展拓扑时核对端接电阻。PB12/PB13 与板载音频功能复用，不同时启用。PA2 与以太网 MDIO 复用，不能默认 RS485 与板载以太网同时工作。

## 演示选择

裸机终端与FreeRTOS采集目标分别构建、下载。选定目标后先运行正常窗口，并在测试记录中注明对应固件。

- 裸机 USART1：`python Tools/pc_uplink_receiver.py --help` 查看串口观察参数。
- RS485：`python Tools/pc_rs485_ping_pong.py --help`；请求为 `V1|PING\r\n`，响应为 `V1|PONG\r\n`。自定义CRLF协议采用100 ms字节间超时。HSE25MHz版已完成1000轮及一小时36000轮通信，执行版本与原始日志见验证记录。
- CAN：PCAN-View 正常模式、500 kbit/s，标准数据帧 ID=123h、DLC=1、Data=01；观察 322h / 5 字节响应。缺ACK专项采用对端Listen-only和手动触发的受控恢复，方法与结果见[专项验证](EVIDENCE.md#专项诊断与恢复验证)。
- RTOS：`python Experiments/FreeRTOS_Queue/Tools/capture_uart.py --list`；选择实际端口观察 v4 正常窗。

```powershell
python Experiments/FreeRTOS_Queue/Tools/capture_uart.py --port COMx --mode sensor-diag --seconds 40 --min-samples 10 --output tmp/rtos-new --firmware Experiments/FreeRTOS_Queue/Output/FreeRTOS_Queue.axf
```

每次采集使用新的输出目录。sensor-diag模式检查正常采集窗口；故障恢复窗口按累计失败、最近状态与有效序号分别验收。地址故障注入通过当前调试映像的符号定位变量，按[RTOS实验方法](RTOS.md)执行。

## 主机侧检查

根目录执行：

```powershell
python -m unittest discover -s Tests/RS485 -p 'test_*.py' -v
.\Tools\run_c_tests.ps1
python -m pytest Experiments/FreeRTOS_Queue/Tests/test_capture_uart.py -q
```

更多目录 `Tests/Application`、`Tests/SHT30`、`Tests/PC`、`Tests/SystemDiagnostics`、`Tests/SystemTiming`、`Tests/SystemWatchdog` 可分别用 unittest discover；`Tests/CAN` 采用其现有测试。RTOS 实际任务 C 测试可创建 `tmp` 后执行：

```powershell
clang -std=c99 -Wall -Wextra -Werror -I Experiments/FreeRTOS_Queue/Tests/fakes -I Experiments/FreeRTOS_Queue/Application/Inc -I BSP/Inc -I Components/SHT30/Inc Experiments/FreeRTOS_Queue/Tests/test_tasks.c Components/SHT30/Src/sht30.c -o tmp/freertos_queue_task_tests.exe
.\tmp\freertos_queue_task_tests.exe
```

主机侧C检查以BSP/HAL、I2C、队列和UART替身隔离硬件，覆盖生产逻辑；Python覆盖协议工具、参考模型及日志解析。软件与实板证据按测试日期、版本和测量对象组织，见[验证记录](EVIDENCE.md)。

## 通信量化复现

检出`v1.0-bench`，接线、编译下载并RUN后，按实际端口运行PC工具。RS485以115200 bit/s通信，CAN适配器需安装PCAN-Basic驱动，并填写本机DLL路径。每次输出到新的目录。

```powershell
python Tools/pc_rs485_ping_pong.py --port COMx --rounds 1000 --response-timeout-ms 500 --firmware Output/IndustrialTerminal.axf --output tmp/rs485-1000-new
python Tools/pc_rs485_ping_pong.py --port COMx --duration-seconds 3600 --interval-ms 100 --response-timeout-ms 500 --firmware Output/IndustrialTerminal.axf --output tmp/rs485-hour-new
python Tools/pc_can_request_bench.py --dll "本机路径/PCANBasic.dll" --channel 0x51 --rounds 1800 --interval-ms 1000 --firmware Output/IndustrialTerminal.axf --output tmp/can-1800-new
```

双总线并行并记录采集串口，可使用组合工具（RS485和采集串口必须是不同端口）：

```powershell
python Tools/run_communication_session.py --rs485-port COMx --uart-port COMy --can-dll "本机路径/PCANBasic.dll" --can-rounds 1800 --output tmp/parallel-new
```

组合工具先保存单轮冒烟和RS485 1000轮结果，再并行启动RS485一小时与CAN 1800次，并保存UART原始数据。组合工具要求`Output/IndustrialTerminal.axf`、`.hex`和`.build_log.htm`来自本次Keil构建。完整参数用各工具的`--help`查看。
