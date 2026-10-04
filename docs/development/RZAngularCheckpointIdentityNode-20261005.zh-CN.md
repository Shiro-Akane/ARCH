# RZ C 节点：checkpoint 角向表示身份（2026-10-05）

## 结论

CHECKPOINT REPRESENTATION IDENTITY PASS；FULL RZ C/D PENDING。
既有 revision 1 无法区分原体积平均角向状态与 m_phi=J/W；本节点升级
写出/读取/拒绝链，不静默迁移。RZ 使用 geometry_semantics_revision=2、
geometry_chart=axisymmetric-rz、mandatory state_semantics=rz-m-phi-j-over-w-v1。
checkpoint layout version 6 不变；existing chart revision 1 旧兼容不扩大或取消。

## 映射与入口

Data/mom_w 原 FP64 bits 序列化，是唯一 m_phi=J_cell/W_cell，
W=integral(r dV)，单位 g/(cm² s)。不增加独立角动量演化数组。
代表速度=m_phi/rho，J/V=m_phi*W/V；同单位不能混淆平均语义。
rho/径向轴向动量/E/rhoX 沿原 V 平均；native X、ENUC/controller 保留。

CheckpointGeometryIdentity 单一所有者定义 current RZ revision/tag；
DriverIO 读取 Runtime immutable profile，再选择 identity，不猜 cylindrical。
HDF writer 发布 tag；reader 在恢复 live hierarchy 前验证 revision/tag。
revision 1 无法证明 W 表示，显式拒绝；missing/wrong/future identity 同样拒绝。
不自动换算、修改或恢复旧 RZ 文件。

## 实际验证

CPU arch_checkpoint_compatibility / CTest PASS。
mixed 5-leaf native RZ 非零 mom_w=.3 精确 round-trip；其他 FP64/X/ENUC
与 controller 精确恢复。rho=2 时代表速度 .15 的语义明确，
但 raw serialization fixture 不是轴正则旋转 IC 或科学演化参考。

真实 HDF 副本分别修改 revision=1、删除 tag、写错 tag，read_chk 全拒绝，
populated live arrays/controller 和原 checkpoint SHA 不变。
future revision 3 写前拒绝，不覆盖旧文件；旧 Cartesian metadata-less
compatibility 与 illegal dimension/chart 反例保持。

实际 DriverIO -> serializer -> HDF -> restored Host Runtime 两种 chart PASS；
index success/failure、create/retry、buffered failure propagation 检查保持。
独立 h5py 读取实际文件：Cartesian revision 1 无 RZ tag，RZ revision 2
有准确 tag；全部 native state/composition datasets FP64。
该 fixture 无时间步进，不外推科学正确性。

既有 internal RZ 两步 RK2 continuation 同批更新 identity：
direction=0/1、inner r=0/1 四例，split=.001/final=.002，5 mixed leaves；
41040 native words 在 split/restart/continuation 精确一致；源文件 SHA 保持。
该轴向平移 fixture 的 m_phi=0，不当作非零旋转/RZ科学参考。
实际源码重新编译/链接，不复用旧 standalone fixture ELF。

## 当前 binary 与冻结短包

baseline b5a6235130adf3659ff74d547e99ddbe2ccf3c12。
新 CPU ARCH SHA256：
1116a7d17d6d63ffa478fb73941be3799aa46b6ec51eee4b136f242a366d7575。

serializer/binary 变化后，复验冻结 JENS uniform-lifecycle-1：
1D/2D/3D × disabled/output-only/active，9 short evolutions + 9 actual restarts，
tmax=.02、split=.01，原输入、阈值和physics保持。
全部 PASS；原生 uniform/restart 精确，off/output state/count 相同，
mass/energy drift=0，JENS 最大相对误差4.50750606346323e-17。
不是非均匀 JENS、RZ、CUDA或长跑验收。

## 未关闭的缺口

RZ-CHK-MEASURE-01：ChkIO 使用当前 config 重建 leaf grid；
StateControlIdentity 没有 domain bounds 或 root-block counts。
当前表示 tag 不能单独证明生成 W 的几何域相同；本节点未宣称
wrong-domain restart 会拒绝。下一步以真实反例补 measure binding，
不能通过改物理域或换算 J 兼容旧文件。

C 的 Init/BC、EOS/声速/JENS、Plotfile/API/Inspector仍待逐个验证；
Plotfile geometry revision 和 repair diagnostics 不由 checkpoint PASS 自动签收。
D 科学收敛、viscosity/axis未决参考、ring original RHS证书、CUDA待完成。
production RZ gate保持，不开放公共 RZ run 或标 complete。

## 复现、留存

    ctest --test-dir build-cpu --output-on-failure -R '^checkpoint_compatibility$'
    python3 validation/io/run_driver_checkpoint_geometry.py --build build-cpu --output-root <new-local-dir>
    python3 validation/io/run_rz_checkpoint_continuation.py --build build-cpu --output-root <new-local-dir>

唯一 worktree /现有 CPU build-cpu，仅 standard incremental parallel28 与
已有可信 compile-command scoped runners；无 configure/newtree/main merge/tag修改。
memory guards 无触发、swap growth=0。
原H5/checkpoints、ELF/full logs/inputs留本机
studio/.local/integration/rz-angular-checkpoint-20261005；
只提交源码、processed scalar/layout/identity summaries和说明。

fetch 后 codex/o8-boundaries仍11a321d5604f9ee62b9f9587c81f14de4f128bc4；
未merge，无Windows适配。
