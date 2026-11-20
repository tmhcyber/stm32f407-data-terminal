# FreeRTOS 终端与最新值邮箱

## 四任务数据采集终端

`IndustrialTerminal_RTOS` 以独立 Keil 目标运行完整终端业务。监控、通信、采集及上报任务的优先级依次为 4、3、2、1。

| 任务 | 职责 |
| --- | --- |
| 监控 | 检查任务完成时间与业务健康，集中刷新 IWDG |
| 通信 | 消费 Modbus 字节和 CAN 帧，保留 TIM2 成帧与 RS485 TC 方向控制 |
| 采集 | 独占 I2C，通过短临界区发布包含两个测点的一致快照 |
| 上报 | 消费快照和 USART1 事件，发送遥测及诊断 |

ISR 先登记事件，再唤醒对应任务。通信任务按 tick 限定消费量。Fault 入口按 EXC_RETURN 选择异常栈，核心寄存器从异常栈指针开始读取，重启后由诊断上行报告。

同负载实板对照记录了双总线应答、采样间隔和 PC 往返时间。三种任务停滞与两种 PSP 异常帧通过重启验证。 [任务源码](../Core/Src/app_rtos_runtime.c) · [量化结果和恢复记录](../evidence/freertos/README.md) · [构建入口](BUILD.md#freertos-四任务终端)

## 最新值邮箱

独立构建目标，采用FreeRTOS Kernel V11.1.0、ARM_CM4F和heap_4，复用SHT30驱动。采集任务与上报任务通过按值复制的容量1邮箱交换数据。

### 从业务目标选择策略

显示当前温湿度时，慢消费者需要优先获得最新采样。我选择 `xQueueOverwrite` 覆盖尚未处理的旧值，避免FIFO积压使显示滞后；B已取出的局部副本保持独立。

A（优先级2）发送测量命令、`vTaskDelay` 等待转换、读取并校验CRC；成功后换算并发布，失败保留诊断信息，有效采样序号保持原值。A每轮末延时1秒。B（优先级1）接收采样、复制诊断快照并上报，上报后延时3秒；超时输出no_new_data。相对延时周期包含任务工作耗时，HAL I2C/UART采用有界轮询。

我根据数据新鲜度需求选择覆盖邮箱，编写测量启动、单位换算、成功发布和失败记录逻辑，并用日志中的序号、数据年龄及诊断字段核对覆盖与恢复行为。

### 共享诊断与计数

SensorDiagnostics更新/复制使用短临界区，I2C、队列、延时和UART放在临界区外。采样副本携带其自身时间，诊断快照反映最近状态，两者各自可追溯。

| 字段 | 含义 |
| --- | --- |
| seq | 成功有效采样序号 |
| rx | B成功接收次数 |
| skip | `seq+1-rx`，截至该次接收序号的有效采样缺口 |
| age | B接收tick减sample_tick，表示数据在MCU内的年龄 |
| fail / last | 累计失败尝试 / 最近一轮状态 |
| fail_stage / fail_status | 最近一次失败的阶段和原因，恢复后保留 |

### 实板验证（2026-10-04，v4）

正常窗口约40秒、14条报告，采集及UART错误字段0，见[正常UART](../evidence/rtos_normal_uart.txt)。

在接线与供电保持不变的条件下，将运行时地址设为 `0x44→0x45→0x44`，注入命令NACK并恢复。日志含4条no_new_data和8条恢复采样；最后旧值seq389、首条恢复seq390，累计fail24，恢复后last0，历史命令失败阶段与原因保留，见[恢复UART](../evidence/rtos_recovery_uart.txt)。

日志中旧有效值的age最高出现2746ms，质量、年龄和当前诊断共同解释数据新鲜度；恢复后的有效序号继续推进。该设计将“本轮恢复正常”和“历史出现过失败”同时保留下来，便于现场追查。

[任务源码](../Experiments/FreeRTOS_Queue/Application/Src/app_queue_experiment.c) · [构建与接线](BUILD.md) · [项目贡献](CONTRIBUTIONS.md)
