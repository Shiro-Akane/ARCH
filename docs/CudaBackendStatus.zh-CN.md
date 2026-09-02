# CUDA 后端与 GPU-AMR 阶段性交接状态

英文权威版本：[CudaBackendStatus.md](CudaBackendStatus.md)。本文与英文版的
状态边界保持一致。

## 用途与验收用语

本文记录 CUDA 与 GPU-AMR 工作的交接状态，是阶段快照，不是生产可用性声明。
以下标签必须严格区分：

- **已完成且已验证**：所述构建或测试已在下文环境中真实完成。
- **已实现、待验证**：源码路径已经存在，并经过代码检查或部分编译资格检查，
  但尚未同时完成完整 CUDA 链接和要求矩阵的真实 GPU 运行。
- **尚未实现**：不存在可工作的 provider/路径。能够解析某个配置，或只安装第
  三方库，都不会改变这个标签。

架构原则保持不变：ARCH 只维护一套数学与物理逻辑。CUDA 复用 CPU-only 的拓扑
构造与共享数学。只有 kernel、设备内存所有权与内存池、stream、event/fence、
外部加速库 handle 等后端客观差异才允许分离。另写一套 CUDA Morton/拓扑、
EOS、重构、AMR flux 或 solver 选择数学，不属于可接受方向。

## 快照环境

- 当前工作树的 CPU 构建与测试已经完成：**17/17 测试通过**。第一次只有一个
  tooling 测试因 WSL 下 Windows `TEMP` 目录只读而失败；将 `TMPDIR`、`TEMP`、
  `TMP` 指向 `/tmp` 后通过。
- 已安装 CUDA 12.3 与 NVCC；以 GCC/G++ 12 作为 host compiler、SM 86 作为
  目标架构时，CUDA 配置成功。
- 在最终的 AMR restriction 源码上，CUDA exchange 与紧凑 AMR-flux 两个
  translation unit 均已通过 NVCC 12.3 的 compile-only 检查。这只是语法/对象
  证据，不代表 archive、链接或运行成功。
- 本机无法做真实设备验证：不存在 `/dev/dxg`，`nvidia-smi` 报告 GPU 被操作
  系统阻止访问。
- 本快照**不记录**完整 CUDA backend archive、完整应用链接或真实 GPU 运行
  成功。若干 CUDA/Host translation unit 已经编译，但资源受限构建被主动停止，
  以避免 WSL 再次失效。

## 已完成且已验证

### 共享架构与 fail-closed 分发

- 后端选择使用共享 registry/factory/capability 路径。request 先解析，再为 CPU
  或 CUDA candidate 具体化，并在 backend 构造前明确拒绝不支持的组合，不会在
  状态构造之后静默回退。
- 二元配置开关采用不区分大小写的 string；CPU 与 CUDA candidate 消费同一个
  resolved configuration。
- 线性 solver 路由不区分大小写。`Auto` 对不超过 30 个核素选择 DenseLU；更大
  的 CPU 与 CUDA candidate 分别解析为 SparseKLU 与 cuDSS。显式 CPU+cuDSS、
  CUDA+SparseKLU 会在 backend 构造前被拒绝。当前 cuDSS route 随后仍会
  fail closed，因为 provider 不存在。
- CPU 的 dispatch/capability、AMR plan 与共享数学、checkpoint 兼容与 SHA-256
  provenance、EOS/table ownership 及既有物理模块回归，均包含在 17/17 通过的
  测试中。

### 共享 AMR、restart 与物理逻辑

- AMR 拓扑、Morton index、邻居分类、regrid 决策和 migration plan 均由 CPU
  权威生成。CUDA 只接收 lower 后的 plan，不实现独立拓扑/数学栈。
- 粗细接口判据、limiter/数学 helper、面积/体积缩放、flux register 权重和
  checkpoint 语义由 CPU/CUDA 调用路径共用。
- 共享 Host checkpoint schema 已为 v3，保存 ENUC 与科学 provenance（解析后的
  EOS、gamma 或 table digest、burn/network/NSE 选择、有序 species metadata）；
  同时保留带明确限制的 v1/v2 reader。
- 既有 CPU 验证覆盖 hydro、diffusion、外部重力、单区 burn、AMR transfer/reflux
  守恒、tabular EOS、restart 及生成网络/KLU。证据边界见
  [validation/README.zh-CN.md](../validation/README.zh-CN.md)；这些 CPU 结果不能
  当作 CUDA 结果。

### 源码组织与构建控制

- 原有大型 CUDA backend 已按职责拆为 core、factory、resource、store、hydro
  control、microphysics control、exchange、AMR flux、diffusion 与 burn 单元。
- EOS device ownership 已按 species/utility 及 Helmholtz/Tabular3D/Tabular4D
  resource 拆分；tabular burn 实例按 EOS family 与内置网络（`aprox13`、
  `aprox19`、`aprox21`、`iso7`）拆分。
- CMake 已加入串行 heavy compile pool、显式阶段顺序和基于文件的 completion
  barrier，避免 Ninja 下 backend archive、solver dispatch 与应用意外重叠。
- 若存在 `ccache`，则启用 CUDA compiler cache。

## 已实现，但完整 CUDA 链接与真实 GPU 验证待完成

以下能力已有源码并进入预期 CUDA backend，但本快照不声明生产级对齐：

- 已注册 flux/reconstruction/integrator 组合的 Cartesian 1D/2D/3D hydro，
  包括 Euler、RK2、RK3 stage 处理。
- Ideal、Helmholtz、Tabular3D、Tabular4D EOS device ownership 与 dispatch。
- 四类 EOS、四个内置网络、三个 ODE solver 的全部 48 种 DenseLU burn route，
  以及支持范围内的既有 NSE route。
- Cartesian RKL1/RKL2 species diffusion。
- Current、Next、Scratch state slot 的同层与混合层 ghost exchange；GPU 粗细层
  路径使用 CPU-lowered plan 执行 device gather/average/scatter。
- PPM 在粗细层面刻意退化到共享 MUSCL-MinMod。这是稳定性/正确性规则，不声明
  整个 AMR 接口仍有三阶精度。
- Hydro Euler/RK2/RK3 紧凑 surface-flux registration 与 reflux。
- RKL1/RKL2 每 stage 紧凑 reflux，包括负 gamma 和紧凑 RKL2 `F(Y0)` cache。
- CPU 权威 regrid + transactional CUDA store 的动态 AMR：先 quiesce device，
  stage topology/store/flux plan，上传包含 ghost 的已迁移 Current state，再发布
  新 generation，最后回收旧 device resource。
- CUDA Current state 通过共享 Host writer materialize 后写 plot/checkpoint；
  restart 先读同一个 Host schema，再构建并上传 CUDA store。

这一组仍缺少必要验收：完整 clean CUDA archive/application link、真实 NVIDIA GPU
上的留存测试，以及同一输入下 CPU/CUDA 定量比较。尤其要保留混合层 Current/
Next/Scratch exchange、动态 regrid + Hydro/RKL reflux、全部支持的 EOS/network/
ODE 组合、连续运行与 split restart、CPU-to-CUDA/CUDA-to-CPU restart 互操作测试。

## 尚未实现

- **cuDSS provider：**`cuDSS` 目前可解析且参与 capability route，但不存在 CUDA
  provider、CMake package binding、resource/descriptor lifetime owner、device
  sparse assembly/active-cell compaction、analysis cache 或 solve/update 状态机。
  只安装 cuDSS 不会定义 `ARCH_HAS_CUDSS_PROVIDER`，也不会移除大于 30 核素的
  gate 或启用执行。
- 生成式 custom network 的 CUDA burn，以及超过 30 核素的 CUDA burn。
- CUDA SparseKLU（有意不支持；KLU 是 CPU provider）。
- CUDA external gravity 与非 Cartesian geometry；CUDA capability contract 会
  拒绝这些组合。
- self-gravity 与 Jeans refinement indicator 在两个当前后端中均不存在；WENO5
  也未在任一后端注册。

## 已知限制与风险

### 构建内存风险

此前 Debug 编译 Tabular4D+aprox19 CUDA translation unit 时，8 GiB WSL guest
的 available memory 曾降至约 **150 MiB**。为避免 guest 被杀，该编译被主动中断。
按功能拆分、串行 heavy lane、immutable owner 降低 debug 信息、阶段 barrier
已经减少重叠，但尚未证明 clean build 的内存上界。

交接阶段先保证正确性与保守构建成功，不强求 `--parallel 6`。在内存工作完成前，
heavy CUDA 资格检查使用 `--parallel 1`。下一阶段内存优化的硬验收标准是：
**一台 16 GiB 机器必须能稳定完成 clean Debug CUDA build 与 link，不得 OOM、
不得使 WSL 终止，也不得依赖过量 swap**。达到这一基线后再验收 `--parallel 6`；
不能把串行成功标记为并行构建成功。

下一步应记录逐 TU peak RSS，比较 Debug 与 RelWithDebInfo，检查 template
instantiation 重复，并只在改善所有权和可读性时进一步按功能提取共享 network/
EOS 设施。不能为了增加文件数量而拆分，也不能把共享数学复制成 CUDA-only 实现。

### 共享 AMR 限制

- coarse-to-fine ghost fill 当前为 piecewise constant，可能在光滑 block 接口制造
  refinement signal，并最终让周期问题全局细化。
- fine-to-coarse restriction 现在以 `sum(V rho X) / sum(V rho)` 保守传递组分。
  Host 曲线坐标 exchange 使用真实 cell volume；仅支持 Cartesian 的 CUDA 路径
  使用等价单位权。CPU 测试和 NVCC 对象编译已通过，真实设备 parity 仍待完成。
- `refine_var = ENUC` 的动态 split-run 等价性还需要 CPU v3 端到端证据与真实设备
  CUDA 证据。legacy v1/v2 checkpoint 会把 ENUC 初始化为零，无法证明该性质。

### 资格与可移植性风险

- 没有真实 GPU，CUDA runtime 行为、数值一致性、race freedom、memory-pool
  lifetime、stream/fence 顺序、regrid 失败回滚和 device memory 峰值均未验证。
- 当前本机配置为 CUDA 12.3、GCC/G++ 12、SM 86。声明可移植性前，至少还应覆盖
  一个支持的 GPU 架构及一个 Release-like 配置。
- Tabular3D/Tabular4D public burn dispatcher 以手写 built-in `NetworkId` switch
  作为编译边界。它必须保持为薄 route，physics/factory policy 仍集中维护；若在
  switch 中复制 policy，就会形成维护债务。

## 下一阶段任务清单

1. 先用 `--parallel 1` 完成 clean CUDA backend archive 与完整 ARCH 应用链接；
   在追求并行速度前修完编译/链接错误。
2. 测量每个 heavy TU 的 peak RSS，满足 16 GiB clean Debug 构建标准；随后测试
   并调优 `--parallel 6`，不得牺牲正确性或单一设施架构。
3. 在真实 GPU 上运行 focused CUDA target，再对 hydro、EOS、burn、diffusion、
   ghost exchange、AMR/reflux、regrid、output、restart 做同输入 CPU/CUDA 验证。
   在 `validation/` 中提交 metrics 与硬件/toolchain metadata；compile-only 不能
   记录为 parity。
4. 为 CUDA store publish/rollback 加 failure-injection test，并验证 regrid 与
   restart 中的 stream/fence/resource retirement。
5. 用一套共享的守恒 limited-linear interpolation 替换 piecewise-constant
   coarse-to-fine ghost fill，然后重复 CPU 与 CUDA AMR 测试；topology 与 stencil
   lowering 继续由 CPU 权威维护。
6. 只有作为真实 CUDA provider 才实现 cuDSS：包括 CMake discovery/build
   contract、RAII handle/descriptor、stream integration、可复用 analysis、device
   sparse assembly/compaction、solve/update/convergence 流程及大于 30 核素/生成
   网络启用；request resolution 继续共享。
7. 只在受支持 Cartesian 矩阵完成验证后再评估 external gravity/非 Cartesian
   CUDA 支持；在此之前继续 fail closed。

## 复现命令

以下是资格检查流程。CPU 序列在本快照中已经通过。CUDA 序列明确为待完成；在
保留完整输出之前，不能引用为成功结果。

### CPU 回归（已验证）

```bash
cmake -S . -B build-cpu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARCH_ENABLE_CUDA=OFF
cmake --build build-cpu --parallel 2
TMPDIR=/tmp TEMP=/tmp TMP=/tmp \
  ctest --test-dir build-cpu --output-on-failure -j2
```

### 保守 CUDA 编译/链接资格检查（待完成）

```bash
CC=/usr/bin/gcc-12 CXX=/usr/bin/g++-12 \
cmake -S . -B build-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARCH_ENABLE_CUDA=ON \
  -DBUILD_TESTING=OFF \
  -DARCH_ENABLE_KLU=OFF \
  -DARCH_FETCH_SUITESPARSE=OFF \
  -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/g++-12 \
  -DCMAKE_CUDA_ARCHITECTURES=86

/usr/bin/time -v \
  cmake --build build-cuda --target arch_cuda_backend --parallel 1
/usr/bin/time -v \
  cmake --build build-cuda --target ARCH --parallel 1
```

记录 `free -h`、swap 使用量、`/usr/bin/time -v` 的 peak RSS、compiler version
和第一条失败命令。串行编译与 16 GiB 标准通过后，再以 `--parallel 6` 重做 clean
build 并单独记录。

### 真实设备验证（待完成；快照主机不可用）

```bash
nvidia-smi
test -e /dev/dxg

cmake -S . -B build-cuda -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DARCH_ENABLE_CUDA=ON \
  -DBUILD_TESTING=ON \
  -DCMAKE_CUDA_ARCHITECTURES=86
cmake --build build-cuda --target arch_cuda_single_level_validation --parallel 1
TMPDIR=/tmp TEMP=/tmp TMP=/tmp \
  ctest --test-dir build-cuda --output-on-failure -R cuda
```

GPU 不存在/不可访问、测试 skip、CMake generation 成功，或只有 compile 成功但
final link 未完成，都不能算真实设备通过。
