# 测试记录格式

```text
schema: TestRecord-v1
date: YYYY-MM-DD
timezone: Asia/Taipei (UTC+8)
commit: <测试时完整SHA及工作区状态>
board: <开发板型号；主机检查填主机环境>
adapter: <USB-RS485/PCAN实物型号与系统识别>
indicators: <指示灯观察及来源>
baudrate: <bit/s和帧配置>
script_version_sha256: <脚本路径及SHA256>
firmware_axf_sha256: <固件映像SHA256和下载记录>
test: <测试项目>
scale: <轮数、频率、时长或主机检查口径>
result: <实际结果及错误分类>
```

## 测试方法

记录接线、供电、配置、同时负载、开始与结束时间。各项观察注明来源和测量对象。

## 结果与日志

保存N/N、超时、错误、耗时、周期偏差与适用的诊断计数，链接原始日志和SHA256。软件检查按用例、断言、场景分类统计。

## 版本与工程贡献

记录测试基线、执行脚本、固件映像、归档提交及发布提交，并关联设计决策、实现入口与诊断案例。
