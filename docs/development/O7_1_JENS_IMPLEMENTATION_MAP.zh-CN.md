# O7.1 JENS：调用方、修改点、独立参考与测试映射

2026-10-03；只读审计基线 fd9502c3ffb5de9c9571d57319a074d4b6c82b00。
本文件是联合执行细则第2节要求的代码前映射，不是实现完成或科学批准。
前序全模型桌面验收仍未完整关闭；本次仅准备数值工作，不提前开放JENS能力。

## 已冻结语义

唯一公式 N_J=sqrt(pi*c_s²/(G*rho))/max(h_active)，G引用共享CGS常数。
rho为已接受总正密度，不是Poisson扣均值密度。声速消费现有EOS/组成闭合；
一般EOS不使用固定gamma。尺度消费GridMetrics::PhysicalSpacing的活动方向最大值，
不使用block宽度、min(dx)或volume的立方根。
不得引入JeansSolver/Manager、jens_enable、G_override、rho/cs/压力floor。
jeans_cells仅一个AMR控制，显式有限实数≥4；推荐8仍非运行默认。
曲率refine_threshold/derefine_threshold不能用于Jeans格数。

## 真实当前代码与修改映射

| 调用方/所有者 | 当前证据 | 实施修改点 | 必需独立证据 |
| --- | --- | --- | --- |
| physics/diagnostics | 现有VelocityDiagnostics被AMR和Plot共用；尚无Jeans叶函数 | 同目录内一个Host/device可调用数学叶函数，AMR/plot两消费者共享；完整职责才单列头文件 | 独立IdealGas解析值、密度/声速/网格缩放、极低正密度和非法/溢出 |
| RefinementIndicatorMath.h | make_selection明确拒绝refine_on_jeans；cell_error只计算Löhner曲率 | Jeans走独立分辨约束并合并flag，不能作为新曲率Field送入cell_error | 均匀零曲率也细化；任一refine/全部可粗化 |
| RefinementThermodynamics.h | BindRefinementThermodynamics批量只提供pressure/temperature/gamma1，HostHydroScope限制临时EOS workspace | 仅JENS启用时取得真实cs²；缓存若保留绑定接受状态/组成/EOS/AMR epoch，不能从旧子阶段复用 | Ideal/Helm/Tabular参考；未启用无新增EOS/遍历；缓存失效矩阵 |
| GridMetrics.h | PhysicalSpacing已有Cartesian和当前曲线坐标定义 | 复用，不复制几何公式；RZ未来改变共享度量后再验收dr/dz | 各向异性、活动轴、极区/原点代表 |
| AmrTree.h EvaluateRefinement | EOS批量→每cell error→块最大error→refinement_flag；当前无Jeans约束 | 独立最小N_J归约，flag合并与明确欠分辨失败；不静默受lrefinemax抑制 | 阈值边界、层级/容量失败的cell/level/minN_J诊断 |
| AmrTree PreparedRegrid / Block.h / RegridTransferMath.h | staging候选树、保守restriction/prolongation后才发布 | 候选父态/父尺度资格在发布前检查，复用保守迁移；拒绝不解析的粗化 | 父态规则待确认；多层/反复迁移与原守恒预算 |
| Driver.h / runtime/DriverRegrid.cpp | 初始deferred regrid走perform_regrid；普通loop按regrid_interval调度；device flags通过同一PrepareRegrid | 初始和每已接受宏步的JENS检查；下一步前满足约束，不受普通interval延迟；不在RK/burn/RKL子阶段改树 | 接受状态与子阶段边界、restart后重算、关闭路径零额外扫描 |
| io/plot/PlotIO.cpp | 已有派生VORT/DIVV；当前未提取JENS；RuntimeParams会禁用jens输出 | 从当前状态/EOS/物理尺度计算JENS，不持久保存为守恒场；只输出不改变推进 | 输出开关同轨迹、regrid/restart后重新计算、单位1 |
| RefinementSelection.h / RuntimeParams.h | 两路径仍以not implemented警告禁用JENS | 整条CPU实现完成前保持unavailable；完成后按明确条件拒绝显式不适用请求，ALL过滤 | 不fallback到DENS、无self gravity的显式输出错误、ALL行为 |
| InputResolution.h / 标准目录 / API PresentationMetadata.cpp | 已有JENS名称和unavailable信息；尚未新增jeans_cells | 一个目录登记条件必填、≥4、单位1、用途/组别；接口身份和checkpoint同步 | schema/inspect/Setup/direct C++一致；missing≠0；无静默填8 |
| Studio runtime schema / AMR表单 | 当前保持JENS unavailable；动态消费binary schema | 跟随已发布能力，新增控制来自Core；不本地复制默认或科学公式 | 原文/Undo/缺项定位/版本和Build身份 |
| cuda/amr/RefinementIndicators.cu | 已复用cell_error和既有workspace/归约，CPU前尚不验收CUDA | 共享数学/flags，新增最小值与失败证据；只回传必要块决策，不下载全域 | CPU后统一构建；真实cuda identity/device safety及相同拓扑规则 |

## 实施与检验顺序

1. 维护者确认下列科学待决项；同时完成前序全模型桌面出口。
2. 共享数学叶函数与CPU解析单测；随后配置条件/只输出路径，不能先声明AMR支持。
3. 接受态EOS证据、block约束和候选父态拒绝；再接初始/每接受宏步事务与上限失败。
4. JeansWave/GravityBox受控尺度场景、重启和输出开关对照；守恒及势/力参考保持原门槛。
5. 完整CPU出口后同步API/Studio支持与用户文档，再按联合计划统一CUDA。
6. 只有科学输入/预算冻结后运行正式benchmark和长轨迹；当前不运行探索simulation凑证据。

沿用 tests/host/amr/test_refinement_indicator_math.cpp、
tests/cuda/amr/test_refinement_indicators.cpp 及现有AMR/重启/配置入口；
独立Jeans数学责任可有小型专用单测，但不另建平行完整CI矩阵。
本次没有执行测试，不能把“存在测试文件”算JENS通过。

## Core维护者待确认：不从测试结果倒推

- 当前EOS闭合声速作为一般EOS定义是否已有准确批准ref？Helm/Tabular独立oracle及误差预算是什么？
- 候选父态是否明确采用现有几何加权保守restriction结果，再用父网格max(active PhysicalSpacing)判断？
  阈值附近的父态/细化策略与临界舍入如何验收？不新增第二可写阈值。
- jeans_cells条件必填在only-output与AMR两路径的最终条件；推荐8是否冻结为模板建议？
- JeansWave/GravityBox尺度变化输入、误差/守恒/势力预算和集成终点。
- 原特殊非物理G输入换算仍待批准，不能作为已迁移JENS科学参考。

科学批准引用尚未在本次审计中取得，保持pending。这不阻塞Linux桌面/plt接口审计等已授权工作。
本文件不修改Core、不编译、不Preview、不演化、不push/tag。
