# 版本与实测固件

`v1.0-bench` 固定2026-10-22通信测试对应的源码、PC工具和完整验证记录，作为本项目的可下载复现入口。

| 对象 | 对应标识 |
| --- | --- |
| 源码与证据归档 | [`v1.0-bench`](https://github.com/tmhcyber/stm32f407-data-terminal/tree/v1.0-bench) |
| 实板通信 | RS485 1000/1000；10 Hz一小时36000/36000；CAN 1 Hz 1800/1800 |
| 实测固件文件校验值（AXF SHA256） | `340c13838941ad25195882a43034510c63e97908474d0b9f4f3033d0ef119374` |
| 测试时Git状态 | `9688122d073a383f3bc2858e00d9f71bcc8efaed + dirty`；原记录完整保留 |
| 源码、工具与实测版本映射 | [319个文件逐项校验](docs/TestRecords/2026-10-22_communication_01/source-map.json) |

## 如何核对版本

测试开始时，工具记录了319个源码和执行脚本文件的SHA256。317个当前工作区文件与当时记录逐字节一致；CAN协议源文件仅调整注释，C代码词元核对一致；串口接收工具仅调整说明文字。两份实测原文也保留在记录目录。映射逐项提供Git文件校验值，以及Git保存文本时LF/CRLF换行转换的还原方式，319项均能还原到与测试时哈希一致的内容。

`+dirty` 表示测试时工作区有尚未提交的内容。此次以标签固定源码与证据，并保留当时的状态字段；标签用于归档这次实测，测试时间和固件指纹仍以原始记录为准。

## 复现入口

```powershell
git clone --branch v1.0-bench https://github.com/tmhcyber/stm32f407-data-terminal.git
cd stm32f407-data-terminal
```

按[构建与接线说明](docs/BUILD.md)编译并下载裸机目标，再用`Tools/pc_rs485_ping_pong.py`、`Tools/pc_can_request_bench.py`或组合运行工具采集新日志。每次新构建的固件单独计算校验值；上述AXF指纹标识2026-10-22的实测映像。

主机侧自动化记录按Python检查、C断言和CAN场景分别统计，BSP/HAL等硬件接口以替身隔离。生产C 477项断言引用同日已执行记录，详见[主机记录](docs/TestRecords/2026-10-22_host_01/README.md)。

[构建与运行](docs/BUILD.md) · [验证证据](docs/EVIDENCE.md) · [个人贡献](docs/CONTRIBUTIONS.md)
