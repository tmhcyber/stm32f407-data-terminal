# FreeRTOS最新值邮箱与诊断

独立构建目标，采用FreeRTOS Kernel V11.1.0、ARM_CM4F和heap_4，复用SHT30驱动。采集任务与上报任务通过按值复制的容量1邮箱交换数据。

## 从业务目标选择策略

显示当前温湿度时，慢消费者需要优先获得最新采样。我选择 `xQueueOverwrite` 覆盖尚未处理的旧值，避免FIFO积压使显示滞后；B已取出的局部副本保持独立。

A（优先级2）发送测量命令、`vTaskDelay` 等待转换、读取并校验CRC；成功后换算并发布，失败保留诊断信息，有效采样序号保持原值。A每轮末延时1秒。B（优先级1）接收采样、复制诊断快照并上报，上报后延时3秒；超时输出no_new_data。相对延时周期包含任务工作耗时，HAL I2C/UART采用有界轮询。

我围绕上述流程编写测量启动、单位换算、成功发布和失败记录核心逻辑，并根据日志核对数据年龄、覆盖与恢复行为。

## 共享诊断与计数

SensorDiagnostics更新/复制使用短临界区，I2C、队列、延时和UART放在临界区外。采样副本携带其自身时间，诊断快照反映最近状态，两者各自可追溯。

| 字段 | 含义 |
| --- | --- |
| seq | 成功有效采样序号 |
| rx | B成功接收次数 |
| skip | `seq+1-rx`，截至该次接收序号的有效采样缺口 |
| age | B接收tick减sample_tick，表示数据在MCU内的年龄 |
| fail / last | 累计失败尝试 / 最近一轮状态 |
| fail_stage / fail_status | 最近一次失败的阶段和原因，恢复后保留 |

## 实板验证（2026-10-04，v4）

正常窗口约40秒、14条报告，采集及UART错误字段0，见[正常UART](../evidence/rtos_normal_uart.txt)。

采用运行时地址 `0x44→0x45→0x44` 制造命令NACK并恢复。日志含4条no_new_data和8条恢复采样；最后旧值seq389、首条恢复seq390，累计fail24，恢复后last0，历史命令失败阶段与原因保留，见[恢复UART](../evidence/rtos_recovery_uart.txt)。这是软件地址注入实验，使用原接线和供电。

日志中旧有效值的age最高出现2746ms，质量、年龄和当前诊断共同解释数据新鲜度；恢复后的有效序号继续推进。该设计将“本轮恢复正常”和“历史出现过失败”同时保留下来，便于现场追查。

[任务源码](../Experiments/FreeRTOS_Queue/Application/Src/app_queue_experiment.c) · [构建与接线](BUILD.md) · [项目贡献](CONTRIBUTIONS.md)
