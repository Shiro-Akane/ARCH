# RZ native measure construction node

**NATIVE MEASURE / RMS PASS；完整 physical RHS 未签收，production RZ gate 保留。**

基线 70ee1d3f894a97303f3a66fe70cac513d9e1e589；唯一工作区 /home/arch/projects/ARCH-compute-optim。主计划科学清单 §7.3 的 geometry/weight 构造依赖，没有新增物理定义或验收阈值。

## 实际所有者与数学范围

CompositePoisson::native_rz_measure_enclosure() 从自己的 root origin/spacing、整数 leaf index 和 level 构造 transient proof data；没有新建 mesh owner、源项、求解器或第二套持久状态。

理想 full-ring 单元体积 V=pi*dr*(2*r_lo+dr)*dz，r_lo=origin+i*dr，dr/dz 为 root spacing 的精确 dyadic 缩放。显式使用 dr 避免相邻径向边相减的相消。邻接数学 pi 的 FP64 外界和逐步向外舍入围住体积；逐叶求和围住总量，再围住 V/sum(V)。实际 GridMetrics volume / 实际 normalized weight 分别给出到该区间的最坏距离，而不是把已存储数值当精确几何。

CompositePoisson::native_rz_norm_interval() 按上述理想 native weights 围住实际传入 FP64 array 的 RMS；scale 防范平方溢出，精确零保持零，不添加 floor。Cartesian/非 RZ、缺失或非有限 array 明确拒绝；dyadic 缩放不能精确表示或区间溢出时拒绝取得 Bounded。

本节点不改变生产 operator、fluid state、source、求解阈值或 provider norm。原 stored-coefficient ledger 和新 ideal-native measure 语义保持区分。此 proof primitive 尚不认证边界/粗细界面多项式拟合、LU、fallback stencil、ring source/observer 的存储坐标差异，也不能直接升级此前 conditional ring RHS status。

## 独立参考和反例

实际 CompositePoisson uniform/mixed leaves × radial origin 0/0.5/0.3 × binary/nonbinary spacing 共12组264 cells。不是另造独立几何替代 operator：probe 输出真实 volume/weights 和当前 root/leaf identity 供读取。

独立 Fraction 用 root 精确数计算 r_lo/r_hi、(r_hi²-r_lo²)*dz，并在 normalized weights 中精确消去 pi；逐 cell 验证真实 weight error upper 和 physical RMS 的平方区间。体积和总量另用100/140位 frozen pi 参考检查区间包含及真实存储误差。脚本不导入生产 interval helpers。原始数组只留本机。

Cartesian certificate 拒绝、缺失/NaN 输入拒绝、精确零 RMS=0 的反例检查通过。该组 PASS 是上述 geometry/measure 范围的证据，不是连续 Phi/force 或旋转演化签收。

## 构建和回归身份

复用原 CPU Release tree 增量编译 ARCH / composite_poisson / self_gravity / gravity_stage_contract，parallel 28，11.161s；memory guard未停止，peak owned RSS 2578820 KiB，swap growth0。

6项受影响 CTest：physical_constants、gravity_stage_contract、composite_poisson_analytic/contract、self_gravity_lifecycle/physics 全PASS（10.66s）。architecture audit 与 diff check PASS。

生产 ARCH SHA256 仍为 ea67946b8e14a32ec7829657708489e96ab013eb97ac3914133a7b300d38547b，与上一节点完全相同；沿用该相同 ELF 的冻结 JENS 9短+9真实restart PASS receipt，没有重复 baseline/长跑。新 probe SHA、源码 SHA 与标量结果见处理后 summary。

## 后续依赖

继续 authoritative face/interior stencil 构造误差，特别是 quadratic weighted Gram/LU 与真实 fallback；再将理想 measure 与 coordinate/source/operator 误差组合到 §7.3 的非循环 residual 判据。旧 axis-force / viscosity finding 和全旋转消费者、独立势/力 gate 仍保留。CPU 对应科学 gate 后才统一 CUDA 和获批长跑。

复现：

    python3 validation/gravity/rz_native_measure_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local output root>

处理后摘要：validation/gravity/results/rz-native-measure-20261005/summary.json。
原始数组、build/CTest全量日志在 studio/.local/integration/rz-native-measure-20261005。未修改 root STATUS，未 merge main，未运行 simulation，未开展 Windows 适配。
