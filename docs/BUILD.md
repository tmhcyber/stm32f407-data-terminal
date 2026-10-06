# 构建与演示入口

这是可阅读和构建的源码快照，不包含 AXF/HEX、商业 Keil 安装包或开发板商家资料。当前源码的构建路径引用已检查；本次作品集整理未重跑固件构建或实板实验。

## 环境

- Keil MDK，Arm Compiler 5.06 update 7 build 960（ARMCC 5）；目标 STM32F407ZGTx，C99。采用其他编译器不保证无需适配。
- STM32F4xx Device Pack / CMSIS 环境：RTOS 历史下载使用 STM32F4xx 2.15.0 pack；按本机环境安装。
- PC Python 3；串口观察需 pyserial，`python -m pip install pyserial`。相关解析器测试使用标准库 unittest，RTOS 测试使用 pytest。
- 固件逻辑主机测试用 Clang，不代替真实 HAL / 内核 / 电气验证。

## 两个目标

1. 裸机：打开 `MDK-ARM/IndustrialTerminal.uvprojx`，选择 `IndustrialTerminal`。当前入口 `Core/Src/main.c` 的 `APP_CAN_EXTERNAL_TEST=1` 使用外部 CAN / HSE 25 MHz；0 为原静默回环入口。初始化只有一个 CAN2 所有者。
2. RTOS：打开 `Experiments/FreeRTOS_Queue/MDK-ARM/FreeRTOS_Queue.uvprojx`，选择 `FreeRTOS_Queue`。依赖本包共享 BSP、SHT30、HAL/CMSIS 与内核，不能单独复制实验目录。

可在 uVision 中 Rebuild，要求检查本次日志的错误与警告。裸机输出位于根 `Output/`；RTOS 输出位于实验的 `Output/`。若首次构建提示输出目录不存在，创建目标目录后重试。工程不包含作者 `.uvoptx` / `.uvguix` 用户调试状态，DAP 的型号与 SWD 设置须在本机选择。

也可在根目录执行 RTOS 构建 helper（展示副本将原硬编码 Keil 路径改为参数）：

```powershell
.\Experiments\FreeRTOS_Queue\Tools\build.ps1 -KeilPath '本机路径\UV4\uVision.com'
```

## 已记录的接线

| 链路 | 连接 / 参数 |
| --- | --- |
| SHT30 | 3V3/GND，PB8=SCL、PB9=SDA；I2C1 100 kHz，地址 0x44 |
| USART1 上行 | PA9 经 J80 到板载 CH340；115200 8N1；实际 COM 口需枚举 |
| RS485 | USART2 PA2=TX、PA3=RX；PC0 高发送、低接收；115200 8N1 |
| CAN2 | PB13=TX、PB12=RX；J13 CANH/CANL，对端共地；500 kbit/s |

开发板 J40：1-2 提供 CAN/485_3V3；RS485 3-5 / 4-6 连接 PA2/PA3；5-7 / 6-8 的 RS232 路径断开。改线和跳帽先断开所有供电。不要让信号线连接到未供电收发器而 MCU 已上电。实际板型不同须按其原理图核对，不能照搬跳帽。

CAN 两端各 120Ω 是目标拓扑；本项目 USB-CAN 实物终端测量曾不确定，历史短线通信已通过，不宣称已验证长线或规范终端性能。PB12/PB13 与板载音频功能复用，不同时启用。PA2 与以太网 MDIO 复用，不能默认 RS485 与板载以太网同时工作。

## 演示选择

先运行所选目标的正常窗口，两个固件需要分别下载，不是同时运行。下载会替换板内固件，保留另一个磁盘工程。

- 裸机 USART1：`python Tools/pc_uplink_receiver.py --help` 查看串口观察参数。
- RS485：`python Tools/pc_rs485_ping_pong.py --help`；请求为 `V1|PING\r\n`，响应为 `V1|PONG\r\n`。100 ms 字节间超时属于自定义实验配置，不是 Modbus 间隔。当前 HSE 版本专项回归待补。
- CAN：PCAN-View 正常模式、500 kbit/s，标准数据帧 ID=123h、DLC=1、Data=01；观察 322h / 5 字节响应。Listen-only 缺 ACK 及手动恢复为历史专项，本快速入口不要求重复注入故障。
- RTOS：`python Experiments/FreeRTOS_Queue/Tools/capture_uart.py --list`；选择实际端口观察 v4 正常窗。

```powershell
python Experiments/FreeRTOS_Queue/Tools/capture_uart.py --port COMx --mode sensor-diag --seconds 40 --min-samples 10 --output tmp/rtos-new --firmware Experiments/FreeRTOS_Queue/Output/FreeRTOS_Queue.axf
```

输出目录须不存在。sensor-diag 只用于无故障正常窗口，不能套用到故障恢复后保留非零历史的窗口。原始地址故障注入使用当时映像的 RAM 布局，本包不提供可照搬地址的注入脚本。

## 主机检查

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

主机 C 使用假 BSP/I2C/队列/UART；Python 含参考模型和合成日志。它们不能证明真实总线或调度器行为。本包制作没有重新执行上述历史验证，阅读 [证据说明](EVIDENCE.md) 时按各自版本解释。
