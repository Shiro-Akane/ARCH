# RZ 实际 Runtime AMR transfer / frozen parent veto（2026-10-05）

基线e2204588897dd97eb95a92ff534dce9f8bbccd3b，唯一Linux/WSL工作区。
结论：内部CPU原事务transfer/ledger/父态veto PASS；生产与完整科学门槛保持。

## 实施

DriverRuntime新增显式regrid_native_rz_candidate，不从SimConfig/API/Driver调用。
仅CPU RZ普通AMR；非RZ/Device/JENS-repair请求拒绝。
共享perform_regrid_impl记录与execute_regrid原完整transaction；
默认perform_regrid仍拒绝RZ。无复制restriction/prolongation/物理数学。

原PrepareRegrid、TopologyIdentityRegistry、migration plans、源backup、
native RZ ghost/boundary、ledger publication及retired-release均照原所有者执行。
原J/W表示、EOS代表闭合与物理阈值没有修改。

## 实际证据

两块root、每块16×16，r=[0,1]/z=[-.5,.5]，DENS-only普通indicator，
lmax1、合法threshold .001/.0005、capacity16。
零旋转与非零可解析输入经真实Runtime initialize/regrid：
2→8→2及no-change成功，新ledger拒绝旧root/fine handles，
新Current interior/ghost readable，no-change保持handles/epoch。

独立long double原生face积分V=pi*(rh²-rl²)*dz，
W=2*pi/3*(rh³-rl³)*dz，比较每次输入的rho/E/rhoX体积收支与m_phi*W。
沿用现有短transfer 1e-12预算；J用sum(abs(m_phi)*W)归一化；
零J使用严格零，无tiny denominator。
最终七行收支最大相对误差2.5020639583995762e-17，零旋转J始终严格零。

冻结四子格W1:7/V1:3、m_phi=1/-16、E=9/16 / 2049/16：
原CandidateParentResolved因CoarseFluid保持该family细化。
实际8→5形成混合mesh，另一个可解析family正常粗化；
四个原子态momentum/energy bits保持，未补热/修J。
不是whole transaction异常rollback。随后显式合法新输入恢复5→2，
收支仍按本次新输入独立比较，不把人为输入替换冒称物理演化。
冻结parent_m=-111/8、parent_eint=-9/128未改。

真实runner重编六源，最终exit0。
默认Cartesian Runtime 4→8→4、七次gather回归PASS；
architecture/diff PASS。未重跑不受影响Studio或旧全科学基线。

## 未覆盖

没有在新混合mesh上执行finite-ring gravity field；没有fatal finalizer/capacity
注入rollback，本项仅证明原父态group veto和合法恢复。
没有Hydro边界/外源力矩冲量、axis/viscosity、1.8空间收敛、
连续势力、完整A→D科学签收、Device或长跑/benchmark。
公开RZ production regrid/Device门槛保持；生产ELF未重建，不声称Build Manifest fresh。

下一步在真实混合拓扑接GravityStage rebind/retired-source和原ring预算，
并验证fatal transaction rollback；后续科学输出仍按原独立参考签收。

处理后summary：validation/gravity/results/rz-actual-regrid-20261005/summary.json。
raw/ELF/full logs在studio/.local/integration/rz-actual-regrid-final-20261005；
初稿与veto加强前日志原样保留在同前缀本机目录，不上传原始数据。
