# focused 独立复核：原六组数值通过，未新增 GPU 运行

2026-09-17：针对[首版汇总器失败](../batch-launch-focused-v1/README.zh-CN.md)，
修正解析器与被冻结的 C++ producer 保持一致：仅精确默认 `2→3/pool2` 要求没有
`storage_controls` 行；非默认容量仍要求唯一正确行。继续核对实际命令、每个宏步的
存储大小、aggregate pool、三个 ODE、完整标记、原场/limiter预算及真实组分演化。

本次是对既有六个成功科学运行的只读复核，**新 GPU 运行数为0**。
原外层 exit=1、原 qualification=false 和失败归档全部保留；没有修改原记录或将失败改写为成功。
新的独立结论为六个 harness、12条存储轨迹、48对宏步数值通过。
不是新 C++ 构建、长程、Helm、sanitizer 或正式性能资格。

## 复核证据链

`reaudit_focused.py` 要求原错误精确匹配、科学子进程 exit0、原record passed，
锁定 C++ producer SHA `551d021ac6ce26378dff6823e135a499cc9bbdc76276a7612a45ef21b5cab205`。
重新核验全部原执行输入、两个factory/可执行文件、源码/网络/库、原归档清单每个成员，
以及原始raw/compact压缩包；核对资源观察完整且未触发护栏。
新 `focused-reaudit-v1/qualification.json` 单独记录 `identities_verified_after=true`。

新增回归直接读取已归档的六份实际日志，另测默认格式、多余默认行、缺失非默认行、
错误ODE、缺步、无演化、失败/部分运行等；10项本机检查通过。它们是解析器回归，不计为新GPU测试。
旧合成fixture不匹配真实producer的缺陷已明确保留在历史提交。

## 双端保全

- raw：21,595,690 bytes，SHA `62dd4e3bdb91096aef2583de9a94db4e7c05d186f516c239a9a1c140425d6631`。
- compact：124,886 bytes，SHA `386e45e8e195f944d67400e98d9c18f0ace423cb68caf8951a6dfbc0d26c4e05`。
- 服务器前缀：`/home/ubuntu/projects/ARCH-native-wave-v4-20260916/batch-launch-focused-reaudit-`。
- 本机：`C:/tmp/ARCH-perf-20260909/build/batch-launch-focused-reaudit-v1-download/`。

70个原文件／35,733,119 bytes、63个原字节投影、raw内嵌投影和两包库存全部核验。
见[独立复核收集回执](batch-launch-focused-reaudit-collection-v1.json)、
[本机回执](batch-launch-focused-reaudit-local-receipt-v1.json)。
只有上传本机新回执后，才启动不同目录的同二进制线程块布局诊断。
