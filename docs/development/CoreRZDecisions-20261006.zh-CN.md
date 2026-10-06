# Core：RZ 契约确认与并行交付

版本：`CORE-RZ-20261006-v1`。日期：2026-10-06。
审阅对象：`studio/compute-optim-integration` 的 `d430de65bf1312e135ceb7b12e18736877e0f8e0`。
本交付从 `origin/main` 的 `25adec4224497981a0c124a3485f786194975be4` 建立，只有确认文档、独立解析工具和处理后静态核对记录，不搬入整个 compute 分支。

本文确认下面的数学定义，允许实施方在隔离候选中实现、验证相应子项。它不是 RZ 科学 PASS，也不解除公开 RZ / CUDA JENS 门槛。文中“确认”指设计与验收定义；“通过”须有对应真实实现的独立证据。

## 1. 当前记录与责任

| 内容 | 本轮确认 | 实施与验收责任 |
| --- | --- | --- |
| JENS 私有 CPU 均匀短包 | 9 次短演化、9 次真实续算的提交记录可核对；限该输入与构建 | 对方继续私有 CUDA 构建、scoped tests、同一冻结短包；失败即停 |
| RZ 外源 | 原生分量、W/V 源项与阶段契约确认 | 对方贯通既有 gravity owner、阶段账本和正负例 |
| RZ 方位粘性 | ν/μ、对称方位剪切及配对能量功确认 | 对方在共享 diffusion owner 内实施；不另建数学 kernel |
| 轴邻格 | 解析状态、局部量度、范数与原收敛要求确认 | 对方修复一般重构/积分并做真实 ghost、AMR、续算验证 |
| 连续面力 | 点值对象、源定义、候选参考方法与解析度规则确认 | 对方实现独立参考；可靠误差界未完成前 finding 仍 OPEN |
| SNIa benchmark | 短终点与资源规则只用于受控性能诊断 | 对方先执行有条件的 2D 试跑；完整耦合科学参考/长包仍由 Core 收口 |

我们提供定义、解析参考和最终科学 review；对方拥有生产实现、运行工具与本机 CPU/CUDA 测试。相同源码所有者不同时由两方修改。JENS、外源、粘性、轴邻格、连续面力分别报告，互不替代，也不人为串成一个大门槛。

## 2. RZ 的共同量度

显式 RZ 是 `(r,z,phi)` 原生正交分量，逻辑二维坐标为 `(r,z)`；它不改写现有二维 polar 的 `(r,phi)` 语义。

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

使用同一应力和真实力臂进入原生 torque face flux、signed AMR register、restriction/reflux。径向面力矩为 `integral(r*tau_rphi*dA)`（定半径面可写 `r_face*A_face*tau_rphi`）；轴向面为 `integral(r*tau_zphi*dA)`，使用对应面 angular measure 和重构，不能套用单个 radial r_face 或 cell r_bar。总能量粘性通量采用 `F_E,visc=-u·tau_n`，在当前通量符号下能量 RHS 为 `div(tau·u)`；方位部分与本次修改的方位应力配对。径/轴已有通量与功不在本轮静默改写。去掉重复方位连接项时须完整证明与新散度配对，不能仅把旧 source 清零。

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

## 7. Benchmark：冻结工程范围，长包继续保留

本轮静态核对四份输入与原输入 SHA、所有声明覆盖值、aprox13 六个文件 SHA、Helmholtz Git LFS oid/size及本机完整表 payload SHA，均一致；见 `validation/core_contracts/rz/benchmark-input-audit.json`。manifest 的 `changes` 含同值显式覆盖，实际差异是其子集，不能把它误读成必然改变了每个值。

同意将两个 `*-benchmark.par` 的 t_end=1e-7 s、实际 checkpoint 分割=5e-8 s 作为受控性能诊断候选。先执行一组 2D CPU 完整终点试跑，现有 scoped/module checks 和新终点字段检查通过、资源 guard 具备后，才进入 CUDA counterpart，再提交 3D/配对计时总预算。任一失败即停止本节点并保留证据；不能报 speedup 或把未到终点前缀当完整 benchmark。

仅使用已批准的原 release 路径：统一 CUDA Release ELF 的 CPU/CUDA 配对，CPU-only baseline 另列。私有 JENS ELF 不混进本 benchmark。原 .par/模型/表/network 身份固定；验证真实 checkpoint 时间和续算终点。保留 verify_coupled 和 compare_backends 的原预算（场 2e-7、ENUC 1e-5、species 1e-9）；后端一致仍不代替完整耦合独立科学参考。

完整配对计时须先交付新终点 readiness/资源实测与批次总预算；下面是固定的计时协议，不自动授权无限重复。首轮固定 CPU 8 threads，在实际拓扑上选择/记录 8 个不同物理核的 affinity；记录 core type，不能猜 P/E 排列，不能事后只保留最快参数。线程/affinity、binary/build identity 必须在配对计时前固定。与 CUDA 构建、科学测试、其他 benchmark 不并发计时；warm-up 每后端一次，再至少 3 组交替串行配对。执行预算不是自动占满所有核的指令。

首批仅授权 2D CPU 一次完整演化和一次真实续算，总墙钟不超过 2400 s；通过后另启 2D CUDA 对应两条，总墙钟不超过 2400 s。两组不并发，产物合计仍受 10 GiB 限制；3D 和 warm-up/至少三组正式配对须据这次实测确认批次总预算后执行，不能把每条上限默认为无限调用。

每条短包上限确认：进程树 RAM 12 GiB、owned GPU allocations 10 GiB、任务产物磁盘 10 GiB、每条完整演化/每条真实续算墙钟各 1200 s。原公共表/输入不计作新产物；磁盘 guard 要计临时、checkpoint、plotfile和日志，预估下一次写入并留系统余量。检查系统与 GPU 当前可用空间；无法落实 guard 则不启动。达到上限标资源终止/未完成，不改物理终点、网格或输入来凑通过。

这些终点只用于工程性能诊断，并不承诺完整耦合科学轨迹已经合格。`1e-6`、分割 `5e-7` 的两份 long 仍未获运行授权；7200 s/30 GiB 也仍只是提议。Core 还需提供完整耦合、开放边界与核能/引力/扩散收支的独立参考和预算。新 RZ 不纳入；旧 polar 不能顶替 RZ；旧 P13 缺项不补默认值。

## 8. 对方同步后可以并行启动

1. 继续既有私有 CUDA JENS 流程：构建 → scoped tests → 原冻结短演化/真实续算；任何失败保留并停该节点。
2. 按第3节实现 typed RZ external binding、W/V 源项与阶段账本，保留原正负例并加 W/V 非均匀反例。
3. 按第4节在同一 diffusion owner 内实现显式 RZ 方位应力/配对功；按第5节准备一般轴邻格重构候选。
4. 独立推进第6节 matched-source 源内/接触参考；无可靠界继续 UNVERIFIED，不妨碍其他已批准子项。
5. 资源空闲且 guard/输入就绪后执行第7节 2D CPU 性能诊断，再按条件推进 2D CUDA counterpart；3D/正式计时先提交批次预算，long 保持未执行。

每次提交标准确源码/ELF/input/table/请求身份、scoped tests 与未完成项。raw H5/plt/checkpoint/全日志留本机，只提交处理后摘要和必要图表。Core 再按子集决定公开范围，不以工程 PASS 覆盖四项 RZ finding。

## 9. 本轮工具与验证范围

`validation/core_contracts/rz/analytic_reference.py` 用 Fraction 和独立多项式积分生成外源、方位应力/功、近轴平衡的精确参考，完全不导入生产源码。输入总量明确标 `per_2pi`，消费时统一乘 full azimuth 的 `2*pi`，不能混为其他测度。

配套 unittest 验证解析恒等式、已知错误替代与非法输入。它们证明参考工具的有限 fixture 数学，不是生产运行、EOS、Poisson、AMR、CPU/CUDA 整体科学 PASS。本轮未 build/run simulation、未执行 GPU 编译。

物理应力依据：[CFD Direct 的 Newtonian fluid 说明](https://doc.cfd.direct/notes/cfd-general-principles/newtonian-fluid)。本轮迁移范围和状态量映射以上述 Core 定义为准，不借一般公式扩大签收范围。
