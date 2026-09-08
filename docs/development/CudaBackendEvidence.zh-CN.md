# CUDA 后端审查历史与证据

贡献者档案：保留中间状态、机型相关观察及已经过时的描述。当前功能与限制见
[后端能力](../CudaBackendStatus.zh-CN.md)。本档案不构成当前候选版本的验收。

英文原文：[CudaBackendEvidence.md](CudaBackendEvidence.md)。英文版是规范文本。

状态日期：2026-09-05。本工作树包含针对 `2226456` 的审查修复。

随后进行的共享 AMR、稀疏求解与坐标重构单独记录在
[CudaRefactorSmoke.md](CudaRefactorSmoke.md)。新实现覆盖下文部分旧能力边界，
但不能继承旧版本的验证结果；当前仍处于短 smoke 集成检查阶段。

## 本轮修复与验收边界

请注意，下文展示的 H100 数据纯属历史结果，并不代表当前工作树的最终验收。具体来说，三份生产级 AMR 与 restart 的 JSON 报告所指向的二进制文件为 `d42711fa…`，而历史最终重链接的产物则为 `e903232e…`。这些原始数据保持原样，仅仅作为历史证据存档，绝对不得被篡改或误导为新代码已通过的运行结果。

- `arch_build_contract` 统一管理 C++ 和 CUDA Host/Device 的严格浮点编译与链接语义，
  禁止重结合、隐式 FMA 收缩和 flush-to-zero 破坏补偿求和。Debug/Release
  使用同一数学实现，不另设 CPU/CUDA 求和公式。
- `LimitedLinearProlongation.h` 统一组分重构、兄弟组合法性、先求和再相减的闭合及
  常数组分回退。后端只绑定存储、执行传输；非法密度在两端均导致整批计划拒绝写入。
- 从冻结 main `8c76be8` 提取不可变 burn 参考数据，并固定 Helmholtz 表身份。
  12 条较长时间步路由独立检查 CPU 行为；CUDA parity 先核对这一基线，再比较两端。
  没有复制旧物理公式，也没有自动刷新基线的入口。
- 两个运行验证器共用 `validation_provenance.py`；源码、运行数据、构建配置、ARCH
  及 checkpoint 比较器身份在运行前后核对。最终由 `qualify_cuda_amr_evidence.py`
  拒绝不完整或身份不一致的证据。

本机限定范围验证和待办记录在
[`validation/amr/results/local-fixes-20260905/`](../../validation/amr/results/local-fixes-20260905/)。
CPU Debug CTest 已通过 21/21；Debug GPU burn policy 已通过 16/16，未放宽
parity 预算。Release 聚焦 CPU/GPU 检查通过 5/5；两个 GPU 数学测试在两种配置下
均通过 memcheck/racecheck。完整运行矩阵及 Release GPU burn 复测仍未完成。
小型 kernel 通过不等于整套 ARCH、AMR/restart、sanitizer 或 16 GiB 长程容量已验收。

## 历史 H100 验收摘要

- 保持 CPU-authoritative topology / Host-lowered CUDA 架构；CUDA 专属代码仅限 kernel、device storage、stream、fence、资源退休与 launch routing。
- 在强制 16 GiB 内存、禁用 swap 的环境中，clean Debug CUDA backend archive 和完整 `ARCH` 可执行文件均已编译、链接成功。
- NVIDIA H100-20C（SM90）上的 10 个生产级 CPU/CUDA AMR 算例、27 个比较检查点全部通过；CPU/CUDA 最大 field-normalized 差异为 `9.99201e-16`。
- Smooth 与 ENUC 驱动的 restart 矩阵均通过 CPU→CPU、CUDA→CUDA、CPU→CUDA、CUDA→CPU，以及 CPU/CUDA 连续运行对比。
- 五个聚焦 CUDA AMR 测试在 `CUDA_LAUNCH_BLOCKING=1` 下全部通过。
- 首次完整 65 项 CUDA CTest 在外部 GPU 争用下通过 52 项；显存隔离后的首轮重跑通过 10/13，并暴露出三个确定性的 burn CPU/CUDA 精度问题。统一补偿 `double` 求和、按实测误差校准单一路由预算后，Burn policy 16/16、原 13 项 13/13、完整 CTest 65/65 均已通过；工具测试 72/72 通过。
- 当前 NVIDIA vGPU 禁用了 GPU debugging，`compute-sanitizer` memcheck/racecheck 无法插桩。尝试日志已经保存，本状态不宣称 sanitizer 通过。

来源检查使用的固定矩阵／重启记录现归入[协议 fixture](../../tests/fixtures/validation_provenance/README.md)，
保留实际记录的身份，用于解析与产物身份不匹配的拒绝控制，不认证当前源码的 H100
科学计算。有效验收数据见 [Validation](../../validation/README.zh-CN.md)。

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
- Ye、Timmes RHS/Jacobian/温度能量、ODE 能量闭合及 NSE 守恒量统一使用固定顺序的补偿 `double` 求和，消除了 Host `long double` 与 CUDA `double` 的隐式分歧。短步 policy 测试保留既有的逐路由/逐字段预算（包括 aprox19、aprox21）；历史 H100 收尾中曾将 aprox19 ROS4 校准为实测最大误差加 25% 余量。这些预算与新增冻结 main 基线独立固定的 ODE 误差/舍入准则分开，本轮不放宽既有 CPU/CUDA parity 预算。
- CPU/GPU 共用 ARCH 检查点，保存原始组分、ENUC、控制器状态、EOS/表 SHA-256、燃烧/网络/NSE 状态和组分信息；CUDA 先将 `Current` 同步到主机，再调用共用写入器。

## 历史已验证环境与构建边界

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

历史 `--parallel 1` 构建基线已完成。Clean `--parallel 6` 属于吞吐优化，本页不宣称其已通过，也不将旧构建结果用于关闭当前修复后的整库验收。

## 生产验证矩阵与历史结果

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

1. 分别构建最终 Debug/Release ARCH 和比较器，重跑完整 CTest、AMR 和两套 restart，
   通过最终身份/覆盖门禁；不能复用旧 H100 数据关闭此项。
2. 补齐完整运行路径的 memcheck/racecheck。H100 vGPU 禁用了调试。本机最初的
   WDDM 错误已解决：用户在 Windows 管理员终端启用了 `GPUDebugger/EnableInterface`，
   无需重启，补偿求和和 AMR 组分两个测试在 Debug/Release 下的 memcheck/racecheck
   共 8 次均通过，未报告错误、泄漏或 hazard。这仅解除本机接口阻碍，
   不代表完整运行路径已通过 sanitizer。
3. 在 16 GiB Host RAM 及明确的显存预算内验证代表性 AMR/burn/restart 长程负载；
   串行编译峰值不证明运行容量、更多细化层级或数值收敛性。
4. 仅在需要并行构建吞吐数据时记录 clean `--parallel 6`；不得削弱 16 GiB、零 swap 验收边界。

这些是资格边界，不改变 AMR 数学，也不允许跳过网络、使用假 validator 或放宽容差。

## 后续重构后的范围

cuDSS、可在 device 调用的生成网络及大型 burn、device AMR 数值迁移和圆柱/球坐标
现已纳入后续实现与 smoke 工作，见 [CudaRefactorSmoke.md](CudaRefactorSmoke.md)，
不能视为已被上述历史验收覆盖。2026-09-06 的[发布重构](CudaReleaseStandard.md)
已接入共用 external gravity CUDA 源项、修正共用几何/CFL 和 AMR 舍入策略，
最终生产资格仍待验证。自重力/Jeans、生成网络中
弱反应率表（包括生成的不可变数组）的 device owner 及 WENO5 注册仍不属于已实现的 CUDA 能力。

## 复现

~~~bash
cmake -S . -B build-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARCH_ENABLE_CUDA=ON \
  -DBUILD_TESTING=ON \
  -DARCH_ENABLE_KLU=OFF \
  -DARCH_FETCH_SUITESPARSE=OFF \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-11 \
  -DCMAKE_CUDA_ARCHITECTURES=90 \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-cuda/bin"

cmake --build build-cuda --target arch_cuda_backend --parallel 1
cmake --build build-cuda --target ARCH arch_cuda_single_level_validation --parallel 1

python3 tools/validate_backend_results.py \
  --manifest validation/amr/gpu_cases.json \
  --arch ./build-cuda/bin/ARCH \
  --checkpoint-validator ./build-cuda/arch_cuda_single_level_validation \
  --build-dir ./build-cuda \
  --source-root . \
  --output-root /tmp/arch-gpu-amr-validation
~~~

Release 必须使用独立的 build/output 目录。两套 restart 和最终聚合门禁的完整命令见
[AMR README](../../validation/amr/README.md)。
