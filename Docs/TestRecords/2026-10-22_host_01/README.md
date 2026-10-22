# 主机自动化检查记录

```text
schema: TestRecord-v1
date: 2026-10-22
timezone: Asia/Taipei (UTC+8)
commit: 9688122d073a383f3bc2858e00d9f71bcc8efaed + dirty
board: 不适用；本条是主机模拟/结构/行为检查
adapter: 不适用；未用USB-RS485或PCAN实物发送数据
indicators: 不适用；实板指示灯观察不用于主机检查结论
baudrate: 不适用
script_version_sha256: 见script_versions.json
test: Python正式目录检查、生产C断言、CAN场景
result: 各范围通过，分开计数
```

- [带头输出摘录](checks.log)：保存当日已执行工具输出的摘录，不是重新执行或完整终端会话。
- [Python收集清单](python_collection.txt)与[范围计数](python_inventory.json)：119项=既有83+本轮工具18+独立RTOS实验18；15个subtest不另外相加。
- 生产C为同日已运行的477项断言检查，另有16个CAN诊断场景及传输/协议/队列测试组，不能与119相加称单元测试数。范围见脚本，不证明真实HAL、中断时序或总线可靠性。
- 工具/测试实现由Codex辅助完成，不能作为用户独立编写全部测试的证据。

原始历史记录保留在工作区Docs/Evidence/Communication/2026-10-22_host_01，本目录为便于README与GitHub链接的可移植副本及输出摘录。
