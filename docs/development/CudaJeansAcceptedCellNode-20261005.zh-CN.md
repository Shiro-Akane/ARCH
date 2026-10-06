# JENS device accepted-cell 计算节点（2026-10-05）

## 身份与范围

开始HEAD a10e1cc1811cf886a9c34a234258f78575149a5d，唯一ARCH-compute-optim工作区。
遵循科学清单第6节冻结JENS及第4.3节CPU子组→统一CUDA顺序。
原CPU ELF SHA256 7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510未变化，
继续沿用匹配的9短演化+9实际restart收据；本节点不是新的CPU短包或完整CUDA lifecycle。

## 实现与职责

新增launch_cuda_jeans_resolution位于原cuda/amr/RefinementIndicators所有者：
活动accepted cells→选定EOS的pressure/sound speed→共享JeansDiagnostics::evaluate_cell→整块minimum。
使用总正密度和GridMetrics最大活动physical spacing、共享CGS G，不扣均值、不复制EOS/Jeans公式、
不加floor或新阈值；任意非法单元或EOS锁存失败使整块summary NaN，不能跳过单元取剩余minimum。

borrowed DeviceJeansWorkspace含active-cell scratch、scalar result、composition scratch和EOS latch。
调用方拥有Current/version/lease preflight、scratch extent、stream和fencing；本节点未把这些职责冒充已接完。
Host preflight在写latch或enqueue前检查布局/workspace及已声明EOS species extent。
当前新launch明确限定Cartesian，不能把旧cylindrical/polar metadata当成已签收RZ。
四种真实EOS view overload均编译；本节点数值签收只覆盖下述IdealGas状态。

## 真实验证

RTX4070Ti、sm_89、CUDA12.8.93、Release原数值合同。
复用301个独立Decimal80/120位原参考：197 valid、104 unrepresentable；另12 invalid。
原16epsilon静态门槛未改，最大relative error1.5432483614913696e-16。
正常/次正规参考均按既有测试标准；不是与CPU相同即科学通过。

24个真实device单/双组分、静止/运动态EOS样本，独立caloric gamma=1.5或27/14，
最大relative error1.9657272738823614e-16；12个species extent mismatch在enqueue前拒绝。
冻结制造包每个1D/2D/3D、L0/L1根patch的spacing和均匀状态共6组合通过；
检查非首active cell改变minimum、invalid ghost不参与、非法active密度/内能传播、
错误后恢复、missing workspace不部分写latch。

全部并入原cuda_refinement_indicators CTest：原curvature/Tabular温度/批次测试仍运行；
实机exit0、无skip。新test-only kernel static archive只承载测试入口，
生产indicator OBJECT仍由原canonical owner消费，未编译第二份生产源码、未新增CTest job或审计例外。
最初独立target/扩展裸OBJECT消费被架构审计拒绝，完整诊断留本机；
最终原架构审计和git diff --check通过。
原编译warning（NVCC long-double device提示、默认比较符声明）保留，
独立长精度误差计算在Host；未消音后冒充更高精度device计算。

## 出口与下一节点

仅device accepted-cell数学与EOS/grid subgroup PASS。
公共JENS CPU-only/auto/CUDA门槛没有移除；DriverRegrid仍拒绝未qualified device JENS。
尚缺CudaBackend摘要消费者与accepted Current身份、真实候选父态device EOS/veto、
initial/accepted-macro/regrid/restart及输出三通道CUDA冻结短包。
Helm/Tabular科学状态、完整ARCH CUDA binary、RZ-AXIS/RZ-VISC和连续Phi/force仍未签收。
无长跑和正式benchmark。原始日志/ELF在studio/.local/integration/cuda-jeans-resolution-20261005，
仅处理后的scalar summary及源码提交review。
