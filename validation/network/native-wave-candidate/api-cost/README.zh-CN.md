# 原生求解／ODE 等待成本诊断

2026-09-17已启动真实服务器任务，尚未收齐；准备包37,376 bytes，SHA
`a375d12930495b43ace157a6715413e1545c09f1c595db295f65dee77af9ebb5`。

在 focused 原轨迹独立复核、布局ABBA诊断及双端保全之后单独运行。
不与GPU实验或编译并行，不修改生产代码，不作正式速度样本。

复用两份已验证的可执行文件，150/200 × BE_NR/BD/ROS4；2→3 单元、pool2，
原四步至1e-10及全部科学预算不变。只编译两个Host LD_PRELOAD观察器并链式装载。
没有新增CUDA fence/event、矩阵操作、资源池或不同的启动形状。

- CUDA观察器：此前已审阅的等待标签配方生成，SHA
  `c046281beb191b5a3f00ce62c41b17880c9a71ae15e3cad5ce9c3349708ab7d2`。
  记录 launch、分配／释放、同步、最近观察到的ARCH kernel之后的MemcpyAsync。
- cuDSS观察器：原已归档的进度观察器，SHA
  `a4196bf3eab9fe6363f871159bb25fb6da6f38fe1d107e0ed3b7611e9fe71b39`。
  记录analysis/factorization/solve/stream sync及native memory estimates。
  当前候选没有使用REFACTORIZATION；此旧观察器不统计那个phase，未来若测试该phase必须明确扩展。

两库的计时可能嵌套，**不能相加推算GPU kernel耗时**；D2H等待标签不是唯一因果，
观察器看不到cuDSS所有内部kernel。所有快照是累积值，不是重复性能样本。
缺失最终观察器记录、错误网络／ODE或布局、未完成数值轨迹必须拒绝。
解析器显式diagnostic模式只给`diagnostic_numerical_pass`，
`trajectory_matrix_pass`及正式性能／应用资格仍为false，不混入未插桩数值门槛。
