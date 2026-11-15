# USART1 诊断上行

`APP_DIAGNOSTICS_ENABLED=1` 时，每5000 ms生成一次诊断快照，经已有 App_UplinkService 以115200 8N1发送。遥测优先，诊断使用一个latest pending与独立active缓冲，保持原遥测完成计数和健康时间的含义。Linux网关将诊断写入SQLite并提供IPC/MQTT查询。

## d=1 帧

ASCII键值、分号分隔并以分号及CRLF结束；19字段各出现一次，整帧≤511字节。十进制使用uint32规范形式；hex为 `0x` 加8位大写十六进制。

| 字段 | 含义 |
| --- | --- |
| d / up_ms | 固定1；快照时刻的启动后毫秒，可回绕 |
| reset_captured / reset_raw / reset_reasons | HAL前捕获有效标志、RCC_CSR原值、位25～31映射到位0～6的复位原因 |
| fault_status / fault_type | 0无提交记录、1有效、2版本错误、3长度错误、4校验错误、5内容错误；异常类型1～4为HardFault/MemManage/BusFault/UsageFault |
| frame_valid / frame_reject / pc | 栈帧可用性、堆栈/对齐/范围拒收位；可用时导出压栈PC |
| cfsr / hfsr | 有效保留记录的SCB状态原值 |
| monitor_valid / health | 监控配置有效性；0启动、1健康、2降级、3不健康 |
| active / observed | 当前与本次启动累计原因位：循环告警/超时、数据陈旧、源离线、采集器故障、上行停滞/错误、监控无效 |
| watchdog_initialized / watchdog / latched | 门控初始化；0等待、1到期刷新、2停喂、3锁存；锁存原因位 |

具体枚举与格式见[映射](../Application/Src/app_diagnostic.c)、[接口](../Application/Inc/app_diagnostic.h)。无有效Fault记录时摘要清零；无有效栈帧时PC清零。诊断在复位后的正常主循环发送，Fault入口保留紧急栈、现场校验和CCM记录路径。

诊断有started/completed/busy/errors/format_errors/replaced独立计数；pending可被更新快照覆盖。健康门控依据本地采集、循环与遥测状态运行。`python Tools/run_stage6_tests.py` 检查宏关闭/启用、最坏长度、回绕、遥测优先和缓冲生命周期。

新Modbus固件经CH340→VirtualBox UART2→Linux ttyS1完成5 s暖机后30 s测试：29遥测、6诊断入库；26个IPC诊断快照、7个MQTT诊断状态逐字段匹配，见[记录](../evidence/modbus/linux-compatible-result.json)。[Linux网关项目](https://github.com/tmhcyber/linux-stm32-gateway)提供接收端实现。
