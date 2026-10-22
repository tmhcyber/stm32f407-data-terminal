# 测试结果与可追溯证据

验证采用“实板通信日志 + 固件计数/波形 + 主机自动化检查”三类证据。每份记录注明日期、配置、统计对象与版本；最新通信结果附完整原文、脚本快照及SHA256核验。

## 2026-10-22通信与采集验证

| 场景 | 结果 | 证据入口 |
| --- | --- | --- |
| RS485连续轮次 | 115200 8N1，1000/1000精确PING/PONG，PC超时与错误0 | [逐轮日志](../docs/TestRecords/2026-10-22_communication_01/rs485_1000.md) |
| RS485持续通信 | 目标10 Hz，3600.000571秒，36000/36000，跳过时隙0 | [一小时完整记录](../docs/TestRecords/2026-10-22_communication_01/rs485_1hour.md) |
| CAN周期请求 | 500 kbit/s，1 Hz，1800/1800正确应用响应 | [CAN完整记录](../docs/TestRecords/2026-10-22_communication_01/can_1800.md) |
| SHT30采集上行 | 3605条VALID/OK；seq5920→9524，采样时间每次增加1000ms | [UART原文与核验](../docs/TestRecords/2026-10-22_communication_01/uart.md) |
| 主机自动化 | Python119项、生产C477项断言，另有CAN16个诊断场景及传输/协议/队列测试组 | [分类记录](../docs/TestRecords/2026-10-22_host_01/README.md) |

测试前30分钟为RS485与CAN并行负载，SHT30与UART持续运行。通信统计由完整应用响应计算，原始TX/RX、逐轮结果与汇总独立核对一致。详细配置、计时定义和结果见[验收报告](../docs/TestRecords/2026-10-22_communication_01/RESULTS.md)。

## 专项诊断与恢复验证

| 场景 | 观察与证据 | 方法/配置 |
| --- | --- | --- |
| RS485方向与响应 | [方向波形](../evidence/rs485_tc.png)、[初始化修正版Watch](../evidence/rs485_final_watch.png) | 2026-09-04；TC后释放方向，正常应答 |
| RS485边界与恢复 | [100轮Watch](../evidence/rs485_100_watch.png)及[诊断案例](CASE_RS485.md) | 分段输入、超长重同步、UART ORE恢复及A/B极性对照 |
| CAN队列满/释放 | [Watch](../evidence/can_queue_full.png)：IRQ2→6，入队2→5，drop0→1，释放后invalid0→3，后续合法请求应答 | 容量3，满时拒收计数；FIFO顺序与不覆盖另由生产C测试验证 |
| CAN周期收发 | [PCAN](../evidence/can_periodic.png)、[Watch](../evidence/can_periodic_watch.png)：收发、入队、处理及成功发送均增加88 | 前期500 kbit/s、1 Hz集成验证；后续扩展至上表1800次 |
| CAN缺ACK恢复 | 恢复正常ACK后手动recover，新请求提交/成功2/1，采集继续 | 对端Listen-only制造缺ACK；恢复时重建控制器/过滤器/中断路径 |
| RTOS正常采集 | [UART](../evidence/rtos_normal_uart.txt)：约40秒14条报告，seq5→44、rx3→16，错误字段0 | 2026-10-04，v4，真实SHT30 |
| RTOS地址NACK恢复 | [UART](../evidence/rtos_recovery_uart.txt)：4条no_new_data、8条恢复采样，fail24、last0，seq389→390 | 运行时地址0x44→0x45→0x44，命令NACK；恢复后保留历史阶段和错误码 |
| Fault与IWDG | UsageFault现场、NRST后记录保留、一次启动消费及健康输入注入后的复位 | 场景说明见[贡献与设计](CONTRIBUTIONS.md) |

## 测量定义与版本追溯

- **通信成功：** PC完整写出/提交请求，并在500ms预算内收到协议匹配的完整应用响应；CAN响应还要求质量为VALID。
- **计时：** RTT包含USB、PC调度和日志处理。每轮发送时间用于核对实际周期，板端采样时间用于核对采集推进。
- **采集连续性：** 依据UART中的质量、序号和采样时间戳。长测的错误统计对象为PC往返；板端内部诊断计数采用上述对应专项的Watch证据。
- **软件检查：** 生产C使用可控BSP/HAL边界验证状态与数据规则；Python覆盖参考模型、协议工具和结构检查。实板另验证物理链路、应用应答和采集上行。
- **版本：** 固件AXF及执行脚本各有完整SHA256，测试时Git基线为9688122+dirty。原始日志正文经独立核验，归档/发布提交可由仓库历史定位。
- **专项配置：** Fault记录验证跨NRST保留；IWDG采用健康输入注入；CAN恢复采用缺ACK与手动恢复；FreeRTOS采用地址NACK注入和独立构建目标。
