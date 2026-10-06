# O7.2–O7.5 RZ 实施映射与 Core review 包

基线 af18b44ae16023fa266bca2cf1a7fa0a2d5a1671。本步落实
JeansRZPlatformHandoff.zh-CN.md §2 的实施前独立 RZ 映射要求。
不是 RZ 实现、科学通过或能力开放；O7.1 CPU 前置及科学确认仍保留。

## 1. 已冻结语义与当前差距

Target 已明确 2D cylindrical=(r,z)，3D cylindrical=(r,z,phi)；
PointCoords.r 仍是球半径，r_cy/z_cy 是柱坐标。旧极平面只能经维护者复核
迁往现有二维 spherical 极平面，不进行字符串批量等效转换。

完整环体约定为 V=pi*(rR²-rL²)*dz，Ar=2*pi*rFace*dz，
Az=pi*(rR²-rL²)，hr=dr、hz=dz。内部约去常数也必须保证对外质量、
能量、角动量、质量矩、通量、IO与分析器一致，不能混用单位方位角积分。

实际 Grid.h:202 仍返回 r_cy/phi_cy，:289 仍令 z_cy=0；
GridGeometryView::PhysicalPosition 的二维分支在区分 cylindrical/spherical前
直接做极平面转换。GridMetrics::PhysicalSpacing 的二维第二轴仍 r*dx2，
GeometricSources 的二维第二动量仍是旋流。GravityBoundary::values 的
dimension==2 分支调用 logarithmic kernel，near_leaf_potential 同样选择log。
这些不是RZ实现，不能仅替换 API 文案、单位或 z alias。

## 2. 当前调用方 → 修改点 → 参考 → 必需测试

| 当前 owner/消费者 | 完整迁移点 | 既有参考与验证入口 |
| --- | --- | --- |
| grid/Grid.h / GridGeometryView.h | 2D x2=z允许长度/负z，去除phi范围限制；轴名/位置转换及PointCoords交叉坐标同步；3D及二维spherical原语义保留 | handoff §4.1；host/grid/test_curvilinear_metrics.cpp、math/geometry/CurvilinearMetricCases.h |
| grid/GridMetrics.h / cuda/common/GridMetricsCache.cu | 同一host/device CellVolume/FaceArea/PhysicalSpacing；完整环体权重、r=0零面积面、1/r体积平均；cache随geometry语义/mesh变化失效 | handoff完整环体公式；curvilinear_metrics、cuda/grid/test_grid_metrics_cache.cu |
| numerics/integrator/GeometricSources.h / TimeIntegratorHelper.h / CUDA HydroSourceKernels | (mom_u,mom_v,mom_w)=(r,z,phi)，无phi导数但第三分量旋流仍活动；唯一源入口，不补质量/总能源，不改积分分裂 | 恒压静止、z平移、带旋流和角动量收支；现有CurvilinearMetricCases及CUDA curvilinear smoke扩展 |
| numerics/diffusion/DiffFlux.h / VelocityDiagnostics.h / CUDA DiffusionKernels | 分量基、两向梯度/黏性/div/curl与r-z一致；不能保留通用dim==2极平面分支；物性仍由真实材料提供 | ViscousGeometryCases.h、cuda/numerics/test_diffusion_rkl_parity.cu；补独立解析场 |
| amr/exchange/CoordinateSeamPlan.h / CoordinateSeamMath.h / GhostExchange / CUDA seam plan | RZ轴线不跨phi+pi找donor；标量/z偶、r/phi奇；r=0和非零内边界区别；保留3D完整转角seam | host/amr/test_boundary_plan.cpp、cuda/grid/test_boundary_plan_parity.cu |
| amr/storage/Block.h / transfer/RegridTransferMath.h / flux/AmrFluxPlan.h / FluxRegister.h | restriction/prolongation接收同一环体volume；粗细面使用同一area/volume；保留拓扑事务/通量唯一owner，不建立RZ第二迁移器 | regrid_migration_fixture、cuda/amr/test_cuda_regrid_migration.cu；真实混合层级质量/角动量/reflux预算 |
| amr/elliptic/EllipticMeshAdapter.cpp / numerics/elliptic/CompositePoisson.cpp / multigrid | RZ轴线regularity、两向physical widths、共同face coefficients/volumes、粗层/零空间/体积范数；A=-L符号保留 | host/gravity/test_poisson_multigrid.cpp、test_composite_poisson.cpp；Phi=a*r²+b*z²独立制造解 |
| physics/gravity/GravityBoundary.cpp/.h / self/GravityWorkspace.cpp / SelfGravity.cpp | 真实有限环体质量/矩、近远场边界、mesh/source缓存失效；RZ禁止2D log与把整环压到(r,0,z)的点源替代 | handoff §4.3；test_self_gravity.cpp、test_gravity_stage_contract.cpp；Core批准的独立源积分/oracle |
| physics/gravity/GravityExecution.h / GravitySource.h / driver runtime/CUDA gravity | 正确native两向面力与同一mass-face flux耦合，引力功一次；CPU/device数学共享，stage/epoch身份一致 | gravity stage contract、self gravity CPU通过后受影响CUDA |
| core/config/ControlRelations.h / driver capability/ResolvedExecutionPlan | 二维RZ不再要求x2为full azimuth/periodic；径向轴线及物理边界独立判定，不能借此开放O8新用户BC | config/entry/inspection矩阵及真实被拒绝输入；逐case支持域 |
| api/configuration/Configuration.cpp / preview/Preview.cpp / Studio | runtime CoordinateMetadata输出r,z及cm，VELY是z、VELZ是phi；schema/inspect/field/AMR统一geometry semantic identity，缓存旧身份失效 | API/native-coordinate、初态mesh及Studio/Host相关检查；非立方体原生Inspector |
| io/plot/PlotIO.cpp / hdf5/HDF5Writer.cpp / io/chk/ChkIO.cpp / CheckpointCompatibility.cpp | 原生bounds/volume与完整环体身份；geometry语义revision与配置v3分开；旧二维cyl checkpoint明确拒绝，新版真实续算 | host/io/test_checkpoint_compatibility.cpp；IO/分析器单位、体积/质量与restart continued-evolution |
| simulation初始化 / core/problem/ProblemHelper.cpp / 所有PointCoords消费者 | z-dependent场真正使用z_cy；r变量不能误当柱半径；历史输入密度、体积、边界和势核逐例判断 | GravityBox/既有模型的准确case输入和独立参考；不复制Init公式 |

RegridTransferMath 自身没有 cylindrical 字符串，但其 fine/coarse volumes 是关键
输入，必须追踪 Block/AMR 调用方。FluxRegister与AmrFluxPlan消费共同FaceArea/
CellVolume。扫描命中不等于找全依赖，不以grep结果代替这条间接路径。

## 3. O7.4 短设计：待 Core 确认的具体决定

现有 GravityBoundary 已有拓扑树/层序遍历、unit-cell moments、密度update、
compensated parent moment组合和host/device near/far leaves。复用这些所有者；
节点质量必须是体积积分的完整环体质量，不是二维面积或单点质量。

- 源：每个RZ叶cell对应有限r-z区域绕轴的完整体积源。须确认密度piecewise-
  constant、势点值/平均值及face force语义；不能用薄环认证有限cell近似。
- 远场：沿现有moment组合与tree opening所有者实现完整旋转源的矩。
  阶数、opening判据、保守环体空间bound由Core审定，不能直接将现有polar
  arc的radius_squared或2D order参数搬到RZ当已验证方法。
- 近场：维护者选择/批准有限cell环体积分或有可靠依据的核；说明轴线、
  近源/源内、有限cell边界的处理及达到预算的方法。当前3点polar quadrature
  不能不经独立误差验证就声称足够；不在这里拍板新的积分阶数/epsilon。
- 误差账本：分别冻结边界截断、有限源积分、空间离散、AMR粗细面、MG残差、
  force/势误差及后端roundoff预算。仅残差达标不代表边界/力科学正确。
- 独立oracle：由不同实现/参考路径对同一有限源、相同点值或体积平均、完整
  环体权重生成参考；不调用生产near/far leaf互证。至少轴线/球对称映射/
  z-dependent环体三个覆盖，并保存参考身份与误差定义。
- 缓存：mesh/geometry semantic revision变化重建volumes/centers/unit moments/
  tree bounds/operator/cache；density accepted state变化update mass moments；
  boundary/control/source身份变化使values失效。CPU/CUDA共用数学，存储归各backend。

上面是决定清单和接线方案，不是已批准科学参数。Core未确认近场方法、开角/
阶数或预算时不进入生产环体边界实现；可独立准备有界实验与oracle工具。
不能运行后据结果修改预算。

## 4. 能力与兼容性 Stop Gate

ChkIO当前仅比较dimension/geometry string/species，旧2D cylindrical与未来RZ
字符串相同，不能阻止错误续算。现有checkpoint format/state-control/config版本
不能自动替代geometry semantic revision；修订策略需与IO owner明确。
旧二极平面checkpoint不自动猜测转换。旧合法二维spherical、1D径向、
3D柱/球以及Cartesian的原数学/物理预算保持；为RZ修改通用dim==2分支时
必须分别列证据，不声称旧polar和新RZ是同一benchmark。

O7.2–O7.4中间实现可留开发分支，直到O7.5整条CPU路径及独立科学review通过，
runtime/Preview/Studio不得发布成可运行RZ；不能先改标签再用旧kernel计算。

## 5. 可复核审计与下一步

扫描tracked src .h/.cpp/.cu/.cuh中的Cylindrical/cylindrical/GridMetrics::/
GeometryView/PhysicalPosition/CoordinateSeam：50 files、229命中位置。
逐文件hash/命中数、直接证据行、准确现有测试入口见精简Summary；完整命中
文本仅留studio/.local/integration/rz-consumer-audit/source-inventory.json。
这是词法候选清单，不保证发现所有间接消费者；上表补入transfer/IO/初始化链。

本步未改Core、未开放capability、未run tests/build/simulation/CUDA。
旧passing checks无相关修改不重复。按照数值依赖，先取得O7.1的Core确认并
执行其CPU单元/集成；RZ随后分层几何、流体、AMR、泊松，O7.4 review后
接正式环体边界，再做O7.5身份/GUI/IO/续算和受影响CUDA。

本报告不改变历史非物理G迁移与architecture audit待批准状态；
no push/tag/main merge、no Windows适配、no原始科学数据上传。
