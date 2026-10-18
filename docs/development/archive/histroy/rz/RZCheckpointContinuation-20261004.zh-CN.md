# 内部 RZ 实际调度 checkpoint 续接

## 范围与准确身份

代码基线456ab252，实际夹具ELF
640a39123598713c50d73872f0ee90439ab5c4a6077181a854ebc18552bbbfab。
新增tests/host/driver/test_rz_checkpoint_continuation.cpp及复用可信CPU编译/链接命令的runner。
实际Runtime/Boundary/Regrid/DriverIO/ChkIO/HDF/PlotIO共9个TU重编；
没有修改生产代码或重新Build主ARCH。准确SHA见同名Summary。

沿用既有scheduled Hydro工程场：rho2、mom_r0、mom_z6、mom_phi0、
ENER21.5、两组分.6/.4，IdealGas gamma1.4；既有工程步长.001。
这不是注册case或维护者冻结的物理终点；.002只表示两次unit更新。
关闭gravity、burn、diffusion，不借此宣称模块耦合或科学演化验收。

## 真调用链

四例：径向/轴向coarse-fine接口×r=0轴线/非零内边界。
每例实际5叶块（四个L1、一个L0）通过explicit RZ native LoadLeafGrid构造。

DriverRuntime.initialize_topology/物理BC/ghost →
ScopedStageBinding →
DriverStages.advance_hydro → 实际SolverRK2/HydroSolverImpl/HLLC-PCM/shared stage/reflux →
controller.advance →
DriverIO.write_checkpoint →
read_chk显式axisymmetric-rz恢复native叶和accepted state →
全新DriverRuntime初始化ledger/ghost →
连续与恢复路径分别再执行真实RK2。

没有仅调用序列化器或手写替代积分器；也没有临时开放公共RZ。
阶段identity重新生成，不要求不同Runtime的handle/clock数值等同；
检查实际Current interior/ghost均可读，科学状态按原bit匹配。

## 结果

4/4 PASS；分割点.001/step1，工程终点.002/step2。
每例10260个uint64 word（logical身份及1280 cells×8个数值槽）匹配，
共41040 word，保留signed zero；包含5个守恒场、ENUC及两组分X。
分割读回与继续后的两路径均原bits一致。
controller time/step/dt_old及repair ledger一致，repair events=0；
所有来源checkpoint SHA前后不变，原H5留本机ignored目录。

首次新fixture编译引用不存在的ledger reference类型，已改为既有聚合参数；
错误日志保留，不把fixture错误归因于科学Core。

## 后续明确缺口

源码审计发现DriverStages.h的Hydro repair代表位置调用
grid.GetPhysicalCoords时未传Runtime chart。RZ若出现真实repair，
事件位置会走旧polar坐标。本轮零repair没有触及此分支；
需后续非零repair实际测试及显式profile接线，不能声称此路径已验证。

本轮不涉及regrid transfer/角动量、有限环体gravity、CUDA、公开RZ CLI或native GUI；
也不替代科学/长期稳定性预算。完整联合目标继续未完成。
仅提交工具与处理后摘要，raw checkpoint/log/ELF本机保存，无push/tag。
