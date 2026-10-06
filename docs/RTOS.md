# FreeRTOS 最新值邮箱与诊断实验

独立构建目标；FreeRTOS Kernel V11.1.0、ARM_CM4F / heap_4。未把裸机系统整体迁移。

## 业务取舍

用户希望显示最新温湿度，选择容量 1 的邮箱覆盖尚未处理的旧值；能预测慢消费者的 FIFO 会积压。覆盖不改变 B 已经取出的按值副本，也不保证完整历史采样。

A（优先级2）发送 SHT30 命令、vTaskDelay 等待转换、读取及 CRC；成功才换算并 xQueueOverwrite，失败不发布旧 measurement、不占有效 seq。A 每轮末延时 1 秒。B（优先级1）接收采样、复制诊断、上报，超时输出 no_new_data；上报后延时 3 秒。相对延时使周期包含工作耗时。HAL I2C / UART 仍为有界轮询，不是 RTOS 异步外设驱动。

共享 SensorDiagnostics 在 A 更新及 B 复制时使用短临界区；临界区外完成 I2C、队列、延时和 UART。诊断与采样是不同观察，不保证同一轮，因此旧值与最近失败可以出现在同一条报告中。

## 计数含义

- `seq`：成功有效采样序号，失败不递增。
- `rx`：B 成功接收次数；`skip = seq + 1 - rx` 为截至本次接收序号的有效采样缺口，不是即时覆盖总数。
- `age`：B 接收 tick 减成功 sample_tick，不包含 USB / PC 显示延迟。
- `fail`：累计失败尝试；`last`：最近一轮状态；`fail_stage/fail_status`：最近一次历史失败阶段与原因。恢复后 last=0，不清除 fail 或历史原因。

## 实板结果（2026-10-04 / v4）

正常窗约 40 秒，14 条报告，错误字段均 0；[原始 UART](../evidence/rtos_normal_uart.txt)。

地址注入 0x44→0x45→0x44；[原始 UART](../evidence/rtos_recovery_uart.txt) 共 17 行：13 条温湿度、4 条 no_new_data，其中8条恢复采样。最后旧值 seq389、首条恢复 seq390；累计 fail24、last0，历史命令 NACK 保留。故障开始后曾收到旧值 age2746ms，需要看年龄和状态。

这是 Codex 执行的事先告知地址注入；未拔线或改变地址脚，不属于物理断线或用户独立未知故障定位。实际读取 CRC 故障仍只有主机注入证据。

## 贡献

用户选择业务策略，在指导下写出核心测量流程、换算、覆盖和失败记录；Codex 完成适配、并发集成、构建下载和证据取证。任务职责、计数和临界区仍有复盘纠正记录；完整独立复现、独立移植、ISR/互斥量专项与长稳未验证。

关键实现：[A/B任务](../Experiments/FreeRTOS_Queue/Application/Src/app_queue_experiment.c)，[构建说明](BUILD.md)。
