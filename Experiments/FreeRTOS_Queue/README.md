# FreeRTOS采集与最新值邮箱

将SHT30采集和串口上报拆为两个任务，以容量1覆盖邮箱保持最新值，并通过质量、序号、数据年龄和累计失败诊断核对运行行为。

[设计与实板结果](../../docs/RTOS.md) · [构建与接线](../../docs/BUILD.md) · [验证证据](../../docs/EVIDENCE.md)

工程共享仓库根目录的BSP、Components、Drivers和Middlewares，使用独立Keil目标构建。
