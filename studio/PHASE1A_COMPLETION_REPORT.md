# ARCH Studio Phase 1A Completion Report

日期：2026-09-13。范围：目标文件 PHASE1A_TARGET.md 的 M0–M7；用户在 M5 暂停后明确授权继续。停止于 Phase 1A，不进入 Phase 1B。

## 交付与使用

真实本地 Plotfile → PlotfilePreviewProvider → LinePreviewData → H5Web LineVis → PlotfileInspector 已实现。Mock Provider、Heatmap Renderer 和原有集中状态保持不变。

运行 npm run dev，访问 http://127.0.0.1:5173/，选择 Real Plotfile，打开 tests/fixtures/sod-1d.h5，选择实际字段。点击绘图区选择最近样本，或输入样本编号。文件只读，UI 不接触 HDF5 dataset 对象。

分支 studio/phase1a-plotfile 源自冻结提交 286b476c3c7ca97fa102a865c18e0af3acd5cc7a（studio-phase0-v0.1.0）。本阶段 checkpoint message 为 feat(studio): integrate real 1D ARCH plotfiles，tag 为 studio-phase1a-v0.2.0。最终 hash 以该 tag 的 git rev-parse 结果为准，并在交付消息报告。

## 验收逐项证据

| 目标条目 | 证据 |
|---|---|
| 1 分支基线 | 开发前 clean tag 派生记录在 STATUS；HEAD 在提交前仍为冻结提交 |
| 2、17 Mock 保持 | 原有 12 项测试不变；最终浏览器切回 Mock 后生成 current 512×512 预览 |
| 3、4 文件打开 | 浏览器选择既有 Sod CPU 文件成功；相同文件副本纳入可复现集成测试 |
| 5–9 来源与元数据 | REAL PLOTFILE、filename、time=0.15、1D、cartesian 已在浏览器 Inspector 验证 |
| 10、11、15 字段枚举 | 真实 Data 字段 DENS/ENER/PRES/VELX；测试和浏览器均验证未知 custom_species 名称可用，无字段名白名单 |
| 12、13 曲线与范围 | LineVis 显示 DENS/PRES；DENS 0.125–1，PRES 0.1–1，与源数据一致 |
| 14 点检查 | 点击 PRES 第 33 样本得到 x=0.5078125、value=0.3054751636143017；h5py 独立核对一致 |
| 16 错误与恢复 | 非 HDF5 浏览器提示与重新打开恢复已验证；错误矩阵和请求恢复有自动测试 |
| 18 检查 | 22/22 tests、lint、typecheck、build、git diff --check 通过 |
| 19、20 Core 与 baseline | 全部变更限定 studio；无 ARCH 编译/运行/baseline 重测；外部 STATUS 哈希保持 |
| 21、22 范围 | 只读必要 Grid/x；无 level/morton/hierarchy/ghost/.par/Setup/Init/Build/Run/SSH；执行按钮禁用 |
| 23–25 状态与封箱 | STATUS 按 Milestone 更新，按目标 message/tag 创建本地 checkpoint 后停止；不自动 push |

## 测试矩阵

22 项测试包括保留的 12 项 Phase 0 测试，以及 1D 适配、真实文件集成、异步请求测试。

- 真实 metadata、实际字段枚举、未知字段、min/max、精确样本值：plotfile.test.ts。
- 非 HDF5、非 ARCH HDF5、缺 Data、缺 time/dim/geometry：集成测试拒绝。
- 不支持字段类型、空字段、NaN/Inf：集成测试拒绝，随后有效字段可恢复。
- 文件读取失败：注入 arrayBuffer rejection，确认失败传播与无虚拟文件泄漏。
- 取消选择：requests.test.ts 验证 null/empty FileList 返回空值；组件在启动请求和清空旧数据显示之前返回。
- 旧异步成功/失败不能覆盖新结果；当前错误之后可恢复：生产 latest-request guard 的直接异步回归测试。
- Renderer 隔离：源码依赖审查，LineRenderer 仅消费 LinePreviewData；未引用 h5wasm 或 Provider。
- 浏览器回归：真实打开、字段绘图、原名未知字段、点选和编号选择、来源标识、Mock 切换。

验证边界：浏览器工具不支持 setFiles([])，因此未自动操作原生文件对话框的“取消”按钮；取消行为已在应用输入边界测试。错误矩阵主要为应用级自动测试，不宣称逐项进行了原生对话框端到端验证。

## 科学数据边界与依赖

真实 fixture 是 baseline 已生成 Sod 文件的原样小型副本，来源见 tests/fixtures/README.md。测试未知字段的浏览器副本只重命名 Data/DENS，用于 UI 验证，位于忽略的 .local，不提交。未新运行模拟。

适配器配对排序 x/value，拒绝重复、非均匀、非有限坐标及长度不匹配数据。仅支持简单 1D uniform 标量；不宣称通用 HDF5 或 AMR 支持，不构造不存在的 TEMP/species。

新增 h5wasm 0.10.3，记录在 DEPENDENCIES.md。完整 NIST/HDF5 许可证见 licenses/h5wasm-LICENSE.txt；与安装包许可证 SHA256 一致：f3ba6b8afe2a0d6f482f29a46672f88668ae02b16dfc4a2e878fd50a4f34fa6a。DEPENDENCY-LICENSES.json 已按 lockfile 更新为 294 条记录。

## 已知限制

- 文件上限 16 MiB，dataset 上限 100 万元素；WASM 在主线程读取，小文件之外无性能承诺。
- 构建主 JS 6,123.69 kB、gzip 1,394.48 kB，包含 WASM，保留 Vite 大 bundle 警告；未扩展优化任务。
- 切离真实模式会卸载该模式，重新进入需重新选文件；Mock 状态保留。
- 主要真实依据为 Sod CPU；不宣称所有 CUDA/Cellular 数据均支持。
- 无自动 push、远程部署或后续阶段工作。

Baseline 记录 E:/.Codex/.ShiroAkane/wsl-setup/STATUS.md SHA256：CCE6860A4D0E64619E905DC180826BE28CCCE1F3100104771F4C07E560D53379。

## 暂存审查说明

完整 git diff --cached --check 报告了 PHASE1A_TARGET.md 与原样 h5wasm-LICENSE.txt 的原文尾随空格/末尾空行。保留原文，不修改许可证；排除这两个导入文档后，其余暂存文件的空白检查通过。前述普通 git diff --check 不包含当时尚未跟踪的文件，以此完整暂存说明为准。fixture 受仓库 Git LFS 规则管理，新 checkout 需正常取回 LFS 内容。
