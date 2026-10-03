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

## 第二轮实际窗口复验与增量修复

从 1abb4d91 clean tree 开始；同一已验证 live desktop PID673163/start3322608，
通过应用 View→Reload 加载 index-6fySSCxx.js，无替代 observer/DOM 事件注入。
Sod 全部叶块轮廓开启时 .125/1 两段场线均在轴框内可见：F2 native 复验通过。
Zoom 后物理点查询返回 index191/block11/i15，DENS=.125、中心 .4990234375，
bounds [.498046875,.5]、measure .001953125；查询点 .4997936886036577 在其内。
原生值保持，未拿 display mean 替代。

CellularDet 同窗口切换文件并显式 metadata/LOD：
time0、20×[16,16]、5120原生cells、32×24显示pixels；
文件 SHA78104fab2ea63a58fd15cb82a4c227e0fee3c5c8c1726091e65855ddcd4dd187。
热图与叶块轮廓正确呈现 x1[0,25.6]/x2[0,12.8]，Level2取消只隐藏轮廓，
Zoom→x1[2.56,23.04]/x2[1.28,11.52]，真实 drag pan 时共同移动，
Fit恢复完整domain。未将这次32×24显示网格等同于non-square原生block测试。

点击左部高密度区域，原生Inspector返回index3074/block12/j0/i2，
query [.5243270271866453,6.509655172413793]、
center [.5,6.5,0]、DENS43375362.843074、
x1bounds[.4,.6000000000000001]、x2bounds[6.4,6.6000000000000005]、
measure .040000000000000015、level2/key2/0/2/0。
独立 h5py 单元读回确认这些值与 FP64 dtype、point inclusion；
处理后的两个单元摘要见 PlotfileNativeDesktopPoint-20261003.Summary.json，
不是全场或演化一致性证明。

### F3：一维默认 Zoom 将高低场线裁出 ordinate（修复待 native）

上一版默认zoom同时缩小field Y范围，在Sod两段常数场上导致field看似消失。
Plotfile spatial navigation 现在1D只改变x1，2D仍改变x1/x2；
同一helper同时用于LOD与原生slice的button/wheel/pan。
Inspector/hit testing仍用同物理x1及原生bounds；没有改数组、Core、配置或科学预算。
补足wheel hooks维数依赖；没有关闭lint规则。

### F4：新增只读工作区控件深色对比度（修复待 native）

此前未继承专用样式的button呈黑字深底，实际可点击却像disabled。
加入ProjectPlotfileAudit局部CSS作用域：按钮、输入、fieldset、键盘focus；
不改变全局产品、配置面板或其它工作区。

最终 npm test305/305、lint/typecheck/build/diff PASS；新production assets
index-DjuR4Vqq.js/index-BAqTuhxL.css。
当前已打开窗口仍是此前assets，不将F3/F4算作native复验PASS。
viewport refinement/race/cancel/recovery、field切换、wheel原生操作和publication/科学语义剩余项继续待办。
Linux production实际Sod/2D读取、显示、point链路已取得证据，但全项目与完整Viewer交付未完成。
本轮没有Core构建、simulation、Preview、push、tag、main merge或Windows适配。

## 第三轮实际窗口复验：F3/F4 已验证，显式 refinement / Fit

实现基线 4e9a364081e097271b2988bd5ce1ec86b864f3b8，操作前 working tree clean。
同一 production Linux WSLg 窗口、PID673163，应用 View→Reload 后加载
index-DjuR4Vqq.js / index-BAqTuhxL.css。继续使用既有 Sod t=0 文件；
没有重新编译 Core、运行 simulation 或生成 Preview。

真实窗口依次执行 Read metadata、Read global display LOD：
12×16 原生 shape、192 stored leaf cells、32×1 显示像素、同一文件 SHA。
初始 x1=[0,1]；显示 ordinate 约 [.08125,1.04375]，两段 DENS=.125/1 可见。
Zoom in 后 x1=[.1,.9]，ordinate 和高低场线保留：F3 缩放复验通过。
真实 drag (850,575)→(990,600) 后 x1 轴显示约 [.0007815,.8008]，
ordinate 保持原范围，场和 block outlines 一同水平移动：F3 平移复验通过。
这段轴读数为截图显示精度，不能当成原生 cell/科学数组精确值。

在这个平移视口点击 Read finer current viewport 后，显示图更新：
Sod discontinuity 附近出现按该视口重新分桶的过渡显示像素，原生叶块轮廓保持。
这仍是 display mean，不是原生存储场变化。Fit full domain 后恢复 x1=[0,1]
以及保留的全域 LOD；没有把 refined-domain 当成 full domain。
按钮、输入和 fieldset 的深色样式已实际加载，启用按钮文字和禁用状态可区分：F4 复验通过。

证据边界：此次只验证上述可见交互和 domain/ordinate；未用截图宣称 HTTP 请求次数、
无配置写入、取消/race 保留行为或 wheel 注入已通过。drag 后部分轴文字出现浏览器选中高亮，
不影响这次映射验证，后续可单独作为 UX finding。完整 Viewer / 联合科学交付仍未完成。
原生 point FP64 独立读回沿用第二轮处理摘要，不重复未变验证。
本轮只更新报告，不改源码；305 项回归及 lint/typecheck/build 的结果沿用 4e9a3640，
没有为文档变更重复运行全套测试。
