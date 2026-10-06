# ARCH Studio Phase 1A M5 阶段报告

日期：2026-09-13

## 结论与停止边界

已完成用户授权的 P1A-M0 至 M5：本地真实 ARCH 1D Plotfile → 独立 Provider → LinePreviewData → H5Web LineVis → 真实 Inspector。按要求停止。本报告不是整个 Phase 1A 的 M7 封箱报告；未创建 Phase 1A commit 或 tag。

分支：studio/phase1a-plotfile。
HEAD 及 Phase 0 tag 指向的提交：286b476c3c7ca97fa102a865c18e0af3acd5cc7a。
当前工作树包含未提交的 studio/ 变更。

## 交付内容

- 本地文件选择，16 MiB 文件上限，只读打开；释放 HDF5 句柄与虚拟文件。
- 读取 time、dim、geometry；从实际 Data datasets 枚举原始字段名。
- Provider 与 Mock Provider 并列，HDF5 对象不进入组件；组件只调用 Provider。
- 独立 LinePreviewData 与 1D Renderer，不重写 Phase 0 Heatmap 数据契约、Renderer 或状态模型。
- 只读取必要的 Grid/x，与对应字段数组配对排序；要求坐标唯一、均匀，拒绝空值、非有限值及不匹配数组。数值 dataset 上限为 100 万元素。
- H5Web LineVis 显示字段；点击绘图区选择最近样本，另有可键盘操作的样本编号输入。
- Inspector 显示文件、时间、维度、geometry、字段、min/max、x/value；未选择字段或样本时显示不可用信息，不补造 TEMP/species。
- REAL PLOTFILE 与 MOCK / DEMO 标识明确。Build、Start、Monitor 保持禁用。

## 真实样本与独立数值核对

使用已存在的 output/local_sod_cpu/SodBeginner_HLLC_plt_0003.h5，没有运行 ARCH 生成新数据。

| 项目 | 浏览器结果与源文件核对 |
|---|---|
| time / dim / geometry | 0.15 / 1 / cartesian |
| 字段 | DENS、ENER、PRES、VELX，来自真实枚举 |
| 样本数 | 64 |
| DENS min/max | 0.125 / 1 |
| PRES min/max | 0.1 / 1 |
| 第 33 个样本 x | 0.5078125 |
| 第 33 个样本 PRES | 0.3054751636143017 |

压力样本读数通过现有隔离 Python 环境的 h5py 独立读取核对，与浏览器完全一致。已检查真实密度曲线截图，并实际点击压力绘图区验证 Inspector。

## 验证结果

- npm test：14/14 通过；原有 12 项测试保留，新增 2 项适配测试覆盖未知字段名保留、坐标/数值配对、范围、最近样本及空/非有限/重复/非均匀数据拒绝。
- npm run lint：通过。
- npm run typecheck：通过。
- npm run build：通过，存在 bundle 大小警告。
- git diff --check：通过。
- 浏览器：真实文件打开、元数据与字段列表、DENS/PRES 绘图、点击选择、编号选择、来源标识均已验证。
- 切回 Mock 后可正常生成 512×512 current 预览，原有状态和字段测试全部通过。
- 非 HDF5 文件显示明确可恢复错误，清除旧文件数据；再次打开有效文件并选择 PRES 后恢复正常。
- 类型检查与源码审查确认 LineRenderer 无 HDF5 reader 依赖；Mock Provider、Heatmap Renderer 和原有状态文件未修改。

## Scope 与 baseline 审计

每个 Milestone 边界按 PHASE1A_TARGET.md 核对并更新 STATUS.md。
所有 tracked 与 untracked 开发变更位于 studio/。未修改 ARCH Core、根目录 baseline 记录；未执行 CPU/CUDA 编译、ARCH 运行或 baseline 重测。未实现 AMR hierarchy、ghost 处理、2D、.par、Setup/Init、PreviewExtractor、Build/Run、SSH 或远程服务。

外部 baseline 记录 E:/.Codex/.ShiroAkane/wsl-setup/STATUS.mdSHA256 保持：
CCE6860A4D0E64619E905DC180826BE28CCCE1F3100104771F4C07E560D53379。

## 依赖与已知限制

新增 h5wasm 0.10.3，使用公开 API。完整 NIST/HDF5 许可证保留于 licenses/h5wasm-LICENSE.txt，SHA256 与已安装包 LICENSE.txt 一致：f3ba6b8afe2a0d6f482f29a46672f88668ae02b16dfc4a2e878fd50a4f34fa6a。

- production 主 JS 为 6,123.63 kB，gzip 1,394.46 kB，包含 WASM；本次未扩展性能优化。
- 同步 WASM 解码仍在主线程；仅面向小型、完整写入的本地文件，不承诺大文件交互性能。
- 切离真实模式后该模式会卸载；重新进入需重新选择文件。Mock 状态保留。
- 只实测简单 Sod CPU fixture；不宣称支持所有 ARCH Plotfile、CUDA 文件或 Cellular AMR。
- 未执行 M6 全部错误矩阵与自动化 UI/竞态测试：取消选择、所有缺失元数据组合、读取失败注入及异步竞态等仍需后续授权的 M6 验证。当前代码有请求序号保护，但不将代码存在等同于竞态测试通过。
- 未执行 M7 依赖全量清单再生成、release archive、提交与 tag。Phase 0 的 DEPENDENCY-LICENSES.json 保持原封箱记录；本阶段依赖增量见 DEPENDENCIES.md 和完整许可证副本。

## 使用

在 WSL 的 studio 目录使用现有 Node 环境运行 npm run dev，访问 http://127.0.0.1:5173/ 。选择 Real Plotfile → Open Plotfile → 选取上述既有 Sod 文件 → 选择实际字段 → 点击曲线检查样本。

当前浏览器已保留真实 PRES 曲线及第 33 个样本的 Inspector。后续工作等待用户的新目标。
