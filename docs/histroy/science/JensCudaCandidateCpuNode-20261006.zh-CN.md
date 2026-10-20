# 隔离候选 CPU 冻结短包节点
状态：已通过（仅本 CPU 均匀短包）；CUDA 待验证，public gate 不变。
准确候选 base ccfcef5bc60f4361a808dc1e83c020094ab50930，
交付 ref 11c665b37deb0f73106e2d2ed90d22c27082b836，
patch SHA256 c026244a12f55900062aa32f213466d6d07dddf388eb18695424b0f2726bb123。

## 真正运行的范围
私有 Release CPU ELF bd0c4434ee152e36e3892dfe215131aed83364cbf72b8318abed4289308b3325。
GCC13.3/C++20、原 HDF5/HighFive/KLU，无新依赖，不复用 public build cache。
原 BoxCampaign 和独立 reader：disabled/output-only/active ×1D/2D/3D，
9次连续演化、实际 checkpoint t=0.01 的9次续算，全部实际终点0.02。
初始/接受宏步/重网格/输出和 strict restart 均按原 frozen contract 检查；
active 每宏步事务有日志覆盖，off/output 原生状态与 solve 次数精确相同。
质量/能量漂移0、原生均匀字段精确、16epsilon诊断/physical residual/repair=0通过。
输入18份在真实应用启动前逐个记录 exact hash，见处理后 cpu-summary.json。

配置 API/checkpoint 检查首轮通过；configuration_input 因私有 snapshot 漏原
validation/burn/inputs/bd-config-v3.par 失败；补精确 base 原夹具后仅复跑该项通过。
未改测试判据/物理源码。prep工具改为归档完整 tracked validation，以防再次漏夹具。
此失败和原日志保留，未把首轮写成全部PASS。

构建28 jobs，memoryguard 最低约14.4GiB available、owned RSS约8.48GiB、swap0；
campaign memoryguard约43.1s、owned RSS约774MiB、swap0，原始输出约128MiB。
CPU实际 enforced：内存/交换增长/总14400s与原单运行1200s；
磁盘仅测量，不冒称CPU hard disk cap。计时不是性能benchmark。
raw H5/checkpoint/input/full日志仅在 studio/.local/integration/jens-cuda-candidate-20261006。

public Core/API/Driver、两个 public cache/ELF SHA再次核对不变。
私有 canonical-root architecture PASS；根扫描的 ignored nested-source duplicate 失败单列。
不修改检查器。CUDA正在独立sm89/heavy1构建，尚无CUDA科学结果。
完整候选通过后仍交 Core review 决定公开范围；四项RZ finding与长包确认各自独立。
