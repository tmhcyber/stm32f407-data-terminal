# 主机侧自动化验证

```text
schema: TestRecord-v1
date: 2026-10-22
timezone: Asia/Taipei (UTC+8)
commit: 9688122d073a383f3bc2858e00d9f71bcc8efaed + dirty
board: 主机测试环境，使用可控设备边界
adapter: BSP/HAL/串口/队列测试替身
indicators: 不适用
baudrate: 由对应协议测试向量定义
script_version_sha256: 见script_versions.json
test: Python检查、生产C断言、CAN诊断场景
result: 分类检查通过
```

| 验证层 | 规模 | 检查内容与来源 |
| --- | --- | --- |
| Python正式目录 | 119项，另列15个subtest | 既有83项、通信工具18项、FreeRTOS观察工具18项 |
| 核心生产C | 477项断言 | 执行生产源码，检查数据服务、通信状态与健康门控 |
| CAN专项 | 16个诊断场景及传输/协议/队列测试组 | 故障状态、协议编码、FIFO/环形队列及恢复路径 |

[执行输出摘录](checks.log) · [Python收集清单](python_collection.txt) · [范围计数](python_inventory.json) · [脚本版本](script_versions.json)

Python 119项由基础工具与模块83项、通信工具18项和FreeRTOS观察工具18项组成。生产C 477项断言引用2026-10-22的主机执行记录，与当日实板持续通信分别验收。BSP/HAL等硬件接口使用测试替身，以确定性输入覆盖异常和边界路径；实板通信日志验证物理链路、应用应答和持续采集。
