# HDF5 checkpoint 与 restart 连续性

英文权威文本见 [README.md](README.md)。

> CPU 状态：checkpoint v2 已通过均匀网格流体以及带 species/burn 的分段连续性测试；CUDA 待测。

本记录验证从中途 checkpoint 恢复后的轨迹与不中断运行一致。v2 除 AMR
叶块守恒状态外，还保存 `dt_old`、下一宏步使用的 burn 限制以及循环阶段，避免
restart 后重复执行本步 regrid 或按步输出。读取器仍能读取 v1 的多维场数组；
由于 v1 不含控制器状态，旧文件不承诺逐步完全一致。

## 审计环境

被审计工作树以 `affde827fcbf317382ed45372912b562652a71c5` 为基线，并包含本页
记录的改动；测试使用 GCC 13.3.0、CPU backend、Release flags
`-O3 -march=native -ffast-math -DNDEBUG` 和两个 OpenMP 线程；硬件为 x86_64
WSL2 下的 Intel Core i7-10700。

## 用例与验收

`uninterrupted.par` 在 64-cell 周期 SmoothAdvection 的 step 25 写出
checkpoint，`resumed.par` 从该文件继续至 step 50。第二组在 aprox13、
Helmholtz、BE_NR、DenseLU 单区燃烧的 step 10 分段，因此同时覆盖全部 species
数组和 burn 时间步限制。

最终 uninterrupted 与 resumed 文件按完整 HDF5 对象比较。所有守恒场、species、
叶块层级和逻辑坐标、标量属性、输出编号以及后续时间步均必须无差异。两组的逻辑
差异均为零。HDF5 分配与元数据布局可能不同，因此容器字节不作为验收标准。

## 复现

~~~bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARCH_ENABLE_OPENMP=ON
cmake --build build --parallel 4
export OMP_NUM_THREADS=2

./bin/ARCH SmoothAdvection validation/restart/inputs/uninterrupted.par
./bin/ARCH SmoothAdvection validation/restart/inputs/resumed.par
h5diff output/validation/restart/uninterrupted/RestartSmoothAdvection_chk_0002.h5 \
       output/validation/restart/resumed/RestartSmoothAdvection_chk_0002.h5

./bin/ARCH BurnOneZone validation/restart/inputs/burn_uninterrupted.par
./bin/ARCH BurnOneZone validation/restart/inputs/burn_resumed.par
h5diff output/validation/restart/burn_uninterrupted/RestartBurnOneZone_chk_0002.h5 \
       output/validation/restart/burn_resumed/RestartBurnOneZone_chk_0002.h5
~~~

一次独立审计 probe（不作为仓库 test target 提交）覆盖两块、两 species 的 v2
写读、旧 v1 多维数据读取、输出编号以及时间步控制状态恢复。真实的旧 v1
step-zero checkpoint 也成功推进一个流体步：它重新计算 CFL、不重复写初始文件，
并使用有限的控制器 fallback。对已经到达终点的 v2 checkpoint 执行 restart 是
no-op，也不会重复写最终文件。配置现在会拒绝 `restart = true` 且
`restart_file` 为空的情况，不再静默启动新算例。
一份使用 `plt_dt = chk_dt = 1e-12` 的 scheduler probe 还确认 `t = 0`
不会重复产生按时间输出；在较长的累积检查中，连续调度与 restart 重建都选择了
完全相同的下一时刻 `0.60000000000000009`。

## 范围标签

- **已验证：** CPU HDF5 v1 多维读取兼容、v1 step-zero 续跑、v2 round trip 与
  no-op restart、均匀网格流体连续性、species/burn 连续性、元数据与输出编号，
  以及空路径拒绝。
- **待验证：** 动态 AMR split-run 的 topology 连续性。schema 能恢复叶拓扑，
  但 `ENUC` 是未写入 checkpoint 的瞬态细化诊断，因此不声明
  `refine_var = ENUC` 的精确 restart。
- **待验证：** CUDA HDF5 对等以及外部库调用过程中断。
