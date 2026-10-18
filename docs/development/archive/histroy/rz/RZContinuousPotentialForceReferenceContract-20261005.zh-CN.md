# RZ 连续势／面力参考：短设计与消费链审计

状态：供 Core review；连续科学 gate 尚未解除。
审计基线：c640c3d2c8c7550f1a97df8dd273f376e65985ed。
唯一工作区：/home/arch/projects/ARCH-compute-optim。
本节点只改文档，不 configure/build、运行 simulation 或更改科学能力、阈值。

## 1. 授权与已完成范围

依据 O7JeansRzReviewQuestions-20261004.zh-CN.md：
- 第4.2节认可完整有限轴对称源、源外 observer 点值工具；源内／接触力、
  cell／face 平均、阶数和分项预算仍待短设计确认。
- 第7节已明确 full-ring piecewise-constant 源、点势、轴解析极限、
  接触势 Duffy 处理和可靠误差账本；第7.4节要求扩展 matched 源参考，
  分开记录近场、边界势、Poisson 解和面力，原空间标准不变。
- 第7.3节明确原始 residual 请求的充分条件不是连续 Phi／力误差界。
  后续批准的接触势实现不等于源内／接触力参考已经签收。

uniform 与静态 mixed 原生求解已经取得独立 Fraction 离散证据：
RZRingBalancedWorkspaceNode-20261005.zh-CN.md、
RZNativeMixedSolveNode-20261005.zh-CN.md。
这是当前源、理想原生 A/B 和原请求的 residual 证据；不能替代本短设计。

## 2. 固定科学定义与真实消费者

每个叶单元是完整方位角的常密度环体：
r in [r_lo,r_hi]，z in [z_lo,z_hi]，dV=2*pi*r*dr*dz。
源取本次发布的真实叶单元 density；不得用制造势的 RHS 当作同一个物理源。
共享 CGS G，Phi 单位 cm^2/s^2，物理加速度单位 cm/s^2。

Phi 未知量对应几何 cell center 点势；外边界 f 对应 face.center 点势。
参考取相同 root/leaf/source 身份和 observer 坐标；不得改为 cell average
或将有限环体替换为无限长柱体。源角向完整、质量和密度语义保持。

实际调用链：
SelfGravity.cpp solve -> solver.gradient(resident_potential, boundary_values)
-> GravityWorkspace side_gather -> patch_gather
-> GravityExecution.h CellAcceleration -> GravitySource.h momentum/work。

| 消费者 | 当前真实含义 | 应独立比较 |
|---|---|---|
| Poisson resident Phi | 单元中心点势 | 同一 observer 的连续点势 |
| composite face_gradient | 最终离散 stencil 的法向势梯度 | 同一面中心的连续法向梯度；物理 g=-gradient |
| curved side_gather | 粗细片段按各 fragment area 归一化汇总负梯度 | 对相同 fragment-center 参考进行同一面积加权 |
| CellAcceleration | 两侧 side acceleration 的算术平均 | 同一两侧参考汇总；不能称 cell-center 点力或体积平均力 |
| gravity_momentum | dt*rho*(g_low+g_high)/2 | 保留现有源应用语义 |
| curved work_faces | Phi_face-Phi_cell 配合 +/-2*A/V 与实际质量通量 | 单独检查能量工作；不能拿 face acceleration 代替 |

这里的 fragment-center 加权不是解析 face-area average。
若 Core 要求连续面积／体积平均，必须明确新增参考及比较含义；
不能静默更换当前输出标签或科学算子。
CellAcceleration 的 inactive components 为零；坐标基底为当前原生正交基底。

实际实现证据：
src/physics/gravity/self/GravityWorkspace.cpp；
src/physics/gravity/self/SelfGravity.cpp；
src/physics/gravity/GravityExecution.h；
src/physics/gravity/GravitySource.h。
生产代码仍只通过 Poisson 势求面力，不新增直接积分生产加速度通路。

## 3. 可复用参考与已知缺口

| 工具／证据 | 可复用范围 | 不能声明 |
|---|---|---|
| rz_ring_axis_reference.py | Decimal 轴线解析 Phi/g_z，g_r=0；有限环体 | 全离轴／接触面力已验证 |
| rz_ring_offaxis_reference.py finite_volume_reference | 源外 Decimal K/E 势与力；轴极限、分区与直接 Newton 诊断 | inside/contact（入口明确拒绝） |
| 同文件 contact_potential_reference | 源内／接触 Duffy 势诊断；阶数、t 分区和精度分别变化 | 接触力参考或可靠求积误差界 |
| FiniteRingBoundaryMath.h / 当前 ring enclosure | 已记录范围内可靠势区间、工作上限与失败传播 | 连续解／面力空间收敛 |
| native solved Fraction reference | 理想 root 几何 A/B、实际 RHS/residual 和原请求 | 连续 Newton Phi/g oracle |

现有 Decimal 工具使用十进制 G=6.67430e-8；生产使用共享常数的 FP64 值。
对照必须记录二者并显式处理表示差异：验证数学常数或精确 FP64 输入应
分别标注，不能将常数表示误差当作 PDE 或力误差。
Decimal 最终输出若转 FP64，也须记录输出舍入；高精度计算不自动成为 certified bound。

多个叶环体参考可线性叠加，但须保留每叶 bounds、rho、source ID、求积状态。
正密度当前范围与 unsupported signed source 不变。
不得将父节点 moments 或另一份解析密度替代 matched piecewise-constant 源。

## 4. 候选参考实施顺序（待 Core review）

A. 先组合已有轴线／源外参考，覆盖 axis/off-axis、uniform/static mixed matched 源，
   显式记录 observer 所属 outside/inside/contact 类别；不能让拒绝类别进入 PASS。
B. 为源内／接触面力单独实现验证工具：候选复用源分割和三角 Duffy，
   从同一 Newton 势的导数推导径向、轴向 integrand，解析消去可消去的 t 因子，
   内部节点不取 t=0；轴使用解析极限，不加 epsilon、不 abs、不删掉源贡献。
   此项是候选设计，不是已批准的可靠力 oracle。
C. 以轴解析、对称性、密度线性、平移、源分区不变性、势导数和独立 Newton
   诊断逐项检查；精度、积分阶数、分区分别变化。阶数差只能称估计。
   不能用共享积分误差的两个计算相符作为唯一证明。
D. 对真实 solve 输出分开列 boundary Phi、cell Phi、fragment force、
   side gather 与 cell acceleration；连续参考、空间离散、代数 residual、
   参考/求值误差分别记账。失败或预算不足显式返回，不改变原请求。
E. 真正 Runtime stage/regrid 的 source publication 独立验证，再按已批准 CPU
   科学子 gate 决定 CUDA 同一数学路径；不能用静态 source retirement fixture
   代替真实 Runtime 所有 block dependency 收集。

原始 phi/rho/rhs/face/native arrays、完整日志留本机 .local 持久目录；
提交准确源码/ELF/request 身份、标量误差/工作量摘要和必要脚本。
没有稳定参考的观察点单独列 UNVERIFIED，不计入全覆盖 PASS。

## 5. 原验收与需要 Core 冻结的项目

原 tests/host/gravity/test_composite_poisson.cpp：
curved enclosed-mass 使用 face.area 加权 RMS force；
curved manufactured 使用原生 norm、face-area RMS，以及独立 coarse/fine
interface RMS，原连续两档 order >=1.8 条件均不改。
这些 fixture 的源和解与有限 pwc ring 不同；不能由其 PASS 推导后者通过。
有限源角点的正则性与可比较区域需要显式定义，不能根据观察值删坏点或降阈值。

请 Core 逐项裁定：
1. 源内／接触力候选是否可采用上述 Duffy 导数参考？是否要求可靠区间界；
   若只有诊断估计，允许关闭的子 gate 应明确限定。
2. fragment-center 点力、side gather、cell acceleration 的比较语义；
   若需要解析平均，请明确哪一项、测度和独立参考。
3. 连续 Phi、边界 Phi、face force、near/contact reference 的分项误差预算，
   样本/分区/最大工作量与失败策略；不能由本次观测值倒推阈值。
4. 有限 pwc ring 的源域、观察域与 refinement sequence；
   现有 >=1.8 fixture 保持，新增短科学 gate 的 norm、axis-local 指标由 Core 指定。
5. RZ-AXIS-01 的近轴闭合／力局部误差及 RZ-VISC-01 的应力和能量工作定义：
   仍保留各自 finding，不因势区间或 residual PASS 自动关闭。
6. 哪些 CPU 子 gate 足以进入对应 CUDA／长跑；未冻结的新 RZ 子集继续隔离，
   不影响已有批准模型的工作。

## 6. 当前结论

离散静态原请求节点已交付；连续 Phi／力参考尚未签收。
本审计不启用 production RZ、不改能力门槛、不修改科学 Core、不启动新 RZ 长跑。
不需要等待完整 RZ 签收才 review 本节点；其他已批准消费者可以继续。
下一实施点为 matched-source 轴线／源外参考组合；源内／接触力和新增连续预算
按上述独立短设计的 Core 裁定执行。
