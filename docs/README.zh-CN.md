# ARCH 文档

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

如果你刚接触 ARCH 或计算流体力学（CFD），可以从这里开始。CFD 将流体区域划分为
许多小单元，逐步计算密度、速度和能量如何随时间变化。ARCH 的算例提供初始状态和
运行设置，程序则调用所选的物理模型和数值方法。

想要运行你的第一个计算任务，请完整跟着下方的[算例指南](guides/SimulationCase.zh-CN.md)从头做到尾。如果需要查找特定的参数设置，请随时查阅[参考手册](Reference.zh-CN.md)。当你需要了解计算精度是如何被严格验证时，请参阅 [Validation](../validation/README.zh-CN.md) 部分。

## 从这里开始

- [构建指南](guides/Build.zh-CN.md)：依赖准备、CPU/CUDA 构建选择与编译内存管理；
- [算例指南](guides/SimulationCase.zh-CN.md)：构建、配置、运行及扩展算例；
- [研究与 API 参考](Reference.zh-CN.md)：参数含义、数值方法选择和代码扩展接口；
- [CUDA 与 GPU-AMR 指南](CudaBackendStatus.zh-CN.md)：支持的功能、CPU/CUDA 的
  共同职责和后端选择；
- [算例目录](../simulation/README.md)：可复用的问题定义和示例输入；
- [物理说明](physics/README.md)：模型来源，并区分维护中的说明与历史数值分析；
- [Verification 与 Validation](../validation/README.zh-CN.md)：测了什么、如何衡量误差，以及 CPU 与 GPU 结果如何比较；
- [EOS 运行时表](../EOS_toolkit/README.zh-CN.md)：表目录、来源和完整性要求；
- [法律与来源索引](legal/README.zh-CN.md)：项目及第三方许可证入口。

## 开发与审阅入口

- [开发者索引](development/README.md)：实现职责划分、代码审阅与测试工作流，以及开发记录的归档；
- [测试](../tests/README.md)与[工具](../tools/README.md)：局部回归检查，以及共用的
  构建、网络生成和验证工具；
- [Validation 总索引](../validation/README.zh-CN.md)：统一记录综合验收结论；
  各模块摘要说明科学方法并链接实际证据。

## 目录契约

- [guides/](guides/README.md) 保存面向任务的工作流；
- `physics/` 提供物理模型的说明，并明确标注历史的数值分析过程；
- `development/` 维护开发者的决策、规范约定以及执行记录；
- `legal/` 作为检索入口；为了兼容标准工具，规范的许可证和 notice 文件始终保留在仓库根目录；
- `validation/` 位于 `docs/` 之外，因为它的记录用于支撑明确的“通过/失败”结论，并独立管理自己不可变的输入、指标和生成的图表；
- 源码与算例目录则存放实际的代码实现和可复用的示例输入，仅辅以紧邻代码的简短局部契约。

新增文档时应加入本索引，并在适当的归属模块中建立链接。不要再创建第二套通用文档或验证目录。
