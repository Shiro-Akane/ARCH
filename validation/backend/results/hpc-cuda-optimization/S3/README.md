# S3：AMR 逻辑计划与工作区复用（开发快照）

日期：2026-09-13。承接 S2 `7db35613`，按 S3a/S3b 两个可回退提交保存。

## S3a：共享逻辑计划缓存

- `GhostExchange::GetPlans` 为同层、粗细层计划和分层端点索引保留一条缓存，CPU/CUDA 共用。
- 使用完整键比较：dimension、active pool 顺序、handle UID/epoch、level、logical coordinates、species 数和 face-neighbor 记录。不是仅比较散列。
- 不缓存 storage generation、slot、字段指针或物理体积：它们不参与逻辑计划生成，在执行前重新解析和校验。几何/layout 仍由当前 grid 降低与绑定，不复用旧几何数值。
- 候选计划全部生成成功才发布；错误候选不替换旧缓存。保留未缓存 Build API，供独立对照和事务路径使用。

## S3b：设备容量复用

- Hydro/边界的批次描述符、同层 exchange 各 phase operations/scratch、粗细层 transfers/scratch/status 由 backend stream owner 持有。
- 容量只随最大请求增长，保留一套工作区，不为历史 topology 积累缓存。每次仍重新上传当前 metadata，不复用 stale view。
- 扩容先创建替代分配，失败保留旧容量；沿用 `CudaQuiescenceGuard` 与 Impl 析构排空保证。不同 topology transaction 仍使用原独立 resolver。
- 同层 gather/scatter 与三个 phase 在同一 stream 顺序执行，在最终 Host 消费边界统一等待。
- 未引入 pinned 内存、异步分配器、新 stream 或库；物理数学与 restart schema 不变。

## 检查与限制

MSVC Debug 窄合同工程编译成功，6/6 Host 测试通过：compute_backend、shared_stage_scheduler、state_residency、device_block_store_lifecycle、amr_operation_plans、same_level_exchange_plan。

新增逻辑缓存命中、mixed-level 新旧计划指纹及端点顺序对照、epoch 失效、非法 neighbor 与重复 handle 拒绝、候选失败后旧缓存保留。架构工具 98/98 通过，源码审计和 diff 检查退出码 0。

CUDA 编译/运行、连续 regrid/restart、OOM failure injection、设备内存高水位和 sanitizer **未测**。复用规模有界是所有权设计，不是实测内存峰值或安全验收结论。按用户要求暂不补长验证，也没有性能收益数字。

只交付固定优化分支的开发快照；两个 main 不动。朋友端阶段 push，个人端仍受 S1 记录的两张历史 LFS 表缺失阻塞，不使用不完整推送。
