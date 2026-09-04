# CUDA 后端与 GPU-AMR 验收状态

英文原文：[CudaBackendStatus.md](CudaBackendStatus.md)。英文版是规范文本。

状态日期：2026-09-05。本工作树基于 GPU-AMR 交接提交 `21d6b2c`，并包含下述收尾与验收改动。

## 验收摘要

- 保持 CPU-authoritative topology / Host-lowered CUDA 架构；CUDA 专属代码仅限 kernel、device storage、stream、fence、资源退休与 launch routing。
- 在强制 16 GiB 内存、禁用 swap 的环境中，clean Debug CUDA backend archive 和完整 `ARCH` 可执行文件均已编译、链接成功。
- NVIDIA H100-20C（SM90）上的 10 个生产级 CPU/CUDA AMR 算例、27 个比较检查点全部通过；CPU/CUDA 最大 field-normalized 差异为 `9.99201e-16`。
- Smooth 与 ENUC 驱动的 restart 矩阵均通过 CPU→CPU、CUDA→CUDA、CPU→CUDA、CUDA→CPU，以及 CPU/CUDA 连续运行对比。
- 五个聚焦 CUDA AMR 测试在 `CUDA_LAUNCH_BLOCKING=1` 下全部通过。
- 首次完整 65 项 CUDA CTest 在外部 GPU 争用下通过 52 项；显存隔离后的首轮重跑通过 10/13，并暴露出三个确定性的 burn CPU/CUDA 精度问题。统一补偿 `double` 求和、按实测误差校准单一路由预算后，Burn policy 16/16、原 13 项 13/13、完整 CTest 65/65 均已通过；工具测试 72/72 通过。
- 当前 NVIDIA vGPU 禁用了 GPU debugging，`compute-sanitizer` memcheck/racecheck 无法插桩。尝试日志已经保存，本状态不宣称 sanitizer 通过。

机器可读证据、精确构建记录和测试日志位于
[`validation/amr/results/h100-sm90-20260903/`](../validation/amr/results/h100-sm90-20260903/)。

## 已完成实现

### 共享 AMR 数学

- 混合层 exchange 支持 1D/2D/3D、X/Y/Z 面及 `Current`、`Next`、`Scratch`。
- coarse→fine 使用一套共享的守恒 limited-linear prolongation；组分先重构 `rho X`，fine→coarse restriction 保持守恒，Host 路径使用物理单元体积。
- PPM 在粗细界面退化为 MUSCL-MinMod；CPU/CUDA 使用同一套界面判据与重构数学。
- ENUC 与五个流体守恒场均参与 exchange 和 topology migration。

### Reflux 与时间推进

- Hydro reflux 覆盖 Euler、RK2、RK3，stage 权重分别为 `1`、`1/2, 1/2`、`1/6, 1/6, 2/3`。
- RKL1/RKL2 每个 stage 都登记并 reflux，包含负 `gamma`；RKL2 的 `F(Y0)` 使用紧凑 surface cache，且不会跨时间步复用。
- Flux register 为面规模；flux scratch 覆盖前完成登记，最终 slot rotation 后对 `Current` reflux。

### 动态 topology 与生命周期安全

- refine/derefine、Morton topology、邻居与迁移计划保持 CPU-authoritative。
- CUDA 在上传 quiesce 后，将 topology、device blocks、handle、reflux plan 作为同一 staged generation 发布；失败时旧 generation 保持有效，旧存储只在 fence 后退休。
- failure injection、stale handle 和 store lifecycle 已有 CUDA 测试覆盖。

### EOS、burn、network 与 restart

- 自由能插值和热力学闭合集中在 `TabularFreeEnergyMath.h`；Host 负责 HDF5、导数表与所有权，CUDA 只负责上传和 view 生命周期。
- 3D/4D 表格 EOS 温度迭代现以当前温度尺度判断 Newton 增量；Direct 和 free-energy EOS 使用有限的 CPU/CUDA 容差，没有复制 device 数学。
- 内置 burn/network 使用统一注册表；不可用或不具 device 能力的 route fail-closed。外部 KLU 仍为 CPU-only。
- Ye、Timmes RHS/Jacobian/温度能量、ODE 能量闭合及 NSE 守恒量统一使用固定顺序的补偿 `double` 求和，消除了 Host `long double` 与 CUDA `double` 的隐式分歧；仅 aprox19 ROS4 使用逐字段、基于实测最大误差加 25% 余量的预算，没有全局放宽容差。
- checkpoint schema v3 为 CPU/GPU 共用，保存 ENUC、EOS/table SHA-256、burn/network/NSE 状态和 species metadata；CUDA 先 materialize `Current`，再调用 Host writer。

## 已验证环境与构建边界

| 项目 | 记录值 |
| --- | --- |
| GPU | NVIDIA H100-20C，compute capability 9.0，20,480 MiB |
| Driver / CUDA toolkit | 570.133.20 / 12.8.93 |
| Host compiler | GCC 11.4.0（该主机没有 `g++-12`） |
| 构建工具 | CMake 4.4.0、Ninja 1.13.2 |
| 内存边界 | systemd user scope，`MemoryMax=16G`、`MemorySwapMax=0` |
| CUDA archive | 176,691,142 bytes；SHA-256 `9fa494d579c5450265789b19a16e83c4c2ba380785980a4fef8a11a9f81a4eb8` |
| `ARCH` executable | 204,248,832 bytes；SHA-256 `e903232e298ee9ea2cc958a404fe370a305463c8008ec5969b4adb9430f4f8e2` |

原始串行 clean archive 用时 `1:11:37`，最大记录 RSS `4,466,132 KiB`；补偿求和改动后的串行 backend 重编译用时 `1:13:21`，最大 RSS `4,752,228 KiB`；最终增量 `ARCH` 链接用时 `1:06.81`，最大 RSS `1,206,036 KiB`；全部 CUDA 测试 target 的原始编译用时 `20:31.74`，最大 RSS `4,807,348 KiB`。所有记录的 swap 均为零。`tu-memory-samples.csv` 保存按 1 秒采样、归因到活跃 CUDA TU 的 cgroup 内存；它包含构建 scope 开销，不能解释为单一编译器进程的独占 RSS。

要求的 `--parallel 1` 基线已经完成。Clean `--parallel 6` 属于吞吐优化，不是已建立的安全内存基线，本页不宣称其已通过。

## 真实 GPU 验证矩阵

生产 manifest 覆盖：

- 1D Hydro AMR 的 Euler、RK2、RK3；
- 多步 refine/derefine topology cycle；
- RKL1 及 RKL2 的 2、3、5 stage diffusion AMR；
- RKL2 负 `gamma` 和 `F(Y0)` cache 生命周期；
- 2D、3D RK3 AMR；
- 每个选定步的 CPU/CUDA checkpoint parity 与守恒检查。

独立 CUDA 测试覆盖所有维度、面方向、粗细两侧及 `Current` / `Next` / `Scratch` exchange。生产 manifest 与合成 kernel 测试的证据分开保存。

Restart 验证同时覆盖 SmoothAdvection 与 `refine_var = ENUC`。Smooth 的五条 route 在所报比较点位完全相等；ENUC 矩阵通过预先声明的 scale-aware tolerance，最大 normalized field/ENUC 差异为 `6.71411e-4`。

## 尚未闭合的资格项

1. 在 bare-metal 或开放 CUDA debugging 的 vGPU profile 上执行 memcheck/racecheck；当前 H100 vGPU 无法满足此项。
2. 仅在需要并行构建吞吐数据时记录 clean `--parallel 6`；不得削弱 16 GiB、零 swap 验收边界。

这些是资格边界，不改变 AMR 数学，也不允许跳过网络、使用假 validator 或放宽容差。

## 明确不纳入本轮 GPU-AMR 收尾

以下属于后续 CUDA 能力扩展，不阻塞 Cartesian GPU-AMR：cuDSS provider、超过 30 核素的 CUDA burn、generated custom CUDA networks、CUDA external gravity、CUDA cylindrical/spherical geometry、self-gravity/Jeans indicator 和 WENO5 注册。

## 复现

~~~bash
cmake -S . -B build-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARCH_ENABLE_CUDA=ON \
  -DBUILD_TESTING=OFF \
  -DARCH_ENABLE_KLU=OFF \
  -DARCH_FETCH_SUITESPARSE=OFF \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_ARCHITECTURES=90

cmake --build build-cuda --target arch_cuda_backend --parallel 1
cmake --build build-cuda --target ARCH --parallel 1

python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_cases.json \
  --arch ./bin/ARCH \
  --checkpoint-validator ./build-cuda/arch_cuda_single_level_validation \
  --source-root . \
  --output-root /tmp/arch-gpu-amr-validation
~~~
