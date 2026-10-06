# Core review 标量测试缺口修复
评审基线3eb727df077b0839c2048e919a28958fba5b124d；Core契约4639774f保持。
close() 显式拒绝任一操作数 NaN/+Inf/-Inf，且保留原有限数舍入比较。
同一离轴场加 g_r=3/10、g_z=-7/20、rho=2，初始u_r/u_z=0，
full2pi径/轴源率为(24/5)*2pi、(-28/5)*2pi，
dt=1e-4 时分别delta_m_r=3/50000、delta_m_z=-7/100000。
非零、异号、不同大小的分量独立断言；初始V能量功仍沿原phi参考。

实际g++编译并运行：正确候选PASS；13个变体全部非零退出：
mom_u/mom_v/mom_w/eng各NaN及Inf共8，
radial/axial/energy误用W分母3、angular误用V分母1、radial/axial串换1。
所有变体均编译成功，不把编译失败计作有效拒绝。
回放原3eb测试时eng=NaN及radial误用W仍退出0，精确复现Core两项finding；
新测试均拒绝。原私有GravitySource候选实现未改、publicCore/ELF门槛不变。

复现脚本 validation/core_contracts/rz/run_stage_integral_mutations.py，
参数--source指向已准备私有candidate/source、--output指定新.local/integration子目录。
source不改写；变体仅在新local include overlay编译。所有full build/log/ELF保留本机，
提交summary只有exit status和header/test/ELF SHA，绝无raw H5/checkpoint。

该节点只关闭标量fixture测试敏感性缺口；stage producer、source/chart/epoch、
全部状态/账本拒绝保护、冻结RK演化/restart仍待真实证据，不关闭外源科学finding。
O8双方按固定SHA/模块整合；41冲突记录作为用户提供的协调事实，
本轮未整分支merge或重做冲突计数。历史Phase分支未退役。
2D性能条件仍未落实；未运行3D、正式重复计时或long。
