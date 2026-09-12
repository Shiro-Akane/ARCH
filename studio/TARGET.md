# ARCH Studio — Phase 0 Target & Guardrails v2

> 用途：给 Codex / Agent 作为本阶段的持续目标文件，用来限制 Scope / 范围并防止长任务跑偏。  
> 当前状态：**WSL2 + ARCH baseline 已经完成，不得重复 baseline 或继续做构建性能优化。**  
> 若本文件与 ARCH 仓库自身的科学计算/构建文档冲突，以 ARCH 上游文档为准；若与用户后续明确指令冲突，以用户最新明确指令为准。

---

# 0. 已确认的 Baseline — 只读事实，不要重做

以下信息来自已经完成的 `STATUS.md`：

```text
Verified commit:
7d4448a9b4a07dd86aa8cfc83fe9d85c7d63a866

Validated Linux working tree:
/home/arch/projects/ARCH-linux

WSL distro:
ARCH-Ubuntu-24.04

Validated build directory:
/home/arch/projects/ARCH-linux/build-cuda

Validated executable:
/home/arch/projects/ARCH-linux/build-cuda/bin/ARCH
```

当前 Release executable **同时支持 CPU 和 CUDA**。

因此：

- 不要另外创建 `build-cpu/`；
- 不要重新做 CPU-only baseline；
- 不要重新做 CUDA baseline；
- 不要继续调 CUDA 编译并发；
- 不要 clean / delete 已验证的 `build-cuda/`；
- 不要重新运行全套 307 Python tests、105 CTest 或 smoke suite，除非用户明确要求。

已经验证：

- CPU / CUDA Release 构建成功；
- 307/307 Python tools tests 通过；
- 105/105 CTest 通过；
- CPU / CUDA Sod 通过；
- AMR / diffusion / restart smoke tests 通过；
- Git working tree clean；
- LFS 实体已就绪；
- 真实 `plt.h5` 输出已经存在。

当前 Phase 0 的 UI 修改**不应触发 ARCH C++ / CUDA 重新编译**。

---

# 1. 工作树与文件安全

## 1.1 唯一可信工作树

后续开发必须基于：

```text
/home/arch/projects/ARCH-linux
```

不要把下面两个副本当成开发源：

```text
E:\.Codex\.ShiroAkane\ARCH
/home/arch/projects/ARCH
```

原因：

- 已验证 baseline 来自 `ARCH-linux`；
- Windows 副本中的科学数据可能仍是 Git LFS pointer；
- 之前跨 Windows → Linux 的 CRLF 行尾曾导致测试失败。

## 1.2 保持 Linux-native

Phase 0 代码和 Git 操作优先在 WSL 原生 Linux 工作树内完成。

不要：

- 把项目来回复制到 NTFS 再开发；
- 无理由批量转换行尾；
- 改写现有仓库文件的 LF/CRLF；
- 将 `node_modules` 建在仓库根目录。

## 1.3 Git 起点

开始 Phase 0 前：

1. 确认当前 commit 与 baseline 一致；
2. 确认 working tree clean；
3. 从当前状态建立独立 branch，例如：

```text
studio/phase0-demo
```

4. 不覆盖用户已有修改；
5. 不自动 rebase / reset / revert 上游源码。

---

# 2. STATUS 记录规则 — 不要覆盖 Baseline 证据

当前根目录 `STATUS.md` 是 **WSL2 + ARCH baseline 的完成记录**。

> **不要把它改写成 Studio 开发日志。**

Phase 0 请新建：

```text
studio/STATUS.md
```

若 `studio/` 尚不存在，则在建立前端目录时一起创建。

每个 Milestone 后更新 `studio/STATUS.md`：

```md
## Current Phase
Phase 0

## Current Milestone
M2 Mock Preview

## Completed
- ...

## Changed Files
- ...

## Dependencies Added
- ...

## ARCH Core Files Touched
None

## Known Issues
- ...

## Scope Check
On target / Needs review

## Next Action
- ...
```

如果：

```text
ARCH Core Files Touched != None
```

立即停止并说明原因，除非用户已经明确批准。

---

# 3. ARCH Studio 的产品定位

ARCH Studio 是 ARCH 的：

- interactive configuration / 交互式配置
- initial-condition inspection / 初始条件检查
- run control / 运行控制
- lightweight monitoring / 轻量结果监视

前端。

它不是：

- CFD Solver / CFD 求解器；
- IDE；
- no-code physics editor / 无代码物理编辑器；
- ParaView / yt / VisIt 替代品；
- CUDA / MPI / OpenMP 开发任务；
- ARCH Core 重构任务。

当前开发者侧重点：

```text
UI / UX
Editor hierarchy
Visualization
Interaction
Frontend prototype
```

---

# 4. Phase 0 唯一目标

本阶段只做：

> **一个可运行、可点击、具有正确 UI 状态逻辑的 ARCH Studio 前端 Demo。**

目标是验证：

```text
Parameters
     ↓
working copy
     ↓
Dirty / Stale
     ↓
Preview
     ↓
MockPreviewProvider
     ↓
PreviewData
     ↓
Renderer
     ↓
Inspector
```

当前不接真实 ARCH Physics。

---

# 5. Phase 0 技术栈

使用：

- React
- TypeScript
- Vite

优先评估：

- `@h5web/lib`

用于中央 scientific visualization / 科学可视化。

当前不要引入：

- Tauri
- Electron
- Rust
- Python backend
- Qt
- SSH
- Slurm
- MPI frontend
- CUDA frontend binding

不要为了 UI 框架在仓库根目录增加 package dependency。

所有 Node / frontend dependency 应位于：

```text
studio/package.json
```

除 H5Web 及 React/Vite 基础依赖外，不要自行加入大型 UI framework、state framework 或 visualization framework；确有必要时先报告。

---

# 6. Phase 0 目录边界

新增内容优先限制在：

```text
studio/
```

建议：

```text
studio/
  package.json
  src/
    components/
      ParameterPanel/
      Preview/
      Inspector/
      StatusBar/

    data/
      PreviewData.ts
      MockPreviewProvider.ts

    state/
      studioState.ts

    types/

  STATUS.md
```

当前禁止为 GUI 修改：

```text
src/
include/
cmake/
simulation/
validation/
tools/
build-cuda/
```

也不要修改：

- solver；
- CUDA；
- MPI / OpenMP；
- EOS；
- AMR；
- `case.cpp` 科学逻辑；
- ARCH build configuration。

如果某项 UI 功能必须修改这些区域才能完成：

> **跳过，报告，不要自行修改。**

---

# 7. UI 信息架构

参考用户提供的草图：

```text
┌──────────────────────────────────────────────────────┐
│ Case / Config / State                                │
├──────────────┬─────────────────────────┬─────────────┤
│ Parameters   │                         │ Inspector   │
│              │      Main Preview       │             │
│ Grid         │                         │ x / y       │
│ EOS          │                         │ rho / T / P │
│ Network      │                         │ Xi          │
│ Runtime      │                         │ AMR Level   │
│              │                         │             │
│ Custom Params│                         │             │
├──────────────┴─────────────────────────┴─────────────┤
│ Build | Preview | Save | Start | Monitor            │
└──────────────────────────────────────────────────────┘
```

原则：

- Main Preview 是视觉中心；
- Parameters 清晰但不抢中心；
- Inspector 相对更窄；
- 风格为 modern scientific / engineering workstation / DCC editor；
- 不要求复刻 ASCII 草图；
- 不复制 Blender / Unreal / Substance 或其他项目的视觉资产。

---

# 8. Phase 0 参数

先使用 Mock Parameters。

## Grid

```text
resolution_x
resolution_y
xmin
xmax
ymin
ymax
```

## EOS

```text
eos_type
```

## Runtime

```text
tmax
backend
```

## Custom / Hotspot

```text
hotspot_x
hotspot_y
hotspot_radius
hotspot_temperature
```

这些都是 frontend working copy。

不读取真实 `.par`。

---

# 9. State Model / 状态模型

至少定义：

```text
ConfigState
saved
dirty
invalid
```

```text
PreviewState
current
stale
generating
failed
```

```text
RunState
idle
```

行为：

```text
修改任意参数
↓
Config = dirty
Preview = stale
```

点击 Preview：

```text
stale
↓
generating
↓
MockPreviewProvider
↓
current
```

`Generating` 可以模拟约 300–800 ms。

状态逻辑必须集中管理，不要散落在各组件里。

---

# 10. PreviewData 契约

Renderer 不应知道数据来源。

至少定义：

```ts
interface PreviewData {
  width: number;
  height: number;

  xRange: [number, number];
  yRange: [number, number];

  field: string;

  min: number;
  max: number;

  values: Float32Array;

  metadata?: Record<string, unknown>;
}
```

当前：

```text
MockPreviewProvider
        ↓
PreviewData
        ↓
Renderer
```

未来：

```text
ArchPreviewProvider
        ↓
PreviewData
        ↓
Renderer
```

以及：

```text
PlotfilePreviewProvider
        ↓
PreviewData
        ↓
Renderer
```

UI / Renderer 不应该知道：

- HDF5 internal layout；
- AMR block layout；
- ghost cells；
- fine/coarse precedence；
- CUDA backend；
- MPI ranks。

---

# 11. Central Preview

按：

```text
512 × 512 PreviewData
```

概念设计数据。

但 CSS 显示尺寸应随窗口响应变化，不要把整个 UI 锁死为 512 CSS px。

MockPreviewProvider 至少生成：

### Density
hotspot / 热点

### Temperature
hotspot + smooth gradient / 热点和平滑梯度

### Pressure
shock-like / step-like field / 冲击式阶跃场

修改：

```text
hotspot_x
hotspot_y
hotspot_radius
hotspot_temperature
```

重新 Preview 后，图像需要有对应变化。

必须明确：

> **这是 Mock / Demo 数据，不是真实 ARCH scientific result。**

---

# 12. Visualization

优先使用：

```text
@h5web/lib
```

实现：

- Heatmap
- ColorBar
- Colormap
- Zoom / Pan（若公开组件稳定支持）

如果 H5Web 接入出现阻塞 Phase 0 的依赖问题：

- 允许临时 Canvas fallback；
- 但 Renderer 接口保持独立；
- 不因此开始研究 ARCH HDF5 / AMR。

当前不要使用：

```text
@h5web/app
@h5web/h5wasm
```

除非用户明确进入真实 H5 Viewer 阶段。

---

# 13. Field Switching

至少：

```text
Density
Temperature
Pressure
```

切换后 Preview 立即切换对应 Mock data。

---

# 14. Point Inspector

点击 Preview 后：

```text
Coordinates
x
y

rho
T
P
```

显示对应位置的 Mock 数值。

Preview 上显示 selected-point marker。

Marker 必须克制，不得误导为科学数据本身。

---

# 15. Save / Revert

Phase 0 不写真实 `.par`。

### Save

仅：

```text
working copy → demo saved state
dirty → saved
```

### Revert

恢复到最近一次 Demo Save。

刷新浏览器后是否保留 Demo state 不是 Phase 0 要求。

---

# 16. 底部按钮

## Preview
实现 Mock Preview。

## Save / Revert
实现 Demo working-copy 行为。

## Build
Disabled / Demo-only。

**不调用 CMake。**

## Start
Disabled / Demo-only。

**不启动 ARCH。**

## Monitor
Disabled / Demo-only。

不要为了“按钮能点”顺手接真实 Build / Run。

---

# 17. 真实 HDF5 的当前边界

Baseline 已经提供真实 HDF5：

```text
output/local_sod_cpu/
output/local_sod_cuda/
output/local_amr_smoke/
validation/eos/results/tabular-extension-20260908/static/
```

这些文件**当前仅作为后续集成资源存在**。

Phase 0：

- 不解析真实 `plt.h5`；
- 不实现 HDF5 tree browser；
- 不实现 AMR reconstruction；
- 不让 H5Web 自己解释 ARCH AMR；
- 不把真实 plotfile integration 混入 Mock Demo。

Phase 0 完成后，等用户确认再进入独立的：

```text
Phase 1 / Plotfile Viewer Integration
```

---

# 18. `case.cpp` / `.par` 边界

## `case.cpp`

Studio 最终可：

- 显示；
- 查看；
- Open in Editor；
- 调用 Build。

但：

> **Studio 不修改 `case.cpp`。**

Phase 0 不解析 `case.cpp`。

## `.par`

未来必须：

```text
disk .par
↓
Load
↓
GUI working copy
↓
Edit
↓
Preview
↓
Save
↓
disk .par
```

Phase 0 不读写真实 `.par`。

---

# 19. Scientific / Diagnostic / Decorative 分层

## Scientific Data
未来来自 ARCH：

```text
rho
T
P
Xi
AMR
```

必须准确。

## Diagnostic Data

例如：

```text
Preview stale
Build state
Resolution warning
```

必须有明确规则。

## Decorative Information

例如：

- background animation
- particles
- splash
- icons
- empty-state visuals

允许自由设计。

但：

> Decorative 不能伪装成 Scientific Data。

---

# 20. 开源参考政策

允许参考：

- `silx-kit/h5web`
- `Kitware/peacock`
- `cfddose/Splash`

## H5Web

Phase 0 可以作为正式 dependency，通过公开 `@h5web/lib` API 使用。

不要复制其内部源码重写。

## Peacock

当前只允许概念参考：

- scientific GUI architecture
- information hierarchy
- workflow

不要复制：

- source
- CSS
- icons
- images
- branding
- UI text

## SplashFOAM

当前只允许 workflow / README 级参考。

不要引入其 LGPL-3.0 源码。

Phase 0 不要花大量时间分析 Peacock / Splash 源码；只有出现明确 UX 问题时才参考。

---

# 21. Milestones

严格按顺序：

```text
M0  Frontend bootstrap + UI shell
M1  State model
M2  Mock Preview
M3  Field switching
M4  Point Inspector
M5  Save / Revert
M6  Phase 0 QA
```

每个 Milestone：

1. 完成；
2. 确认 Demo 可运行；
3. 更新 `studio/STATUS.md`；
4. 重新读取本文件的 Scope；
5. 再进入下一项。

不要并行扩张多个 Milestone。

---

# 22. 防跑偏检查

## 每次开始工作前

明确：

```text
Current Phase:
Current Milestone:
Allowed Files:
Forbidden Areas:
Next Deliverable:
```

## 以下操作前必须重新检查本文件

- 新 dependency；
- 修改 `src/`；
- 修改 `include/`；
- 修改 `cmake/`；
- 修改 `simulation/`；
- 修改 `validation/`；
- 修改 CMake；
- 修改 CUDA；
- 修改 MPI/OpenMP；
- 修改 `case.cpp`；
- HDF5 parsing；
- AMR；
- SSH / Slurm；
- Tauri / Electron；
- 大规模移动/重命名文件。

若不属于 Phase 0：

> **停止，不执行，说明原因，等待用户确认。**

## 长时间没有完成明确 Milestone 时

停止扩张并做：

```text
1. 我当前在做什么？
2. 它是否直接服务 Phase 0 验收？
3. 有没有更小实现路径？
4. 是否碰到了 ARCH Core？
5. 是否应该停止并报告？
```

---

# 23. Phase 0 明确禁止

- 重新跑 baseline；
- 继续优化 CUDA 编译；
- 删除/清理 `build-cuda`；
- 修改 ARCH Core；
- CFD；
- EOS；
- AMR reconstruction；
- CUDA；
- MPI；
- OpenMP；
- CMake integration；
- 真实 Build button；
- 真实 Start / Stop；
- SSH；
- Slurm；
- scheduler；
- checkpoint/restart；
- 3D volume rendering；
- BVH / RT Core；
- hardware monitor；
- MPI topology；
- 自动解析/重写 `case.cpp`；
- 真实 `.par` 读写；
- 真实 `plt.h5` integration；
- 把 Mock 描述成真实科学数据；
- 自动进入 Phase 1。

---

# 24. Phase 0 验收标准

用户应可以：

1. 启动 Demo；
2. 看到三栏 ARCH Studio；
3. Preview 是视觉中心；
4. 修改 `hotspot_x`；
5. Config 立即变 Dirty；
6. Preview 立即变 Stale；
7. 点击 Preview；
8. 显示 Generating；
9. hotspot 随参数移动；
10. Preview 回到 Current；
11. 切换 Density / Temperature / Pressure；
12. 三个场明显不同；
13. 点击 Preview 某一点；
14. Inspector 显示 x / y / rho / T / P；
15. Save 后 Dirty 消失；
16. 再修改后 Revert 能恢复；
17. 改变窗口尺寸后布局不严重错位；
18. lint / typecheck / production build 通过；
19. `git diff` 证明 ARCH Core 未被修改；
20. 根目录 baseline `STATUS.md` 未被改写；
21. `studio/STATUS.md` 完整；
22. 未自动进入 Phase 1。

---

# 25. Phase 0 完成后

只做：

1. `lint`
2. `typecheck`
3. frontend production build
4. Demo smoke test
5. 实现报告
6. changed files 列表
7. dependency + license 列表
8. known issues
9. 更新 `studio/STATUS.md`
10. **停止**

不要自动：

- 接真实 `.par`；
- 接真实 `plt.h5`；
- 接 PreviewExtractor；
- 接 Build / Run；
- 重新编译 ARCH；
- 接 CUDA / MPI；
- 进入 Phase 1。

---

# 26. 最终原则

```text
完成当前 Phase
>
顺手做下一阶段
```

```text
小步可运行
>
大规模架构
```

```text
稳定数据接口
>
直接绑定 ARCH internals
```

```text
正确状态反馈
>
按钮数量
```

```text
明确 Mock
>
伪装 Scientific Result
```

```text
保留已经验证的 baseline
>
重复构建和重新验证
```
