# ARCH 运行时分发与可扩展后端架构

## 1. 背景与目标

ARCH 希望只编译一次可执行文件，随后通过 `.par` 文件在运行时切换网格参数、时间积分器、通量格式、重构方法、限制器、EOS、重力、燃烧网络、ODE/线性求解器以及 CPU/CUDA 后端。这个目标本身是合理的，但“运行时可切换”不等于必须把所有策略类型作为一个巨大模板类型传入同一个主循环。

当前架构把所有选择嵌套在一条泛型 lambda 和模板分发链中：

```text
EOS
  -> Gravity
    -> Burner(Network, ODE, LinearSolver)
      -> TimeIntegrator
        -> Flux
          -> Reconstruction/Limiter
            -> run_simulation<全部策略类型>
```

编译器必须生成上述集合的笛卡尔积。本设计的目标是在保持 `.par` 运行时切换能力的同时，把模板限制在真正需要内联和静态优化的数值核中；Driver、注册表和策略选择本身使用稳定的非模板 ABI。

## 2. 已量化的现状

基于 Release/O3 CPU 构建的审计结果：

- `src/main.cpp` 直接包含原重型 `src/driver/SolverDispatch.h`。
- `src/main.cpp.o` 大小为 `83,419,680` bytes。
- 最终 `bin/ARCH` 大小约为 `47,607,344` bytes。
- 同一对象内实际生成约 3,600 个 `run_simulation` 模板特化。
- 加上 Driver 内日志 lambda 和 OpenMP outlined function，每个组合约出现三个相关符号，共约 10,800 个符号。
- 相比之下，`ProblemHelper.cpp.o` 约为 `333,688` bytes，网络 `.cpp` 对象仅为 KB 级；编译负载高度集中在一个翻译单元。

当前已实现组合数可由下式解释：

```text
4 EOS
x 2 Gravity（none / external）
x 5 Burner（Dummy + iso7 + aprox13 + aprox19 + aprox21）
x 3 TimeIntegrator（Euler / RK2 / RK3）
x 5 Flux（VL / SW / Roe / HLL / HLLC）
x 6 Reconstruction 变体（PCM / PPM / 4 个 MUSCL limiter）
= 3,600
```

这不仅使 `main.cpp` 的编译占满 I/O 和内存，也意味着修改 Driver、ODE 或某个公共头时，编译器可能重新处理大量与该修改无关的组合。

### 2.1 P0/P1 当前状态

P0/P1 已把 `SolverDispatch.h` 缩为只含前置声明和非模板 `DispatchSolver()` 的轻量头，并把原有模板实现移入 `SolverDispatch.cpp`。同时统一了时间积分器参数：

- 首选新键 `time_integrator`；
- 兼容旧键 `timeintegrator`；
- 分发端只读取 `config.numerics.time_integrator`。

这一步使 `main.cpp` 不再直接实例化整个策略矩阵，显著改善入口文件和普通使用方的增量编译依赖。

JAIST 上本次 Debug 低并发构建的实测也显示了负载转移：`main.cpp.o` 为
`1,131,400` bytes，而 `SolverDispatch.cpp.o` 为 `374,861,704` bytes。该数据
不能与旧 Release 对象大小直接比较，但足以证明入口已经变轻、组合矩阵仍集中在
单个翻译单元；因此它是隔离步骤，不是最终的编译量优化。

必须明确：P0/P1 只是把重模板实例化从 `main.cpp` 隔离到 `SolverDispatch.cpp`，尚未减少 3,600 个组合的总数。完整 Release 构建中，负载仍会集中到新的 `SolverDispatch.cpp`。真正降低总实例化数量需要继续完成 P2 至 P5。

## 3. 设计原则

1. 运行时选择只发生在粗粒度边界，不能在每个 cell、每条反应或每个矩阵元素上反复做字符串比较。
2. Flux、Reconstruction 和具体 EOS 的热循环继续使用模板，使编译器保留内联、常量传播和 SIMD 优化机会。
3. 时间积分器只负责 RK stage 编排，应通过函数指针调用空间 RHS；一次 stage 一次间接调用的成本可忽略。
4. Burner 与流体空间离散独立分发，不能让 Network 类型乘入所有流体求解器组合。
5. CPU 和 CUDA 共用同一份配置语义和能力注册表，但设备端数值核保持静态模板；不得在 CUDA device code 中使用 host virtual function、`std::function` 或字符串分发。
6. 不支持的组合必须显式失败，不能静默回退到另一网络、另一求解器或 CPU。
7. 每个已编译能力都应可查询、可打印、可测试。

## 4. 目标架构

### 4.1 RunSelection 与能力注册表

所有 `.par` 字符串在启动阶段规范化为枚举，形成只读的 `RunSelection`：

```cpp
struct RunSelection {
    Backend backend;
    EosKind eos;
    GravityKind gravity;
    IntegratorKind integrator;
    FluxKind flux;
    ReconstructionKind reconstruction;
    LimiterKind limiter;
    NetworkKind network;
    OdeKind ode;
    LinearSolverKind linear_solver;
};
```

注册表使用枚举 key 查找已经编译进程序的函数入口。字符串大小写、别名和旧参数兼容只在解析阶段处理一次。建议提供 `ARCH --list-capabilities` 或等价输出，列出当前二进制支持的 CPU/CUDA、EOS、网络和数值格式组合。

注册表应为显式中央注册表或编译期数组，避免依赖静态构造顺序。如果使用静态库，未被引用的自动注册对象可能被链接器丢弃；对象库或显式引用可以避免这一问题。

### 4.2 非模板 Simulation Driver

`run_simulation` 应变成单一非模板实现，只持有下列稳定句柄：

```text
SimulationDriver
  - EosContext
  - SpatialOperator
  - RuntimeIntegrator
  - GravityOp
  - BurnerHandle
  - Grid / FluidState / SimConfig / SpeciesManager
```

Driver 继续负责：

- 时间步和 CFL 管理；
- Strang splitting 或燃烧调用时序；
- I/O、checkpoint 和 restart；
- OpenMP/MPI 层面的全局调度；
- 错误传播和诊断。

Driver 不应知道具体的 `NetAprox19`、`FluxHLLC<...>` 或 `HelmEos` C++ 类型。

### 4.3 SpatialRhs 显式实例化

空间离散是最需要静态优化的部分。建议保留：

```cpp
template <typename Flux, typename Reconstruction, typename Eos, Backend B>
void evaluate_spatial_rhs(/* stable buffers and typed EOS context */);
```

每个显式实例化由一个非模板 launch wrapper 暴露为 `SpatialRhsFn`。函数指针在一个完整 RK stage 外层调用一次，进入 wrapper 后所有 cell/face 循环仍是具体模板类型。

时间积分器不再作为 `SpatialRhs` 的模板参数。Euler、RK2、RK3 使用同一个 `SpatialOperator`，分别调用一、二、三次 RHS 并组合 stage buffer。这样立即去掉 integrator 维度的乘法。

按当前能力估算：

```text
5 Flux x 6 Reconstruction/Limiter x 4 EOS = 120 个 CPU SpatialRhs 特化
```

如果某些 Flux 与 Reconstruction 实际不兼容，只注册有效组合，数量还可继续下降。CUDA 后端拥有独立的一组 kernel launch wrappers，不应与 CPU 特化放在同一巨大翻译单元。

### 4.4 BurnerHandle

燃烧应从流体模板链中拆出。`BurnerHandle` 保存不可变 context 和一个粗粒度 integrate 函数入口：

```cpp
struct BurnerHandle {
    void *context;
    BurnResult (*integrate)(void *context,
                            CellBurnState &state,
                            const EosRuntimeView &eos,
                            const BurnConfig &config);
};
```

实际 adapter 仍可显式实例化：

```cpp
BurnerAdapter<NetAprox19, Solver_BE_NR, DenseLUSolver, HelmEos, Backend::CPU>
```

这样 Network、ODE 与线性求解器只乘入 Burner，而不会乘入 Flux、重构和 RK。当前只有四个网络和一个已实现的 ODE/线性求解器；按四种 EOS 计，约为 16 个 Burner 特化。一次 cell burn 只有一次间接调用，相比刚性 ODE、Jacobian 和 LU 分解的计算成本通常可忽略。

CUDA burner 同样由 host registry 选择 launch wrapper，网络 RHS/Jacobian 和矩阵核本身继续是 device 可编译的静态模板。

### 4.5 EosContext

EOS 同时被流体热循环、燃烧、初始化和 I/O 使用，需要两种视图：

1. SpatialRhs wrapper 内部使用具体 EOS 类型，保留 cell 内调用的内联能力。
2. Driver、I/O 和 BurnerHandle 使用稳定的 `EosRuntimeView`/函数表。

`EosContext` 必须拥有底层 manager 及其 table/view 的完整生命周期。当前 `EOSDispatcher` 在泛型 lambda 返回后销毁局部 manager；重构时不能保存指向该局部对象的裸指针。建议使用拥有型 context，例如 `std::unique_ptr` 加明确 deleter，或一个受控 variant 存储。

EOS 函数表必须只读且线程安全，以便 OpenMP 多线程共享。对 CUDA，应另外提供设备可复制的 POD view；host EOS 对象和 HDF5 manager 不可直接传入 device。

### 4.6 GravityOp

Gravity 不应成为整个 Driver 的模板参数。建议把重力源项作为独立的 `GravityOp`：

- `GravityNone`：空操作；
- `ExternalGravity`：读取 POD 参数并更新源项；
- 未来的 self-gravity：在单独模块中完成 Poisson 求解后提供源项场。

GravityOp 在每个 stage 的源项阶段调用，不与 Network、Burner 或 TimeIntegrator 形成模板笛卡尔积。如果为追求 CPU 热循环内联，可为少数 gravity 类型提供独立函数入口，但仍不要把它乘入 Burner。

## 5. 建议文件边界

```text
src/
  driver/
    SolverDispatch.h             # 轻量公共入口
    SolverDispatch.cpp           # 解析选择并组装运行时句柄
    SimulationDriver.h           # 非模板接口
    SimulationDriver.cpp         # 唯一主循环实现

  runtime/
    RunSelection.h               # 规范化枚举和组合 key
    CapabilityRegistry.h
    CapabilityRegistry.cpp       # 显式中央注册表

  numerics/
    hydro/
      SpatialOperator.h          # SpatialRhsFn / 稳定 buffer 接口
      SpatialKernel.h            # 热循环模板定义
      instances/
        SW_Ideal.cpp
        SW_Helm.cpp
        HLLC_Ideal.cpp
        HLLC_Helm.cpp
        ...                      # 每 TU 包含少量显式实例化
    integrator/
      RuntimeIntegrator.h
      RuntimeIntegrator.cpp      # Euler/RK2/RK3 非模板 stage 编排
    burnsolver/
      BurnerHandle.h
      BurnerRegistry.cpp
      instances/
        BurnIso7Cpu.cpp
        BurnAprox13Cpu.cpp
        BurnAprox19Cpu.cpp
        BurnAprox21Cpu.cpp
        BurnAprox19Cuda.cu
        ...

  physics/
    eos/
      EosContext.h
      EosContext.cpp
      EosRegistry.cpp
    gravity/
      GravityOp.h
      GravityOp.cpp
```

实例化声明可使用 `extern template`，定义只出现在指定 `.cpp`/`.cu` 中，防止其他翻译单元再次隐式生成相同代码。实例化文件应按 Flux+EOS 或 Network+Backend 分组，避免重新形成一个 jumbo TU。

## 6. CMake 组织

停止用单一 `GLOB_RECURSE` 把所有源文件直接塞入可执行目标。建议使用显式源文件列表和分层目标：

```cmake
add_library(arch_build_options INTERFACE)
target_compile_features(arch_build_options INTERFACE cxx_std_20)

add_library(arch_core STATIC ...)
add_library(arch_eos STATIC ...)
add_library(arch_hydro_cpu OBJECT ...)
add_library(arch_burn_cpu OBJECT ...)
add_library(arch_networks OBJECT ...)
add_library(arch_problems OBJECT ...)

add_executable(ARCH src/main.cpp)
target_link_libraries(ARCH PRIVATE arch_core arch_eos ...)
target_sources(ARCH PRIVATE
    $<TARGET_OBJECTS:arch_hydro_cpu>
    $<TARGET_OBJECTS:arch_burn_cpu>
    $<TARGET_OBJECTS:arch_networks>
    $<TARGET_OBJECTS:arch_problems>)
```

建议：

- 重模板目标关闭 Unity Build；Unity 会重新制造巨大 TU。
- PCH 只放稳定的标准库、`FluidState`、`Grid` 和公共 EOS 接口，不放频繁修改的 Timmes rate、Network 或总分发头。
- Ninja job pool 限制同时进行的重模板编译数，例如 4，避免再次打满 I/O 和内存。
- `ccache`/`sccache` 改善重复和增量构建，但不能替代减少模板组合。
- CPU 与 CUDA 对象库分开，CUDA OFF 时不配置或链接任何 CUDA 对象。
- 保持 C++20 设置唯一且一致，不再同时声明 C++17 feature。

## 7. CUDA 两层开关

CUDA 必须有编译期和运行时两层选择。

### 7.1 编译期开关

```cmake
option(ARCH_ENABLE_CUDA "Build CUDA backend" OFF)
```

- `OFF`：构建纯 CPU 二进制，不依赖 CUDA toolkit/runtime。
- `ON`：编译 CUDA 对象并把 CPU 与 CUDA capability 同时链接进同一个二进制。

编译期开关决定“这个二进制是否具备 CUDA 能力”，不能由 `.par` 在运行时补救。

### 7.2 `.par` 运行时开关

建议使用：

```text
compute_backend = auto   # cpu | cuda | auto
cuda_device = 0
```

语义：

- `cpu`：始终选择 CPU/OpenMP/MPI 路径，即使二进制包含 CUDA。
- `cuda`：要求 CUDA 已编译、运行时可用、设备存在，且所选组合已注册；任一条件不满足立即报错退出。
- `auto`：仅在 CUDA 能力完整且组合受支持时选择 CUDA，否则使用 CPU，并打印一次明确的选择原因。

建议额外允许更细粒度配置，例如 `hydro_backend` 与 `burn_backend`，但第一阶段应先保证单一 `backend` 的一致语义，避免每一步发生隐式 host/device 拷贝。

### 7.3 Fail-loud 要求

以下情况必须拒绝运行并列出可用能力：

- CUDA OFF 构建收到 `compute_backend=cuda`；
- 无可用 GPU 或 CUDA runtime 初始化失败；
- 所选 Network/ODE/LinearSolver 没有 CUDA 实现；
- 所选 EOS 没有 device view；
- restart 文件和 network/species/backend 元数据不兼容；
- 用户请求尚未实现的 SparseKLU、ROS4 或 self-gravity。

禁止静默切换网络、改变核素数、关闭燃烧、退回数值 Jacobian 或切换 CPU。只有 `backend=auto` 可以按已记录的规则回退 CPU。

## 8. `.par` 运行时切换语义

一个已经包含相应 capability 的二进制，应支持无需重新编译的切换：

```text
time_integrator = RK2
solver = HLLC
reconstruct = muscl
limiter = mc

eos_type = helmholtz
gravity_type = none

use_burn = 1
network_name = aprox19
ode_solver = BE_NR
linear_solver = DenseLU

backend = cpu
```

解析阶段完成：

1. 参数别名归一化；
2. 枚举转换；
3. 组合合法性检查；
4. capability 查找；
5. species/network 元数据一致性检查；
6. 输出最终选中的完整策略。

运行阶段不再读取策略字符串。网格尺寸、几何和边界目前本来就是运行时数据，`Grid` 不是模板爆炸来源，不需要为了本次重构模板化。

## 9. 迁移计划

### P0：基线和参数一致性

- 保存四网络 CPU/OpenMP 的物理误差、HDF5 输出和性能基线。
- 统一 `time_integrator`，兼容旧 `timeintegrator`。
- 所有未知组合 fail-loud。

状态：已完成参数键统一。

### P1：隔离入口 TU

- `SolverDispatch.h` 改为轻量非模板接口。
- 原实现移动到 `SolverDispatch.cpp`。
- 确认 `main.cpp` 不再包含重网络、ODE、EOS 和 flux 模板。

状态：已完成。注意总特化数尚未降低。

### P1.5：已完成的模板乘积解耦与构建实测

当前实现进一步引入了 `BurnerHandle<EosPolicy>`。启动阶段仍由静态模板生成并校验
`Network × ODE × LinearSolver` 的具体 burner；进入主时间循环后，流体驱动只保存一个
粗粒度函数指针句柄。一次函数指针调用对应“一个单元的一次完整燃烧积分”，不会进入
Newton、Jacobian 或 LU 的内层循环，因此热路径内部仍是可内联的静态模板。

这一步把 burner 的约 16 个有效组合从
`EOS × Gravity × Integrator × Flux × Reconstruction` 的乘积中拿了出来。它没有取消
`.par` 切换；恰恰相反，`.par` 只在启动时选择并构造句柄，编译完成后仍可切换网络、
ODE 和线性求解器。

JAIST 节点上的 Release/O3 证据如下：

- 旧的嵌套式 `SolverDispatch.cpp` clean build 运行超过 16 分钟仍未产生对象，现场观察
  `cc1plus` RSS 已超过 17.3 GB，因此主动终止；
- 最终纯 CPU Release/O3 clean build（`ARCH`、`-j2`）为 `4:27.72`，峰值 RSS
  `2,150,564 KiB`；配置阶段另为 `6.82 s`；
- `SolverDispatch.cpp.o` 为 `20,832,896` bytes，`main.cpp.o` 为 `135,512` bytes，
  最终纯 CPU 可执行文件为 `12,902,040` bytes；
- 最终 CUDA fat-binary 在网络头真实变化后的主目标增量重建为 `4:22.95`，峰值 RSS
  `2,159,500 KiB`；CUDA runtime 静态链接，动态依赖中没有 `libcudart.so`；
- 相对旧结构超过 16 分钟且现场 RSS 已超过 17.3 GB 的未完成构建，最终 CPU clean build
  至少快 3.6 倍，峰值内存约降低 88%；旧构建因为被主动终止，不能把该时间比当作精确上界；
- 8 × 2 的真实 Helmholtz/aprox13 Cellular 燃烧 smoke 通过，species count 为 13，输出
  `sum(X)` 保持 1，且 Ne20/C12 等组分实际发生变化，证明句柄没有绕过燃烧。

该阶段还把 `main.cpp`、runtime registry、problem sources、CUDA runtime 与重分发对象拆成
独立 CMake 源清单；只有 `arch_solver_dispatch` 使用 PCH。不同 build tree 的可执行文件写入
各自的 `<build-dir>/bin`，避免 CPU/CUDA 构建互相覆盖。CUDA validation/benchmark
可执行文件全部标记为 `EXCLUDE_FROM_ALL`，日常构建 `ARCH` 时不会额外重复编译重网络头。
下一步 P2 仍需要把 hydro 的约
120 个有效空间算子拆成显式实例化对象，才能得到稳定、可测的 network/flux 单文件增量编译边界。

### P2：抽出 SpatialRhs 和运行时积分器

- 定义稳定的 `SpatialOperator` 接口。
- 将 Euler/RK2/RK3 改成非模板 stage 编排。
- 对 CPU 空间核做显式实例化和 registry。
- 对 P1 前后 CPU 结果做逐位或严格误差回归。

预期：去掉 integrator、burner 和 gravity 对 hydro 模板的乘法，是减少总实例化的核心阶段。

### P3：拆分 EosContext、GravityOp 和 BurnerHandle

- 明确 EOS manager/view 所有权。
- 将 gravity 作为独立 source operation。
- 每个 Network/ODE/LinearSolver 建独立 burner adapter。
- 合并 `ProblemHelper` 与 `BurnDispatcher` 中重复维护的 network switch，改由 NetworkRegistry 提供 species 和 factory。

### P4：CMake 对象库和显式实例化

- 使用明确的 CPU hydro/burn/network/problem 对象库。
- 加入 `extern template`，消除意外的重复隐式实例化。
- 加入 Ninja job pool、编译缓存和按目标的 PCH。
- 记录每个对象的大小、峰值内存、I/O 和 clean/incremental build 时间。

### P5：CUDA capability

- 新增 `ARCH_ENABLE_CUDA`。
- 为受支持的 SpatialRhs、Network RHS/Jacobian、ODE 和矩阵操作提供 `.cu` 实例化。
- 注册 host launch wrapper，加入 `compute_backend=cpu|cuda|auto`。
- 检查 device EOS view、常量核数据和 FluidState 内存布局。
- 完成 CPU/CUDA 误差、守恒、确定性和性能验证。

## 10. 验证与验收

每个迁移阶段必须分别验证架构、物理和性能。

### 架构

- `main.cpp` 不包含重模板头。
- `run_simulation` 只有一个非模板实现。
- `nm -C`/对象统计确认不再出现 3,600 个完整 Driver 特化。
- capability 列表与 `.par` 可选项一致。
- 未支持组合均能 fail-loud。

### 物理

- 四个网络 RHS、Jacobian、LHS 与 Timmes Fortran 基准保持既有容差。
- NSE 丰度、质量、电荷和能量闭包保持既有容差。
- CPU 重构前后相同初值的 Cellular 输出逐位一致；如编译器/执行顺序变化无法逐位一致，必须报告每变量的 L1/L2/Linf、守恒量和前沿位置误差。
- CUDA 对 CPU 使用同样的误差报告，并最终增加同设置 FLASH 结果的宏观对比。

### 性能

- 记录 clean build wall time、最大 RSS、读写 I/O 和最大对象大小。
- 记录 incremental build：修改 Driver、单个 Network、单个 Flux 时各自重编译范围。
- 运行时分别记录 hydro、burn、EOS、I/O 和 host/device transfer 时间。
- CUDA 加速比必须排除初始化和首次 JIT 暖机影响，同时报告端到端速度与纯 kernel 速度。

## 11. 预期结果与限制

完成目标架构后，CPU 热流体核预计由约 3,600 个完整 Driver 特化下降到约 120 个 SpatialRhs 特化，燃烧约 16 个独立特化，另加少量非模板 integrator、gravity 和 registry 代码。具体数量取决于最终启用的 EOS 和有效格式组合。

这不是对 clean build 时间或运行性能的无条件倍数承诺。显式拆分会增加对象文件数量和最终链接工作，但它能：

- 消除一个 83 MB 级 jumbo main 对象；
- 显著降低无关代码修改触发的重编译范围；
- 允许限制重模板并发，避免 I/O/内存峰值；
- 让 CPU/CUDA 能力以可查询、可验证的方式共存；
- 保持编译后通过 `.par` 一键切换的原始设计目标。
