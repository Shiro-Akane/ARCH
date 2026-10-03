# G 常数退役：待维护者确认的科学输入迁移

状态：**UNAPPROVED_ANALYSIS_ONLY**。代码已统一使用共享 CGS G，旧键明确
RETIRED_PARAMETER。本报告提供换算依据，不批准新物理输入，不证明离散或演化验收。
历史结果和原始文件保持不动；本轮没有执行 ARCH/FLASH、修改 .par 或调整阈值。

## 可复核来源与分析方法

运行只读脚本：

    python3 validation/gravity/analyze_g_input_migration.py

结果见 GravityConstantMigrationAnalysis.json，包含当前来源 SHA-256。
共享常数读取 src/physics/constant/PhysicalConstants.h，当前为
6.67430e-8 CGS。径向参数读取 radial_1d.py 的 regrid_cycle，EOS 参数读取
GravityBox.par；历史中心语义直接读取不可变基线
8fc0dd25eefd2243e8c36f85440bac46994e2e73 的模型及 .par。

以下相似变换仅针对固定 gamma/Cv、无反应网络的理想气体 Euler–Poisson：
令长度、速度、密度分别乘 a、c、b，q=Gnew/Gold，则时间乘 a/c，
压力乘 b*c²，温度和比内能乘 c²，势乘 c²、加速度乘 c²/a。
Poisson 与动量方程同时相似要求：

    q * b * a² = c²

径向原生积分度量不变时，质量乘 b*a^d，能量乘 b*c²*a^d；
球对称 d=3，柱对称单位轴长 d=2。相对误差可以比较，但绝对量不能直接
与历史量相等。连续方程相似不保证离散 AMR、固定绝对 floor、
求解器容差和舍入行为相似；不得为了通过修改这些控制或冻结门槛。

## 径向 refine/coarsen：候选而非已迁移输入

原场景：G=1e-20、rho0=1e7 g/cm³、域半径 1e8 cm、width=2e7 cm、
T0=1e9 K、amplitude=.2、nblockx1=4、lrefinemax=1、
refine=.01、derefine=.005、regrid_interval=2、max_steps=40、tmax=.1。
内外径向边界 reflecting，自引力 isolated。

当前理想气体 gamma=5/3、Cv=1.2471693927e8 erg/(g K)，
P=(gamma−1)rho Cv T，cs²=gamma(gamma−1)Cv T。
原 cs²=1.385743769666667e17，G*rho*L²/cs²=7.21634130269656e-15，
声学时间 L/cs=.2686324869165411 s。
按不含几何系数的 1/sqrt(G*rho) 定义，引力时间为 3162277.6601683795 s。

### 候选 A：只缩放密度/压力（待批准）

保持 a=c=1，b=Gold/Gnew=1.4982844642883897e-13：
rho0'=1.4982844642883896e-6 g/cm³，背景压力由 8.314462618e23
变为 1.2457430169455972e11 erg/cm³。
温度、域、宽度、速度及时间控制不变；G*rho、上述无量纲引力强度与
引力时间保持一致。这只证明代数相似，不证明科学 PASS。

未改 sml_rho=1e-12；floor/rho0 从 1e-19 变为 6.6743e-7。
初始背景高于 floor，仍需验证演化和 AMR 转移全部状态无 floor repair。
a=c=1 时比内能不变，但不能据此声称全部数值控制等效。

另有独立的初态迁移差异：历史径向模型默认 center_x=0，历史公共 .par
未指定 center_x；当前公共 .par 显式 center_x=5e7，径向脚本未覆盖。
对 amplitude>0 的高斯，这会移动初态峰。若批准保持历史物理形状，
应在该径向场景明确使用历史 center_x=0，不能无依据修改全局公共默认。
本轮未修改该输入或脚本。

tmax=.1 是上限，max_steps=40 也可能先结束。原 regrid_cycle 没有断言
实际到达 .1 s；需从历史输出确认真实终点及终止原因，再记录新运行相同证据。
不能把参数 tmax 当作已完成的演化终点。

冻结验收继续保持：refine/coarsen/no-change 均实际出现；质量及能量相对
漂移各 <1e-12；所有 plot 的 Gauss 相对误差 <1e-7；floor repair 为零。
批准后才可运行完整对应演化，并提交拓扑序列、终点与误差证据。

### 其他形式变换：未选用

只缩长度（b=c=1）：a=3.870767965518457e-7，新域半径
38.70767965518457 cm、width=7.741535931036914 cm，等效 tmax
3.8707679655184576e-8 s。改变物理尺度和时间控制，不作为冻结输入。

只缩温度（a=b=1）：c=sqrt(q)=2583466.663225984，
T'=6.6743e21 K，时间同样缩短。EOS 物理适用域未确认，也不作为候选运行。
不得自动选择任一变换或缩放阈值。

## FLASH/ARCH Jeans：常数身份与参考同步待确认

arch_jeans_o6plus.par 和 _128.par 的历史 G 为 6.67408e-8，
当前共享值为 6.67430e-8，相对变化 3.296334476066143e-5。
compare_jeans_o6plus.py 的解析参考仍写有历史 G。
FLASH .par 本身不证明其编译常数，需实际构建身份或常数来源证据；
外部 FLASH 执行不是本轮前置条件。

在现有 rho=P=1.5e7、L=1.14411、mode=2、gamma=5/3 下，
k=2πmode/L，omega²=gamma*P/rho*k²−4πG*rho：
omega² 从 188.48300574448461 到 188.48259105425433；
omega 从 13.728911309513387 到 13.72889620669682，
相对变化 −1.1000738679989297e-6；在 .1 s 的线性相位差
−1.5102816567136303e-6 rad。两者仍在稳定分支。

这些仅是频率敏感性计算，不是全场误差、离散振幅或冻结预算的通过证据。
不静默缩放 rho/P 以匹配旧频率，不修改振幅、cell-average 补偿、floor
或误差门槛。新 ARCH 输入与解析参考需经确认后同步共享 G；旧结果保留
不同常数身份，不能复用作新契约验收。FLASH G 未确认时应明确标识对照差异。

## 请维护者确认

1. 是否批准径向候选 A：只缩密度/压力，保持温度/域/时间控制，
   并在该场景显式保留历史中心 0？若不批准，请提供冻结的新输入。
2. 历史实际终点及 step-40 终止语义应如何与新运行匹配？
   是否确认上述 Gauss/守恒/拓扑/零修复门槛保持原义不变？
3. 是否批准 ARCH Jeans 输入与独立解析参考同步共享 G？
   FLASH 的真实 G 由何处核实，旧对照结果如何标识常数身份？

批准前仅保留分析，不执行输入迁移或相应演化 campaign。

## 配置错误测试的独立语义

run_self_gravity.py 原 restart-reject-gravity_G 已拆为配置层
RETIRED_PARAMETER 拒绝；gravity_rtol/atol/max_cycles 仍测试 checkpoint
controls 不相容。不同 saved G 的拒绝由 checkpoint_compatibility 单元测试覆盖。
该测试语义迁移不批准任何物理场景，也不代表演化 campaign 已执行。
