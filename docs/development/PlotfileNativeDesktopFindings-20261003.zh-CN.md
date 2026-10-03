# Linux production Plotfile 桌面 UAT：第一轮 finding

基线：2f41c0f9c2c0289a55f492d95f67447b97aea228。
本轮使用既有 Linux Electron、production assets 和本机既有 t=0 H5；没有新 Core Build、simulation 或 Preview。
Computer Use 对真正 WSLg 独立窗口操作；没有用 DOM 注入/HTTP/SSR 代替 native UAT。
完整联合目标未完成。

## F1：stale binary 阻断只读桌面启动（已修实现，native 启动复验通过）

原 launcher 显示 Local Host failed: Tracked source or compiler inputs changed. Build required before real preview。
host/desktop.ts 优先调用 WorkflowRunner.discovery，内部要求 Preview freshness，因此只读工作区也不能进入。
改为静态 selected-binary ConfigurationAdapter.discovery；保留其 binary fingerprint 双重检查和 authoritative registry/source mapping。
启动不授予 Init/AMR 权限、不伪造成功 Build。缺失/不支持 registry 仍失败。

回归使用真实可执行 fixture、变更 tracked source 与 binary：
静态注册可读、buildId=selected-binary:<sha>、fieldModels=[]、amr=null；
Preview readiness=false；Workflow discovery 与 Preview start 继续拒绝。
真实重启后窗口 ARCH Studio — ARCH-compute-optim 已出现，Real Preview 禁用并显示 Build required。
旧 launcher 正常关闭后 /proc/672396 已消失，再启动 PID673163/start3322608；没有并行复制旧会话。

## F2：Sod 极值场线被边界/轮廓遮挡（根因复现，修复待 native 复验）

真实页面依次选择 Real Plotfile、输入 Sod 本机路径、Read metadata、Read global display LOD。
观察到 t=0、Cartesian 1D、12×16 shape、SHA3817608…、192 stored cells→32×1 display pixels。
来源区域显示 Sod、actual binary a5d34672…、raw config digest、ideal gamma1.4、SodGas；
run/effective/build/sourceGit/unitSystem 保持 unknown。

图初始所有 native outlines 可见，但 DENS=.125/1 水平线贴满 frame 边界；
关闭 native outlines 后线显现，证明极值范围+轮廓遮挡，而非 reader 没有数据。
加入仅 1D display Y 范围 5% 留白；保持 raw数组、finite field/color range、2D spatial domain 和 checkpoint 不变。
相关测试证明 raw极值落在 clip 内、原值/色标范围不变及非有限值保留。
已生成新 production assets index-6fySSCxx.js；当前已打开窗口仍为此前 assets，不能把它算作修复后的 native PASS。

## 本轮验证

npm test 304/304 PASS；lint/typecheck/build/git diff --check PASS。
首次测试 303/304：旧精确断言 .08125 与计算值 .08124999999999999 不同；
修正为显示轴舍入预算断言（Number.EPSILON），未调整科学验收阈值。
Vite 保留已有大 chunk 非阻断提示，没有借机打包重构。
原始 H5、plt、checkpoint、完整输出留本机 ignored；本次仅改 Host/Desktop gate、display range、相关测试与报告。

## 未完成 / 后续

- 新 assets 的 Sod field/outline native 复验、zoom/pan/Fit、原生 point Inspector。
- CellularDet 2D 实际窗口、field切换、level filter、viewport refinement、race/cancel/failure retention。
- 当前 native scroll wheel 注入没有观察到滚动，实际 scrollbar drag 可用；不据此断言 app wheel broken。
- Real Config 上“Build selected … tracked inputs validated”文案与 stale readiness 并列，可能造成误读；需要单独核对状态表达。
- 按 23ff77c4f contract 继续补 reviewed单位/基底/测度normalization、完整来源身份与读取适配验证。
- 全域扫描成本、缓存/索引与大文件仍待；固定像素不代表首次扫描量小。
