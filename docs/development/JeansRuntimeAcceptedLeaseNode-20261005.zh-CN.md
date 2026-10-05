# JENS accepted Current：实际 Runtime lease 节点（2026-10-05）

## 身份与实施范围

起始 HEAD 3beaed0e5b5ccc6263a874e4be5a1fcc33173c3f，唯一 ARCH-compute-optim 工作区。
SCOPED PASS；不是完整 JENS CUDA enforcement/lifecycle。
DriverRuntime 新增显式 evaluate_current_jeans_resolution 入口：
验证 committed topology、全体 Current 共同 StateVersion、ledger side/readability、
completion/pending transfer、backend storage，整批 preflight 后才求值。
Host 路径直接消费原 AmrTree::MinimumJeansCells，Device 路径调用原 backend。
检查返回 extent、有限正数；不复制 EOS/Jeans/G/spacing 数学。
活动单元 diagnostic 不依赖 ghost，因而不无故增加 halo 或 Host materialization。

本节点尚未替换 ordinary Driver 的 ensure_jeans_resolution/regrid enforcement。
关闭 JENS 的既有 early return 保持，未在关闭路径加入扫描。
startup/regrid CUDA JENS gate 以及 RZ gate 均保留。
下一节点须完成真实 device candidate-parent restriction/EOS/veto，
再把两类摘要纳入原 PrepareRegrid decision；不得使用陈旧 Host 父态。

## 实际 CPU Runtime fixture

扩展原 gravity Runtime fixture；重编测试与相关 Runtime/GravityStage TU，
没有 configure 或重建 CPU ARCH 应用。
通过原 authoritative IdealGas pressure/sound speed 调用 shared Jeans leaf。
4 root blocks/64 active cells 的真实 Runtime 摘要与原 tree order 相同。
故意仅发布最后一块新 version，整域请求在任何 EOS 求值前拒绝。
真实 refine 4→8、coarsen 8→4、no-change 后新摘要 extent 仍正确。
将整域 Current 发布为 device-only，无 backend 的 Host 路径明确拒绝，
不是自动取 Host 数组或假 materialization。原 GravityStage 7次gather仍通过。
Controller time=0、steps=0；无 simulation 演化验收含义。
CPU生产ELF仍为7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510；
旧冻结9+9仍只代表对应ELF，当前 source/build freshness 不冒称 current。

## 实际 CUDA Runtime fixture

原 cuda_regrid_transaction CTest 使用 canonical arch_solver_dispatch，
没有复制 Runtime TU，没有新增 CTest job/audit exception。
真实 initialize_topology→prepare_backend_bindings→make_cuda_backend→upload_initial_state，
然后消费新 Runtime 入口。上传后所有 Host rho 故意NaN，Device摘要仍与上传前一致。
2 blocks只新增4 kernels、16B D2H、一次stream completion、0B H2D；
没有全场materialization或Host EOS回退。

非首块version mismatch和非首块pending D2H分别在任何backend工作前拒绝，
检查 counters 全部不变。恢复时真实显式D2H并quiesce，
之后才complete_transfer；不能以token alone将poisoned Host宣称synchronized。
再次请求成功。time=0/steps=0，不是simulation或CUDA科学短轨迹。
原4与41 species的device refine/restrict/rollback、survivor和stale-host检查保持PASS。
最终CTest 1/1 PASS，无skip。独立Jeans Decimal/caloric参考仍由上一数学节点提供；
本节点结果一致只证明Runtime lease/routing，不成为新科学参考或阈值。

## 证据与出口

处理后summary：validation/gravity/results/jeans-runtime-lease-20261005/summary.json，
记录定稿input、CPU fixture和CUDA test ELF/log SHA256。
本机raw：studio/.local/integration/jeans-runtime-lease-final-20261005，
以及jeans-runtime-cuda-final-20261005.*；原始诊断/ELF不提交。
当前科学RZ、候选父device EOS、完整CUDA冻结短包、长跑与benchmark仍未完成。
Linux/WSL范围不变，不开展Windows适配。
