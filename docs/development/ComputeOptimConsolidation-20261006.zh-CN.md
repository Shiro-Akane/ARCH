> 后续整合已在 `compute/optim` 完成，当前入口见[本轮集成报告](ComputeStudioIntegrationReport-20261006.zh-CN.md)、[GUI 分支退役记录](GuiBranchRetirement-20261006.zh-CN.md)及[release 收束计划](ComputeStudioReleasePlan-20261006.zh-CN.md)。下文保留早前分支整理节点的原始范围，“GUI 分支本轮不动”等描述属于当时状态。

# compute/optim：分支归并与交付入口

日期：2026-10-06。此轮调整集成路线与分支管理，不发布新的科学能力。

## 当前集成路线

`compute/optim` 是最终汇总与审计分支。Core、Studio/Host 及相关验证改动先在各自工作线完成，随后按模块归入这里；整合验收完成后，再由维护者合并到 `main`。

本次从原 `compute/optim` 的 `8fc0dd25eefd2243e8c36f85440bac46994e2e73` 出发：

| 来源 | 冻结提交 | 此次处理 |
| --- | --- | --- |
| O8 用户边界、CPU 执行优化、RT AMR 修复与 Plotfile 验证桥 | `11a321d5604f9ee62b9f9587c81f14de4f128bc4` | 完整保留其后续 8 个提交，fast-forward 到该节点 |
| Core RZ 定义、独立解析 fixture、输入静态核对 | `4639774fe3c94ae27b3e831d0f0b9d6340de34fd` | 合并契约和参考工具，契约版本仍为 `CORE-RZ-20261006-v1` |
| RT 首次触发、ledger、observer 与数据审计记录 | `85168a2c55cbba8486833baed89afc525de4441b` | 合并完整记录，保留处理后证据及其原始内容 |
| Studio/Host/O7 集成线 | 当前审阅节点 `1f743efd7cf7d0793a766f3c2cd4fdca1bc9cadd` | 保留独立工作线；完成本轮收尾后再按模块整合 |

`main` 未合并或改写。本次整合后的 `src/`、`include/`、`cmake/`、`tests/`、`simulation/`、`tools/` 和顶层 `CMakeLists.txt` 与 O8 冻结节点一致；额外导入的是 Core 参考工具和 RT 记录。没有将同事的私有 RZ/JENS 候选应用到公开生产源码。

当前 `compute/optim` 已包含 O8 实现，不再只是早期计划分支；完整 Studio 工作台仍在它自己的集成线。不要将此时的 `compute/optim` 当作已经完成 Studio/O7 整合的工作台。

## 分支处理

| 分支 | 处理与保留理由 |
| --- | --- |
| `compute/optim` | 保留，作为唯一最终汇总入口 |
| `main` | 保留，发布基线不动；本地旧 `main` 的整理另行处理 |
| `codex/gnn-data-positivity` | 保留，独立 GNN 工作线不移动、不合并、不删除 |
| `codex/e3-audit-snapshot` | 按维护者确认退役；早期 CUDA 快照内的 GNN 是意外上传，旧实验不引入当前集成线 |
| `3D`、`arsenicer-combinationV2` | 按维护者确认退役；离线保存历史，不将旧分支整体 merge 回现代源码 |
| `codex/rt-amr-validation-notes-20261003` | 记录完整归并后退役远端引用 |
| `codex/o8-boundaries` | 已完整归入汇总线；原工作树仍占用此分支并含未提交文件，暂留指针，待工作树负责人完成交接后删除 |
| `codex/core-rz-decisions-20261006` | 契约已归并；暂留冻结交接引用，供当前 Studio 收尾核对 |
| 所有保留的 Studio/GUI、UI contract 和 review 分支 | 本轮不动，避免打断实现和既有交接 |

旧 3D 的独立 `*3D.h` 与 V2 的旧 CUDA 布局已不适合直接覆盖当前实现。现代对应入口包括 `src/driver/DriverUtils.h` 的统一多维 CFL、`src/cuda/hydro/`、`src/cuda/microphysics/eos/owners/`、`src/cuda/diffusion/`，以及 `src/amr/` 的 topology/storage/transfer/exchange/flux 所有者。此处是源码结构核对，不宣称旧分支的每个科学场景已重新运行。

三个 legacy 引用已存入本机 `legacy-before-retirement.bundle` 并通过 `git bundle verify`；该增量包依赖原 `compute/optim` 历史中的祖先对象。恢复时在具备该历史的仓库核验 bundle，再 fetch 指定的历史引用即可。离线包不上传，不增加公开归档分支或 tag；删除分支也不等于清除已有历史对象。

RT 记录中的 `gnn-data-contract-20261005.json` 是数据契约审计记录，随 RT 文档保留；它不是 E3 意外上传的训练实验，也不改变独立 GNN 工作线。

## 验证边界

- 原来源节点均是汇总线祖先，源码逐路径与 O8 一致。
- Core 独立解析参考 10/10、既有 backend 验证工具 34/34、小型 Plotfile 反例检查 13/13 在本机通过，共 57 项。Plotfile 检查复用已有 Linux `yt_env` 的 h5py/numpy；未安装新依赖。
- 同事 RZ 节点的 CPU 标量、真实 patch、scheduler 拒绝保护及 mutation 检查在隔离候选中独立复核，详见[本轮收尾](StudioRoundClosure-20261006.zh-CN.md)。
- 未在本轮重新做整套 CPU/CUDA 编译、科学长跑或正式性能计时；历史 O8 结果继续按其原提交与输入读取。
- RT 归档中的 `.patch` context 空行和原始 `.par` 尾部空格原样保留；完整导入的 whitespace 检查会报告这些历史内容。生产源码和新增维护文档的 whitespace 检查通过，不修改归档内容来改变证据身份。

## 后续整合

Studio 负责人继续在原 `studio/compute-optim-integration` 的冻结节点收尾。先 fetch 查看本次汇总及契约，暂不直接 merge 新 `compute/optim` 或 O8 到正在验证的候选。

先前预检发现 O8 与 Studio 有 41 个冲突文件，这只是当时基线的预检结果。整合时须重新计算，按配置/API、backend、调度/阶段、数值 owner、IO、测试/CMake 分组 review，保留两侧已验证的正向改动。由维护者统一解决，不能用整侧覆盖或重新引入静默回退来消冲突。

旧 Studio Phase tags 保持原值。基于旧主干的 Phase 分支无需整条 merge；按需要引用 checkpoint 和当前集成线。
