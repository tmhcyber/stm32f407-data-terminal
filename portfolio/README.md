# 项目作品集

[六页 PDF](project-portfolio.pdf) · [首页预览](preview.png)

六页作品集（2026-11-21 由离线 HTML 单一编辑源重构）按三大项目主线系统呈现工程设计与实测成果：

1. **01 / 全景总览与跨端系统架构**：三大项目技术蓝图、硬件实物与引脚配置矩阵、核心量化成果速览。
2. **02 / 项目一 · 上：多总线通信协议栈与采集状态机**：SHT30 非阻塞采集状态机、CAN 中断环形队列与 5 字节大端编码（PCAN-View 88 次实测与队列/ACK 恢复）、Modbus RTU `0x04` 从站与范围校验。
3. **03 / 项目一 · 中：Modbus 8 MS/s 方向波形与 1 小时并行长测**：Saleae `8 MS/s` 三通道释放波形（末停止位后 `6.02～6.14 μs` 释放）、`600/600` 与 `320/320` NACK 恢复、`v1.0-bench` 1 小时 36000/36000 甘特图及固件/源码版本追溯。
4. **04 / 项目一 · 下：代表性独立故障诊断与 Fault / IWDG 门控**：RS485 时间交错无符号下溢四步定位、正向模差守卫与接收异常恢复、TC 方向切换波形，以及 Fault 跨复位单次消费与 IWDG 三态门控。
5. **05 / 项目二：FreeRTOS 协作与四任务整机迁移**：容量 1 `xQueueOverwrite` 最新值邮箱与 `v4` 24 次 NACK 恢复日志（`seq 389→390`）、四任务终端同板 `61.5 s` 对照实测、三类任务停滞 `2.00–2.13 s` IWDG 重启与基本/FPU PSP 异常帧保留。
6. **06 / 项目三：Linux + STM32 诊断网关与个人贡献总览**：跨端 C11 网关架构、连续 8 小时（`28,800.134802 s`、`0` 重启、`28,794` 遥测 + `5,759` 诊断全入库获 PUBACK）与普通/ASan 90 项、TSan 48 项复验、代表性设计判断及个人贡献边界。

`project-portfolio.html` 内嵌同一份 PDF，便于浏览和下载。

配套资料：[项目首页](../README.md) · [构建与接线](../docs/BUILD.md) · [个人贡献](../docs/CONTRIBUTIONS.md) · [验证证据](../docs/EVIDENCE.md) · [诊断案例](../docs/CASE_RS485.md) · [FreeRTOS 实板记录](../evidence/freertos/README.md) · [Modbus 记录](../evidence/modbus/README.md)。
