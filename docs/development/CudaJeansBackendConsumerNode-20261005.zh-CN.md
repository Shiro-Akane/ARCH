# JENS CUDA backend consumer 节点（2026-10-05）

## 当前状态

SCOPED PASS：backend consumer、Host fallback contract 与相关 CUDA CTest 实机通过。
仅为本节点验证，不是完整 CUDA JENS 生命周期或 RZ 科学签收。
起始 HEAD：23f34a9a222ebbb49c214a5064ef157722c20d2c。
唯一工作区：/home/arch/projects/ARCH-compute-optim。
CPU ARCH SHA256：7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，未重建。
当前源码存在新增 interface 输入，不能据此宣称旧 CPU Build Manifest current。

## 实现职责

ComputeBackend 的 evaluate_jeans_resolution 默认明确拒绝，不自动切换 Host EOS；
原 CudaBackendIndicators owner 实现 accepted Current 批量 block minima。
整批先核对唯一 handle、AMR epoch、storage generation、Current slot、
Cartesian layout、species extent；非法后项不得留下前项已 enqueue 的工作。
复用原 refinement arena/summary 和每块 EOS latch；
同一 stream 顺序复用 arena，只下载每块一个 FP64 scalar，并一次完成后返回。
非法 accepted cell 或 EOS 失败使整个批次失败；不跳过坏单元、不增加 floor，
不复制 Jeans 数学或 EOS、G 与 spacing 定义。

BackendStateAccess 没有 StateVersion/publication 身份。
本方法不把 Current slot 冒充完整 accepted publication；
调用方须通过原 residency ledger 确认同版本、Device readable、
completion 和 pending transfer。现有 complete_device_boundary 已承担相应检查。
Driver JENS CUDA gate 本节点保持，不能仅因 backend 方法通过而打开。

## 已完成检查

相关 CPU compute_backend 原 target 重编译及 CTest 1/1 PASS：
默认无能力 backend 对空批次和非空批次都明确拒绝，没有隐式回退。
该测试不重编译 CPU ARCH 应用，不重跑未变 binary 的冻结 9+9。
当前 architecture audit 与 git diff --check PASS。

## 本节点实际 CUDA 验证

原 cuda_multiblock_hydro CTest 新增测试，不新增 job：
三种时间积分器的初始及 Current rotation 后 ordered scalar minima；
scratch shrink、reversed order、精确 transfer/fence counters；
duplicate handle、storage、Next slot、epoch 的整批 preflight；
空批次 no-op；第二块 rho=0/NaN 或负 eng 的整批错误及恢复。
以上新增检查全部实机通过：Current markers=6、failure recovery markers=3，
覆盖 Euler/RK2/RK3。cuda_multiblock_hydro 与 cuda_refinement_indicators 共 2/2 PASS，
没有 skip。原 independent numeric 197 valid/104 unrepresentable/12 invalid、
24 caloric 和 6 dimension/level patch 检查再次通过。
这些 scalar-vs-batch 对照只验证 routing，不作为新的科学 oracle。
独立 Decimal/caloric 数学参考仍属于上一 accepted-cell 节点。
完整 backend target 构建 exit0；耗时2890.973s、采样 peak owned RSS1986608KiB，
采样 baseline/peak swap0，guard未停止。随后两个相关 target 的最终增量检查
ninja: no work to do.，确认构建期间新增测试已进入产物；源码 fingerprint 前后相同。
此耗时只为编译观察，不是科学 benchmark。处理后摘要位于
validation/gravity/results/cuda-jeans-backend-20261005/summary.json，包含源码/ELF/log SHA256。

## 下一消费链节点的实际依赖

1. Driver 使用 residency ledger 检查 accepted Current 后调用原 backend，
   将 compact minima交给原 AMR decision；关闭 JENS 时不增添遍历或同步。
2. 候选父决定必须来自同一真实 device restriction 状态及 authoritative EOS。
   现有 AmrTree::CandidateParentResolved 使用 Host block，不可将陈旧 Host 状态
   或 device leaf minima 代替 parent EOS/veto。
3. CudaBackendMigration 已有 private store transaction 和原 restriction kernel，
   但它在 topology 选择后执行，不是目前可直接调用的候选父前置接口。
   下一节点须保留原 transfer 数学和未发布 scratch/事务职责，
   区分 inadmissible-parent veto 与其他 fatal failure，不能部分发布。
4. 最后才解除已验证能力子集的 gate，构建真实 ARCH CUDA binary，
   执行冻结 initial/accepted macro/regrid/restart/output 三通道短包。
   Runtime routing 和 CPU/GPU 同值均不替代独立科学验收。

## 尚未完成的出口

完整 CUDA JENS 生命周期、其他 EOS 科学状态、RZ A→B→C→D
全部消费者和科学签收、正式同终点 benchmark、批准长跑均未完成。
原始日志与二进制保存在 studio/.local/integration/cuda-jeans-backend-20261005。
本节点不启动新 RZ 子集、不更改预算、不开展 Windows 适配。
