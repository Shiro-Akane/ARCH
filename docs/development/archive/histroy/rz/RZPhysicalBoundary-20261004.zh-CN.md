# RZ 物理边界与 mixed Hydro 联动

## 共享逻辑规则
BoundaryPlan 的内部 RzAxis=4：保留既有Periodic0/Outflow1/Reflecting2/Inactive3。
不是新增可编辑配置字符串；Config/API boundary enum 没有扩展。
规则仅允许二维lower x1：
径向镜像 donor，轴向j不变，无phi半周移位；
rho/eng/ENUC/species/mom_z 偶，mom_r/mom_phi 奇。
源选择、component_mapping、Host lowering/executor 复用原 owner，
不在BCHandler建立第二套复制循环。

BCHandler接受显式 GeometrySemantics。
只有prepared source domain从r=0开始且当前patch确实r=0才选择axis plan；
非轴线patch与非零内边界维持用户普通BC。
RZ r=0 与径向 periodic 冲突时明确拒绝，不fallback。
旧无Grid logical_plan()保留兼容；新增logical_plan(Grid)返回经chart/布局验证的实际选择。
未将公开runtime Grid切换为RZ，未改变旧polar/spherical 默认映射。

## 独立执行证据
真实padded Grid/FluidState/BCHandler。
272个axis、axial、outer和lower-r/lower-z corner ghost单元，
rho/三个momentum/eng/ENUC/两species 与独立donor/sign参考逐位比对。
包括+0/-0反射，轴线mom_z保留、mom_phi取反，不做j移位。
角点经既有X→Y相位组成(-r,-z,-phi)。
全域r=0 profile在non-axis patch仍与legacy BC完整状态hash一致；
nonzero source domain内边界也保持普通reflecting规则。
wrong Grid chart/不匹配r=0patch在写state前拒绝。
错误axis位置/维度、r=0 radial periodic明确拒绝。

旧1D/2D/3D冻结BoundaryPlan指纹、BCHandler全state与ENUC快照未更新且通过。
CPU boundary_plan / curvilinear_metrics / amr_operation_plans 3/3 PASS。

## 联动增量
现有mixed Hydro夹具不再只借助解析physical ghost：
stage前实际RZ BCHandler→exchange，stage后BCHandler→exchange→reflux。
四组5-leaf/5120 active-cell，radial/axial × inner0/1；
constant-state最终最大误差仍3.55618e-17。
之后只定向重编/重跑变化的curvilinear_metrics，1/1 PASS；
未重复unchanged boundary/AMR checks。
完整source/base/test ELF身份与stream精度见Summary，
日志留ignored studio/.local/integration/rz-physical-boundary-20261004。

## 未完成
生产IHydroSolver/driver scheduler、public chart/config/source identity、
扩散/RKL、gravity/IO/checkpoint与CUDA仍待统一迁移。
BC与混合单阶段不替代非均匀/长期独立科学验证；
角动量regrid finding保持开放，未修改预算。
CUDA消费显式profile时必须使用真实Grid-aware plan选择，旧no-arg不能代表RZ选择。
未完整Build ARCH、未运行simulation、未push/tag/main merge；
raw H5/plt/checkpoint不提交。
