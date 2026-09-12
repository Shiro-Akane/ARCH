# S1 最小 batch API 与共同接口草案

状态：S0 冻结草案，供维护者、MPI/物理负责人共同审阅；下文“尚未修改”描述冻结时点。用户随后要求直接推进，实现及签名调整见 [S1 开发记录](../S1/README.md)，不代表共同审查已经完成。

## 已核实入口与最小方案

`Driver.h` 当前逐块调用 `ComputeBackend::compute_hydro_dt(access,cfl)` 和 `execute_hydro_stage(access,descriptor,dt,token)`。`CudaBackendHydroControl.cpp` 每块提交后回传并等待。`StateResidency::is_complete` 只读令牌状态；`StageScheduler` 据返回令牌发布批次，不能把 pending 当 Complete。

拟在既有 `ComputeBackend.h` 增加两个 Host 可见的薄接口（名称/签名待确认）：

```cpp
// Request/result are host metadata, with no CUDA/MPI headers or EOS catalogue.
HydroDtBatchResult compute_hydro_dt_batch(
    std::span<const HydroDtRequest> requests, double cfl);
state::CompletionToken execute_hydro_stage_batch(
    std::span<const BackendStateAccess> currents,
    const scheduler::StageDescriptor& descriptor, double dt,
    state::CompletionToken expected);
```

`HydroDtRequest` 包含现有 access 与 Host 已生成的 block logical key；不能由 CUDA 重做 Morton/topology。首轮结果可保留按输入顺序的每块候选，批量下载后交现有 `DriverReduction::reduce_block_minimum`，先消除逐块同步。若设备汇总为一个 `ReductionResult`，必须证明 accepted_count 和 tie key 与既有层级归约一致；不能只拿 double 最小值丢失错误和逻辑身份。

错误信息至少含 stage、block logical identity 和可用的 cell/source；保留原 NaN/EOS 哨兵适配。共享最小归约对一般 NaN 的处理不等于允许 EOS 失败被忽略。当前 CFL 参数在既有数学层作用一次，批量接口不重新乘 CFL；diffusion/burn 宏步限制和最终时间裁剪仍留在 Driver 原顺序。

S1 可先仍逐块发 kernel，但将回传结果暂存在 backend-owned 有界数组，所有 launch/拷贝后在一次边界等待，检查全部状态再返回。分组/容量分片需明示，不保证永远一次 kernel。S2 才合并某个热点算子的 launch，不同时重写存储布局。

## 完成、失败、空批和寿命

- 返回 batch completion 前，该批所有设备写/回传真正完成；成功返回与 scheduler token 值一致。失败抛出/返回明确错误，scheduler 不发布新 state/version、不继续正常 checkpoint。
- 所有描述符先验证：slot、generation、路线、去重、required block 覆盖。合法局部空批为 no-op/零贡献，不等同缺失必需块。现有单进程最终空归约仍 Error；未来空 rank 的局部零贡献与全局 finalize 由 MPI 侧共同确认。
- Host 回传缓冲必须比所有使用它的异步工作活得久，包括 enqueue 失败后的异常清理。复用现有 quiescence/RAII；保持 backend 资源 owner，不把异步指针藏在栈上逃逸。
- 跨块 scratch 不别名；保持每方向 flux scratch 覆盖前 registration、stage 权重、ghost、slot 轮换和最后 reflux。单 CTA barrier 不能冒充 grid 全局屏障。
- S1 不要求 event/pending/completed 全面重构。若必须引入，另开公共契约审查，明确 query/wait、失败和清理；本草案不授权先改 scheduler。

## 并行开发交界（全部待共同确认）

| 交界 | CUDA 侧 | 其他负责人需确认 |
|---|---|---|
| 局部 CFL/错误 | 本地候选、有效数、逻辑键和错误；不直接全局 finalize | MPI 跨 rank 汇总的 sole owner、局部空 rank 与最终空结果 |
| 完成与 ghost | 设备 ready 与消费者 consumed 边界，明确 buffer 寿命 | 通信完成是另一依赖；不默认 CUDA-aware MPI，不假设 MPI 等待 CUDA stream |
| exchange/块身份 | 复用 Host-lowered transfer；区分边界/本地/远端 | rank 归属变化即使 topology 不变也要失效绑定；需要最小通知而非预建分布式 runtime |
| 物理策略 | 绑定已解析 policy/view 和明确 CUDA capability | 数学、独立参考与能力注册由原 owner；新 CPU-only 能力必须启动拒绝，不静默退回 CPU |
| checkpoint | 真正完成后 materialize，调用共同 writer | 不在 CUDA 优化中另造 MPI IO 或 checkpoint schema |

修改点预计为 `ComputeBackend.h`、`Driver.h`、`CudaBackend.h`、`CudaBackendHydroControl.cpp` 及现有 backend 资源 owner。共享 ReductionSpec/数学原则上不变；StageScheduler/StateResidency 只补合同测试。公共头不引入全套 EOS/network/MPI/CUDA 重型头。

## S1/S2 的实际测试入口

| 范围 | 现有入口及新增覆盖 |
|---|---|
| S1 Host 契约 | `reduction_contract`、`compute_backend`、`shared_stage_scheduler`、`state_residency`；补空/单/多/重复 key、错误不发布 |
| S1 CUDA | `cuda_reduction_contract`、`cuda_hydro_eos_failure`、`cuda_single_level_smoke`、`cuda_multiblock_hydro`；补多块 EOS 失败与 batch 缓冲生命周期 |
| S2 CUDA | `hydro_leaf_parity`、`cuda_hydro_dispatch`、`boundary_plan_parity`、`cuda_amr_exchange`、`cuda_hydro_route_matrix`、`cuda_hydro_integrator_matrix`；补空/尾/多块、方向、species、粗细界面和 scratch 不覆盖 |
| 原两档测速 | 既有 `run_sedov_amr_timing.py`，4/8、终止 0.02、8 线程、预热1/正式3；候选定版至少5。计数诊断另跑，不混入正式样本 |
| 影响公共资源/exchange 时 | `cuda_store_lifecycle`、`cuda_regrid_transaction`、`cuda_regrid_migration`、`diffusion_rkl_parity`、RKL1/RKL2、burn/真实 cuDSS、四向 restart 与 restart 后 regrid |
| 最终集成 | `validation/amr/gpu_cases.json`、`gpu_curvilinear_cases.json`、`validation/backend/cases.json`、`validation/network/runtime_cases.json`，smooth/ENUC restart 与标准 qualifier；不是只跑 Sedov |

具体发现/通过数量由 CTest inventory 确认；未安装、未编译、skip 不算通过。CUDA 设备写/异步寿命/缓存变化的 memcheck/racecheck 及适用同步检查需在支持调试的硬件补齐；当前服务器限制单列。
