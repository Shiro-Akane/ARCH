# RZ checkpoint native W 测度身份（2026-10-05）

## 结论

RZ-CHK-MEASURE-01 的 domain/root-count/cell-shape 身份缺口已补并验证。
NATIVE MEASURE IDENTITY PASS；FULL RZ C/D PENDING，production gate 保留。
不将身份一致性当成独立几何公式精度、旋转科学或完整恢复事务的签收。

## 发布与恢复

AmrTree 增加只读 GetRootGrid，IO 读取真实树的 immutable root domain。
write_chk 从已绑定的 RZ tree 拷贝域和 root blocks，校验传入 config 相同；
不会只信任当前配置然后给旧 native 状态贴新域。验证在 mkdir/发布前完成。

当前 revision 2 + state tag 的 RZ checkpoint 必须有 NativeDomain：
version=1；bounds_order=r_min,r_max,z_min,z_max，bounds 为FP64，coordinate_unit=cm；
root_blocks=[Nr,Nz]，cell_shape=[BLOCK_NX,BLOCK_NY]，
measure_normalization=full_rotation。
原layout6的 native x1-fastest flattened-cell order保持，不改字段、物理坐标、
FP64 arrays、共享 W/V 公式或 checkpoint controller。

读取验证 shape/finite/positive/版本/单位/normalization；cell-shape乘积须与
实际 payload cells_per_block 一致。read_chk 在重建 live hierarchy 前，
精确匹配当前 config 的 domain/root counts 和实际编译的每轴 cell shape。
同product但不同axis shape不能通过。unknown/缺失不作默认猜测或自动迁移。
刚发布的内部 revision2/tag-only 文件若无NativeDomain同样明确拒绝；
原始文件仍保留，不转换。existing chart 不新增强制域字段，旧兼容保持。

W=integral(r dV)，CGS单位cm^4；V为cm^3且全旋转。
mom_w=m_phi单位g/(cm² s)，J=m_phi*W为g cm²/s；
J/V、代表速度m_phi/rho不作为第二演化数组。
算法/测度精度仍由单一GridMetrics所有者及独立科学gate验证。

## 实际反例与同一性

6类改变当前config：r_min/r_max/z_min/z_max/Nr/Nz。
实际 read_chk 均拒绝，populated arrays/controller不变；
实际write_chk同样拒绝把当前tree贴上异域身份，原checkpoint SHA不变。
7类实际HDF腐损：missing NativeDomain、malformed bounds、NaN bound、
wrong coordinate unit、per-radian normalization、future domain version、
same-product different axis shape；均拒绝且不改变live状态/原文件。

mixed 5-leaf、1280 native cells，原/恢复W逐值完全一致；
long-double累计非零J两侧为12.5663706143591711387。
这证明identity解释保持，不是用此serialization fixture替代科学旋转IC。
fixture rho=2/m_phi=.3/E=100，其native原值与composition/controller继续精确。
旧RZ identity/tag反例、旧Cartesian兼容与非法shape/domain规则同批保持。

第一次重跑因已有corruption副本文件名撞车退出，旧日志保留；
fixture改成选择新未占用文件名，不清理/覆盖旧样本。
新增W/J fixture首次误用命名空间，编译明确失败；
改为真实GridMetrics::Rz::AngularMomentumMeasure后通过，不改几何/阈值。
不是绕过失败或将失败当PASS。

## Driver、续跑与独立读取

实际DriverIO发布/恢复、失败传播和index生命周期检查PASS。
既有internal RZ RK2 continuation四例（direction0/1，inner0/1）PASS；
5mixed leaves、split=.001/final=.002，41040 native words一致。
该fixture angular motion为零，不用于旋转科学精度签收。

独立h5py读5个实际RZ checkpoint，逐个核对fixture的domain/root counts、
[16,16]cell shape、单位/normalization与FP64 native layout。
Cartesian原revision1且无NativeDomain；未扩展其正式恢复scope。

## binary 与冻结 JENS

baseline 739065e82cd60894db19fd3aa6d0dd0d382d343f。
新 CPU ARCH SHA256：
44086004d1a3cc09411f13099e1ce6c906ba20d33e86a9e49bb154404b4b209d。
source hashes见processed summary；
由于reader/serializer/binary改变，复验原uniform-lifecycle-1：
9短演化+9实际checkpoint restart，tmax=.02/split=.01，
原physics/config/end budget/thresholds不变，全部PASS。
native uniform/restart精确，off/output state和solve counts相同；
mass/E drift=0，max JENS relative error仍4.50750606346323e-17。

## 留存与后续

复用唯一ARCH-compute-optim、原CPU Release build-cpu，标准incremental
parallel28+现有可信compile-command scoped runners。guards无触发、swap增长0。
只提交源码、scalar/identity/shape summaries及报告；full logs/H5/checkpoints/ELF
在studio/.local/integration/rz-checkpoint-measure-20261005，不上传。

    ctest --test-dir build-cpu --output-on-failure -R '^checkpoint_compatibility$'
    python3 validation/io/run_driver_checkpoint_geometry.py --build build-cpu --output-root <new-local-dir>
    python3 validation/io/run_rz_checkpoint_continuation.py --build build-cpu --output-root <new-local-dir>

此节点关闭RZ-CHK-MEASURE-01的明确身份范围。
其他invalid topology/allocation失败事务、C Init/BC/EOS/Plotfile/API/Inspector、
D科学收敛、RZ-VISC-01/RZ-AXIS-01、ring整体certificate、CUDA仍独立待完成。
不修改物理定义/阈值，不开放public RZ生产能力，不新建workspace/main merge/tag，
不做Windows适配。
