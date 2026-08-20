# ARCH 文档

英文原文：[README.md](README.md)。英文版是唯一规范文本；若中英文内容不一致，以英文版为准。

本目录是维护中文档的统一入口。

## 从这里开始

- [算例指南](guides/SimulationCase.zh-CN.md)：构建、配置、运行及扩展算例；
- [研究与 API 参考](Reference.zh-CN.md)：参数、策略和扩展契约；
- [物理说明](physics/)：实现边界、来源和数值对齐记录；
- [Verification 与 Validation](../validation/README.zh-CN.md)：可执行结论、验收标准、指标和后端一致性；
- [EOS 运行时表](../EOS_toolkit/README.zh-CN.md)：表目录、来源和完整性要求；
- [法律与来源索引](legal/README.zh-CN.md)：项目及第三方许可证入口。

## 目录契约

- `guides/` 保存面向任务的工作流；
- `physics/` 保存长期维护的设计、来源和数学说明；
- `legal/` 只提供查找入口；规范许可证与 notice 文件保留在仓库根目录，以兼容标准工具；
- `validation/` 位于 `docs/` 之外，因为其中的记录支撑明确的通过/失败结论，并自行管理不可变输入、指标和图件；
- 源码与算例目录应保留实现、可复用示例输入和必须紧邻代码的简短局部契约。

新增文档时应加入本索引，并在适当的归属模块中建立链接。不要再创建第二套通用文档或验证目录。
