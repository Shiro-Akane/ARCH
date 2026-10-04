# 两条架构规则：恢复决定下的限定候选

Authority：Core 11a321d5604f9ee62b9f9587c81f14de4f128bc4 第2.1节。当前checkpoint 4a7b43075a8b41726c20af6c3ffd6fadc41dcd96 已push。本工具原规则没有独立数字ID，以下使用真实配置键作为稳定review ID，不能误称新增CI规则。

| 规则ID/实际位置 | 原判定及最小复现 | 职责映射前后 | 候选 |
| --- | --- | --- | --- |
| audit_tree.protected[src/core/config/RuntimeParams.h] | audit_tree完整tracked tree：protected mainline authority changed；旧marker parser.GetBool在迁移后消费者中已不存在。旧记录O7ArchitectureCurrentAudit保留。 | 前：RuntimeParams直接parser读取；后：ConfigurationInput统一Analyze/RequireDeclaredInputs，checked标准输入Resolve；RuntimeParams仍是消费者，ConfigParser INVALID_BOOLEAN保护保留，没有新增配置权威。 | 要求analysis与RequireDeclaredInputs两个marker同时存在；旧token不能替代新gate，不在生产中插空代码迎合审计。 |
| _CUDA_FOCUSED_LINK_OBJECT_CONSUMERS[arch_cuda_burn_controller_parity] | CMake精确source tuple仅接受.cu，实际consumer另含CompositionInput.cpp，触发OBJECT files may enter only their canonical owner。 | 原三EOS OBJECT仍由既有owner持有；测试consumer调用严格组成/case输入分析，需要CompositionInput.cpp这个非OBJECT helper。不是把backend_core对象借给测试，也不是更换数学所有者。 | source tuple只增加已登记compositioninput.cpp；EOS对象数量、次序、target和canonical owners不变，无wildcard/skip。 |

候选差异见O7ArchitectureRulesCandidate-20261004.patch；尚未应用audit源码，不声称审计已PASS。它只改上述marker与精确consumer元数据，shared math、物理阈值、ConfigParser保护及其他层次规则不变。若Core认为第二项涉及实际所有权豁免，按第2.1节先review后集成；不会从“当前代码能绿”反推白名单。

配套mutation验收清单：缺analysis、缺RequireDeclaredInputs分别失败；只留parser.GetBool仍失败；缺CompositionInput、替换其他helper、增加backend_core任一失败；三EOS对象删/换/重排保持拒绝。运行配置拒绝/声明ABI/OBJECT所有权测试不可删除，字符串审计不替代真实执行gate。

此前自动审批拒绝改规则的原记录保留；本轮先按Core恢复要求给出具体candidate，未绕过审批，也未把用户泛授权解释成blanket exception。处理后报告/patch可review，原完整audit错误在本机，不上传全量日志。

## 2. Core 决定与集成出口（2026-10-04）

评审基线：`4b5e496a9943099c203c6b001b1d95a56d9edf72`。
**批准所列 candidate 的两项精确迁移及配套测试更新**；授权仅限候选所展示的
配置 gate marker、该测试 consumer 的唯一非 OBJECT helper 和支持 tuple marker
的检查逻辑，不扩大 backend OBJECT 所有权或取消任何其他层次约束。

1. RuntimeParams 的两个载入入口都已先执行 AnalyzeConfigurationInput、
   RequireDeclaredInputs，再消费 resolved 标准输入。protected marker 因职责迁移
   更新为这两个必要条件；保留 ConfigParser 保护及真实非法／缺项／声明 ABI 检查。
2. CMake 当前确实仅给 burn controller parity 增加 CompositionInput.cpp 普通源；
   原 Helm/species/utils 三个 OBJECT owner、次序和 link 关系保留。
   允许更新精确 source tuple，禁止 wildcard、额外 backend_core、另一数学主体或 skip。

维护者在唯一 checkout 上以**内存候选**执行 tracked-source 审计：旧规则准确产生
两条 finding，候选结果为零；九项负向 mutation 均被拒绝：缺 analysis、缺声明检查、
只有旧 GetBool，以及 helper 缺失／替换、额外 backend_core、EOS OBJECT 缺失／替换／重排。
这是候选规则验证，生产 tools/audit_architecture.py 尚未修改，不能称实际 CI 已绿。
没有建立第二份 ARCH 源码树；mutation 只覆盖内存中的两个文件内容。

集成时必须同批更新 `tests/tooling/architecture/test_audit_architecture.py`：
其 `protected` fixture 仍只提供旧 parser.GetBool，直接应用 candidate 会使原合法
fixture 失效。迁移该 fixture 的实际 gate 内容，加入上述反例，保留原独有覆盖。
复用现有 tooling discovery 和架构入口，不添加独立 CI workflow，也不降低断言。
完成现有架构套件、实际源树审计和相应配置执行 gate 后，才关闭这两条 finding。
测试目录中的微型 fixture 不构成新的 ARCH checkout；本机工作区继续唯一。
