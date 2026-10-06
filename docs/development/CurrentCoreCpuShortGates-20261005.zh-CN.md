# 当前 CPU binary 的获准短 gate 复验

## 原因与实际构建结果

历史 O7ResumeShortGates 使用 e9d5cf22 binary；当前 production CPU 为 7d3bd3d。
JENS 已有 7d3bd3d 的匹配 9+9 receipt，但原 mixed-3D 与两个径向迁移样本
没有该 binary 的最新 receipt，因此只重新执行这三个受影响样本。

运行前 source HEAD c2b76107617d780f5c06d87eb224aa53ef07a784，working tree clean。
现有 build-cpu 的 source root 为本工作区，Release，ARCH_ENABLE_CUDA=OFF。
受内存保护执行标准 cmake --build build-cpu --target ARCH --parallel 28：
实际返回 no work to do，没有重编译 ARCH 对象、没有独立 configure。
dry-run 的 glob/CMake regeneration 提示不能作为有对象待编译的证据。
实际 CMakeCache SHA 与 binary SHA 前后均相同。
591 个 tracked src/simulation/cmake 构建输入前后 SHA 一致，完整索引留本机。

这证明标准 Ninja 依赖检查在当前工作区无待构建工作；
不是外部库、动态 loader 及所有依赖的完整 Build Manifest。
dependenciesComplete 仍为 false，不更新旧 Studio managed Manifest。

## 当前实际结果

原 Core 11a321d56 第2.1节限定授权，复用 run_o7_resume.py，
不改输入、科学定义或门槛。OMP_NUM_THREADS=1，CPU。

| 样本 | 原短范围 | 实际指标 |
|---|---|---|
| native-mixed-3d | 2 steps，非均匀 Cartesian AMR | 10 solves；质量漂移1.193257e-16；能量/初始势能4.619052e-6；refine 1/no-change 1 |
| spherical-refine-coarsen | 40 steps，原限定 IdealGas 低G迁移 | 162 solves；Gauss8.175608e-9 <1e-7；质量2.948434e-15、能量1.953299e-15 <1e-12；refine 2/coarsen 1/no-change 18 |
| cylindrical-refine-coarsen | 40 steps，原一维径向柱体 | 162 solves；Gauss3.972522e-12 <1e-7；质量2.989633e-15、能量2.003389e-15 <1e-12；refine 2/coarsen 1/no-change 18 |

三个样本都 repair events=0；所有记录的原残差目标通过。
迁移两个初态 ENER/rho 与原 cv*T 相对差4.440892e-16/2.220446e-16，
密度仍高于原 floor，未调整 floor 或给修复换名。

**终点范围：** mixed-3D 实际终点0.0005208006218809392，未到tmax=.02。
球／柱体实际终点0.008355035768485626/0.008354359727176531，未到tmax=.1。
三者都按原 max_steps 停止；全部只支持原短 gate，不支持相同物理终点 benchmark
或任何长轨迹通过。一维 cylindrical 不等于二维 RZ 科学支持。

本次保护器未触发，观测 swap 增长0，峰值 owned RSS452812KiB；
这些是短包保护器观测，不是正式 benchmark 性能、峰值GPU资源或长跑预算。

## 身份与数据

binary SHA256：
7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。

处理后 [summary](../../validation/gravity/results/current-core-cpu-gates-20261005/summary.json)
包含输入 fingerprints、修复/事务计数、实际时间、原始文件 SHA 与本地索引；
不包含原生场数组。原始 H5、trace 与全量日志保存在
studio/.local/integration/current-core-cpu-gates-20261005/replay。

未变 binary 的冻结 JENS 9短+9真实restart继续引用
rz-stencil-construction-20261005/summary.json，不重复运行。
未执行 CUDA、正式计时、新RZ或长跑；连续势/力、axis/viscosity及完整Runtime RZ
科学门槛仍开放，整体目标未完成。平台仍 Linux/WSL，不开展 Windows 适配。
