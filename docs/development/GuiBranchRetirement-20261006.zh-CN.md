# GUI 分支退役与共同基线

2026-10-06，Studio 的 `b7cb8b69` 收尾节点已完整合入 `compute/optim` 的 `9e4a5cfd7b317869241328fd1e0985a9e2d52f84`，CPU/桌面验收见[集成报告](ComputeStudioIntegrationReport-20261006.zh-CN.md)。本轮精简 GUI 分支引用，不修改 Core、Studio 运行代码或科学门槛。

## 保留的开发入口

`origin/compute/optim` 是当前共同开发基线和最终审计入口。`main` 仍是发布基线，待 release 收束后再合入；专用 GNN 分支保持独立。这里“保留一个”指一条统一集成线，不另保留长期 GUI 镜像分支。

新一轮 Studio/Host 工作从最新共同基线创建短期任务分支，记录起点 SHA、范围和文件所有权。验收并合入后删除任务分支；不从旧 GUI checkpoint 继续累计，也不重新创建已经退役的同名分支。

## 本轮退役范围与判据

| 引用 | 删除前冻结提交 | 核对结果与内容去向 |
| --- | --- | --- |
| 远端 `studio/compute-optim-integration` | `b7cb8b69845d44d6cb0ec4d6b19f4aabb4c65b14` | 是集成节点的祖先，独有提交数为 0；源码与完整历史已被实际 merge 保留 |
| 本地 `studio/compute-optim-integration` | `86bec324018349c6d81df84a3cced3ed9f9a2792` | 是当前集成线的祖先，无未合入提交、无占用它的 worktree；本地指针可安全删除 |
| 远端 `codex/studio-core-ui-contracts` | `502eadcb33a9e2d3c8bd079a10dbdc8af208ef10` | 仅一个独有历史提交，修改主线同步文档/旧目录说明；独有交接文档归档，当前 API 已采用 v3 / 95 参数 / 16 模型响应 |
| 远端 `review/studio-v0.4.2` | `4c0fd5c1242d9a48d2a75d7a2c546abb34f83627` | 10 个旧 prototype/API 历史提交不在当前祖先链；早期 Studio 通过子树接收，Core API 后续重构为当前真实实现。原提交和文件树保存在离线包，不重新套用旧源码 |
| 远端 `studio/phase2e-core-contracts` | `31f6f8dbc7e6841c9c90992699b2a89f4dd58603` | 14 个旧历史提交不在当前祖先链，包括已被后续工作台吸收的阶段代码；末提交的 A/B 最小契约文档补入历史归档，gap 报告原已归档 |

后三项不能写成“Git 已完整 merge”。它们依据旧实现已经被接收/替代、独有文档已保留以及原提交可恢复而退役。此次没有把旧版本代码反向覆盖已验收实现，也不据分支清理宣布所有科研功能完成。

## 历史材料与恢复

[补齐的历史交接资料](../../studio/docs/archive/retired-branches-20261006/README.md)记录来源 commit、原路径与 Git blob。A/B 文档保持原正文；旧同步文档只增加历史标识并修正迁移后的链接，原数值和测试结论不改写。旧 Gap、Phase/UAT 记录仍在既有 archive。

未被当前祖先链保留的三个分支已保存为 `gui-before-retirement.bundle`，通过 `git bundle verify`，包内 heads 与冻结 SHA 一致。它是增量包，依赖以下两个祖先对象，均存在于共同基线历史中：

- `7d4448a9b4a07dd86aa8cfc83fe9d85c7d63a866`
- `48f6d357d6b8085501d2012d70e923080bae3402`

离线审计包位于维护者本机的 `reviews/gui-branch-retirement-20261006/`，附删除前 refs 清单和删除后核对记录；不上传原始历史包，不新增归档分支或 tag。新 clone 不自带该离线包，需要恢复旧分支时向维护者取得它，在具有上述祖先对象的仓库中核验后 fetch 指定历史 ref。

恢复示例（`/path/to` 替换为实际收到的包路径）：

```bash
git bundle verify /path/to/gui-before-retirement.bundle
git fetch /path/to/gui-before-retirement.bundle \
  refs/remotes/origin/review/studio-v0.4.2:refs/heads/archive/studio-v0.4.2
```

本轮删除采用远端旧 SHA 校验：分支在审核后发生推进时删除必须失败，不能覆盖并发推送。`main`、已有 tags、GNN、O8/Core RZ 分支、其他 worktree、Zenodo 以及本机原始运行数据均不在清理范围。

## 给协作者的当前操作

先保存现有工作，再 fetch/prune，切换并更新 `compute/optim`。不要把新任务直接写回旧交付分支；具体命令见[联合交付入口](StudioConfigurationHandoff.zh-CN.md#12-从当前分支建立自己的工作区)。新的任务分支应从最新 `origin/compute/optim` 创建。

本轮只改分支管理和文档；上一节点的 Core/Studio 验收结果继续按其源码身份读取。RZ scientific finding、原生交互 UAT 和后续 Plotfile 仍按[release 计划](ComputeStudioReleasePlan-20261006.zh-CN.md)完成。
