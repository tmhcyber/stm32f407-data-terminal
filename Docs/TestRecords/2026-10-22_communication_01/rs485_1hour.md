# RS485 持续通信

```text
# schema: TestRecord-v1
# date: 2026-10-22
# timezone: Asia/Taipei (UTC+8)
# commit: 9688122d073a383f3bc2858e00d9f71bcc8efaed
# dirty_worktree: true
# board: 野火 STM32F407ZG 霸天虎 V2
# adapter: USB-RS485转换器，系统CH343/COM7；历史型号记录UTS-T02
# indicators: 现场观察：指示灯正常亮
# baudrate: 115200 bit/s，8N1
# script_version_sha256: {"Tools/communication_bench.py": "dd18e7d25c806401bf80a8824713ac2f2c13e1dffe97782a1a4b7234ed909d88", "Tools/pc_can_request_bench.py": "9dee054becebe8751de62508cf0b1db218a401daf49b5a9a9311bc4bbdfb9d19", "Tools/pc_rs485_ping_pong.py": "9a1e95ced0c40a169742c5b54e684e9dea0e8fff00992dac92e01ff3b69d54b0", "Tools/run_communication_session.py": "9ba789a88612b9722e500a6330fb17ed251cadebe0538782cb36ac6a1c32dad4"}
# firmware_axf_sha256: 340c13838941ad25195882a43034510c63e97908474d0b9f4f3033d0ef119374
# test: RS485 持续通信
# scale: 3600秒，目标10 Hz；前30分钟叠加CAN
# result: PC链路通过：36000/36000
```

版本追溯采用测试时Git基线、工作区状态、固件AXF及执行脚本SHA256；归档版本可由Git历史定位。

固件通过Keil编译下载；本记录保存对应主机AXF文件与执行脚本哈希。

[完整带头日志（gzip）](rs485_1hour.log.gz) · [统计原文](rs485_1hour.summary.json)

原始JSONL正文SHA256：`e2af8e539892be2f7e2db8cf38e1e0fe7ff8ab7e6ac3a9c6442c174a22f41dcb`；解压后去掉END_HEADER行及其之前的头部即可得到逐字节原文。

实测时长：3600.000571秒；超时：0；错误：0；跳过时隙：0。

逐轮TX/RX与汇总计数经[独立核验](verification.json)一致；采集连续性见[UART记录](uart.md)，测试方法与测量对象见[验收报告](RESULTS.md)。
