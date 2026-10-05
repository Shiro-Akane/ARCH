# 当前 CPU 源码身份、冻结 JENS 与原科学子组

## 为什么复验

原生产CPU ARCH仍为7d3bd3d；其历史receipt有效，但不能代表后来workspace/Runtime源码。
本次标准增量构建现有build-cpu的ARCH，source HEAD
e91e74b60cdf153a0c6b5b7f258a764214cc30f9，唯一工作区与CMAKE_HOME_DIRECTORY一致。
Release/CUDA OFF、parallel28，没有configure或新worktree。
实际新ELF为5d454c03404b8b6a9fae082d82270886904637e04df7d9b59a29682bc56c2be0。
因binary实际改变，复验受影响包；不是重复未变baseline，也不改写历史receipt。

590 tracked输入聚合SHA
55546417e863618664f290e8bdf91be25e2905d413472bcda139ca60b319a86b
与当前CUDA full receipt完全相同。两种binary/构建条件分别记录，
不把doc HEAD或CPU ELF冒充CUDA identity，不更新/覆盖Studio managed Manifest；
完整外部依赖freshness仍需原Host manifest边界说明。

## 冻结 CPU JENS

复用BoxCampaign.jeans_uniform_lifecycle和原check_jeans_uniform.consolidate。
uniform-lifecycle-1：1D/2D/3D关闭/仅输出/实际约束共9演化+9真实restart全部PASS。
真实连续终点.02、checkpoint分割.01、续算终点.02，严格native payload/属性相等。
零源Phi/g/静止速度精确0，密度/温度/压力/能量/组分精确制造常态；
质量和总能量漂移0（原<=1e-12），max JENS相对误差4.50750606346323e-17（原16 epsilon）。
active 128/4096/131072 cells，29/55/83个接受状态事务覆盖完整，
无候选父提前coarsen。关闭/仅输出native state与solve counts逐位相等，
原repair/residual/lease/generation检查沿原入口通过。
不以本均匀短包替代非均匀参考、碎裂或长轨迹，不修改公开CUDA门槛。

## 同源 CPU 原完整子组

原Campaign.full、Box/Radial.run_checks(quick=False)共74记录PASS：
Wave32、Box14、Radial28；58真实科学执行、12预期拒绝、4汇总。
58份gravity trace均device=0，显式CPU input；原repair、residual、
空间/时间收敛、净自力、burn/diffusion、球/柱Gauss/AMR及严格restart通过。
与CurrentCudaOriginalFullNode各自独立参考检查并列，不把CPU/GPU相同当科学参考。
详细原指标和真实输入/最后Plot终点见summary。

仍保留PCM粗时间阶探针energy_budget=None的限制；该探针不宣称能量合格。
native多维两步、径向12步/低G等效40步没有到原tmax，不冒充长跑。
原阈值/低G迁移依据/共享物理未变，没有新Floor或容差。

## 观察与交付

CPU build29.612秒，peak owned RSS9863608KiB；
冻结JENS47.098秒、peak817956KiB；
原full38.096秒、peak3752964KiB，三项swap增长0。
这些是一次科学执行观察，不是正式benchmark或最优CPU配置；
不能直接将CPU38.096与CUDA177.382算成统一终点加速比。

[处理后summary](../../validation/gravity/results/current-cpu-identity-20261005/summary.json)
记录current source/binary/输入/原脚本/冻结contract与标量证据。
全部H5、plt、checkpoint、raw log/trace及ELF留
studio/.local/integration/cpu-current-frozen-jeans-20261005与
studio/.local/integration/cpu-current-original-full-20261005。

CPU JENS current-source receipt已更新；CUDA冻结9+9候选仍待明确本地验证授权。
RZ A→B→C→D/full-ring runtime force-work/连续参考/axis/viscosity科学gate保持。
正式benchmark须另完成有限线程筛选、冻结输入/终点/参考预算与交替重复。
尚未冻结的新长包不启动；无Windows适配、main merge、tag修改或整体完成声明。
