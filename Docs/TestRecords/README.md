# 测试记录

此目录统一保存可公开审阅的测试记录与日志。原始采集仍位于Docs/Evidence；运行中的日志不移动、不改写，结束后在这里生成无损归档。未通过的测试也保留，不用后一次覆盖。

- [本轮实板通信记录](2026-10-22_communication_01/README.md)：RS485连续1000轮、一小时持续通信，CAN 1 Hz 1800次。
- [本轮主机自动化检查](2026-10-22_host_01/README.md)：Python检查、生产C断言和CAN场景分别计数。
- [统一记录模板](TEMPLATE.md)。

每份记录和日志头必须包含：日期/时区、测试时commit及工作区是否有未提交改动、开发板、USB-RS485/PCAN实物型号和系统识别、指示灯观察及来源、波特率/帧配置、测试脚本名称及SHA256。实板另外记录固件映像SHA256、规模、负载条件、结果与原始日志摘要哈希。未知字段写未记录，不据指示灯推断应答成功。

commit列描述测试时的版本；测试后归档提交或GitHub发布提交另记，不得反写成已测试版本。带头.log.gz解压后包含文本头和未经改写的原始JSONL正文，正文哈希按记录校验。原始二进制UART使用无损.gz及配套元数据，不向原始字节中插入文本头。

完成后生成/更新仓库根README中的六列表：日期、提交、测试项目、规模、结果、日志链接。GitHub README保留现有首页，插入同一张表及Docs/TestRecords日志，不用工作区教学README覆盖线上首页。

本轮归档命令（在原工程工作区执行，不发送测试数据；公开展示仓库仅附执行/归档脚本快照，不代表其Tools目录已更新）：

```powershell
python -X utf8 Tools/archive_communication_records.py --source Docs/Evidence/Communication/2026-10-22_board_01 --output Docs/TestRecords/2026-10-22_communication_01
```

归档后仍需核对UART、版本条件和未验证边界，再发布最终结论；该命令不提交、不推送。
