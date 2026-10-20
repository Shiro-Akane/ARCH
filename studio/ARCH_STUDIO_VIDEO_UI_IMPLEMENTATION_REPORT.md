# ARCH Studio 视频 UI 实施与验收报告

## 结论与范围

视频反馈中的 V01–V10 已完成针对性修改；V11 采用显著、可收起的 Terminal，V12 按视频明确意见延期。当前 production frontend 与真实 CPU Core/Local Host 已完成浏览器桌面 UAT。Linux Electron 独立窗口确实启动并加载 production assets，但原生窗口捕获无法对应到目标窗口，其视觉和原生文件对话框验收未确认，不把浏览器结果写成原生桌面通过。

本轮只修改 Studio 呈现和必要交互适配，未修改 Core 科学源码、Host 业务逻辑、Parser/round-trip、Working Copy/EditHistory 或 Preview/AMR 执行器。UI实施与本地验收阶段没有运行 simulation、生成科学 Plotfile/checkpoint、修改物理定义、扩大阶段、push 或 merge。后续用户已明确授权发布本次 review branch / PR，仍不授权合并。原始视频、完整日志、H5、构建产物及截图保留本机。

## Git 与交付身份

- origin： https://github.com/Shiro-Akane/ARCH.git
- fetch 后共同基线：compute/optim，e69a859705da0efc0c7abad64c8949f9e4934951。
- 发布 review branch：studio/ui-video-review-20261010，直接从当前 origin/compute/optim 的 e69a859705da0efc0c7abad64c8949f9e4934951 创建。
- 使用本轮独立开发 worktree；不上传个人工作目录路径或环境配置。
- 原本地验收 checkpoint：studio/video-ui-review-20261010 / studio-video-ui-2026-10-10 → 0915439eda3d36c8fd6ea0be019c07b093b6f08c，保持不变。发布分支重新以当前上游为父提交，采用 feat(studio): refine UI workflow and simplify interface；未改写原 checkpoint。
- 旧 Studio 集成工作树保持 clean，HEAD b7cb8b69845d44d6cb0ec4d6b19f4aabb4c65b14。共同线包含它，0 ahead / 71 behind；未重做历史集成。
- Node 24.21.0 / npm 11.19.0；新工作树执行 npm ci，不复用旧 node_modules。Git author 仅沿用仓库 local 配置。

## 视频问题 → 修改 → 验收

| ID / 视频区间 | 修改前问题 → 当前行为 | 状态 | 验收证据 |
| --- | --- | --- | --- |
| V01 / 00:00–00:58 | 四项 workspace 混合真实工作与历史样本 → 正常入口只有 Config / Plotfile，默认 Config；样本保留 ?demo=hotspot / ?demo=cellular 调试入口，仍明确 mock/snapshot | 完成 | workspace SSR 测试；production 两项选择；真实只读 H5 打开 |
| V02 / 00:28–01:26 | 顶栏与横栏重复身份 → 单一 MODEL / PARAMETERS 头部显示实际选择模型和文件、Unsaved；完整来源与关联折叠，文件名不符警告仍在折叠外 | 完成 | header SSR；Sod 与 Cellular 文件切换、保存后文件名同步；不依据文件名自动选模型或宣称 verified |
| V03 / 01:24–02:22 | 长列表滚走分类按钮 → sticky 分类导航，可切换和收起；导航展开态对应组展开态 | 完成 | 长 Runtime 滚动后导航仍在参数区域顶部，position:sticky；实际切换 Runtime / Grid |
| V04 / 02:20–03:46 | Case 与独立金色 Custom 区域分裂 → 统一 Case parameters，保留 schema、模型 metadata、真正文件 custom；retired 不混入 Custom | 完成 | 动态 schema/未来组/去重 SSR；Sod 102 个实际控件 key 唯一，含本次 95 个 base + 7 个 case；Cellular 8 个 case + 3 个 custom，xhe4 可搜索编辑 |
| V05 / 03:44–04:14 | 常用 Runtime 靠后 → Runtime 首位默认打开，其他组和未知 Core 组可达 | 完成 | 参数呈现测试与 GUI 导航；既有组内工作流排序保留 |
| V06 / 04:00–04:42 | 先填 axes 才看到 geometry → Coordinate system 在 Grid axes 之前 | 完成 | 参数呈现测试；GUI Grid DOM 顺序 |
| V07 / 04:40–06:06 | x1 与物理 x 重复 → 标题优先消费 Core displayName，raw axis/key 在 Help；Core 未给物理名称时保留 raw，不猜测曲线坐标 | 完成 | coordinate presentation 测试；Cartesian labels 与未激活 axes 的 Core 回退；真实预览 x/y + cm |
| V08 / 06:04–07:02 | 主操作分散、Run/Restart 另起区域 → Configure / Build / Update Preview / Run / Restart / Terminal 同一主操作组；状态/history 次级 | 完成 | workflow SSR；1280×720、1920×1080 单行所有六项可达；原 Run owner、准备/确认/停止逻辑保留，未执行 simulation |
| V09 / 07:00–09:22 | 每个字段重复 raw key、未知/无量纲和长说明 → label/control/权威物理 unit 常驻，完整三层值、来源和说明进键盘可达 Help | 完成 | Help Enter 展开；raw cfl=bad 错误与 Preview disabled/stale 明显，Ctrl+Z 恢复；Diffusion permitted/unknown/forbidden SSR；路径检查失败仍可见 |
| V10 / 10:16–11:42 | Open Config 容易误认为选模型 → Open Parameter File (.par) / Load Project Parameter File；Model selector 独立 | 完成 | 本地 file chooser 加载二维 .par；明确选择 CellularDet，保留替换确认和 Host Save 生命周期 |
| V11 / 06:04–07:02 | Terminal 入口不明显 → 主栏显著 Terminal 按钮，有界输出可展开/收起，状态不中断 | 采用可选方案 | 四项 workflow 测试；实际成功 Build stdout；关闭/重开 drawer 可查看日志。未故意延长 Build 以声称人工取消保护测试 |
| V12 / 09:20–10:18 | 面板宽度/UI scale 的讨论 → 按视频暂不做可拖宽和新缩放系统 | 明确延期 | 本轮没有 pane resize、移动端专用 UX 或额外 UI scale 功能 |

数字只描述本次 binary 的证据，不成为永久前端目录/模型计数常量。所有原有标准参数继续由 runtime schema 枚举；未知组/字段、显式非法 token、retired 与 forbidden removal 路径保留。

## 实际变更文件

| 范围 | 文件 |
| --- | --- |
| 正常入口与头部 | studio/src/App.tsx；studio/src/components/WorkspaceHeader.tsx；studio/src/data/workspacePresentation.ts；studio/src/components/ConfigurationBridge.tsx；studio/src/shell-review.css |
| 参数分组、坐标和 Help | studio/src/components/ParameterPanel/ConfigPanel.tsx；StandardCatalog.tsx；parameterPresentation.ts；parameter-review.css |
| 主工作流与 Terminal | studio/src/components/WorkflowBar.tsx；RunControls.tsx；workflow-review.css |
| 样式和 production 页面信息 | studio/src/main.tsx；studio/index.html（移除过时 Phase 0/mock-only 标题与描述） |
| 回归 | studio/tests/ui-render.test.ts；parameter-video-review.test.ts；workflow-presentation.test.ts |
| 本轮报告 | studio/ARCH_STUDIO_VIDEO_UI_REVIEW_2026-10-10.md；studio/ARCH_STUDIO_VIDEO_UI_IMPLEMENTATION_REPORT.md |

CSS 在 main.tsx 集中导入，避免 SSR 测试 loader 将组件侧 CSS 当 JavaScript 解析。Run/Restart 保持一个状态所有者，只把 actions/status/details 置入同一栏；执行函数不重写。

## 自动检查

| 检查 | 实际结果 |
| --- | --- |
| npm ci --no-audit --no-fund | PASS，独立依赖目录 |
| npm test | PASS，379/379，0 failed / cancelled / skipped |
| npm run test:host | PASS，148/148；是专用 Host 重跑集合，与 npm test 有重叠，不宣称 527 个独立测试 |
| npm run lint | PASS |
| npm run typecheck | PASS |
| npm run build | PASS；最后仅 HTML title/description 调整后又执行一次相关 production build，JS/CSS hash 未变化 |
| git diff --check | PASS；报告完成后的最终检查及 checkpoint 状态见交付记录 |

测试覆盖已有 .par round-trip、Undo/Save、动态 schema/retired/validation、Host 请求边界、Build/Preview 身份、warm session、cancel/timeout/late-result retention、AMR 与只读 Plotfile。没有把 UI 测试当作科学演化或 CUDA 数值验收。既有 >500 kB bundle 提示仍在，非阻断，未借此开展拆包性能任务。
## 真实 Core / Build 身份

首次只读审计没有发现共同线 e69a 的现成 matching binary/manifest，因此建立本轮独立 CPU Release build，而不是把旧 b7cb binary 冒充新源码。

- source root：本轮独立 worktree（具体绝对路径仅保留本机 Manifest，不交付个人路径配置）。
- source Git HEAD：e69a859705da0efc0c7abad64c8949f9e4934951；构建时 repository dirty=true 来自本轮 Studio UI 编辑，科学源码未改。
- build directory：build-video-ui-cpu；target ARCH；输出 build-video-ui-cpu/bin/ARCH。
- CPU Release / GNU 13.3 / C++20 LTO / CUDA OFF / KLU ON；没有 CUDA 编译或性能任务。
- executable SHA-256：240a4f573631c76daf45797ba7b0f95512e3050d2bb67f7e9f2131b4e9b47d23，16484112 bytes，mtime 2026-10-10T09:49:11.887Z。
- 受控首次构建 parallel=20，available-memory 下限4 GiB、swap-growth 上限256 MiB、timeout1800s，并检查宿主磁盘余量；约65s完成，owned RSS peak约7.37 GiB、minimum available约15.20 GiB、swap growth=0，未触发 guard。
- 当前 binary 只读能力检查通过：schema v3、95 standard keys、16 registered models；Sod/Cellular source association与compiled source SHA匹配。注册不等于全部 Preview 支持。

production GUI 中又执行一次 Host-owned existing Build Profile 的标准 Build，Ninja 明确 no work to do；这是获取真实成功 Manifest 的一次必要动作，没有手工伪造 Manifest。

- profileId：studio-existing-cadf3f1b73a92a85。
- profile fingerprint：0e940872eead90ab3b28d4578786236e9bb9602ca9898436d514c7b33527f851。
- buildId：799b719d-4bd6-4381-8fab-b3ddd33e5602。
- start/end：2026-10-10T09:56:57.388Z / 2026-10-10T09:56:58.377Z。
- inputsStableDuringBuild=true，Build 前后 binary fingerprint相同。

完整 dependency freshness 仍 UNKNOWN：该既有配置没有 CMake file-api reply；link input identity 对 archive 大小/可用性检查失败，runtime root identities 未提供。界面如实显示 freshness-unknown、compiler identity incomplete；Preview 使用 last successful tracked build，明确未独立证明全部依赖 freshness。不能把这写成完整 build current 或 case ↔ binary verified。最终 UI-only commit 改变 Git HEAD，不改变本次 tracked Core 输入，也不篡改历史 Manifest 的 source HEAD。

## production GUI 结果

| 流程 | 实际操作及结果 |
| --- | --- |
| 环境 | 新 production dist，localhost 127.0.0.1:4178；真实 Local Host 127.0.0.1:4180，显式项目与 matching CPU binary，非 dev server、非旧浏览器状态。Electron Linux也加载此 dist，不使用 Vite dev |
| 桌面布局 | 1280×720 / 1920×1080：六个主操作一行可达，参数导航粘附，中心和侧栏可独立滚动。760×720仅检查基础溢出和可达性，无横向页面溢出；窄窗口 workflow 可经普通页面滚动访问，不追求手机完整度 |
| Help / validation / Undo | 键盘 Enter 展开 cfl Help，原始key、来源和三层值仍可查。cfl=bad显示明确错误、阻止 Preview，旧成功 field/AMR stale保留；Ctrl+Z恢复0.4及Saved状态 |
| Save As / Reopen | cfl修改为0.35；GUI Save As在Linux真实磁盘写出Sod-video-saved.par，重新加载仍0.35。原始simulation/Sod/Sod.par及加载副本SHA均9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843；保存副本SHA78c53e778fe599ed5769d891f46d951afcf8f1d2ea8fae203b89cc6639a30d88。该验证不等于原生文件对话框验证 |
| Sod field | 真实512个init samples，x_pos=0.5 marker；Inspector index100，x=0.19628906，DENS=1/PRES=1；生成匹配Initial AMR：8 leaf / L0，identity一致 |
| Cellular input | 本地chooser打开实际二维.par，文件名不推断模型；用户路径明确选择CellularDet。旧CellularPreview2D.par缺tmax，Core如实报required并阻止Preview；只在本轮UAT Working Copy中显式插入tmax=0，原始fixture不改 |
| Cellular field / Inspector | 先128×128，再Nx32/Ny24，真实shape=[24,32]、x1-fastest、x3=0cm；7fields含VELY，EOS Helmholtz ready / 19 species，domain 25.6×12.8cm。切换Pressure，index100对应i4/j3、x3.6/y1.8666667；raw DENS约4.4054946e7、PRES约1.4649561e25 erg/cm^3、T约4.7126944e9K |
| Initial AMR | Cellular真实20 leaves：L1=4 / L2=16，两次completed passes；同一field/config/build/EOS匹配overlay。block 1:2:0:0显示level、bounds、16×16 cellShape、0.4cm spacing；无AMR cell field array，未把init samples叫AMR cell values |
| workspace continuity | Config → Plotfile → Config仍保留Cellular工作副本、模型、field/AMR；xhe4搜索只有一份custom控件 |
| Plotfile read-only | 打开仓库已有studio/tests/fixtures/sod-1d.h5：4blocks、每block16samples、Cartesian1D、x1-fastest。read samples/Inspector raw DENS=1，首stored center=0.0078125；未知单位/来源/完成发布状态仍标未证实，无插值替代原始值 |
| Terminal | 点击标准Build，真实no-work stdout、成功状态可查看；drawer Close只控制展开态，再打开仍保留有界日志 |
| 最后 production smoke | 最终HTML title为ARCH Studio；重载后选择正确Cellular模型，加载真实saved副本，当前real2DPreview正常；browser error/warn日志为空 |

人工取消尝试碰到 warm Preview 已先完成，没有伪称实际 cancelled；取消、worker reap、timeout、旧响应拒绝及失败保留由本轮已有 Host/Studio回归覆盖。未执行新的科学演化、restart或benchmark来代替UI验收。

## 本机证据与未完成项

处理后的自动检查、binary/Manifest及进程信息：本机未交付证据目录。完整媒体、GUI截图、完整日志、H5和生成输出保留本机，不提交。

| 文件 | 内容 |
| --- | --- |
| media-probe.json / media-sha256.json / asr-manifest.json / asr-full.txt | 全片时长、hash、26窗口音轨识别与覆盖；不是词级人工转录 |
| uat-1280x720-sod.png | 桌面Sod参数/真实field/布局 |
| uat-1920x1080-cellular-amr.png | 二维真实field、AMR和参数界面 |
| uat-terminal-build-output.png | 实际Build stdout/状态 |
| uat-760-basic-layout.png | 窄窗口基本布局 |
| uat-plotfile-readonly.png | 真实H5读取和raw Inspector |
| uat-final-production.png | 最终production标题及正常Config界面 |
| final-checks.json / host-manifest-summary.json / current-core-build-identity.json | 自动检查与构建身份 |
| process-cleanup.json | 已关闭本轮Electron、Host、production preview及其子进程，无该工作区ARCH worker，4178/4180不再监听 |

未完成/明确保留：原生Linux窗口视觉捕获及native dialog UAT未确认；V12 pane resize/UI scale延期；完整dependency freshness不独立证明；bundle size提示保留。均未借此进行科学、Host身份系统、性能或Windows适配重构。

## 发布前复核（2026-10-10）

git fetch origin 后确认 compute/optim 仍为 e69a859705da0efc0c7abad64c8949f9e4934951，它已包含此前 Studio 集成 checkpoint。原本地验收提交领先1、落后0，无divergence；新 review branch 从当前上游创建，再移入本轮19个Studio交付文件，无冲突、无需合并Core。

用户授权发布后，重新顺序执行 npm test（379/379）、npm run test:host（148/148，集合重叠）、lint、typecheck、production build，全部PASS。源码、测试、CSS、HTML与原UAT checkpoint一致；发布整理只更正两份报告的branch/授权状态并移除本机路径配置，沿用已完成的1280×720 / 1920×1080 production GUI验收及明确限制，没有新增UI改动触发重测。

个人Node/PATH设置、本地启动命令与未交付的.local测试配置不进入公开文档；启动方法按studio/README.md。最终Git仅交付Studio源码、测试和两份报告，node_modules/dist/.local/build、媒体、截图和完整日志未纳入diff。原本地tag、历史成果及原工作树保留；不直接更新compute/optim/main，不force-push，不合并PR，不进入新阶段。
