# ARCH Studio — Phase 1A Real Plotfile Integration Target & Guardrails

> 目标：在已经冻结的 Phase 0 前端基线上，第一次接入 **真实 ARCH `plt.h5` 数据**。  
> 本阶段只验证：**真实 Plotfile → Adapter → PreviewData → 现有 Renderer / Inspector**。  
> 不进入 `.par`、`Setup()+Init()`、Build/Run、SSH、AMR 重建或远程执行。

---

# 0. 已冻结基线

Phase 0 已完成并冻结：

```text
Commit:
286b476c3c7ca97fa102a865c18e0af3acd5cc7a

Tag:
studio-phase0-v0.1.0

Branch:
studio/phase0-demo
```

Phase 0 已验证：

- React 18 / TypeScript / Vite；
- Parameters / Preview / Inspector 三栏 UI；
- centralized state / 集中状态；
- Dirty / Stale / Generating / Current / Failed；
- 512×512 Mock Preview；
- Density / Temperature / Pressure 字段切换；
- H5Web Heatmap；
- Point Inspector；
- Save / Revert；
- tests / lint / typecheck / build；
- ARCH Core 无修改。

> **Phase 1A 不允许破坏或重写 Phase 0 已工作的 Renderer、状态模型和 UI 骨架。**

开始 Phase 1A 前必须：

1. `git status` 确认 clean；
2. 确认 tag `studio-phase0-v0.1.0` 存在；
3. 从冻结基线切新分支：

```bash
git switch -c studio/phase1a-plotfile studio-phase0-v0.1.0
```

4. 不在 `studio/phase0-demo` 上继续开发。

---

# 1. Phase 1A 一句话目标

实现：

```text
真实 ARCH plt.h5
      ↓
PlotfilePreviewProvider
      ↓
PreviewData
      ↓
现有 H5Web Renderer
      ↓
现有 Inspector
```

目标不是“实现完整 HDF5 Viewer”。

目标是证明：

> **Phase 0 的前端数据契约可以无重构地消费真实 ARCH 结果。**

---

# 2. 当前只允许使用的真实数据

优先使用 baseline 已生成或仓库已有的**小型、本地、已完整写入**的真实 Plotfile：

```text
output/local_sod_cpu/
output/local_sod_cuda/
validation/eos/results/tabular-extension-20260908/static/
```

可选：

```text
output/cellular_run_20260913_011131/
```

但 Cellular 仍视为 experiment / fixture，不作为唯一实现依据。

## 不要

- 重新运行 ARCH 只是为了生成测试数据；
- 重新编译 CPU/CUDA；
- 修改 simulation case；
- 下载新的大型科学数据；
- 使用正在写入中的 `.h5`；
- 假定所有 HDF5 都是 ARCH Plotfile。

---

# 3. Phase 1A Scope / 范围

本阶段只做以下内容。

## A. Plotfile Provider

新增独立数据源，例如：

```text
studio/src/data/PlotfilePreviewProvider.ts
```

或职责等价的结构。

它必须与：

```text
MockPreviewProvider
```

并列，而不是替换或污染 Mock Provider。

期望关系：

```text
MockPreviewProvider ─────┐
                        ├─→ PreviewData → Renderer
PlotfilePreviewProvider ─┘
```

Renderer 不应该出现：

```text
if (hdf5) ...
if (mock) ...
```

之类的数据源分支。

---

# 4. Phase 1A 第一优先级：真实 1D Plotfile

**第一阶段只要求 1D。**

原因：

- 能验证真实 HDF5 数据链；
- 不涉及 2D block 拼接；
- 不涉及 AMR 重建；
- 可以直接验证 H5Web `LineVis`；
- 风险最低。

必须完成：

1. 选择或指定一个真实小型 `plt.h5`；
2. 读取根 metadata：
   - `time`
   - `dim`
   - `geometry`
3. 枚举真实 `/Data` 下存在的 field；
4. 不写死：
   - DENS
   - PRES
   - TEMP
   - species
5. 用户选择一个 1D scalar field；
6. 将其转换成统一的 PreviewData / PlotPreviewData；
7. 使用 H5Web `LineVis` 或独立 1D Renderer 显示；
8. Inspector 显示：
   - file
   - time
   - dimension
   - geometry
   - field
   - min
   - max
9. UI 必须明确显示：

```text
REAL PLOTFILE
```

与：

```text
MOCK
```

区分。

---

# 5. HDF5 Reader 实现原则

Phase 1A 可以使用适合浏览器读取本地小型 HDF5 的公开依赖，例如：

```text
@h5web/h5wasm
```

但新增依赖前必须记录：

- package name；
- version；
- license；
- why needed。

优先使用公开 API。

不要复制 H5Web 内部源码。

---

# 6. HDF5 Schema 边界

当前只允许读取 ARCH 文档和实际 Plotfile 中已经明确存在、且 Phase 1A 必须使用的最小集合。

允许读取：

```text
root attributes:
time
dim
geometry

/Data
actual available datasets
```

如果 1D 显示需要坐标，则读取 ARCH 真实文件中与该 field 对应的最小 Grid 数据。

## 禁止在 Phase 1A 做

- 通用 HDF5 browser；
- 任意 HDF5 schema inference；
- AMR hierarchy reconstruction；
- coarse/fine precedence；
- ghost cell 处理；
- checkpoint parsing；
- 3D volume reconstruction；
- remote HDF5 streaming。

如果真实文件出现上述需求：

> **停止该样本，换一个更简单的 1D / uniform fixture，或报告阻塞。**

不要自行扩展 Scope。

---

# 7. PreviewData 契约保持稳定

优先复用 Phase 0 已有的 `PreviewData`。

如 1D 数据无法合理表达在当前结构中，可以新增一个**最小且明确的 discriminated union / 判别联合类型**，例如：

```ts
type PreviewData =
  | HeatmapPreviewData
  | LinePreviewData;
```

但必须满足：

- Renderer 只消费 PreviewData；
- Provider 负责数据来源；
- UI 不直接读 HDF5；
- 不把 HDF5 dataset object 泄漏进 React component；
- 不把 HDF5 internal path 作为 UI 核心状态。

如无需修改现有 PreviewData，则不要为了“架构更漂亮”而重构。

---

# 8. Data Source State

Phase 1A 至少区分：

```text
Mock
Real Plotfile
```

UI 中要有清晰来源标识。

建议状态：

```text
Data source:
Mock | Plotfile
```

真实 Plotfile 打开后显示：

```text
REAL PLOTFILE
filename
time
dim
geometry
```

Mock 仍保持：

```text
MOCK / DEMO
```

不要让用户把 Mock 和真实 ARCH 数据混淆。

---

# 9. File Open / 文件选择

Phase 1A 只要求：

```text
Open Plotfile...
```

能够选择一个本地 `.h5` 文件。

要求：

- 不自动扫描全盘；
- 不默认访问任意目录；
- 不修改文件；
- 只读打开；
- 失败时显示明确错误；
- 非 ARCH Plotfile 应失败或提示 unsupported，不要猜数据结构。

---

# 10. Field Selector

真实 Plotfile 模式下：

```text
Field Selector
```

必须来自：

```text
/Data
```

真实存在的 dataset。

不要写死：

```text
Density
Temperature
Pressure
```

显示层可以做友好映射，例如：

```text
DENS → Density
PRES → Pressure
TEMP → Temperature
```

但：

- 原始 field name 必须可追踪；
- 未知 field 必须仍能显示原名；
- species 名称不要自行添加 `X_` 或改写。

---

# 11. 1D Point / Sample Inspector

Phase 1A 的 1D Line View 至少允许：

- hover/click 某位置；
- 显示 x；
- 显示当前 field value。

如果 Phase 0 Inspector 已经有 rho/T/P 固定字段：

- 不强行把真实 Plotfile 伪装成拥有全部字段；
- 真实模式下 Inspector 应根据当前文件实际字段调整；
- 不存在的数据应明确为空或 unavailable。

不要制造不存在的科学数据。

---

# 12. Phase 1A 不做 2D AMR

这是本阶段最重要的 Guardrail。

如果打开：

```text
Data/DENS
```

发现数据由多个 block 组成，且需要：

```text
Grid/level
Grid/morton
coarse/fine hierarchy
```

才能正确绘制：

> **不要实现。**

Phase 1A 只验证 1D / 简单 uniform 数据。

AMR 正式方案应由 ARCH 侧：

```text
PreviewExtractor
```

处理后返回规则 PreviewData。

H5Web 不负责理解 ARCH AMR。

---

# 13. 不修改 ARCH Core

Phase 1A 仍禁止修改：

```text
src/
include/
cmake/
simulation/
validation/
tools/
build-cuda/
```

除非用户之后明确授权。

不修改：

- HDF5Writer；
- PlotIO；
- solver；
- CUDA；
- MPI/OpenMP；
- EOS；
- AMR。

如果前端因为文件格式问题必须修改 Core：

> **停止并报告，不自行修改。**

---

# 14. 不重新编译 / 不重新运行 ARCH

Phase 1A 默认：

```text
NO ARCH REBUILD
NO CUDA BUILD
NO BASELINE RETEST
```

现有真实 Plotfile 已足够完成本阶段。

只有用户明确要求新增 fixture 时才允许运行一个已有 binary；仍不得因此修改 Core。

---

# 15. 不进入 `.par` / IC Preview

Phase 1A 明确禁止：

- 真实 `.par` parser；
- 真实 `.par` Save；
- `case.cpp` parsing；
- `Setup()+Init()`；
- `ARCH --preview`；
- stdin / IPC；
- Initial Condition Preview；
- Validation pipeline。

这些属于后续阶段。

---

# 16. 不进入 Build / Run / Monitor

保持：

```text
Build
Start
Monitor
```

禁用。

不要实现：

- CMake；
- Ninja；
- process spawning；
- stdout/stderr；
- Stop；
- Restart；
- checkpoint；
- source/binary mismatch；
- file watcher。

---

# 17. 不进入 Remote

Phase 1A 禁止：

- SSH；
- Slurm；
- PBS；
- localhost agent；
- remote API；
- WebSocket；
- remote HDF5。

不要因为最终架构可能远程化而提前开发。

---

# 18. 错误处理

至少验证：

1. 用户取消文件选择；
2. 文件不是 HDF5；
3. HDF5 不是 ARCH Plotfile；
4. 缺少 `Data`；
5. 缺少根 metadata；
6. field dataset 类型不支持；
7. field 为空；
8. 数值含 NaN / Inf；
9. 文件读取失败。

原则：

- 不 crash；
- 不显示旧文件数据冒充新数据；
- Error state 可恢复；
- 重新打开有效文件后恢复正常。

---

# 19. Milestones

严格顺序：

```text
P1A-M0  Branch + scope audit
P1A-M1  Local HDF5 file open
P1A-M2  ARCH metadata + /Data discovery
P1A-M3  Real 1D field → PreviewData
P1A-M4  H5Web LineVis integration
P1A-M5  Real Inspector + source labeling
P1A-M6  Error handling + QA
P1A-M7  Completion report + checkpoint
```

每个 Milestone：

1. 完成；
2. Demo 能运行；
3. 更新 `studio/STATUS.md`；
4. Scope Review；
5. 再继续。

不要并行扩展。

---

# 20. 测试要求

新增测试至少覆盖：

- valid ARCH 1D plotfile metadata；
- field enumeration；
- unknown field name；
- min/max；
- NaN/Inf handling；
- unsupported file；
- switching Mock ↔ Plotfile；
- stale async result 不覆盖新文件；
- source label 正确；
- renderer 不直接依赖 HDF5 reader。

现有 Phase 0 测试不得删除或放宽。

---

# 21. Phase 1A 验收标准

完成后必须可以：

1. 从 `studio-phase0-v0.1.0` 派生的 Phase 1A branch 启动；
2. Phase 0 Mock 功能仍全部可用；
3. 用户点击 `Open Plotfile...`；
4. 打开一个真实 ARCH 1D `plt.h5`；
5. UI 标识 `REAL PLOTFILE`；
6. 显示真实 filename；
7. 显示真实 time；
8. 显示真实 dim；
9. 显示真实 geometry；
10. 自动枚举 `/Data` 实际字段；
11. 字段列表不写死；
12. 选择字段后显示真实 1D line plot；
13. min/max 来自真实数组；
14. click/hover 能检查 x/value；
15. 未知 field 仍可按原名选择；
16. 无效文件有可恢复错误提示；
17. 切回 Mock 后 Phase 0 行为正常；
18. tests / lint / typecheck / build 通过；
19. ARCH Core 无修改；
20. 没有重新编译 ARCH；
21. 没有实现 AMR reconstruction；
22. 没有进入 `.par`、Setup/Init、Build/Run、SSH；
23. 更新 `studio/STATUS.md`；
24. 提交 Phase 1A checkpoint；
25. 停止，不自动进入 Phase 1B。

---

# 22. Completion / 封箱

完成 P1A-M6 后：

1. `npm test`
2. `npm run lint`
3. `npm run typecheck`
4. `npm run build`
5. `git diff` audit
6. dependency/license audit
7. 编写：

```text
studio/PHASE1A_COMPLETION_REPORT.md
```

8. commit：

```text
feat(studio): integrate real 1D ARCH plotfiles
```

9. 建议 tag：

```text
studio-phase1a-v0.2.0
```

10. 报告 commit hash / tag / tests / known issues；
11. **停止。**

不要自动开始：

```text
2D uniform
2D AMR
.par
IC Preview
Build/Run
SSH
```

---

# 23. Phase 1A 最终原则

```text
Real data
>
More features
```

```text
1D correct
>
2D approximate
```

```text
Existing Renderer reuse
>
UI rewrite
```

```text
Provider isolation
>
HDF5 leaking into components
```

```text
Documented ARCH data
>
Guessing schema
```

```text
No AMR
>
Incorrect AMR
```

```text
Stop after Phase 1A
>
“顺手把 Phase 1 做完”
```

