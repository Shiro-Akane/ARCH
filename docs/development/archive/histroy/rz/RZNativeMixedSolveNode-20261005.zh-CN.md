# RZ static mixed AMR native solves and source retirement

**STATIC MIXED NATIVE ORIGINAL REQUEST / SOURCE RETIREMENT PASS；不是完整RZ科学签收。**

基线aaaffa39e8a9ccf9d0b19c8860288abc5a0e7de1；
唯一/home/arch/projects/ARCH-compute-optim。本节点只扩展numerical fixture和独立reference，
没有修改scientific Core、原source/AMR/solver数学、生产能力或任何阈值。

## mixed真实求解

复用同一native_ring_solved_probe，增加独立native-ring-mixed-probe入口，
不重跑已通过均匀组。4x4 root中心四个root cell细化至level1：
每例12 coarse +16 fine=28 cells，包含真实coarse-fine face fit；
axis/off-axis两种radial origin0/0.5，z=-0.5，rho=1，共享CGS G=6.6743e-8。
既有curvilinear isolated operator、source tree/current identity、CPU execution source、
certified ring、canonical effective_rhs、multigrid solve、actual apply/subtraction、
native source/B/assembly/A/evaluation ledger均真实调用。

face target1e-18、maximum_boxes_per_leaf65536、maximum_leaf_evaluations100000、
max_cycles200、原rtol=1e-10、atol=0完全保持。没有更松或更严格的特例请求。
实际ring和solve成功后，ideal native assessment在原请求下接受；
physical_status仍UncertifiedInput，不改production RZ gate。

最终probe准确SHA记录于summary。该最终build实际运行40.430s，两例56 cells/132 faces。
独立Fraction按root geometry、实际final construction policy、exact Gram重建所有A/B，
验证actual RHS到理想source+boundary box、actual residual evaluation、
worst native residual norm²及original request。
参考进一步直接验证每个RHS box的worst residual²<=rtol²*其minimum RHS native norm²。
axis总residual upper8.592618076165241e-17 <= T_safe1.1564498921357118e-15；
off-axis5.638391462135799e-16 <=1.677867015381812e-15。
scope由实际record里的mixed标记与网格给出，不再将参考限制文案硬写为两种uniform roots。

## 实际density/身份更新的拒绝路径

ring-source-retirement调用同一静态mixed operator与真实tree.update/ring_boundary。
先确认current ring可消费；然后实际修改rho[0]=1.25、time=.125、
dependency version=2、storage generation=2，调用真实update。
旧root certificate和旧request ID均被拒绝；新ring绑定修改后的source ID及增加generation。
wrong topology epoch10不能更新bound epoch9的tree；该失败update也退役已存证书。
两种radial root均通过，无simulation stage或时间演化。

这不是通过手动递减一份ring generation伪造生命周期检查。
但fixture使用一个抽象numerical-domain dependency ID：
不能据此证明生产Runtime对所有AMR blocks的density dependency收集完整，
也不能当作真实regrid/migration/stage publication或restart事务验收。
上述实际生产路径继续保留独立gate。

## 回归及身份

production CPU ARCH SHA仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510：
沿用匹配冻结JENS9短+9实际restart，未重复未变化ELF的短包。
scientific Core无改动，复用aaaffa39节点的6项Core scoped+9项ring数学合同receipt；
本节点执行新的两例mixed实际求解、独立Fraction和两例真实source retirement。
architecture audit、diff check PASS，源码/最终probe/raw record SHA链在summary。

本节点只compile现有CPU Release的arch_composite_poisson，parallel28；
没有configure/build migration、没有新worktree或merge main，
没有simulation/H5/plot/checkpoint输出或Windows工作。

## 余项与复现

continuous Phi/force和原空间收敛、一般root舍入坐标构造、
Runtime AMR/stage source publication、全RZ角动量消费者仍需验证。
2x2 interpolation、axis/viscosity finding未关闭；
production能力、CUDA及新长跑仍按对应完整CPU科学gate执行。

    build-cpu/arch_composite_poisson native-ring-mixed-probe > <new local probe.json>
    python3 validation/gravity/rz_native_ring_solved_reference.py --probe-record <probe.json> --output <new scalar summary>
    build-cpu/arch_composite_poisson ring-source-retirement

处理后summary：validation/gravity/results/rz-native-mixed-solve-20261005/summary.json。
raw phi/source/rhs/face/coefficient arrays和完整日志留studio/.local/integration/rz-native-mixed-solve-20261005；
只提交源码、独立脚本和scalar摘要，不上传原始H5/plt/checkpoint。
