# v1.0-bench 标签重编译核对

日期：2026-10-23。标签提交：`da25ac3f43e05d368c06624ac1ed74851689cb38`。

## 方法

1. 使用`git archive v1.0-bench`导出独立目录，创建Output及Listings输出目录。
2. 从标签Git内容读取[source-map.json](../2026-10-22_communication_01/source-map.json)，按各项换行映射还原319个文件，与记录的测试时SHA256逐项比较：319/319一致。Git导出时可能按本机core.autocrlf转换换行，映射核对使用Git原始内容。
3. 使用Keil MDK、Arm Compiler 5.06 update 7 build 960、STM32F4xx DFP 2.15.0，执行`uVision.com -r IndustrialTerminal.uvprojx -t IndustrialTerminal -j0 -o rebuild.log`。结果：0错误、0警告；Code=26832、RO-data=508、RW-data=172、ZI-data=4176。
4. 先确认保留的实测AXF SHA256等于测试记录中的`340c1383…119374`，再与新构建AXF比较。两个整文件SHA256不同。
5. 解析两份ELF32小端AXF的程序头，逐一对比全部PT_LOAD段的地址、文件大小、内存大小、标志及内容；可装载段逐字节相同，段内容SHA256均为`169129b8…64fc5c`。

## 结果

标签重编译得到与实测AXF相同的可装载程序内容。整份AXF的文件哈希不同，差异位于可装载段之外；具体非装载字段的差异原因未进一步定位。

[完整比对结果](comparison.json) · [源码映射核对](source-check.json) · [版本结论与完整哈希](../../../SNAPSHOT.md)

本次为离线重编译与产物对比。RS485、CAN与采集连续性数据见[2026-10-22实板结果](../2026-10-22_communication_01/RESULTS.md)。
