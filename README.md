# STM32F407ZG 数据采集与通信终端

陶铭浩 · 2027届 · 嵌入式软件 / MCU固件方向

基于STM32F407ZG、HAL和Keil，完成温湿度采集、串口上报、RS485请求应答、经典CAN通信及Fault/IWDG诊断；同仓库另设FreeRTOS采集与最新值邮箱固件。

## 代表性案例

- **RS485收到完整PING却无回复：**结合逻辑分析仪和Watch追踪时间先后，定位主循环缓存时间与ISR更新时间相减产生无符号下溢，导致首字节误超时清除。[查看故障分析](docs/CASE_RS485.md)
- **CAN中断与业务解耦：**接收ISR入环形队列，主循环校验并响应；满队列拒收计数，缺ACK后支持手动恢复。[查看代码](BSP/Src/bsp_can_rx_queue.c)
- **系统故障诊断：**记录异常现场与复位原因，按健康条件门控IWDG刷新。[查看门控逻辑](Application/Src/app_watchdog_gate.c)
- **FreeRTOS最新值策略：**慢消费者仅需最新采样，容量1邮箱覆盖旧值；失败不占有效序号，恢复后保留历史错误。[查看任务与诊断](docs/RTOS.md)

## 数据流与两个构建目标

```text
裸机主项目
SHT30 -> I2C/CRC -> 周期采集 -> 数据快照 -> USART1 -> PC
PC PING -> USART2/RS485 -> 有界帧/超时/重同步 -> TC后释放方向 -> PONG
CAN RX IRQ -> 环形队列 -> 主循环校验 -> 温湿度请求应答
Fault/复位现场 -> 循环健康评估 -> IWDG刷新门控

FreeRTOS固件（同仓库，复用SHT30驱动）
采集任务 -> 测量/阻塞等待/读取 -> 成功覆盖最新值邮箱
上报任务 -> 接收数据副本 -> 诊断快照 -> 串口输出
```

两个Keil目标分别构建；Fault/IWDG、CAN和RS485位于裸机目标。最新值邮箱按值复制。

## 源码、运行与证据

| 内容 | 入口 |
|---|---|
| 接线、构建、PC观察 | [运行说明](docs/BUILD.md) |
| RS485状态机和时间判断 | [应用源码](Application/Src/app_rs485_service.c) |
| CAN请求校验与响应编码 | [协议源码](Application/Src/app_can_protocol.c) |
| FreeRTOS采集/上报任务 | [任务源码](Experiments/FreeRTOS_Queue/Application/Src/app_queue_experiment.c) |
| 实板日志、波形及适用版本 | [验证证据](docs/EVIDENCE.md) |
| 个人代码、设计、诊断及协作分工 | [贡献说明](docs/CONTRIBUTIONS.md) |
| 2026-10-05历史作品集 | [4页PDF](portfolio/project-portfolio.pdf) |

## 分层

Core负责初始化与中断入口，Application表达采集/通信/健康策略，Components封装器件协议，BSP适配板级资源与HAL。Drivers保留ST HAL/CMSIS，Middlewares中的FreeRTOS只用于单独固件，Tests/Tools提供主机检查与PC观察工具。

## 版本与许可

源码基线为2026-10-05展示快照，2026-10-06首次公开。2026-10-22文档修订对齐V8.1项目名称与贡献表达，没有更新固件或重跑实板；历史作品集PDF仍为2026-10-05版本，各项实验条件见证据页。

第三方代码与许可见[THIRD_PARTY.md](THIRD_PARTY.md)。自编部分尚未另行授予通用开源许可证，供求职展示与审阅。
