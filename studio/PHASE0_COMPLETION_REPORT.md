# ARCH Studio Phase 0 完成与自检报告

日期：2026-09-13（Asia/Tokyo）

## 结论与范围

Phase 0 的 M0–M6 已完成。已验证 Mock 工作副本 → Dirty/Stale → Preview → 三场渲染 → 点选 Inspector → Save/Revert 的闭环。本报告的“完成”仅指 TARGET.md 约定的前端原型，不是完整科学计算工作站、通用 HDF5 Viewer 或生产部署。

用户额外授权的 Cellular 运行/静态样本保留，但本次 M6 不继续扩展真实数据集成。没有重编译 ARCH，也没有重跑 baseline 套件。

## 启动与复现

Linux 工作树：`/home/arch/projects/ARCH-linux`；分支 `studio/phase0-demo`。
Baseline：`7d4448a9b4a07dd86aa8cfc83fe9d85c7d63a866`。Phase 0 checkpoint `57836fc75d7582c74b160a2f423bac133fea4068` 已提交并推送。正式 release commit 通过 tag `studio-phase0-v0.1.0` 定位；本次发布提交与 tag 的远端推送不包含在本地打包任务内。

```bash
cd /home/arch/projects/ARCH-linux/studio
export PATH="/home/arch/.local/opt/node-studio/bin:$PATH"
npm ci
npm run dev
```

开发入口：http://127.0.0.1:5173/ 。生产构建后 `npm run preview` 使用 http://127.0.0.1:4173/ 。这两个服务只绑定 loopback。当前已运行服务可直接复用。

```bash
npm test
npm run lint
npm run typecheck
npm run build
npm audit --omit=dev
```

## 功能与设计

- React 18 / TypeScript / Vite；状态集中在 studioState reducer。
- 参数有效性检查、异步取消及 revision 防护，避免过期生成覆盖新配置。
- PreviewData 将渲染与数据生成分离。Mock 默认 512×512，分辨率限制 2–512；非真实物理计算。
- H5Web 公开 HeatmapVis / Annotation / 数据坐标事件，支持色条、Viridis、缩放、平移、Fit 与单元点选。
- 点选读数为所在单元中心及该单元的三场值；字段切换保留选择，编辑清除旧读数。
- Save 仅更新内存中的 saved copy；Revert 恢复并取消异步请求。仅当现有预览参数匹配恢复配置时回到 current。
- Build/Start/Monitor 禁用。Composition 与 AMR 不在 Mock 中建模，显示空值。

## 22 条验收结果

| 编号 | 验收项 | 证据与结果 |
| --- | --- | --- |
| 1 | 启动 Demo | 通过：开发端口 5173 与 production preview 4173 均启动并加载成功。 |
| 2 | 三栏 ARCH Studio | 通过：桌面 Parameters / Preview / Inspector 三栏目视验证。 |
| 3 | Preview 为视觉中心 | 通过：桌面中央预览最大，手机优先显示 Preview。 |
| 4 | 修改 hotspot_x | 通过：浏览器编辑 0.65、0.6 等值成功。 |
| 5 | Config 立即 Dirty | 通过：浏览器状态及 reducer 测试通过。 |
| 6 | Preview 立即 Stale | 通过：编辑后显示 stale，旧图有提示并禁止旧值点选。 |
| 7 | 点击 Preview | 通过：开发及生产页面均验证按钮。 |
| 8 | Generating 状态 | 通过：500 ms 异步流程中已实际观察生成状态。 |
| 9 | 热点随参数移动 | 通过：M2 验证中心移至 (0.75,0.25)，半径与幅值改变；provider 测试验证。 |
| 10 | Preview 回到 Current | 通过：成功生成后 current；失败恢复后亦通过。 |
| 11 | 切换三个场 | 通过：M3 与生产包均检查字段选择。 |
| 12 | 三场图案不同 | 通过：Density 热点、Temperature 热点加梯度、Pressure 阶跃经目视及数据测试验证。 |
| 13 | 点选 Preview | 通过：数据坐标事件映射到单元，拖动阈值避免误选。 |
| 14 | Inspector x/y/rho/T/P | 通过：开发及生产包实际显示五个数值；偏移域、上边界和越界单元测试通过。 |
| 15 | Save 清除 Dirty | 通过：内存快照保存后 saved，浏览器通过。 |
| 16 | Revert 恢复最近 Save | 通过：0.7→保存→0.9→撤回=0.7；生产包 0.6 场景亦通过。 |
| 17 | 窗口缩放布局 | 通过：390×844、768×1024、1280×800 及先前 1280×720 检查通过，无横向溢出。 |
| 18 | lint/typecheck/build | 通过：最终执行均通过；12/12 单元测试通过。 |
| 19 | ARCH Core 无修改 | 通过：git diff --exit-code 及暂存区 diff 通过；所有未跟踪源码均在 studio/。 |
| 20 | Baseline STATUS 未改写 | 通过：实际文件 wsl-setup/STATUS.md SHA256 与初始值一致；根目录原本无 STATUS.md。 |
| 21 | Studio STATUS 完整 | 通过：已更新 M6 完成状态、限制、改动、依赖和停止动作。 |
| 22 | 不自动进入 Phase 1 | 通过：未实现通用 HDF5 viewer、真实 Build/Run 或后台服务；仅保留用户另行授权的 Cellular 静态样本。 |

## 测试结果与限制

- 12/12 Node 单元测试通过：状态转换、无效参数、三场数据、热点响应、数值溢出、取消、点选边界和 Save/Revert 快照一致性。
- lint 零警告、TypeScript 检查、production build 通过；生产依赖 npm audit 报告 0 已知漏洞。这不是完整安全审计。
- 浏览器验收覆盖开发和真实 dist 构建页面；不是跨浏览器自动化测试矩阵。
- 主动使用 hotspot_temperature=1e100 触发 provider 失败，验证 Failed 提示及 Revert/重新生成恢复。
- 原 M2 矮窗口热图裁切已修复；M6 未发现新的阻断问题。
- 未执行 CUDA/CPU baseline 测试；不能据前端检查宣称科学结果正确。

## 已知问题

1. H5Web/Three.js 依赖令主 JS bundle 约 1,310 kB，gzip 约 355 kB，存在 Vite >500 kB 提示；未隐藏该提示。
2. 开发浏览器出现上游 THREE.Clock 弃用警告，现有渲染和交互正常。
3. 依赖 WebGL；渲染异常由边界组件显示错误提示。未覆盖全部 GPU/浏览器设备。
4. Save 仅内存保存，刷新即重置，符合 Phase 0 要求。
5. Cellular 为固定一维静态结果，非通用文件导入器；该运行在 200 步终止，未达到设定 tmax。
6. 没有进行正式可访问性审计或长期性能压力测试。

## 依赖与许可证

直接前端依赖和许可证见 [DEPENDENCIES.md](DEPENDENCIES.md)。锁文件中 293 条解析依赖记录的许可证见 [DEPENDENCY-LICENSES.json](DEPENDENCY-LICENSES.json)，没有未声明条目；清单包括平台可选包，不表示全部包都在当前平台运行。
离线 Cellular 工具的 h5py 3.16.0 与 NumPy 2.5.3 仅位于被忽略的 studio/.local，非前端运行服务。未复制 Peacock/Splash 源码或品牌资产。发布分发前仍需保留各包自带许可证及 notices。

## 保护区与来源

- Baseline 文件实际位于 `E:\.Codex\.ShiroAkane\wsl-setup\STATUS.md`，没有覆盖不存在的根目录 STATUS。
- Baseline SHA256：`CCE6860A4D0E64619E905DC180826BE28CCCE1F3100104771F4C07E560D53379`。
- TARGET.md 与用户附件逐字节哈希一致：`4A1DCD3C7EECD055B6812A6835CC43D03A82EDA3F2DA7B0B1E9FB2EA086C3B9D`。
- 原 ARCH src/include/cmake/simulation/validation/tools/build-cuda 未修改。
- 先前 Cellular 测试输出位于独立 output/cellular_run_20260913_011131，保留且不参与 Mock 计算。

## Changed files

- `studio/.gitignore`
- `studio/DEPENDENCIES.md`
- `studio/DEPENDENCY-LICENSES.json`
- `studio/QA-M0.md`
- `studio/QA-M2.md`
- `studio/README.md`
- `studio/STATUS.md`
- `studio/TARGET.md`
- `studio/eslint.config.js`
- `studio/index.html`
- `studio/package-lock.json`
- `studio/package.json`
- `studio/scripts/import_cellular.py`
- `studio/src/App.tsx`
- `studio/src/components/CellularSample.tsx`
- `studio/src/components/Icon.tsx`
- `studio/src/components/Inspector/Inspector.tsx`
- `studio/src/components/ParameterPanel/ParameterPanel.tsx`
- `studio/src/components/Preview/Preview.tsx`
- `studio/src/components/Preview/Renderer.tsx`
- `studio/src/components/StatusBar/StatusBar.tsx`
- `studio/src/data/MockPreviewProvider.ts`
- `studio/src/data/PreviewData.ts`
- `studio/src/data/selection.ts`
- `studio/src/main.tsx`
- `studio/src/samples/cellular.json`
- `studio/src/state/studioState.ts`
- `studio/src/state/useStudio.ts`
- `studio/src/styles.css`
- `studio/tests/mock.test.ts`
- `studio/tests/selection.test.ts`
- `studio/tests/state.test.ts`
- `studio/tsconfig.json`
- `studio/vite.config.ts`
- `studio/PHASE0_COMPLETION_REPORT.md`（本报告）

## 停止点

Phase 0 完成并停止。没有自动开启 Phase 1；后续真实文件集成、部署或核心运行控制需新的明确目标。
