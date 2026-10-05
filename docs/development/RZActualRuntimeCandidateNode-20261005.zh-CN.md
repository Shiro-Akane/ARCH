# RZ 实际 Runtime → GravityStage → 原服务候选节点（2026-10-05）

基线703fc2bdfeda4d129b2beb9eba1cfa93e45c1ab2，唯一Linux/WSL工作区。
结论：真实CPU root Runtime租约/三槽stage候选PASS；完整RZ科学签收仍OPEN。

## 实施与边界

原GravityStage新增显式 Qualification::NativeRzCandidate。
默认仍Production，无SimConfig/parser/API/Studio入口可选择候选资格；
要求RZ chart、SelfGravity和CPU Runtime，Device拒绝。
复用原stage全块ledger readable检查、Runtime handles、slot/version和每次storage lease；
原服务使用同一source/G/rtol/atol、finite ring、完整residual ledger及force/work。
候选只写单独native_rz_candidates.tsv，physical_qualified=0；
不读普通report、不给backend发布physical patch，也不开放plot/CFL/Hydro消费者。

真实fixture经原BCHandler(RZ)/DriverRuntime.initialize_topology构造，
与前节点相同两块512cells、r=[0,1]/z=[-.5,.5]、rho1。
Current、Scratch、Next版本由真实Runtime stage_context ledger/clock发布。
Capture记录原GatherDensity每个native cell，逐块逐offset与实际选中buffer一致；
非活动buffer置NaN，不能默读Current。没有以服务手填身份替代Runtime租约。

## 终止证据

run_gravity_runtime_contract.py --native-rz 重编六个trusted existing CPU源并链接。
exit=0，ACTUAL_RZ_RUNTIME_CONTRACT_PASS，三次actual gather、512cells。
lease=1/4/5（中间非法请求消耗lease），source generation=1/2/3，
每条total upper4.443476783658658e-15 <= safe1.4684682266979847e-14。
未发布Scratch、仅非首Scratch未发布均在gather前拒绝；
失败使旧candidate不可读，完整发布后原合法请求恢复。
普通potential/plot/CFL/非首Hydro patch消费拒绝candidate；
显式invalidate退役。默认Production stage RZ bind在gather前拒绝。
time=0、steps=0；未进行Hydro/EOS演化。

受影响的默认实际Cartesian Runtime六源重新编译：4→8→4、七次gather及
非首lease/version回归PASS。architecture audit / diff check PASS。
未重复不受影响的Studio或旧全科学套件；此节点没有Device数值资格。

## 不可提升的范围

实际Runtime root topology已验证，但RZ execute_regrid原门槛未移除；
负例验证保持拒绝，不把它写成拓扑迁移PASS。
下一步须完成真实混合拓扑/退役handle/迁移及角动量消费者验收。
连续势/力、一般rounded几何、axis/viscosity、Hydro能量与角动量、
完整A→D、CUDA/长跑仍未签收。选中slot同一密度，不证明演化场误差。
独立物理输出/正式配置及API仍不可读candidate，不声称生产支持。

标量summary：validation/gravity/results/rz-actual-runtime-20261005/summary.json。
本机raw目录studio/.local/integration/rz-actual-runtime-20261005包含完整log/ELF，
Cartesian回归在rz-stage-cartesian-regression-20261005。
原始数据不上传。生产ARCH ELF未重建，不声称当前Build Manifest fresh。
