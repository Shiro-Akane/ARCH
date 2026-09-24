# P13 曲线坐标 CUDA 自引力验收（2026-09-24）

**结论：**在受测的完整 `2π` 方位角、`gravity_boundary=isolated`、奇点流体面 reflecting 范围内，CPU 与 CUDA 共用自引力 Poisson/MG、源项和坐标接合算术。一维球/柱、二维极坐标及三维柱/球坐标的真实设备运行通过解析场或同输入耦合对照，包含原点、轴线、极点、混合 AMR、近真空与跨后端续算。部分方位角、域外质量、圆心小单元的时间步代价以及真实 SN Ia 的长期物理精度不由此开放或证明。

## 实现边界与验证方法

坐标接合的插值、组分单纯形检查、原生动量符号和 ghost-only 有效 donor 回退位于共享的 [CoordinateSeamMath.h](../../../../src/amr/exchange/CoordinateSeamMath.h)。Host 与 CUDA 都消费同一拓扑 donor 计划；设备适配器在 [CudaBackendCoordinateSeam.cpp](../../../../src/cuda/runtime/amr/CudaBackendCoordinateSeam.cpp) 绑定活动或暂存块身份，只上传 stencil/块视图、回读一个错误状态字，流体大数组保持在显存。Driver 对常规阶段和 regrid 暂存态均按物理边界、同层、粗细、坐标接合的次序完成后才发布 ghost。泊松迭代没有全域 Host 往返，也未新增 CUDA 专用重力数学、用户控制参数或新的源项函数。

本机为 i7-10700（8 核／16 线程）与 RTX 3060 Ti（8 GiB，SM 86）；CUDA Toolkit 12.3、GCC 12、Release 双精度构建。最终程序 SHA-256 为 `6971d28b93561ede81c01162821cd5224cbd1feeb83341373724419db4d5db61`。下述速度比较由**同一个最终 Release 程序**在 CPU16 与 CUDA1 上执行，输入仅改后端、输出目录和接受步数。驱动时间取 `run_timings.tsv`，端到端时间用单调时钟；引力阶段、迭代、传输取程序自身的 `gravity_solves.tsv`，重网格单列且不与 Driver 时间相加。每个正式速度点交替运行 CPU/CUDA 三对，取各侧中位数后相除。CPU8 的四个相同规模检查均慢于 CPU16，包括三维大网格约 112 秒对 76.7 秒，因此使用本机较快的 CPU16 基准。

[run_cuda_matrix.py](../../curved/run_cuda_matrix.py) 覆盖现有四模块输入与规则网格／混合 AMR；[compare_backends.py](../../curved/compare_backends.py) 按 `(level, Morton)` 匹配叶块，在同一物理时间核对初末态全场。两侧都须有真实 CUDA 分发证据、零修复、燃烧能率、正的扩散限步、实际 AMR 变化和每次泊松残差达标。独立解析性仍由 P8–P12 CPU 制造解及一维 Gauss 参考承担；后端相等本身不是多维解析精度证明。

## 数值、耦合和生命周期

| 场景 | 本机结果 |
| --- | --- |
| 一维球／柱均匀密度，各 32 径向单元 | CUDA 势相对误差分别 `6.30e-15`、`1.01e-14`；力／独立 Gauss 误差 `2.66e-14`、`1.05e-14`。 |
| 一维球／柱，12 步动态 AMR 与续算 | 初始 4、末态 8 叶块；最大动态 Gauss 相对误差 `1.21e-8`、`3.86e-13`；封闭域质量漂移 `<7e-16`、含势能总能漂移 `1.21e-5`、`2.57e-5`；同后端检查点续算逐位一致。 |
| 二维原点与原生二维球图，四模块混合 AMR | CPU/CUDA 相同的粗细叶块、物理时间与全部输出场；二维球图最终加速度矢量归一 Linf `7.34e-12`，无修复。 |
| 三维柱轴与球原点／两极，四模块混合 AMR | 两种几何都实际重网格，保留方位角粗细接合；球域最终加速度矢量归一 Linf `1.75e-7`，最大残差／目标 `0.447`，无修复。 |
| 二维 Cartesian 锚点与 4×4 极坐标规则／混合网格 | 全部 CPU/CUDA 同输入对照通过；对应加速度矢量归一 Linf 为 `3.38e-15`、`3.73e-12`、`2.60e-11`。 |
| 二维原点近真空，`rho0=1e-12 g/cm³` | CPU/CUDA 最小密度都为 `1e-12 g/cm³`，16 个叶块、各 10 次引力求解均达标且零修复；最终力矢量误差 `1.81e-12`。 |
| 二维原点双向跨后端检查点 | 连续 3 步对照“2 步检查点 + 异后端续至第 3 步”；CPU→CUDA、CUDA→CPU 均为 10 叶块，最终力矢量误差 `6.71e-12`、`1.73e-11`。 |
| 二维原点 120 步 CUDA 长运行 | 实际 AMR 拓扑变化、120 步接受、零修复，所有泊松求解达标；与 P12 留存的同物理输入 CPU 末态对照，最终力矢量误差 `1.41e-11`。历史 CPU 与本轮 CUDA 的二进制版本不同，不把两者耗时当正式加速比。 |

普通流体与势、压力、温度等预定归一 Linf 预算是 `2e-7`，核能率 `1e-5`，组分质量分数绝对 Linf `1e-9`。加速度也维持 `2e-7`，但作为原生正交基下**一个物理矢量**，以完整矢量峰值归一，并分别保留 `GACX/Y/Z` 的绝对误差。初版按单个切向分量峰值归一时，三维球极点 `GACY` 初／末态分别出现 `3.30e-7`／`2.59e-6` 的比值；其末态最大绝对差只有 `5.09e-7 cm/s²`，完整力矢量峰值为 `4.16 cm/s²`，实际矢量归一误差 `1.75e-7`。另以更严格 `gravity_rtol=1e-10` 复测，矢量误差进一步降至 `9.47e-8`；原单分量比值仍会因极点附近该分量很小而放大。故更改的是坐标分量的归一参照，**没有提高 `2e-7` 的矢量预算或降低泊松残差门槛**。

紧凑数值结果保存在 [qualification-summary.json](qualification-summary.json)；现有 `arch_cuda_amr_exchange` 测试入口追加 2D 原点、3D 柱轴和球原点／两极的逐场 Host/GPU 比较，分别触及 128、2048、6144 个接合 ghost。针对新增 kernel 的 `compute-sanitizer` memcheck 报告 0 errors，racecheck 报告 0 hazards／0 warnings；racecheck 的工具范围不能替代所有全局并发证明。CPU AMR 计划、后端能力、复合泊松解析与自引力物理回归通过；没有新增 CI 作业或降低既有测试预算。P2 的 `UniformGravity` 仍是有独立消费者的数学参考，不参与生产 composite 分发，因此未作为“旧生产路由”删除。

## 本机正式速度与限制

以下为同一 Release 二进制每组 CPU16/CUDA1 **三对运行、分别取中位数后求比**。Driver 时间排除输出，端到端包含输出；Poisson 单列以辨别加速来自哪里。加速列末尾的“保守”值以该组三轮中最快 CPU Driver 对 CUDA 中位数求比，因此仍受限于这台机器的负载与规模。完整样本、重力阶段、迭代、重网格及总线计数见 [performance-summary.json](performance-summary.json)。

| 四模块输入 | 末态叶块 | CPU Driver 中位数／范围 (s) | CUDA Driver 中位数 (s) | Driver 加速／保守 | 端到端加速 | CPU／CUDA Poisson 中位数 (s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 2D 极坐标 2×2 混合 AMR | 10 | 5.781／5.697–6.560 | 3.979 | 1.453×／1.432× | 1.287× | 0.343／1.549 |
| 2D 极坐标 4×4 规则网格 | 16 | 8.057／7.732–8.212 | 5.375 | 1.499×／1.439× | 1.352× | 0.623／2.140 |
| 2D 极坐标 4×4 混合 AMR | 28 | 17.571／14.471–19.231 | 7.637 | 2.301×／1.895× | 2.011× | 1.352／2.479 |
| 3D 柱坐标轴线混合 AMR | 11 | 76.660／75.884–91.287 | 47.581 | 1.611×／1.595× | 1.582× | 7.340／5.980 |

4×4 AMR 的 CPU Driver 波动明显，不能把 2.301× 当作稳定的单点承诺；以最快 CPU 样本计仍有 1.895×。小网格 GPU Poisson 比 CPU 慢，整个四模块运行的收益来自其他设备阶段与驻留；3D 柱坐标规模上 Poisson 中位数才略快，而边界／源项阶段仍有优化空间。各 CUDA 输入均实际分发，11 次引力求解的残差达标、零状态修复，混合 AMR 组均发生一次拓扑改变；3D 柱坐标的重力累计 113850 次 kernel、968 B H2D、123552 B D2H 及 15488 次同步，说明当前并非低启动开销实现。速度仅适用于上述硬件、输入和短时物理范围；受测 `SNIaCoupled` 是执行一致性示例，不是白矮星或 FLASH 4.8 SN Ia 的解析精度声明。

复现时从仓库根目录构建启用 CUDA 与测试的 Release 程序，并安装 Python `numpy`/`h5py`。例如：

```sh
python3 validation/gravity/curved/run_cuda_matrix.py \
  --arch build-ci/cuda-focused/bin/ARCH \
  --pair polar:validation/gravity/curved/inputs/p12_polar_origin.par:3 \
  --pair regular:validation/gravity/curved/inputs/p13_polar_origin_4x4_regular.par:3:regular \
  --pair polar-amr:validation/gravity/curved/inputs/p13_polar_origin_4x4_amr.par:3 \
  --pair cylinder-3d:validation/gravity/curved/inputs/p12_cylindrical_3d_axis_mixed.par:3 \
  --output /tmp/arch-p13-recheck --cpu-threads 16 --repeats 3
python3 validation/gravity/curved/check_cross_backend_restart.py \
  --arch build-ci/cuda-focused/bin/ARCH \
  --source validation/gravity/curved/inputs/p12_polar_origin.par \
  --output /tmp/arch-p13-restart-recheck
```

完整过程日志与大型 plot/checkpoint 留在本机临时目录，不加入仓库；机器可读的紧凑数值摘要与源码位置在本目录留存。FLASH 耗时不属于本次 P13 文档。
