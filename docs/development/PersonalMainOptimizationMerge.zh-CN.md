# 优化阶段合并到个人 main（2026-09-17）

用户本轮明确要求合并个人 main，并选择保留 predictive-AMR 采集接口、完成兼容迁移。
这次授权仅适用于 Arsenic-er/ARCH 的 main；Shiro-Akane/ARCH 仍只更新固定协作分支
`codex/hpc-cuda-optimization`，不更新朋友的 main。

## 合并边界

- 合并前个人 main：`e12b96c15e022691d788f4b67faba4dc06da342e`。
- 本轮优化与记录截止：`4742e8dad21b7728ae68391fefd65282a5f231eb`。
- 两线共同祖先为 `d646668`，各有 241／143 个独有提交；普通合并报告 112 个冲突。
- 活跃求解器、构建图和测试组织沿用已验证的优化版本，不把旧单体 CUDA 后端与新拆分后端拼接。
  旧实现及旧测试仍保留在 main 的父提交历史中。
- 个人 main 独有的 benchmarks、datasets、experiments、归档文档、日志和脚本继续留在合并树中；
  旧 `CudaBurnPulse` 验证算例也保留并参与新 CPU 完整构建。
- 朋友固定分支只接收共享接口迁移及本轮新验证材料，不把个人 main 的整套独有研究目录带过去。
- native-wave、窗口和 leaf-inline 候选仍在 validation 下，未启用生产注册。

## predictive-AMR 接口迁移

保留 Phase 0 schema v2、五类输出、节点/边身份、数值判据与 2:1 closure 分别记账。
`predictive_amr_record` 默认关闭，兼容旧的 `0/1` 和当前 `true/false`；prefix、horizon、history 保留。

Host 从原共享指标计算捕获诊断值；CUDA 从已回传的紧凑 device 指标捕获同一字段，
不重新用 Host 公式决定 refinement。观察回调仍在 RippleCheck 后、拓扑发布前执行。
只在显式开启观察器时为其 materialize accepted Current（含 ghost），默认路径不新增这类回传。
最终状态也使用同一可见性/同步边界。回滚和池复用同时恢复/清空诊断字段。

没有更改 EOS、网络、ODE、reflux、checkpoint schema、KLU/cuDSS 库或科学容差。
记录器原有的 Host 诊断统计保留，未移入 device 数学。

## 本轮验证与限制

见 [合并验证记录](../../validation/backend/results/main-merge-20260917/README.zh-CN.md)。

- 新 Host 合同覆盖 1D/2D/3D、开关和旧参数、原数值标签、只读字段、失败回滚、回收复用及五类文件。
- 完整 CPU ARCH 新编译/链接成功；相关 5 项 CTest 通过。
- 实际 CPU 算例 15 次运行、9 组对照：1D Sod、2D/3D Sedov，包含旧优化二进制对照、
  同二进制采集开关对照、3＋3 步 restart 对照。开关和 split-run 要求完整 checkpoint 数据集逐值相等。
- 本机架构审计与 100 项审计工具测试通过。
- 未重新运行完整 CUDA 构建或 GPU 采集专项测试；这些不是本次 CPU 验证的结论。
  现有 S1–S5 GPU 证据仍归属于原提交/二进制，不能改标为新合并产物的测量。

## 阶段实验入口

- [总优化计划](MultiphysicsCudaOptimizationPlan.zh-CN.md) 与 [HPC 阶段计划](HpcCudaOptimizationPlan.zh-CN.md)。
- [S5 正式矩阵](../../validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/all-modules-summary.zh-CN.md)：
  11 模块、1,188 运行／1,155 比较通过；燃烧为 aprox13＋真实 Helm，不能外推超大网络。
- [150/200 窗口 factory](../../validation/network/results/native-wave-20260917/window-factory-v2/README.zh-CN.md)：
  新对象与链接通过、归档已验证；新窗口三 ODE 真实轨迹尚未启动，超大网络性能未验收。

Git 保存已提交的参数、指标、结果、原始文本日志及归档哈希/回执；本次合并验证的 raw 包较小，
也完整上传 Git。历史大体积 raw 包仍按原约定保留服务器与本机双副本，不声称全部历史二进制/HDF
都装进普通 Git。失败记录不删除、不改写为通过。
