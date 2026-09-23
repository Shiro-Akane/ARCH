# SNIa2DCoupled：二维四模块联动冒烟与本机计时

本页保留当时提交使用的 `SNIa2DCoupled` 注册名与原始数值。当前工作树已将示例扩为二维/三维的 [`SNIaCoupled`](../../../../simulation/SNIaCoupled/README.md)；历史命令仅适用于原提交，不作为现行运行入口。

日期：2026-09-23；分支：`physics/selfgravity`。正式输入与算例说明见
[SNIa2DCoupled](../../../../simulation/SNIaCoupled/README.md)。这里验证正常 Driver
中的 Hydro、自引力、aprox13 核燃烧与**热**扩散同时开启，且 CPU/CUDA 完成同一终点；
这是执行和后端对照记录，不是 SN Ia 解析解、爆轰收敛或三维白矮星认证。

## 构建与运行身份

同一份 CUDA Release 可执行文件同时运行 CPU 和 CUDA；SHA-256 为
`5de96bc4119e3f08405791cc154262b8a7cab86104e5fedba34f71d6904a6943`。
GCC 12、CUDA 12.3、SM86，KLU/cuDSS 均在构建中启用，未启用 fast-math/FMA 合并。
硬件为 i7-10700（8 核/16 线程）、RTX 3060 Ti 8 GiB、约 7.7 GiB RAM。
[最终增量 CUDA 构建日志](cuda-release-build.log.gz)通过，重新配置测试用 Python 后的
[构建复查](no-work-build.log)显示无待编对象。

输入为 CGS 的 1 cm × 1 cm 二维周期方盒，16×16 活跃单元，50/50 C/O 燃料，
`rho0=1e7 g/cm^3`、`T0=1e9 K`、`Tpeak=3e9 K`、1% 光滑密度热点。
使用 Helmholtz EOS、HLLC/MUSCL、RK2、aprox13/BD/DenseLU、RKL2 热扩散、
周期 Poisson 自引力，无 AMR；每次接受 120 个宏步，到达 `t=1.11181e-9 s`。
GPU 后端解析结果和实际操作见[原始包](inputs-and-logs.tar.gz)中的
`SNIa2DCoupled_backend_plan.txt` 与 `SNIa2DCoupled_backend_trace.tsv`。
没有把物种扩散算作此算例验收内容。

## 联动检查

四次正式运行各有 120 个接受步、362 次 Poisson 求解（其中 240 次是 RK 阶段求解），
每次残差均低于目标；最差 `residual/target` 为 0.498。CPU 求解记录 `device=0`，
CUDA 求解记录 `device=1`。最终输出的九个检查字段全为有限值，质量相对漂移
`2.22e-16`，低值修复事件为零。`c12` 最大变化 `2.58e-5`，`ENUC` 最大值
`5.18e20 erg/(g s)`，`GPOT/GACX/GACY` 均非零，说明反应和自引力确实进入了计算。
CUDA 扩散调度表记录每步两次、总共 240 次 RKL2 半步，每次两阶段；CPU 日志与
配置确认热扩散路由初始化，最终 `dt_diff` 为 `4.46956e-8 s`。
该有限步长和设备调度证明扩散路径被调用，不给出独立扩散误差界。

两后端在终点的最大逐场相对差异：`ENUC` 为 `1.14e-12`，其他所查场均低于
`1e-14`。四次完整检查、场变化、设备标记和残差汇总见 [summary.json](summary.json)。
[原始包](inputs-and-logs.tar.gz)保存每次输入、运行日志、重力/扩散/设备诊断、
修复账本，以及四次正式运行的初末 HDF5 plot，便于独立复核；未收录 checkpoint。

## 本机端到端计时

10 步预选比较 CPU 1/4/8/16 线程，按 Driver 时间选择最快的 4 线程；
CUDA 先运行 5 步预热。下面是随后两组成对 120 步计时。端到端值使用包裹整个
子进程的单调时钟；应用内部 Driver 时间来自 `run_timings.tsv`。
`/usr/bin/time` 的原始墙钟数也保留在 [raw_timings.json](raw_timings.json)，
但在本机与单调时钟相差数秒，因此不用于加速比。

| 次数 | CPU 端到端 | CUDA 端到端 | 端到端加速 | CPU Driver | CUDA Driver |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 91.45 s | 69.88 s | 1.309× | 90.23 s | 68.49 s |
| 2 | 92.47 s | 70.53 s | 1.311× | 91.11 s | 69.10 s |

端到端加速中位数为 **1.310×**。这个 256 单元的小案例仍表现出本机正加速，
但不代表其他网格、AMR、边界或 EOS/网络组合的通用加速比；更广负载的结果见
[P5–P7 验收记录](../p5-p7-20260923/README.md)。

## 回归与复现边界

现有 Poisson、引力阶段/生命周期/快速物理及配置/预览 API 共 6 项局部
CTest 在最终构建上 [6/6 通过](ctest-final.log)，没有修改测试内容或数值预算。
第一次 [CTest 记录](ctest-initial.log)为 5/6：`self_gravity_physics` 的系统 Python
缺少 `h5py`，尚未执行物理检查；将本地 CMake 的 `Python3_EXECUTABLE` 指向
已有工具环境后原样重跑通过。GUI 前端专项测试本轮不重复执行。

从仓库根目录可将输入复制到独立目录，分别附加 `compute_backend=cpu` 或
`compute_backend=cuda`，指定不同 `out_dir`，然后运行：

```sh
./build/selfgravity-p1-gpu/bin/ARCH SNIa2DCoupled <输入副本.par>
```

`summary.json` 中的 `/tmp/arch-snia2d-20260923` 是本机运行来源；归档内使用
相对 trial 目录。其他机器需要重新配置 CMake，并准备 Helmholtz 表路径。
