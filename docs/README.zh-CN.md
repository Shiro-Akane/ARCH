# ARCH 文档

[English](README.md) · [项目首页](../README.zh-CN.md)

从[构建与首次运行](../README.zh-CN.md#构建)开始，再用[算例指南](guides/SimulationCase.zh-CN.md)查看输出或编写自己的问题。输入、输出和物理常数使用 CGS。

[ARCH Studio 本地桌面指南](guides/Studio.zh-CN.md)介绍 CLI 呼出、配置编辑、初始场/AMR 和只读 Plotfile 工作流。

## 选择功能和配置

- [功能清单](Features.zh-CN.md)：各模块、坐标、边界及自引力的可用范围。
- [参数与 API 参考](Reference.zh-CN.md)：参数含义、方法组合、算例接口和重启要求。
- [CUDA 指南](CudaBackendStatus.zh-CN.md)：后端选择与 AMR 执行方式。
- [构建指南](guides/Build.zh-CN.md)：依赖、预设、EOS 表与编译资源。
- [算例目录](../simulation/README.md)：可运行的物理问题与示例输入。

## 理解结果

- [验证索引](../validation/README.zh-CN.md)：各模块的误差、守恒、耦合和后端检查。
- [物理说明](physics/README.md)与[EOS 表](../EOS_toolkit/README.zh-CN.md)：模型和数据来源。
- [版本说明](releases/README.md)：已标识源码版本的变化。
- [法律与来源](legal/README.zh-CN.md)：代码、模型和数据的许可。

## 开发与交流

[开发者指南](development/README.md)说明代码归属、进行中的自引力／GUI 工作和历史记录。[测试指南](../tests/README.zh-CN.md)与[工具索引](../tools/README.md)供修改源码时使用。构建、运行或数值问题可按[反馈指南](guides/Reporting.zh-CN.md)整理可复现材料。
