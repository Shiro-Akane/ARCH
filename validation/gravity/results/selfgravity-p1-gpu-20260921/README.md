# P1 Driver 拆分：CUDA 验证与 CI 整理

本记录只验收 P1。数值源码为 `95b858fc9f97147a8bced5146dd229dcc3d08df4`；
构建/验证流程整理后的受测提交为 `74a755e596a29dce7f13ac7864ee3692553d7166`。
没有引入 Poisson、multigrid 或自引力生产求解器；`gravity_type=self` 继续明确拒绝。

## 证据边界

前序 [CPU 拆分对照](../selfgravity-p1-20260921/README.md) 比较 GUI Core 拆分前程序和 P1
程序，10 个案例、40 份检查点场量零差，另有 4 组旧检查点续算。本次沿用这份历史记录，
不把 Debug CPU 基线说成新 Release CUDA 程序。

本次重新构建 P1 的 Release CUDA 程序，并在同一个可执行文件内选择 CPU/CUDA 后端做
数值对照，检查设备资源、AMR、Burn/Diffusion、两种重启相位及内存/竞争错误。
这是 P1 受影响路径的验收，不是全部 EOS/网络/稀疏 provider 的科学验证，也不是加速比测量。
没有另外编译拆分前 GPU 程序，因此不声称拆分前后 GPU 逐位相等或性能不变。

## 构建与复现

环境为 WSL2、GCC/G++ 12、CUDA Toolkit 12.3、RTX 3060 Ti（8 GiB）、`sm_86`，
Release、OpenMP/CUDA/cuDSS/KLU 均开启。HighFive、SuiteSparse、cuDSS 复用本机已有依赖；
启用原有浮点约束，不改优化级别、物理公式或数值测试预算。

用户允许高负载编译，本机使用总并发 3、重型 CUDA 并发 2，内存守护保留 512 MiB，
允许最多 1536 MiB 新增 swap，并监控持续内存/IO 压力。这是本次约 7.7 GiB WSL
环境的执行选择，不修改全项目默认资源配置。
主构建用时 3044.938 秒（50.7 分钟），受监控进程峰值 RSS 5447864 KiB（5.20 GiB），
最低可用内存 1900392 KiB（1.81 GiB），swap 增长为零，未触发守护停止。
最新 CMake 增量只重链接比较器，8.206 秒完成。记录见 [构建环境](build-environment.json)、
[主构建日志](build.log)和[增量日志](incremental-build.log)。这些数值不是求解性能。

构建目录为 `build/selfgravity-p1-gpu`。主程序 `bin/ARCH` 和以下受影响目标共同构建：

```text
arch_cuda_single_level_validation arch_cuda_compile_probe
arch_cuda_regrid_transaction arch_cuda_regrid_migration arch_cuda_amr_composition
arch_cuda_store_lifecycle arch_cuda_amr_exchange arch_cuda_hydro_dispatch
arch_cuda_diffusion_rkl_parity arch_cuda_multiblock_hydro
arch_cuda_multiblock_diffusion arch_cuda_multiblock_burn arch_cuda_reduction_contract
arch_gravity_stage_contract arch_shared_stage_scheduler
```

实际运行命令、结果和用时保存在 [qualification-run.json](qualification-run.json)；每组正式运行报告保留源码、
程序、构建配置、依赖与输入身份。路径指向原始运行目录，不要求其他机器使用同一个绝对路径。
复现时从仓库根目录执行相同命令并改用新的输出目录。普通对照固定 `OMP_NUM_THREADS=2`、
`OMP_DYNAMIC=FALSE`；active-ENUC 续算沿用现有记录的 4 线程。设备检查串行运行。

## CI 与验证流程改动

- `physics/selfgravity` 的 push/目标 PR 触发原有完整 Tooling + CPU Release CI；仍无路径过滤。
- checkout 只下载当前 CPU 测试需要的 Helmholtz LFS 表。实际检查证明 Helm 表还原且其他大表
  保留指针；远端完整测试验证所需数据齐全。没有缩减 CTest 选择。
- 架构审计在进入目录前排除已配置的 CMake 构建树和辅助 Git checkout；继续扫描未跟踪源码、
  `build_helpers` 一类正常模块和生产目录中的 Git 子模块。无需反复导出干净快照。
- 检查点比较器保持原文件、CLI、字段/时间/控制器容差，移到 Host 测试构建；CPU-only 不再
  为比较 HDF5 链接整个 CUDA 后端。原 40 对 CPU 检查点通过新构建比较器复核，错步反例仍拒绝。
- CTest 报告检查新增明确命名的 `driver-cuda` 局部 profile，要求必要锚点与全部所选条目；
  缺项、额外结果、skip 均失败。完整 CPU CI 继续核对整个配置 inventory。

原数值测试及其通过标准没有修改。工具逻辑变化增加了正反例；首次运行缺少 NumPy/h5py，
安装到独立验证环境后重跑全部工具测试。CPU 比较器首次构建的 ccache 权限错误也保留为环境
诊断；禁用该次编译缓存后正常构建。二者均未通过删测试或改断言解决。

[远端 CI 运行](https://github.com/Shiro-Akane/ARCH/actions/runs/35613817405) 已在受测提交通过
Tooling、CPU Release 和 CI required；机器可读记录见 [hosted-ci.json](hosted-ci.json)。

## 完成状态

**P1 GPU 门槛通过，仍停在 P1。** 汇总见 [evidence.json](evidence.json)。所有正式运行报告
均核对运行前后源码/程序/依赖身份，受测代码和输入无未提交修改；整理的文档与结果不进入该哈希范围。

| 检查 | 结果与边界 |
|---|---|
| Driver CUDA CTest | 22/22，零失败、零跳过，192.24 秒。覆盖共享调度、引力准备、AMR 交换/组合/迁移/事务、生命周期、归约、Hydro、RKL parity，以及 3/1024/1025 批次边界与全局 species。完整清单与输出见 [inventory](ctest-inventory.json)、[JUnit](ctest.xml)、[覆盖核对](ctest-coverage.log)。 |
| 均匀 none/external | 3 案例、18 次 CPU/CUDA 执行、9 次比较：MUSCL 平流、外引力 RK2/RK3，采样 step 1/5 与 `t=0.1`；后端场量差为零。保留平滑波与常加速度参考，未声称单分辨率复验收敛阶。[记录](none-external.json) |
| 外引力 + AMR/扩散 | 3 案例、24 次执行、12 次比较：RK2/RK3 与 RK3+RKL2，采样 step 1/2/5 与 `t=0.1`；最大后端场绝对差 `1.4433e-15`，仍使用 `rtol=2e-10, atol=2e-12`。[记录](gravity-amr.json) |
| 固定物理终点 | 在已保存的 6 个 CPU/CUDA 终点上复核原重力解析解与 mass/rhoX 守恒；最大解析场 Linf `6.0841e-14 < 1e-12`，质量漂移为零，组分最大相对漂移 `3.1319e-15`。[记录与复现命令](gravity-endpoints.json) |
| 动态/曲线 AMR smoke | 3 案例、5 次 CUDA 执行：Sedov 动态拓扑、柱/球坐标 AMR 扩散及恢复；只是执行/设备/相位检查，不充当独立物理误差验证。[记录](amr-runtime.json) |
| active-ENUC 耦合与重启 | Helmholtz+aprox13+BD+RKL2，热/黏性/组分输运均开启；12 条运行路线、9 次比较。两种检查点相位下的同后端/跨后端恢复均通过，所有路线均有实际 ENUC 步长约束，CPU/CUDA 连续运行均发生运行期拓扑变化。[耦合检查](active-enuc.json)、[重启证据](active-enuc-restart.json) |
| Compute Sanitizer | AMR composition 与 regrid transaction 各执行 memcheck、racecheck，共 4 次；每次确认一个实际被检测的 CUDA 进程，零错误/泄漏/竞争警告，完整报告归档于 `sanitizer/`。 |
| 工具与 Host 比较器 | 355/355 工具测试、零跳过；Host-only 比较器复核原 40 对 CPU 检查点通过，错误步数仍被拒绝。[工具日志](tooling.log)、[比较记录](host-comparator.json) |

重启比较沿用现有预算 `rtol=5e-9, atol=5e-12`，ENUC 与 burn-dt 使用原有 `1e-3`
专用预算。按各字段检查点峰值归一化的最大差为 `5.7335e-12`，ENUC 最大归一化差
`7.8936e-13`，burn-dt 最大相对差 `7.8940e-13`；四组同后端续算场量差为零。
不要求不同后端控制器逐位一致，终态强制输出的计数修正规则也保持原状，没有新增忽略项。
固定终点守恒沿用清单规定的层级归一化量及原绝对/相对预算，不改其量纲约定。

没有执行新的 Poisson/MG、FFT/Green FFT、全组合 EOS/网络/provider 验收或性能 campaign。
本包不能作为 P6 自引力 CUDA 完成或“已获得加速”的证据。GUI 前端 `studio/` 独立工作未纳入提交。
