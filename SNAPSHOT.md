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

`v1.0-bench`已将实测源码、执行工具和证据固定为可下载版本。原记录中的`9688122 + dirty`是采集时的基线与工作区状态；319项文件映射将当时文件对应到归档内容，固件AXF指纹对应实际下载映像。

## 标签代码与实测固件的对应结论

2026-10-23从`v1.0-bench`（提交`da25ac3f43e05d368c06624ac1ed74851689cb38`）导出独立目录，使用Arm Compiler 5.06 update 7 build 960全量构建`IndustrialTerminal`，结果为**0错误、0警告**。319项源码及脚本记录按映射还原后，SHA256全部与测试记录一致。

**重编译AXF与测试当天AXF的整文件哈希不同；两个AXF的全部可装载程序段（ELF PT_LOAD）内容、装载地址、长度及内存大小一致。** 程序段逐字节比较通过，文件差异位于可装载段之外。因此，本标签能够重建与实测AXF相同的可装载程序内容，整份AXF文件则不是逐字节相同。

| 比较对象 | SHA256 / 结果 |
| --- | --- |
| 2026-10-22实测AXF | `340c13838941ad25195882a43034510c63e97908474d0b9f4f3033d0ef119374` |
| 2026-10-23标签重编译AXF | `1129951f6c2646333fa3143558e8ddc8fa0dfc1ad30f4985d40279b96752e40e` |
| 两份AXF共同的可装载段SHA256 | `169129b8d73db1d7629b29fe712cc44fec4974c663d2bb2cffe1de32dc64fc5c` |
| 可装载段 | 地址`0x08000000`；文件内容27512字节；内存大小31688字节；共1段 |

[构建与二进制对比记录](docs/TestRecords/2026-10-23_tag_rebuild/README.md)保存方法、参数及机器可读结果。本次执行离线构建和文件对比，通信性能结果来自2026-10-22实板记录。

## 复现入口

```powershell
git clone --branch v1.0-bench https://github.com/tmhcyber/stm32f407-data-terminal.git
cd stm32f407-data-terminal
```

按[构建与接线说明](docs/BUILD.md)编译并下载裸机目标，再用`Tools/pc_rs485_ping_pong.py`、`Tools/pc_can_request_bench.py`或组合运行工具采集新日志。每次新构建的固件单独计算校验值；上述AXF指纹标识2026-10-22的实测映像。

主机侧自动化记录按Python检查、C断言和CAN场景分别统计，BSP/HAL等硬件接口以替身隔离。生产C 477项断言引用同日已执行记录，详见[主机记录](docs/TestRecords/2026-10-22_host_01/README.md)。

[构建与运行](docs/BUILD.md) · [验证证据](docs/EVIDENCE.md) · [个人贡献](docs/CONTRIBUTIONS.md)
