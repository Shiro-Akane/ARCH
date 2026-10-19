# 配置迁移后的架构审计：失败与最小修订提案

## 当前结果

源码：2589a8e8c79befb97df87bda8e6d1b93d9e29385，审计前工作树 clean。
python3 tools/audit_architecture.py . exit 1，真实失败：
- protected mainline authority changed: src/core/config/RuntimeParams.h
- OBJECT files may enter only their canonical owner: arch_cuda_burn_controller_parity

python3 -m unittest tests.tooling.architecture.test_audit_architecture：
106 tests，105 pass、1 fail，无 skip。失败是现有
test_repository_tree_passes_audit，同样报告以上两项。
不是 full Python/tooling suite 或 CPU/CUDA/scientific验收。

## 实现与旧预期的差异

1. tools/audit_architecture.py 的 protected[RuntimeParams.h] 要求 parser.GetBool。
   O7.0 已迁移到 AnalyzeConfigurationInput，随后 RequireDeclaredInputs，再 Resolve
   checked standard inputs。ConfigParser 的 INVALID_BOOLEAN 保护仍独立存在。
   旧 token 不再出现在该消费者，不能加无效代码/注释来伪造通过。
   新入口正确性的运行证据仍来自既有 configuration_input/
   configuration_entry_contract/v3 及 case identity checks；字符串审计不能替代它们。
2. cmake/tests/CudaTests.cmake 的 arch_cuda_burn_controller_parity 在
   789fa341af2b6bbea84ac7c5965240b03145b655 已增加
   src/core/config/CompositionInput.cpp（严格 case loader 迁移）。
   三个 EOS OBJECT 消费者未改变：eos_helm/eos_species/eos_utils。
   audit 的精确 executable source tuple 仍只有 .cu entrypoint，因此拒绝。
   这是待审的 test consumer 声明差异，不是本轮已验证 CUDA linking/device执行。

## 待确认的最小修订

只修改 tools/audit_architecture.py 和对应 tests/tooling/architecture 单元测试：
- RuntimeParams 的 protected marker 改为同时要求
  arch::config::AnalyzeConfigurationInput 与 input.RequireDeclaredInputs();。
  任一缺失均失败；ConfigParser INVALID_BOOLEAN protection 原样保留。
- arch_cuda_burn_controller_parity 的精确 source tuple 仅增加当前已登记
  src/core/config/compositioninput.cpp（审计 normalization 为 lower-case）。
  保持原三对象的数量、顺序和 target 限制；不新增通配符/任意 source 或 OBJECT allowance。
- 增加负向 mutation coverage：旧 parser token不能代替新 gate；缺 analysis/
  RequireDeclaredInputs 任一失败；缺 CompositionInput/换任意别的 source/增加
  backend_core 对象全部失败。当前精确接线应通过。

这是改变审计预期，存在漏检风险，必须审阅；字符串存在性检查本身不证明每个
入口都执行 gate。所需运行契约测试不能删除或以此替代。不得恢复宽松 loader、
删除失败项、修改物理定义或预算来通过审计。

## 审批与未完成项

修改审计和测试的工具操作被自动审批拒绝，理由是可能通过改变保护规则接受当前实现。
操作未执行，以上两个源文件没有修改；本文件仅提供提案，不应用 patch。
等待用户明确确认上述限定的规则迁移，再按正常审批重新评估。
当前架构审计仍 FAIL，不能标 O7.0 完整验收通过。

另发现 O7.0 计划列出的几何配置/coordinate seam/Poisson 中 acos(-1)
归一 math::pi/two_pi 尚未完成；先核对逐位及舍入，再按所有权和受影响检查整理。
本轮未修改这些数学消费者。历史非物理 G 输入迁移仍待 Core owner 批准。

Linux 独立窗口有 agent capture，但用户可见性仍待新确认；3C 未签收。
没有运行新 simulation、Build/CUDA、上传 raw data、push 或 release tag。
