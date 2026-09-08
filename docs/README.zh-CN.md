# ARCH 文档

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本目录是维护中文档的统一入口。

## 从这里开始

- [算例指南](guides/SimulationCase.zh-CN.md)：构建、配置、运行及扩展算例；
- [研究与 API 参考](Reference.zh-CN.md)：参数、策略和扩展契约；
- [CUDA 与 GPU-AMR 指南](CudaBackendStatus.zh-CN.md)：支持的功能、CPU/CUDA 的
  共同职责和后端选择；
- [算例目录](../simulation/README.md)：可复用的问题定义和示例输入；
- [物理说明](physics/README.md)：模型来源，并区分维护中的说明与历史数值分析；
- [Verification 与 Validation](../validation/README.zh-CN.md)：可执行结论、验收标准、指标和后端一致性；
- [EOS 运行时表](../EOS_toolkit/README.zh-CN.md)：表目录、来源和完整性要求；
- [法律与来源索引](legal/README.zh-CN.md)：项目及第三方许可证入口。

## 开发与审阅入口

- [开发者索引](development/README.md)：实现职责、维护冻结要求和保留的开发记录；
- [测试](../tests/README.md)与[工具](../tools/README.md)：局部回归检查，以及共用的
  构建、网络生成和验证工具；
- [Validation 总索引](../validation/README.zh-CN.md)：统一记录综合验收结论；
  各模块摘要说明科学方法并链接实际证据。

## 目录契约

- [guides/](guides/README.md) 保存面向任务的工作流；
- `physics/` 汇总模型说明，并明确标注历史分析；
- `development/` 保存开发决策和执行记录；
- `legal/` 只提供查找入口；规范许可证与 notice 文件保留在仓库根目录，以兼容标准工具；
- `validation/` 位于 `docs/` 之外，因为其中的记录支撑明确的通过/失败结论，并自行管理不可变输入、指标和图件；
- 源码与算例目录应保留实现、可复用示例输入和必须紧邻代码的简短局部契约。

新增文档时应加入本索引，并在适当的归属模块中建立链接。不要再创建第二套通用文档或验证目录。
