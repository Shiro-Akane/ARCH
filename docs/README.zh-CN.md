# ARCH 文档

[English](README.md) · [项目首页](../README.zh-CN.md)

建议从构建指南和小型 Sod 算例开始。ARCH 的输入、输出和物理常数统一使用 CGS 单位。推荐 Linux；Windows 11 提供相同的 CPU／CUDA 与 Studio 工作流程，已测试配置及限制见[平台支持](releases/PlatformSupport.zh-CN.md)。

## 开始使用

| 任务 | 指南 |
| --- | --- |
| 在 Linux 或 Windows 安装依赖并编译 | [构建](guides/Build.zh-CN.md) |
| 运行 Sod、查看输出和编写算例 | [模拟算例](guides/SimulationCase.zh-CN.md) |
| 编辑参数、预览初态和图形化查看输出 | [ARCH Studio](guides/Studio.zh-CN.md) |
| 选择物理模型和数值方法 | [功能清单](Features.zh-CN.md) |
| 查询参数、边界条件和公共接口 | [参考手册](Reference.zh-CN.md) |

[算例目录](../simulation/README.md)提供可运行示例；[CUDA 指南](CudaBackendStatus.zh-CN.md)介绍后端选择；[边界指南](guides/UserBoundaries.zh-CN.md)说明自定义流体与势边界。

## 结果与复现

- [验证](../validation/README.zh-CN.md)：参考问题、误差预算、收敛和后端对比。
- [物理说明](physics/README.md)与[EOS 表](../EOS_toolkit/README.zh-CN.md)：模型和数据来源。
- [版本说明](releases/README.md)：功能变化与兼容性。
- [引用](zenodo/README.md)与[许可](legal/README.zh-CN.md)：软件、代码和数据的署名要求。

## 开发与反馈

修改实现时，可查阅[开发者指南](development/README.md)、[测试](../tests/README.zh-CN.md)和[工具](../tools/README.md)。[反馈指南](guides/Reporting.zh-CN.md)介绍如何整理可复现的构建、运行或数值问题。
