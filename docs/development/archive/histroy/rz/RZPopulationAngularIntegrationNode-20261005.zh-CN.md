# RZ C：真实 Init → Root → Driver ghost 接线节点

基线 c3364e001d47c0cb50e4d8b2fa07d3713ec70e36，依据科学清单8.1/8.4，
继续唯一 /home/arch/projects/ARCH-compute-optim。硬件14700K + RTX4070Ti；
本节点只用CPU，没有Windows适配或CUDA签收。

## 真实行为变化

- ProblemHelper::PopulateState 的 explicit RZ 分支实际调用共享 InitialRzCellState，
  rho/mom_r/mom_z/E/rhoX按V平均，mom_w按W平均。物理 Init 仍给 primitive/EOS输入，
  没有复制模型物理或新增模型、可演化J/ell数组、精度配置。
- 全部块只写临时FluidState。OpenMP失败收集后先rethrow；
  全成功才以no-throw swap交付。修复、不可解析平均、species/数组布局错误明确拒绝。
  原Cartesian/polar点转换及其density-floor修复路径保留。
- RZ在初始化时只交付物理内区；ghost标记unavailable（NaN），不在负半径取V/W，
  不用外域Init外推假装已应用物理BC。padding不属于物理单元。
- Driver::InitializeRootState将context geometry绑定真实InitRootGrid，
  在Init前预验证现有BC计划；随后用原BCHandler填物理ghost。
  块间ghost占位由真实DriverRuntime::initialize_topology用committed handles、
  现有RZ exchange替换后才发布。初始化未虚构handle或复制ghost执行器。

RZ-INIT-W-01 的真实midpoint接线缺口关闭。
RZ-INIT-REPAIR-01 在**初始化范围**关闭：新路径不发布修复候选或错误V-only角向修复。
旧历史RZ floor工程夹具已变成拒绝/不变反例；没有修改numerical floors本身，
没有通过改变E/J使高旋流过关。
运行期repair报告/checkpoint ledger的角向语义仍需显式迁移/审查，不能据此宣布全部账本完成。

## 最终源码的真实 scoped 结果

两根块共512 native cells，256条axis/outer/interpatch ghost donor/sign逐位检查：
最大状态算术误差3.5527136788005009e-15；
独立旋转J积分相对误差3.5132429098631294e-17，
沿用原2e-12算术工程门槛及1e-12角动量门槛。
真实Root→Runtime拓扑/BC/exchange，无timestep和scientific output。

跨块候选一块可解析、另一块闭合不可解析，拒绝后所有原field/species/ENUC/repair
bits不变；NaN padding/ghost也用bits比较。species不匹配、截短数组与未知profile
在callback前拒绝。原polar正常/repair及RZ正常z-dependent EOS夹具通过。

注册Gaussian仍使用实际SetupChecked→InitializeRootState→PopulateState/EOS，
simulation/GaussianPulse/Gaussian.cpp 未改，准确source SHA：
fb21321b48c8455973bd971f6bf7d20b7f5dfbc410bb897dff8c65c5e8f55ca9。
分别加载工程probe的1/2/4根块每轴（max_blocks=32）输入；
域、EOS、Gaussian参数固定，256/1024/4096 cells。
独立long-double erf原函数给原生r*d r*d z单元平均，不调用生产求积。
RMS组分误差4.4743054915595053e-6、2.8246530940384619e-7、
1.7701627525356574e-8；order3.9855181615834749 / 3.9961197797852464。
沿Core原至少1.8要求，不凭实测拟新absolute误差阈值。
这是初始化单元平均一致性，非AMR演化、旋转平衡、力或完整模型科学签收。

首轮旧midpoint Gaussian参考失败记录保留；新参考不是用当前输出反推。
另一个首轮失败是NaN sentinel用vector ==导致的快照误判；
已换为严格bit比较，未改变失败事务行为，日志均保留。

## 回归、build 与身份

最终相关C++目标已重编；7项Core/API回归全通过：
preview_initial_conversion、preview_api_contract、preview_full_model_contract、
preview_mesh_geometry、initialization_probe、preview_session_contract、preview_cellular_2d。
architecture audit / diff check通过；没有frontend改动或重复无关desktop UAT。

现有CPU Release标准增量build、parallel28，无configure。
最终guard16.160s、peakRSS6154088KiB、swap增长0。
生产ARCH SHA256：
7c4d8d3c450b07daccd3e0cb21242bc3c5e499371e9a247e49fcefbea73fec16。

该**最终binary**匹配的冻结 uniform-lifecycle-1 9短演化＋9真实checkpoint续算通过。
终点0.02、真实split0.01、输入/原阈值不变；mass/energy漂移0，
maxJENS relative error4.50750606346323e-17，全部restartExact。
最终guard45.103s、peakRSS819132KiB、swap0；这是工程短包，不是CUDA/长跑benchmark。
新增layout preflight改变binary后重新运行最终receipt；首轮证据不覆盖最终source。

## 仍未关闭

完整C/D、运行期repair角向报告与checkpoint ledger、RZ-VISC-01、RZ-AXIS-01、
完整环体source/geometry/RHS组合certificate仍需推进/科学review。
初始化Gaussian四阶和旋转积分不能替代axis force/evolved至少1.8的独立门槛。
production RZ gate保留；不自动开启RZ Preview/长跑或CUDA。

处理后证据validation/amr/results/rz-population-angular-20261005/summary.json。
原始H5/plt/checkpoint、对象/ELF、全量日志仅在ignored
studio/.local/integration/rz-population-angular-20261005（final-binary保存最终receipt）。
无新workspace、main merge、tag或Windows工作。
