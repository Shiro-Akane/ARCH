# S1：批量 CFL／Hydro EOS 状态回传（开发快照）

记录日期：2026-09-13（本地工作跨过午夜；构建目录沿用 20260912）。

**状态：实现已提交，Host 薄合同检查通过；CUDA 编译、设备正确性、安全和性能均未验收。** 用户要求先直接优化、不继续补跑验证，因此不补跑 S0、大网络或 GPU 性能实验，不发布验收 tag。不将 S0 基线数据当作 S1 的结果。

- 起点：`152fe22e2ac20c25b892c2a78685d2a0698bb9a0`。
- 代码提交：`c847a40e8c4de0015c4159deb71b201d921cad17`；下述检查在提交前的同一改动工作树执行，期间仅补充了未运行的 CUDA 测试用例。后续文档提交不改变生产源码。
- 固定交付分支：双方 `codex/hpc-cuda-optimization`；不修改 main，不改冻结基线，不强推。

## 实际改动

`ComputeBackend` 增加两个薄接口，既有单块入口保留兼容：

```cpp
std::vector<double> compute_hydro_dt_batch(
    std::span<const BackendStateAccess> currents, double cfl);
CompletionToken execute_hydro_stage_batch(
    std::span<const BackendStateAccess> currents,
    const StageDescriptor& descriptor, double dt, CompletionToken expected);
```

结果与输入逐项对应。首轮不传入新的 logical-key DTO：原 Driver 根据同一输入顺序构造 block logical keys，继续调用原 `DriverReduction::reduce_block_minimum`。结果长度不足时拒绝；CFL 仍由既有数学层应用一次，diffusion/burn 限步和最终时间裁剪顺序不变。局部空批返回空候选／完成的无操作；最终空归约仍由原契约拒绝。

CUDA 将既有每块 launch 的结果指针绑定到 backend-owned 紧凑数组。不新增 kernel、不移动数学公式；整个批次仍在同一 stream 上顺序执行，保留共享 species scratch、逐方向 flux registration、原 stage 权重、ghost 刷新、slot 轮换及 reflux 顺序。

下表为**代码中的 API 调用次数**，不是测量的加速比；N 是非空批中的块数。

| 操作 | 原路径 | S1 路径 |
|---|---|---|
| 一次 CFL：D2H 调用 | 2N | 2 |
| 一次 CFL：显式 stream wait | N | 1 |
| 一个 Hydro stage：D2H 调用 | N | 1 |
| 一个 Hydro stage：显式 stream wait | N | 1 |
| CFL kernel 数／下载字节 | 2N／12N（double + int） | 不变 |
| Hydro stage kernel 数／状态字节 | 按原 route／4N | 不变 |

批缓冲只保留到历史最大请求容量；设备侧约 `12 × peak_blocks` 字节，Host 状态缓冲约 `4 × peak_blocks` 字节，另有本次返回的 double 数组和 Host 描述符。增长时先成功分配新 buffers 再替换；批次等待完成后才复用或销毁。删除已无消费者的每块 `cfl_result` 分配；每块 `cfl_status` 仍供 refinement indicators 使用，未删除。

本轮使用普通 Host 缓冲，在全部 kernel 入队之后集中下载；CUDA 对 pageable memory 的调用仍可能产生隐式等待，因此不声称只有一次底层阻塞，也不声称已经实现传输／计算重叠。本轮只消除逐块下载／等待，不引入 pinned memory、events、新分配库或 Graph。

## 失败与完成边界

- 整批先验证 Current slot、有效 block/storage、重复块、活跃 generation、stage 描述符；Driver 继续负责必需块覆盖。AMR route views 在提交前绑定。
- 每块独立 EOS latch，避免后面的有效块清除前面的错误；沿用原 CFL `ReductionStatus` 和 EOS sentinel。
- 全部下载及 stream quiescence 成功后才读状态、返回结果或原 expected token。错误带 block UID、epoch、status；stage 错误另带 stage 编号。未增加原 latch 没有提供的 cell 定位能力。
- Host 目的缓冲比现有 `CudaQuiescenceGuard` 活得久，第二次 enqueue 失败也先排空再析构；Impl 析构继续沿用已有 quiescence 保证。
- 失败不返回正常完成令牌，scheduler 不发布本 stage。此路径是失败终止，不承诺撤销已经写入的私有输出 slot 或 flux scratch；不能捕获错误后当作成功继续正常 checkpoint。
- 共享 `ReductionSpec`、`StageScheduler`、`StateResidency`、物理／数值头、restart schema、依赖版本、构建浮点选项均无改动。

## 已运行的短检查

本机 Windows，MSVC 19.44.35228、x64 Debug、`/fp:strict`。不是服务器 GCC/NVCC 构建，也不是完整 ARCH 集成构建。

| 检查 | 结果 |
|---|---|
| Host `compute_backend`（新增 batch 契约） | 编译并通过 |
| Host `shared_stage_scheduler` | 编译并通过 |
| Host `state_residency` | 编译并通过 |
| Host `device_block_store_lifecycle` | 编译并通过 |
| `tests/tooling/test_audit_architecture.py` | 98/98，通过 |
| `tools/audit_architecture.py .` | exit 0 |
| `git diff --check` | exit 0 |

第一次直接调用 cl 未提供构建生成目录，报缺少 `CustomNetworkRegistry.generated.h`；原失败日志保留。随后临时小 CMake 工程直接 include 项目现有 `cmake/CustomNetworks.cmake`，由同一生成器生成注册表，编译四个已有 Host 测试；不手写注册表、不修改依赖配置。原始小工程及配置／构建／CTest 日志见 [evidence](evidence/)。

命令记录（以下 source/build 为本机路径；小工程不进入生产 CMake）：

```text
cl /nologo /std:c++20 /EHsc /utf-8 /fp:strict /I src tests/host/test_compute_backend.cpp ...
  -> exit 2，缺失 generated include（完整日志保留）
cmake -S build/hpc-s1-local-20260912 -B build/hpc-s1-local-20260912/host-build -G "Visual Studio 17 2022" -A x64
cmake --build build/hpc-s1-local-20260912/host-build --config Debug --parallel 2
ctest --test-dir build/hpc-s1-local-20260912/host-build -C Debug --output-on-failure
python -B tests/tooling/test_audit_architecture.py
python tools/audit_architecture.py .
git diff --check
```

Host 新增覆盖：空／单／多块顺序、后续无效 generation/epoch/slot、重复块的整批预拒绝、pending 或不同完成令牌拒绝、第二块抛错不返回成功。架构工具同时检查新 batch 所属文件与 launch/wait/counter 顺序；标量包装器只允许精确委托，缺失 batch owner 或夹带额外 enqueue 会拒绝。

## 已编写但未运行

现有 `cuda_multiblock_hydro` 增补 Euler/RK2/RK3 的同设备单块／批次对照、结果顺序、1→2→1 容量复用、空／非法批、一次显式等待计数、首块／末块 CFL NaN 与 ghost-only stage EOS 失败及清除复用。用例与生产 CUDA 修改尚未经过 NVCC 编译，不能计为通过。

GPU 数值矩阵、AMR/restart 集成、性能采样、sanitizer 仍全部待做。当前 H100 vGPU 的调试能力限制不因本轮 Host 检查而消失。下一实现阶段是 S2：按已有热点选择具体算子的跨块 kernel 合批；不能把本轮回传合批写成 kernel 已合批或 GPU 已提速。

## 同步边界

发布前核对朋友 main 仍为 `7d4448a9`、用户 main 仍为 `e12b96c1`，朋友固定分支为本阶段起点；用户固定分支仍未创建。用户仓库上次 push 被共享历史中 26 个缺失的 LFS 对象阻止，此问题独立于 S1 源码。不禁用 LFS 完整性检查、不删历史资产。最终推送状态与交付 SHA 以实际 Git 回执为准；不将部分同步写成双端完成。

新代码没有上传服务器。此前代码传输权限限制未绕过；当前开发快照可先供合作者审阅，不声称服务器已经运行本版本。
