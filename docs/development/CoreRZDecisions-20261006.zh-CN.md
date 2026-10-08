# Core：RZ 数学与验收契约

版本：`CORE-RZ-20261006-v1`。日期：2026-10-06。
审阅对象：`studio/compute-optim-integration` 的 `d430de65bf1312e135ceb7b12e18736877e0f8e0`。
本交付从 `origin/main` 的 `25adec4224497981a0c124a3485f786194975be4` 建立，只有确认文档、独立解析工具和处理后静态核对记录，不搬入整个 compute 分支。

本文确认下面的数学定义和验收范围。2026-10-07 起，全部生产实现、字段接口、科学判断和
CPU/CUDA 验收已由当前维护者接手；外部协作仅限 UI 调整。开头提交身份描述原始移交基线。
文中“确认”指设计与验收定义；“通过”须有对应真实实现的独立证据。

## 1. 当前记录与责任

| 内容 | 本轮确认 | 实施与验收责任 |
| --- | --- | --- |
| JENS 私有 CPU 均匀短包 | 9 次短演化、9 次真实续算的提交记录可核对；限该输入与构建 | 维护者完成最终 CPU/CUDA 构建、同一冻结短包及错误检查 |
| RZ 外源 | 原生分量、W/V 源项与阶段契约确认 | 维护者贯通唯一 gravity owner、阶段账本和正负例 |
| RZ 方位粘性 | ν/μ、对称方位剪切及配对能量功确认 | 维护者在共享 diffusion owner 内实现和验收 |
| 轴邻格 | 解析状态、局部量度、范数与原收敛要求确认 | 维护者完成重构/积分、真实 ghost、AMR、演化与续算 |
| 连续面力 | 点值对象、源定义、候选参考方法与解析度规则确认 | 维护者完成独立参考、误差界及实际消费者验证 |
| SNIa benchmark | 短终点与资源规则只用于受控性能诊断 | 维护者完成完整耦合科学参考和预算，再执行受控计时/长跑 |

维护者统一负责定义、独立参考、实现、执行和最终科学 review；辅助 worker 在冻结范围内
提交候选或证据，每个文件仅有一个写入者。新接口采用同一共享数学和真实生命周期，迁移全部
消费者后删除被替代的旧实现。JENS、外源、粘性、轴邻格、连续面力分别报告。

## 2. RZ 的共同量度

显式 RZ 是 `(r,z,phi)` 原生正交分量，逻辑二维坐标为 `(r,z)`；球坐标二维继续采用 `(r,phi)` 极平面。柱坐标二维的旧极平面构型已纳入退役，不能按名称重新解释为 RZ。

全环体 `dV=2*pi*r*dr*dz`；`W=integral(r*dV)`，`J=integral(r*rho*u_phi*dV)`，保存的 `m_phi=J/W`。其他线动量、密度与总能量使用 V 平均。`m_phi/rho` 是保存态的代表速度，不能无条件当成真实场的 V 平均速度。

所有单位采用 CGS：r/z 为 cm，g 为 cm/s²，ν 为 cm²/s，μ 为 g/(cm*s)，应力为 dyn/cm²，功率密度为 erg/(cm³*s)。φ 分量属于本地基底，不把它改称 Cartesian z 分量。

## 3. 外源：确认可实施

来源记录至少明确 `unknown`、`external-native-orthonormal`、`finite-ring-current-state` 与 chart 身份；默认 unknown 拒绝。名字可由既有接口所有者调整，不能通过类名或模型白名单识别。外源记录配置/阶段身份；finite-ring 还须满足已有 density/topology/potential epoch 一致性。不能仅添加一个 enum 就免除旧状态检查。

外源三个分量对应 `(g_r,g_z,g_phi)`。每个 RK 阶段 s 使用本阶段实际重构的物理场：

- 径/轴动量源：`integral(rho*g_r*dV)`、`integral(rho*g_z*dV)`；
- 力矩源：`Q_J,s=integral(r*rho*g_phi*dV)`，保存态增量由 `dt*Q_J,s/W` 给出；
- 总能量源：`Q_E,s=integral(rho*(u_r*g_r+u_z*g_z+u_phi*g_phi)*dV)`，保存态增量由 `dt*Q_E,s/V` 给出。

全步源账本使用当前 integrator 的真实 RK tableau 权重。力、功、源状态必须属于同一 stage，不用最终态回填阶段功。Euler 的时间截断误差单列，不能要求它等于连续解析动能的完整二次时间项，也不能额外加 heating 来掩盖误差。finite-ring 的既有质量通量功继续保留，不用外源 cell-work 取代。

原 24 组合的离轴域、`g_phi=±0.025`、`dt=1e-4`、10 步及原 normalized-J `1e-12` 预算保持；质量、组分、repair 和能量阶段账本按原定义核对，开放边界扣除实际通量。新增一个非均匀方位场反例：r=[1,3]、dz=2、rho=2、`rho*u_phi=2*r`。此时 `m_phi_W=60/13`、`m_phi_V=13/3`，用 `m_phi_W*g_phi*V` 算功会错；交付工具给出精确积分。

`g_phi!=0` 的常数源仅在离轴域使用。含轴域的径/方位矢量分量须有正则极限，平滑轴对称场通常 `g_r,g_phi=O(r)`；g_z 可有有限非零轴值。方位外源是外加体力/力矩，不假设它来自轴对称标量自引力势。

unknown、chart/epoch 错配应在任何 stage 写入前拒绝。反例检查 Current、Next、Scratch、发布记录和已有账本，不把“只检查 Current”扩写为全事务证明。公开 gate 保留至该子集真实演化/续算验收完成。

## 4. 方位粘性：确认有限迁移范围

源码输入 `nu_visc` 是运动粘度 ν，实际动力粘度为 `mu=rho*nu`。沿用现有系数求值与 EOS/face-density 所有者，不新增用户 μ 参数。

本轮批准显式 RZ 的方位剪切：

```
tau_rphi = mu * (d_r u_phi - u_phi/r)
tau_zphi = mu * d_z u_phi
L_phi = (1/r²)*d_r(r²*tau_rphi) + d_z(tau_zphi)
```

使用同一应力和真实力臂进入原生 torque face flux、signed AMR register、restriction/reflux。径向面力矩为 `integral(r*tau_rphi*dA)`（定半径面可写 `r_face*A_face*tau_rphi`）；轴向面为 `integral(r*tau_zphi*dA)`，使用对应面 angular measure 和重构，不能套用单个 radial r_face 或 cell r_bar。总能量粘性通量采用 `F_E,visc=-u·tau_n`，在当前通量符号下能量 RHS 为 `div(tau·u)`；方位部分与本次修改的方位应力配对。此处方位迁移保留径/轴已有通量与功；后续完整张量修正按第12节明确替换旧算子，不叠加执行。去掉重复方位连接项时须完整证明与新散度配对，不能仅把旧 source 清零。

纯径向变化的旋流参考：

```
strain = d_r u_phi - u_phi/r
Q_heat = mu * strain² >= 0
(1/r)*d_r(r*u_phi*tau_rphi) = u_phi*L_phi + Q_heat
```

若有轴向方位梯度，还须包含 `mu*(d_z u_phi)²`。热耗散来自相同功/动量的相容分解，不能在总能量中重复加一次。

刚体旋转 `u_phi=Omega*r` 在 constant/linear/quadratic μ 下均为零方位剪切。旧 variable-μ 的 `Omega*d_r(mu)` 是旧 vector-Laplacian 的结果，不再作为新显式 RZ 对称方位应力的物理期望；旧失败记录和非 RZ 回归保留，不能修改容差使其变绿。

这项确认只覆盖方位应力迁移及配对功，不声称整个一般可压缩流的全部 Newtonian 应力张量已签收；其余分量若需要改物理定义，单独立项。含轴 scalar μ 采用平滑偶延拓，u_phi 采用奇延拓/正则 u_phi/r。线性 μ fixture 限离轴域。制造边界明确指定解析延拓或相容的零剪切条件，不能用直接复制 u_phi 的 ghost 去验证刚体零剪切。

## 5. 轴邻格：确认参考和验收口径

保留原 `N=16/32/64/128`、gamma=1.4、rho=1、P0=5、轴线 `[0,1]` 与离轴 `[1,2]`、z=[-0.125,0.125]。刚体 Ω=1，补充 Ω=4 的旋转占优例和 `u_phi=Omega*r+a*r³`、a=1/4 的一般光滑旋流。后者的独立压力为：

```
P(r) = P0 + rho*(Omega²*r²/2 + Omega*a*r⁴/2 + a²*r⁶/6)
```

先比较真实 native 初始 RHS，再进入固定物理终点的演化、混合 AMR、真实 ghost、regrid/restart。此处先冻结初始 RHS/解析参考；实际新演化输入的终点与时间误差预算须在实施方提交同一套输入、RK/边界约束后单列签收，不能把 RHS 通过标成演化通过。

全域残差采用 native V 权：`L1=sum(V*abs(R))/sum(V)`、`RMS=sqrt(sum(V*R²)/sum(V))`、`Linf=max(abs(R))`。原连续两档 `order>=1.8` 保持，不能删首格、降低阶数或换 norm 关闭旧 finding。

局部固定报告：(a) 首两个径向 native 层的 Linf；(b) 固定物理区间 r∈[0,1/8] 的 V 加权 L1/RMS/Linf；离轴 counterpart 用 r∈[1,9/8]。mixed AMR 按真实 leaf/局部 h 记录，不把不同布局混成 uniform 阶数。force-density 归一化用输入决定、与 h 无关的 `S*=P*/L*`，其中 `P*=P0+rho*U*²`、L*=1 cm、U* 为上述解析速度在域内的固定最大值。EOS 压力用 P*，能量用 `P*/(gamma-1)`；禁止用随 h 发散的 `P/r` 作残差分母制造二阶。

解析平衡的 force-density 参考为零，逐项保留 pressure face divergence、几何源、V/W、真实 kinetic integral 和代表态 EOS 差值。平滑制造解的局部 force 收敛同样须达到原 1.8 要求；舍入区间单列，不凭观测误差改阈值。EOS 转换必须与 V 能量及 W 方位动量相容，一般重构/积分负责修复，不识别 Ω 或某个 case 做补丁。

首格 `[0,h]` 的已有反例保持：真实 KE=h²/4，代表 KE=9h²/32，代表 EOS P=P0+19h²/80；精确 pressure divergence/source 都为 `2*P0/h+h`，旧代表 source 为 `2*P0/h+8*h/5`，source-only 缺陷为 `3*h/5`。解析工具验证这一阶缺陷仍被辨认，不把它改成新的可接受参考。

## 6. 连续面力：确认对象与候选方法

源是当前发布的全部真实 leaf 常密度 full-ring cell，CGS G、真实 bounds、source/density/topology epoch 全部匹配。势参考为 `Phi(x)=-G*integral(rho_s/|x-x_s| dV_s)`，力为同一固定源的 `-grad_x Phi`。不是无限长柱体，不换制造 RHS；保持正密度支持范围。

比较对象沿既有消费者：cell-center 点势、boundary face-center 点势、fragment-center 法向点力、fragment-area side gather，以及两侧 gather 的算术平均 cell acceleration。后两者不冒称连续面平均或 cell 平均；想新增平均参考须独立定义。生产路径继续由 Poisson 势求梯度，不接入直接积分生产力路径。

批准候选工具独立推导源内/接触 Duffy 导数或直接 Newton 力积分，并验证奇异项可积性与积分/求导条件。固定 source domain 的 observer 求导不凭空新增表面质量；若采用随 observer 移动的分区，分区接触项须证明一致。轴上径向力用对称极限零；完整源、平移/分割不变性、密度线性、轴线/源外解析例逐项检查，不加 softening/epsilon、不丢接触点。

误差账本分开列 reference quadrature、potential-difference truncation、精度/输出舍入、空间离散、边界和 algebraic residual。`estimate`、`observed drift`、`certified bound` 分开。参考可信预算按将要比较的原 fixture tolerance 分配，可靠参考不确定度最多占该容许误差的 1/10；这仅是参考解析度条件，不新设或放宽生产科学公差。没有预先冻结的 fixture 公差时，结果只作诊断，不能从当前差值倒推公差。满足阶数漂移但缺可靠界的点标 UNVERIFIED；工作上限、坏点和未解析必须传播。

原 manufactured/coarse-fine `>=1.8` 与 physical residual 原门槛不变。常密度环体接触/角点另分类记录，不能强行宣称它等同光滑 manufactured 解，也不能删坏点得到全覆盖。具体 full-coupled 新误差预算与尚无可信参考的点继续由 Core review 收口。`RZ-CONTINUOUS-FORCE-REFERENCE-01` 保持 OPEN。

### 离散残差、连续场与耦合收支

离散泊松残差、连续势与力的空间精度，以及流体和引力的总能量收支，是三个相互独立的验收对象。求解器返回收敛，仅表明其当前离散迭代满足了内部工作目标；原始科学请求还需要提供完整的误差证书。此外，候选场即便通过了离散残差认证，也不会自动转化为公共物理能力。

具体而言，设 `E_b` 包围实际 RHS 与理想 RHS 的误差，`E_c` 包围完整残差的误差。原始容差按 `T_safe = max(atol, rtol * lower(norm(b_hat) - E_b))` 计算；完整验收则比较 `upper(norm(r_hat)) + E_c <= T_safe`。范数计算采用原生全环体积权重的区间包围。同时，原 RHS 下界、用户容差和累计迭代上限始终予以保留。

针对原生 isolated Dirichlet 边界，同一系数会同时出现在齐次势算子和给定边界 RHS 中。完整残差中的构造缺陷需按 `delta_M * (phi_anchor - datum)` 进行联合包围；其中，样本差、几何误差、源误差、边界积分误差、组装及实际残差运算误差各计一次。这种相关性仅限于这条实际边界表达式，不能推广为对任意 Neumann 或 Robin 消元的许可。旧的 RHS 构造误差仍用于精确 RHS 范数下界，不能以联合残差误差代替。

若完整证书仍有正的误差预算余量，可复用同一次源、边界和驻留势以收紧内部求解工作目标。每次追加的迭代都计入同一个原始上限，并需要重新检查完整证书。相反，若预算已被证书误差占满、证书非法或迭代停滞，则必须明确宣告失败，不能更改物理量或用户容差。

连续场验收需独立比较所有实际源对应的单元中心点势、真实面片中心的法向点力，以及原面积加权的侧向和单元加速度；离散面值行不能直接当作连续面中心点势。此外，独立参考的完整覆盖与不确定度也需要分别验收。给定制造解的空间收敛，不能替代实际 isolated 源的连续参考。

耦合验收需使用同一实际阶段的势、质量通量及原 RK 权重，并记录物质边界流出、引力功、粗细界面功和拓扑改变。`0.5 * sum(rho_V * Phi_point * V)` 是明确的离散点势能定义；它与连续体积平均势能不同，且源到点势的映射也不能未经证明就假定为自伴随。有限时间步的截断误差、空间映射误差与登记遗漏需分别判定。真实的反应、扩散、AMR 和 checkpoint 续算仍需整条运行证据，接口收据通过不能代替收支证明。

当前 Host 公共 prescribed RZ 已完成短程动态 AMR 与严格续算；孤立场连续精度／总能量、完整耦合和 NativeDevice 仍需独立签收。Private NativeSelf 证据限定于各自实际消费者与事务，不能替代这些出口。

## 7. Benchmark：冻结工程范围，长包继续保留

本轮静态核对四份输入与原输入 SHA、所有声明覆盖值、aprox13 六个文件 SHA、Helmholtz Git LFS oid/size及本机完整表 payload SHA，均一致；见 `validation/core_contracts/rz/benchmark-input-audit.json`。manifest 的 `changes` 含同值显式覆盖，实际差异是其子集，不能把它误读成必然改变了每个值。

同意将两个 `*-benchmark.par` 的 t_end=1e-7 s、实际 checkpoint 分割=5e-8 s 作为受控性能诊断候选。先执行一组 2D CPU 完整终点试跑，现有 scoped/module checks 和新终点字段检查通过、资源 guard 具备后，才进入 CUDA counterpart，再提交 3D/配对计时总预算。任一失败即停止本节点并保留证据；不能报 speedup 或把未到终点前缀当完整 benchmark。

仅使用已批准的原 release 路径：统一 CUDA Release ELF 的 CPU/CUDA 配对，CPU-only baseline 另列。私有 JENS ELF 不混进本 benchmark。原 .par/模型/表/network 身份固定；验证真实 checkpoint 时间和续算终点。保留 verify_coupled 和 compare_backends 的原预算（场 2e-7、ENUC 1e-5、species 1e-9）；后端一致仍不代替完整耦合独立科学参考。

完整配对计时须先交付新终点 readiness/资源实测与批次总预算；下面是固定的计时协议，不自动授权无限重复。首轮固定 CPU 8 threads，在实际拓扑上选择/记录 8 个不同物理核的 affinity；记录 core type，不能猜 P/E 排列，不能事后只保留最快参数。线程/affinity、binary/build identity 必须在配对计时前固定。与 CUDA 构建、科学测试、其他 benchmark 不并发计时；warm-up 每后端一次，再至少 3 组交替串行配对。执行预算不是自动占满所有核的指令。

首批仅授权 2D CPU 一次完整演化和一次真实续算，总墙钟不超过 2400 s；通过后另启 2D CUDA 对应两条，总墙钟不超过 2400 s。两组不并发，产物合计仍受 10 GiB 限制；3D 和 warm-up/至少三组正式配对须据这次实测确认批次总预算后执行，不能把每条上限默认为无限调用。

每条短包上限确认：进程树 RAM 12 GiB、owned GPU allocations 10 GiB、任务产物磁盘 10 GiB、每条完整演化/每条真实续算墙钟各 1200 s。原公共表/输入不计作新产物；磁盘 guard 要计临时、checkpoint、plotfile和日志，预估下一次写入并留系统余量。检查系统与 GPU 当前可用空间；无法落实 guard 则不启动。达到上限标资源终止/未完成，不改物理终点、网格或输入来凑通过。

这些终点只用于工程性能诊断，并不承诺完整耦合科学轨迹已经合格。原移交时 `1e-6`、分割 `5e-7` 的两份 long 未获运行授权，7200 s/30 GiB 为当时提议。当前整套 O 系列已授权维护者完成；长跑仍须先具备完整耦合、开放边界与核能/引力/扩散收支的独立参考、冻结批次预算和实际 Host 空间护栏，不沿用超过当前存储余量的提议。上述历史 benchmark 输入不包含新 RZ，旧 polar 不能顶替 RZ；旧 P13 缺项不补默认值。O9 的 RZ 长轨迹在其自身科学前置通过后正常执行，O10 网格重设计另行讨论。

## 8. 原设计实施顺序与当前验收入口

以下步骤保存最初的设计依赖，不作为重新实现清单。外源绑定、W/V 阶段账本、方位应力和原生重构已有实现及限定证据；当前状态与下一出口以[O 系列总表](ComputeOptimizationPlan.zh-CN.md#当前执行校准2026-10-08)为准。仅真实缺项或失败重新进入实现；O9 长时验证继续执行，O10 网格设计待讨论。

1. 继续既有私有 CUDA JENS 流程：构建 → scoped tests → 原冻结短演化/真实续算；任何失败保留并停该节点。
2. 按第3节实现 typed RZ external binding、W/V 源项与阶段账本，保留原正负例并加 W/V 非均匀反例。
3. 按第4节在同一 diffusion owner 内实现显式 RZ 方位应力/配对功；按第5节准备一般轴邻格重构候选。
4. 独立推进第6节 matched-source 源内/接触参考；无可靠界继续 UNVERIFIED，不妨碍其他已批准子项。
5. 资源空闲且 guard/输入就绪后执行第7节 2D CPU 性能诊断，再按条件推进 2D CUDA counterpart；3D/正式计时先冻结批次预算，long 在科学准备条件和资源限额具备后执行。

每个节点记录准确源码/ELF/input/table/请求身份、scoped tests 与未完成项。raw H5/plt/checkpoint/全日志留本机，只提交处理后摘要和必要图表。维护者按科学证据决定公开范围，不以工程 PASS 覆盖未关闭的 RZ finding。

## 9. 本轮工具与验证范围

`validation/core_contracts/rz/analytic_reference.py` 用 Fraction 和独立多项式积分生成外源、方位应力/功、近轴平衡的精确参考，完全不导入生产源码。输入总量明确标 `per_2pi`，消费时统一乘 full azimuth 的 `2*pi`，不能混为其他测度。

配套 unittest 验证解析恒等式、已知错误替代与非法输入。它们证明参考工具的有限 fixture 数学，不是生产运行、EOS、Poisson、AMR、CPU/CUDA 整体科学 PASS。本轮未 build/run simulation、未执行 GPU 编译。

物理应力依据：[CFD Direct 的 Newtonian fluid 说明](https://doc.cfd.direct/notes/cfd-general-principles/newtonian-fluid)。本轮迁移范围和状态量映射以上述 Core 定义为准，不借一般公式扩大签收范围。

## 10. 原生均值与低热能旋流的新增核对

独立有理数核对确认：W 方位动量均值与 V 总能量均值不能直接合成普通 Cartesian 点态动能。取首格 r=[0,1]、rho=1、u_phi=r、点比内能 e=1/64，单位 dz 下的 per_2pi 量为 V=1/2、W=1/3、I=integral(rho*r²*dV)/(2*pi)=1/4、J=1/4。保存 m_phi=3/4、E_V=17/64，而普通恢复得到 `E_V-m_phi²/(2*rho)=-1/64`，物理点场的比内能仍为正 1/64。该反例由独立 Fraction 参考覆盖；它不属于小值或舍入误差。

已知真实正密度场时，Cauchy–Schwarz 给出角动能总量下界 `K_phi >= J²/(2*I)`。与 E_V 比较必须再除以 V。本例由刚体旋转达到等号。只知道 rho_V 时 I 未唯一确定；任意密度重构给出的 I 不能自动成为权威充分条件。点 EOS、均值 veto、重构参考态、AMR 父子证明和阶段接受需采用同一有依据的原生表示；不能仅修改一个 `valid()` 判断并宣称全链路通过。

目前新增矩重构仍处于内部验收范围：配置的 MUSCL/PPM 与 coarse-fine 限制器语义、轴向面求积、均值热力学缓存及 AMR 原生能量判定尚需统一。旧父格反例须带完整 rho/bounds/表示规则复核，不从本反例推断它无效。公开 RZ 门槛保持至真实演化、AMR 和续算证明齐备。

## 11. 统一原生热力学数值闭合（实施接口冻结，尚未科学验收）

### 11.1 表示与数值含义

继续保存 `rho,m_r,m_z,E,rho*X` 的 V 均值与唯一 `m_phi=J/W`，不新增演化惯量数组。共同正密度二次重构 `rho_*(r)` 保持当前 V 密度均值，并使用真实、同阶段的径向邻居及反射 ghost。临时 `I_*=2*pi*dz*integral(rho_* r^3 dr)` 是明确的**数值重构闭合**，不是未知连续场真实 I 的证书。RZ 的空间算子、热力学、扩散与 transfer 必须消费同一个版本/几何/ghost 身份的闭合；不能在不同消费者各自选一个方便的 I。

记 `M=rho_V V, P_r=m_r,V V, P_z=m_z,V V, J=m_phi,W W, H=E_V V`。共同代表热态使用

```
u_r=P_r/M; u_z=P_z/M; Omega=J/I_*
e0=(H-(P_r^2+P_z^2)/(2*M)-J^2/(2*I_*))/M
```

代表 EOS 输入为 `(rho_V,e0,Xbar)`；实际面/源点使用 `rho_*(r)` 与物理速度。借用普通 shared state recovery 时，将方位代表动量映成 `m_eff=m_phi,W*sqrt(rho_V*W^2/(V*I_*))`，使同一个现有热能判定获得上述 e0。比例和动能必须指数缩放，热能可解析性的原8 epsilon以及 `sml_rho/min_eint/max_eint` 不变。反射负半径 ghost 使用 signed capacity 与奇偶关系，热能用正物理惯量，不能生成负动能。

基线物理 profile 为

```
rho=rho_*(r); m_r=rho*u_r; m_z=rho*u_z; m_phi=rho*Omega*r
E=rho*(e0+(u_r^2+u_z^2+Omega^2*r^2)/2); rhoX_s=rho*Xbar_s
```

它保持全部原生均值。theta=0只能返回该物理基线，不能返回 raw mixed-measure `FluidVector`。高阶与基线共用同一 rho*，并用同theta限制动量、能量和组分。若正性调整改变了密度 polynomial，组分高阶同步使用 `q_high,s=q_poly,s+Xbar_s*(rho_*-rho_unlimited)`，保留每个 V 均值及总组分和；不能用逐点normalize来代替积分守恒。实际消费者点态还须通过真实 EOS 与既定上下界。

常量惯量捷径不能替代此闭合：取 `rho=7/8+r^2/4`、r=[0,1]、Omega=1、常数点比内能epsilon，则 `rho_V=1,I=J=25/96,H=25/192+epsilon/2`（per_2pi），常量rho惯量I0=1/4给出的比内能为 `epsilon-25/2304`。rho平均值相同并不使 I 相同。另有分段正密度反例，均值允许域不能从rho均值唯一推出。

max_eint的上限集合也不凸：rho=1、e_max=1/2时 `(m_r,E)=(+1,1)` 与 `(-1,1)` 都符合上限，中点比内能为1。重构ray搜索必须检查实际选中的点态；不能把下限的凸性推导延伸成上下限全域或 RK/RKL 正性证明。

### 11.2 阶段、AMR 与耦合所有者

RZ输入先完成全层级真实物理BC与ghost，随后借用只读 closure context；身份至少含数组slot/version、ghost来源version、拓扑及几何。叶数学无全域额外状态，不开另一套后端公式。

每个输出分为有限量/正密度/simplex预检与post-boundary closure/EOS科学验收。现有 source消费及repair receipt owner 保留；另外一个post-ghost gate检查整个候选层级。Euler、所有RK最后一级与final reflux同样必须完成ghost和gate。拒绝由既有Runtime事务恢复原数组、身份、时间和账本；“数据写完”不能代替科学接受。

Init先检查实际采样点再做V/W积分，径向使用Gauss4；任意用户函数仅称数值求积。初始BC/AMR ghost就绪后才验收native closure。checkpoint读入先做结构/finite/rho/simplex预检，再于真实恢复的BC/ghost完成后验收；数值闭合修订须带明确身份，旧private checkpoint不能被静默重解释。

AMR restriction继续真实V/W求和。先建立候选拓扑及其ghost，然后进行closure/EOS验收；不可表示的coarsening保留children，有限、单调移除候选，并重查受影响stencil。prolongation回退积分parent物理基线到各child自己的V/W，不复制不同半径的raw parent。最终候选拓扑通过后才发布。

Burn继续原单zone ODE和两次半步，不把closure迁移变成多节点ODE：固定M/I*/Pr/Pz/J，核能增量 `delta E_V=rho_V*delta e_nuc`，用原kinetic保留的方式完成hand-off。EOS点函数及核网络内部算法不改变。Self-gravity保留既有flux-compatible能量耦合的唯一authority；外源方位功与力按既定V/W场积分，不能另加同一份rho*u·g。

角向粘性用同I*、同rho*及对称正链接。固定rho半离散耗散/守恒不等于有限步热性；完整FE/RKL张量、非线性系数、AMR及变化BC仍要真实逐阶段post-ghost验收。不能以谱步长界单独宣称热能正性。

### 11.3 实施和签收依赖

先提取共同moment/density叶函数，再加入原生thermo adapter与profile，随后迁移Init/transfer/ghost预检、真实post-boundary gate及所有Hydro/CFL/Burn/Gravity/diffusion/IO消费者。配置的PCM/MUSCL/PPM和coarse-fine stencil语义必须逐项核对，不能把一个径向特例称作所有方法已支持。RZ user BC需要点primitive与native ghost的独立转换及初始EOS绑定，不能直接套普通Cartesian mean函数。

以上为root冻结的实施规则，既有空间阶数、独立参考误差预算、能量/组分/角动量收支及公共门槛保持。完整实现、CPU/CUDA演化、AMR与真实续算通过前，本节不是release PASS。


### 实际引力功存储节点与总能量出口

[单元侧存储节点](NativeGravityWorkLayoutNode-20261007.zh-CN.md)修复真实curved左右系数别名，CPU/CUDA共用原公式，Cartesian原数据/顺序保持。原owner非线性实际行及四真实field账本/透明性/故障回滚已通过；独立端点Green分解、连续空间精度、AMR总能量及反应四模块仍验收中。本节点不开放公共Native或Device资格。accepted-Current与Hydro-stage用途应共用一个场所有者并分别验证真实源租约；用途不替代科学资格，且不把accepted Current伪装成零dt Hydro阶段。

## 12. 完整黏性张量的实施边界

运动粘度继续由 nu_visc 控制，动力粘度为 mu=rho*nu。完整实现采用同一三维零体粘度 Stokes 本构，计算坐标和降维不会改变迹项系数；不增加独立的用户 mu 或 bulk 参数。

```text
G_ij = physical covariant velocity gradient
tau = mu*(G+G^T-(2/3)*tr(G)*I)
F_E,visc = -u dot tau_n
Q = tau:G = 2*mu*dev(sym(G)):dev(sym(G))
```

共享点本构已在原 CPU 曲线几何 owner 中通过了 57 个独立解析例的验证，原有 PDE 断言保持不变。该本构提供应力、牵引、功和收缩，非法或不可表示的结果将不予发布；目前它尚未接入完整的径向/轴向算子，CUDA 亦未进行编译。生产环境的更新需要提供真实横向梯度、同一次应力的动量与能量通量、张量几何源、边界功、AMR 以及实际时间步的证据。原向量 Laplacian 与新张量不能重叠执行，耗散也不能再次作为独立加热项叠加到总能量中。

原拟议的 Q2 普通边与轴向奇三次边在粗细网格界面处存在整段不连续的问题，仅靠挂点一致性不足以解决。后续若采用连续且保均值的重构，普通边、轴边和粗细界面必须统一使用同一 Q3 边迹空间；细网格限制的是粗网格的整段多项式。在轴上，ur 为奇函数、uz 为偶函数；uz 属于自然轴边界，不能将其有限轴值当作零值壁面条件处理。原有的四次径向 uz bubble 不属于 Q3，故不能沿用 Q3 的证明逻辑。

对真实Q3径/轴速度与正mu，5个径向乘4个轴向正求积点足以证明Gram零值仅来自连续零偏应变。这个充分条件不声称积分精确或点数最少。合法零模包括轴向平移、同率homology和 ur=2*b*r*z、uz=b*(z*z-r*r) 的special-conformal模式；不能误将最后一项归为数值checkerboard。真实边界会进一步约束零模。实际系数行、密度均值、轴空间、粗细限制和边迹仍须在生产实现中逐项核对。

若采用弱梯度转置，必须用真实B、W和速度质量矩阵M构造K=B^T*W*B。正半定性不自动提供真实有限体积牵引或正确AMR限制；轴向净内力还需要B*e_z=0，粗细速度映射P对应的力必须采用P^T。Lambda=max_i(sum_j(abs(K_ij))/M_i) 可控制冻结线性算子的谱，但dt<=2/Lambda只给出前向Euler的加权动能条件，不能替代热能正性、非线性RKL阶段或移动边界的验收。

验收继续覆盖常量和变密度/粘度、同率及异率压缩、横向剪切、轴正则、移动壁面、真实粗细界面、热能和组分拒绝、演化及fresh checkpoint续算；公共能力按整条证据开放。上述候选重构与弱梯度只是实施约束，当前不授予完整张量或Device资格。

### 变密度完整应力的离散否定例

均匀常系数的谱证明不能转移到一般变密度情形。独立3×3周期例使用严格正rho、固定nu=1及mu_face=(rho_L+rho_R)/2；若法向梯度为面差、切向梯度为相邻中心差的平均、随后直接套点应力，真实全局粘性功为−250661/1000。Root与DPS分别以精确有理数复核；见[处理后反例](../../validation/gravity/results/release-closure-20261007/public-arithmetic-density-fv-work-summary.json)。这是对候选离散的否定，不是既有vector-Laplacian生产路径的失败，也不单独证明特征值符号。DPS的总功正确，但初版y分组索引错误经唯一定点返修更正，原交付保持。

完整新算子必须在实际数值密度和量度上配对梯度、应力和散度，并独立验证加权动能耗散、热能与同一总能量面功。质量加权内积中的rho不能当作常量省去；总能量面散度相消本身不足以签收。后续不采用该已否定的直接替换，保持同一Host/CUDA数学、真实轴和粗细接口及原科学门槛。

### Cartesian 完整应力运行接入（2026-10-08）

均匀 Cartesian 的 Host/CUDA 面通量现在共用乘积加权横向梯度与三维零体积黏度应力；能量使用同一面速度与同一牵引配对，不额外添加黏性热源。实际 CPU owner 的 1/2/3D 制造解收敛、1D 三分量质量加权动能及真实外部功检查通过，原网格、密度对比、物理参数和误差窗口保留。其解析参考按完整 Stokes 方程迁移，原曲线坐标的旧算子参考尚未迁移。

周期均匀网格的变系数离散功已用 64 组精确有理数数据独立验证非负恒等式，矩阵行界另以 12 组 1/2/3D 非等步长、468 行独立验证质量对称性及常速度零模。该结果不授予物理壁面、AMR、曲线坐标、RKL 或非线性热能耦合的完整资格；这些仍须各自真实 owner 验收。CUDA 当前重新编译中，过期测试 EOS 的温度接口已按生产理想气体关系补齐，未改变其压力公式或数值门槛。

### 当前点与边界验收范围

当前 CUDA 原几何 owner 已通过 Cartesian 完整应力、协变点梯度及正黏性可表示性检查；真实 CPU BCHandler 的1/2/3维周期与反射 slip 配对功也通过。它们支持各自有限范围，不扩展为 RKL、粗细界面或曲线完整算子的验收。原生薄环角动量散度已通过实际两布局 CUDA 原门槛；对无效数学输入的全 Device 事务锁存仍须完成。

冷态反射墙需要非线性墙压：明确 gamma-law 能力采用实际 H/B 点的稀疏波或冲击解，通量的零质量、零总能量功与零组分输运保持。新点因子直接检查该同一低阶通量的两侧基态，避免借用不同 LLF 基准的限制系数。原 CPU owner 与独立参考已通过，当前 CUDA 原 HydroLeaf 已运行通过（0.43 s）；通用 EOS 不允许用有效 gamma 冒充同一解，精确通用墙面与完整 Native 运行继续开放。
