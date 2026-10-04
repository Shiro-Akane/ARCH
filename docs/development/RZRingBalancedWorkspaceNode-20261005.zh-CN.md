# RZ bounded balanced workspace and actual nonzero native solves

**WORKSPACE / ACTUAL DISCRETE ORIGINAL REQUEST PASS；不是完整RZ科学签收。**

基线5ee8d79fc7290a29f1e5a70d18e42f25f17075c7。
唯一/home/arch/projects/ARCH-compute-optim，科学清单§7.3原请求和§7.4有界CPU数学节点。

## 根因与同owner修改

旧finite_ring_potential_enclosure每次subdivision线性遍历全部box，
重算lower/upper累加并选择largest width；高box数量下工作为二次量级，
逐项outward累加也给总区间加入随box数增长的舍入宽度。
先前actual axis4x4在1e-18 face target、65536 boxes/leaf、100000 leaf evaluations下WorkLimit，
尚未进入Poisson；该failure receipt和输入未删除。

同一有限环体owner新增RingBoxReduction：原box interval作为leaf，
complete binary tree维护outward lower/upper sum、最大FP64 width与其index。
replace仅更新该leaf至root路径；无从已舍入total做减法，无另一套积分。
容量在原maximum_boxes内，节点少于等于2*next_power_of_two(maximum_boxes)；
资源仍属CPU bounded controller，积分/K/导数/余项公式均未改变。
empty child按exact zero identity传播；有效两child相加继续向外舍入。
同宽选择earliest index，保持旧扫描的subdivision tie规则。
非有限/负lower/反向interval、累计overflow或原box failure均明确失败；zero没有floor。

只改变瞬时workspace的归约/选择，不改变叶源、观察点、共享G、积分formula、
near/far/contact分支、split axis/midpoint、caller rtol/atol或任何box/work上限。
归约误差界更紧不等于更改科学验收标准。

## 独立证明与合同

实际workspace probe：capacity1/3/16/257/4096/65536，720次真实replace；
独立Fraction重建所有当前leaf interval精确和，返回lower<=精确lower和<=精确upper和<=返回upper。
同时对照原FP64 width largest choice及earliest index；
每次node更新计数严格1+ceil(log2(next_power_of_two(capacity)))。
这是资源复杂度证据，不用CPU墙钟证明数学。
empty/exact zero、subnormal、1e±300、tie、capacity越界、NaN/nonfinite/negative/overflow反例通过。

9项既有数学合同全PASS：
ring-contact-gauss3、ring-contact-log、ring-separated-gauss、ring-far-leaf、
ring-native-face、ring-enclosure、ring-axis-enclosure、ring-k-interval、boundary-acceptance。
原WorkLimit/zero-target等反例继续保持失败传播。
6项Core scoped CTest全PASS，9.72s；architecture audit和diff check PASS。

## 真正的非零source solve

完全相同原严格输入：两种radial origin0/0.5、z=-0.5、4x4 Cartesian index的native RZ mesh，
rho=1、shared CGS G=6.6743e-8、curvilinear isolated boundary；
face absolute target1e-18、maximum_boxes_per_leaf65536、maximum_leaf_evaluations100000。
现在真实ring均Bounded，existing multigrid得到actual Phi，再用真实apply/subtraction生成residual。
assessment消费source/B construction/root potential/RHS assembly/A construction/evaluation及ideal native RMS。
caller原请求仍rtol=1e-10、atol=0；没有simulation timestep/output。

最终actual probe的32 cells、76 faces由独立Fraction root geometry/Gram重建A和B：
分别检查actual computed RHS距离ideal RHS interval、actual residual evaluation，
完整原始residual box的native norm²、returned upper和原请求。
更直接检查每个RHS box对应的最坏residual²<=rtol²*该box最小RHS native norm²。
axis：total residual upper6.306021722740261e-17 <= T_safe1.1370095481114307e-15；
off-axis：6.833362380027573e-17 <=1.6516908386960193e-15。
这是native discrete问题原请求的独立检查，不只相信solver Converged。

初次独立参考因probe缺construction字段拒绝；已补实际final policy并重跑最终actual probe，
不根据文件名/样本数推断policy。最终Fraction两例PASS。
原boundary积分可靠界仍由共享leaf证明和相关合同负责，当前脚本对其真实native消费独立验收；
它不是另一份continuous Phi/force参考。

当前scope只关闭这两种均匀exact-coordinate4x4的strict-budget WorkLimit：
2x2 interpolation finding、一般root舍入坐标、mixed AMR实际solve与continuous Phi/force仍未签收。

## 身份与计时边界

旧axis失败诊断203.241s；新最终two-case成功diagnostic35.464s。
由于旧检查在第一case失败，此数值不是matched成功benchmark，不推导正式性能倍数或长跑结论。
最终12组source/RHS（264 cells）、8组native reduction（176 cells）回归PASS。
production CPU ARCH SHA仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，复用匹配冻结JENS9短+9restart，不重复同ELF短包。
增量CPU Release build parallel28，guard未停止，swap growth0；
probe build peak590424 KiB，production peak1809700 KiB。
未new/configure build tree、未merge main、未改root STATUS或Windows适配。

## 发布边界与复现

production RZ gate仍保持；后续continuous Phi/force、general geometry、自然recovery、
axis/viscosity及全A→B→C→D CPU签收完成后，再统一CUDA与批准benchmark/long runs。

    python3 validation/gravity/rz_ring_balanced_reduction_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local output>
    build-cpu/arch_composite_poisson native-ring-solved-probe > <new local probe.json>
    python3 validation/gravity/rz_native_ring_solved_reference.py --probe-record <probe.json> --output <new scalar summary>

处理后summary：validation/gravity/results/rz-ring-balanced-workspace-20261005/summary.json。
raw workspace/phi/rhs/coefficient/boundary arrays、完整日志及ELF留studio/.local/integration/rz-ring-balanced-workspace-20261005。
原始H5/plt/checkpoint不提交。源码/scalar摘要/独立脚本可以直接review，本节点不声称整阶段完成。
