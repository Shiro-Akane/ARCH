# O7 当前架构审计：clean tracked 源码复核

2026-10-04，源码 73ae158c203005f3ca82be0a65d0d835ad1e9848。
fetch 后 compute/optim=8fc0dd25eefd2243e8c36f85440bac46994e2e73，
codex/o8-boundaries=23ff77c4f08419de2b3c5eadee214da2af25784e，均无更新。
本轮没有改变任何 audit 规则、科学实现或测试预期。

## 当前结果

同一 HEAD 通过 git archive 提取完整 tracked 源码到独立新目录。
该目录不复用 node_modules、build 或本地 ignored 工作副本；Python 审计不需要 npm 环境。
python3 tools/audit_architecture.py .：exit 1，只有两项：
- protected mainline authority changed: src/core/config/RuntimeParams.h
- OBJECT files may enter only their canonical owner: arch_cuda_burn_controller_parity

python3 -m unittest tests.tooling.architecture.test_audit_architecture：
106 项、105 PASS、1 FAIL（test_repository_tree_passes_audit），同样两项。
这不是 full Tooling/CTest、CPU/CUDA 或科学验收。

源码核对：RuntimeParams 文件和文本入口均先 AnalyzeConfigurationInput，
再 RequireDeclaredInputs；旧 parser.GetBool token 不再是该 owner 的实现。
CudaTests.cmake 的 burn controller target 已含 CompositionInput.cpp；
audit 的 source tuple 仍只允许原 .cu 文件，EOS OBJECT 三个 owner 未改。
限定迁移提案仍见 O7ArchitectureAuditMigrationReview.zh-CN.md。

## 初轮额外命中与证据隔离

开发目录直接运行得到79项 violation。额外命中来自 ignored 的
studio/.local/integration/studio-ci-candidate-20261004 内完整 CMake/CUDA 源码副本。
现有 audit 递归扫描目录，不等同 git tracked tree inventory。
没有删除该净安装验证证据、自动清理目录或放宽 scanner；
随后纯 tracked archive 的复核用于区分正式交付源码与本地证据副本。
首轮完整 stdout/stderr 与105/106结果保持本机，不把它改写为两项失败。
这不是通过排除正式源文件制造 PASS；clean 源码仍真实 FAIL。

## 后续与审批边界

当前已有 Studio 334/334/local lint/build 证据只覆盖对应 job。
新 CI required 同时要求 Tooling/CPU/Studio；这两条失败未解决前，
不能声称完整 required CI 或 O7.0 全验收通过。
曾自动审批拒绝修改保护规则，原因是可能通过改规则接受实现。
本轮已向用户请求限定迁移及反例测试授权；未应用 patch。
批准后正常重新评估，仅迁移两处精确规则，保留所有权与拒绝行为并验证负向 mutation。

原始两轮日志和完整 tracked archive 留在本机 ignored 目录：
studio/.local/integration/o7-architecture-current-20261004。
没有 configure/build/simulation、科学阈值变化、push/tag/main merge。
其他科学待决、完整CPU/CUDA和冻结O9目标保持未完成。
