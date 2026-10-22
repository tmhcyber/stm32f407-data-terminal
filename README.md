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

<!-- TEST_RESULTS_BEGIN -->
## 测试结果

| 日期 | 提交（测试基线） | 测试项目 | 规模 | 结果 | 日志链接 |
| --- | --- | --- | --- | --- | --- |
| 2026-10-22 | `9688122+dirty` | RS485 连续轮次 | 1000轮，停止等待 | PC链路通过：1000/1000 | [记录与日志](Docs/TestRecords/2026-10-22_communication_01/rs485_1000.md) |
| 2026-10-22 | `9688122+dirty` | RS485 持续通信 | 3600秒，目标10 Hz；前30分钟叠加CAN | PC链路通过：36000/36000 | [记录与日志](Docs/TestRecords/2026-10-22_communication_01/rs485_1hour.md) |
| 2026-10-22 | `9688122+dirty` | CAN 周期请求 | 1800次，1 Hz；叠加RS485 | PC链路通过：1800/1800 | [记录与日志](Docs/TestRecords/2026-10-22_communication_01/can_1800.md) |
| 2026-10-22 | `9688122+dirty` | 主机自动化检查 | Python 119项；生产C 477项断言；CAN 16场景及测试组 | 各自通过，分开计数；C复用同日结果 | [检查记录](Docs/TestRecords/2026-10-22_host_01/README.md) |

测试提交含未提交修改（+dirty），由记录中的固件及脚本SHA256进一步绑定。长测前30分钟两条总线并行；主机检查、C断言和实板轮次分别计数。
<!-- TEST_RESULTS_END -->

## 分层

Core负责初始化与中断入口，Application表达采集/通信/健康策略，Components封装器件协议，BSP适配板级资源与HAL。Drivers保留ST HAL/CMSIS，Middlewares中的FreeRTOS只用于单独固件，Tests/Tools提供主机检查与PC观察工具。

## 版本与许可

源码基线为2026-10-05展示快照，2026-10-06首次公开。2026-10-22先完成V8.1文档修订，随后补充上表实板量化证据与日志；公开固件源码仍是展示快照，本次实测绑定记录中的9688122+dirty工作区及固件/脚本哈希；历史作品集PDF仍为2026-10-05版本，各项实验条件见证据页。

第三方代码与许可见[THIRD_PARTY.md](THIRD_PARTY.md)。自编部分尚未另行授予通用开源许可证，供求职展示与审阅。
