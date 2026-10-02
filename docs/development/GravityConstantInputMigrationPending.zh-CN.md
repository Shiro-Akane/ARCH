# G 常数退役：待维护者确认的科学输入迁移

代码已统一使用共享 CGS G，旧键明确 RETIRED_PARAMETER；本文件不批准新物理输入。
历史结果和原始文件保持不动。

## 径向 refine/coarsen 场景

位置：validation/gravity/radial_1d.py 的 regrid_cycle。
原输入 gravity_G=1e-20、temperature0=1e9、width=2e7、amplitude=.2、
nblockx1=4、lrefinemax=1、regrid_interval=2、max_steps=40、tmax=.1。
原验收要求实际 refine/coarsen/no-change 均发生，质量及能量误差小于 1e-12。

需要维护者提供或确认等效 CGS 输入：
- 保持的无量纲引力强度 G*rho*L^2/c_s^2、特征时间比及其独立计算依据；
- 对 rho/长度/温度/时间所作的变换，以及 EOS 有效域和数值分辨率的影响；
- 与原参考同义的 Gauss/势解、拓扑序列和冻结误差预算。

不能只删 gravity_G 后运行，不能通过关闭引力或调整 floor/阈值恢复通过。
该场景当前尚未迁移，也未作为通过证据。

## 外部对照目录中的 ARCH 输入

validation/gravity/flash/arch_jeans_o6plus.par 及 _128.par 使用 6.67408e-8，
当前共享值为 6.67430e-8。需要确认 ARCH 新输入及独立解析参考是否同步更新、
旧结果如何标识不同常数身份。外部 FLASH 程序运行不是本轮前置要求。
文件尚未修改，旧计时/结果不复用为新契约验收。

## 可直接迁移的错误测试

run_self_gravity.py 原 restart-reject-gravity_G 已拆出为配置层
RETIRED_PARAMETER 拒绝；gravity_rtol/atol/max_cycles 仍测试 checkpoint
controls 不相容。不同 saved G 的拒绝由 checkpoint_compatibility 单元测试覆盖。
这种测试语义迁移不改变任何物理场景预算。演化 campaign 尚未重新执行。
