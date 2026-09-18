# CUDA 优化阶段交付：超大网络速度优化后置

2026-09-18。用户确认可将 150/200 核素的速度优化放到后续，先推送其他模块已验证成果。
本次是固定协作分支的阶段交付，不是“所有规模、所有代码版本、安全及性能全部验收”的发布。
不启动新大网络实验，不修改双方 main，不更换库、物理数学或误差预算。

## 可以交付的已验证成果

以下性能属于各自注明的冻结优化版本/算例；不是把旧二进制成绩重新标为最新 main 成绩。

| 模块与证据 | 已验证结果 | 限定范围 |
|---|---|---|
| [S4 Hydro＋动态 AMR](../../validation/backend/results/hpc-cuda-optimization/S4/timing-20260913/README.md) | 两档 Sedov 对最快已测 CPU16 的同线程比约 1.588／1.667；120 次运行、140 组线程内＋16 组跨线程比较 | Hydro＋AMR 整体端到端，不是独立 AMR 加速比；无燃烧/扩散 |
| [S5 内置燃烧](../../validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/burn-summary.zh-CN.md) | 128 块三 ODE 对最快 CPU 约 1.98／2.61／2.00 倍 | aprox13＋真实 Helm＋DenseLU，不代表所有内置网络或 150/200 稀疏网络 |
| [S5 扩散](../../validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/diffusion-summary.zh-CN.md) | 128 块 RKL1 基本持平，RKL2 约 1.35 倍 | 小规模仍可能 CPU 更快；不把 RKL1 的约 0.36% 差异宣传为明显加速 |
| [S5 六种全输运耦合](../../validation/backend/results/hpc-cuda-optimization/S5-fixed-formal-20260914/all-modules-summary.zh-CN.md) | 128 块对最快 CPU 约 2.88–5.08 倍，覆盖 Hydro、燃烧、扩散、动态 AMR | 保留共享组分修复和原预算，宏步/字段/regrid 对齐不等于逐单元 ODE 尝试次数完全一致 |

S5 正式矩阵共 11 组、1,188 次运行、1,155 次比较，已全部通过并归档。
CPU 从该轮 1/8/16 线程中取最快中位数；S4 有独立五档扫描。
服务器为 H100-20C 20 GiB vGPU＋Xeon Gold 6338 VM，不是独占完整 H100/物理核心。
小规模交叉点、失败/中止记录及原始样本保留；GPU 对 CPU 的收益不等于 S5 单项改动的净收益。
阶段交付包含之前已经推送的优化代码，不另外重新标记一次“新增性能提升”。

## 超大网络：正确性进展保留，速度工作延后

150/200 原生产路线有完整 Helm 数值对照及正式计时记录，但原小规模完整应用 GPU
仍比 CPU8 慢约 5.0–10.3 倍。这个结论与其他已合格模块分开。
新的 native-wave/window/leaf-inline 候选始终留在 `validation/`，没有接入生产 CMake/路由。

新窗口 factory 的[六组 focused](../../validation/network/results/native-wave-20260917/window-focused-v1/README.zh-CN.md)
及[十二组容量回归](../../validation/network/results/native-wave-20260917/window-capacity-v1/README.zh-CN.md)
均已通过、双端核验。容量轮为 24 条轨迹／96 对宏步，最大场误差约 3.199e-14，原预算 2e-10。
pool32 的单次专用 harness 诊断 GPU 仍约为逐单元 CPU 参考的 2.15–4.52 倍；
不是正式全应用计时，也不能与上面 CPU8 的倍率直接混用。

保留后续清单：新候选长轨迹、真正 64/128 多页窗口、真实 Helm 全应用与配对性能。
本次不继续消耗 GPU 做这些实验；也不删除已经通过的候选或关闭原生产大网络能力。

## 发布边界与后续集成

- 固定推送到 Arsenic-er/ARCH 和 Shiro-Akane/ARCH 的 `codex/hpc-cuda-optimization`，同一 SHA、fast-forward，不强推。
- 本次新增十二组容量证据、回执和交付说明；此前优化代码已在分支中。大二进制原包仍按既有规范保存在服务器/本机，Git 保存完整文本投影、清单和哈希引用。
- [个人 main 的 predictive-AMR 兼容迁移](../../validation/backend/results/main-merge-20260917/README.zh-CN.md)已完成 CPU 集成验证，后续九项 CPU 补测也通过；该合并树的完整 CUDA/采集接口 GPU 回归尚未补齐。不能用此前 S4/S5 证据替代。
- vGPU 的 memcheck/racecheck 调试限制仍未解除，不能发完整内存安全验收标签。
- 将已验证优化迁入新的 main 或发布二进制前，应按最终集成树独立复核；本次不自动合并朋友 main，也不将个人研究材料带入朋友仓库。
