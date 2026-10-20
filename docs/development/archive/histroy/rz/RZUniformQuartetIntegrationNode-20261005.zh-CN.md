# 严格等密度四子完整环体积分 / mixed Runtime 重绑（2026-10-05）

基线e490c58658bb30f03ac9e20c6dca455e688fd2ad。
结论：原1280cell失败输入及粗化后的512cell原请求PASS。
RZ-MIXED-RUNTIME-WORK-01在该记录输入范围关闭；旧失败producer/日志不覆盖。

## 严格同一数学与所有者

改动仅原GravityBoundary source-tree积分策略，没有粗化真实网格或平均density。
严格native/root精确坐标资格成立时，检查四个直接叶子siblings完整分区：
全部共享边完全相等、无缺失/额外子格、每个rho严格同值。
因此原piecewise-constant完整环体Newton积分在不相交四子矩形之和
严格等于其矩形并集上的同密度积分；接触奇点的局部可积与零测度共享边
不改变此加性。不是点质量、平均源、bbox推断或远场截断替代。

同一finite_ring_potential_enclosure提供原有certified区间，
allowance=down(4*原leaf allowance)，原65536 box上限不变；
优先该完整积分，失败才用原far/descent，不接受partial enclosure。
每次求积及额外fallback都计入原leaf+parent global cap100000。
range/kernel/AGM计数包括失败尝试，不隐藏代价。
完整source inputs/G/generation不删改，候选每次从当前source重新形成；
原source/B/assembly/A/evaluation/native norm与完整最终请求ledger继续验收。

## 真实结果

同前失败工程输入：2→8→5实际Runtime事务、1280cells、rho1、
r=[0,1]/z=[-.5,.5]、rtol1e-10/atol0、cycles200、work caps原值。
Mixed epoch3 field residual upper4.6056448529745771e-15
<=safe1.6571030555802408e-14。
合法新工程输入粗化到2blocks/512cells后，
Stage重绑epoch4；upper4.9809729219668673e-15
<=safe1.4684682266926192e-14。
两次Current lease=1/2，full source inputs匹配实际新ledger；
原mixed field显式退役、候选普通plot/Hydro reader仍拒绝，
physical_qualified=0，time0/steps0。
粗化前显式新输入不冒称连续物理演化。

runner编译+两次检查223.405秒、peak owned RSS1283872KiB、swap0，
guard未中断；不是benchmark。没有提升cap或缩小失败域。
本轮没有export实际mixed全部visit计数，不编造数字；
原cap检查仍作用于每一次实际已收费尝试。

## 独立参考与反例

精确uniform资格：48尝试/48接受、192原叶贡献完整；
非均匀density不尝试，origin=.3未证明精确几何即使uniform也不尝试。
独立Fraction从原cell metadata重建完整四子分区，
并验证源质量/原生积分矩加性，四个uniform/mixed记录PASS。
同四例88cells完整原source/B/residual/norm账本及预算独立Fraction PASS，
这不是所有1280 Runtime数组的独立continuous field验收。

CPU analytic3.18秒 / self生命周期9.19秒 PASS；
CUDA-enabled Host生命周期8.81秒 PASS，尚非Device RZ数值资格。
architecture/diff PASS。生产ARCH ELF未重建，不声称Build Manifest fresh。

## 科学门槛与后续

一般非均匀源费用finding不能由这个限定修复自动关闭；
一般rounded geometry也保留原下降。接下来验证fatal transaction rollback、
独立连续势/力及Hydro力矩、axis/viscosity和原空间收敛，
完整A→D与Device/长跑仍OPEN，生产能力gate不改。
JENS CUDA公共门槛候选未借本节点绕过。

处理后summary在validation/gravity/results/rz-uniform-quartet-20261005。
原始Runtime/ELF/日志在studio/.local/integration/rz-quartet-mixed-runtime-20261005
及同prefix，raw参考在rz-quartet-reference-20261005。本机持久保留，不上传。
