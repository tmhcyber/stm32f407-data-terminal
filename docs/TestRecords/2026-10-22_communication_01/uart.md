# SHT30 UART连续观察

```text
schema: TestRecord-v1
date: 2026-10-22
timezone: Asia/Taipei (UTC+8)
commit: 9688122d073a383f3bc2858e00d9f71bcc8efaed + dirty
board: 野火 STM32F407ZG 霸天虎 V2
adapter: 板载CH340，系统COM9
indicators: 现场观察：指示灯正常亮
baudrate: 115200 bit/s，8N1
script_version_sha256: 捕获器scripts/run_communication_session.py；解析器scripts/pc_uplink_receiver.py；核验器scripts/analyze_communication_evidence.py，哈希见verification.json与uart_metadata.json
firmware_axf_sha256: 340c13838941ad25195882a43034510c63e97908474d0b9f4f3033d0ef119374
result: 3605条完整报文，VALID/OK，序号及采样时间戳连续
```

[原始字节流（gzip）](uart.raw.gz) · [原始分块与时间（gzip）](uart_chunks.jsonl.gz) · [元数据及摘要](uart_metadata.json) · [完整核验](verification.json)

二进制字节流不插入文本头，避免改变原文；本页和元数据是它的配套记录头。分块日志可逐字节重建字节流，SHA256已核对。原文1355480字节，3605条，seq5920→9524、value_ms5919015→9523015，步进分别1和1000ms。

捕获窗口17:10:51.871→18:10:55.843，覆盖正式窗口的全部请求应答。序号、MCU采样时间戳和完整报文均连续；详细测量方法见[验收报告](RESULTS.md)。
