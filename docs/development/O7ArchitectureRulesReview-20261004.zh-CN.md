# 两条架构规则：恢复决定下的限定候选

Authority：Core 11a321d5604f9ee62b9f9587c81f14de4f128bc4 第2.1节。当前checkpoint 4a7b43075a8b41726c20af6c3ffd6fadc41dcd96 已push。本工具原规则没有独立数字ID，以下使用真实配置键作为稳定review ID，不能误称新增CI规则。

| 规则ID/实际位置 | 原判定及最小复现 | 职责映射前后 | 候选 |
| --- | --- | --- | --- |
| audit_tree.protected[src/core/config/RuntimeParams.h] | audit_tree完整tracked tree：protected mainline authority changed；旧marker parser.GetBool在迁移后消费者中已不存在。旧记录O7ArchitectureCurrentAudit保留。 | 前：RuntimeParams直接parser读取；后：ConfigurationInput统一Analyze/RequireDeclaredInputs，checked标准输入Resolve；RuntimeParams仍是消费者，ConfigParser INVALID_BOOLEAN保护保留，没有新增配置权威。 | 要求analysis与RequireDeclaredInputs两个marker同时存在；旧token不能替代新gate，不在生产中插空代码迎合审计。 |
| _OBJECT_TEST_CONSUMERS[arch_cuda_burn_controller_parity] | CMake精确source tuple仅接受.cu，实际consumer另含CompositionInput.cpp，触发OBJECT files may enter only their canonical owner。 | 原三EOS OBJECT仍由既有owner持有；测试consumer调用严格组成/case输入分析，需要CompositionInput.cpp这个非OBJECT helper。不是把backend_core对象借给测试，也不是更换数学所有者。 | source tuple只增加已登记compositioninput.cpp；EOS对象数量、次序、target和canonical owners不变，无wildcard/skip。 |

候选差异见O7ArchitectureRulesCandidate-20261004.patch；尚未应用audit源码，不声称审计已PASS。它只改上述marker与精确consumer元数据，shared math、物理阈值、ConfigParser保护及其他层次规则不变。若Core认为第二项涉及实际所有权豁免，按第2.1节先review后集成；不会从“当前代码能绿”反推白名单。

配套mutation验收清单：缺analysis、缺RequireDeclaredInputs分别失败；只留parser.GetBool仍失败；缺CompositionInput、替换其他helper、增加backend_core任一失败；三EOS对象删/换/重排保持拒绝。运行配置拒绝/声明ABI/OBJECT所有权测试不可删除，字符串审计不替代真实执行gate。

此前自动审批拒绝改规则的原记录保留；本轮先按Core恢复要求给出具体candidate，未绕过审批，也未把用户泛授权解释成blanket exception。处理后报告/patch可review，原完整audit错误在本机，不上传全量日志。
