# P5–P7 自引力基本验收封包

日期：2026-09-23；分支：`physics/selfgravity`；父提交：`253e1c51`。
本记录所在提交即候选源码。程序摘要和设备信息见 [identity.json](identity.json)。
实际支持与算法约束见 [实施记录](../../../../docs/development/P5P7GravityAcceptance.zh-CN.md)，
用户案例见 [GravityBox](../../../../simulation/GravityBox/README.md)。

## 构建与验证身份

CPU 完整回归使用 GCC 12 Debug、CUDA/KLU 关闭：60/60。
最终边界、非有限力失败处理与同步计时修改后，相关 8/8 再通过。
CUDA 使用 GCC 12、CUDA 12.3、Release、SM86、KLU/cuDSS 开启；数学不启用
fast-math/FMA 合并。最终全目标增量构建完成 155 个步骤；稳定 EOS/网络对象提前构建并复用。
编译日志含既有默认比较运算声明/未使用函数类 NVCC 告警，没有 ODR 或编译错误。

[CUDA Driver 回归](cuda-driver-ctest.log) 23/23，包括原 22 项范围和已存在的 KLU
161 方程测试。未声称运行该 CUDA 配置的全部 135 项 CTest。
[CPU 全回归](cpu-ctest.log)、[最终局部回归](cpu-final-focused.log)、
[Python 工具](tooling.log)（357/357，无跳过）与架构审计均通过。

最终 Release 程序同时运行 CPU 和 CUDA 路由作对照；最终程序摘要为
`2087ad2ba2d4af9901febfcead64756443b6f14957ad9cb397ad4d5410c07549`。
完整 CPU 52 条和最终 CUDA 42 条、所有性能测量均以此冻结程序执行。
GPU 原周期 32 条在增量阶段执行，随后修改仅补齐非有限力失败检查和同步计时；
最终资格矩阵另行验证最终程序，不把增量阶段的性能当最终测量。
GUI 前端未重跑，独立 `studio/` 工作未进入封包。

## 证据索引

| 内容 | 结果与记录 |
| --- | --- |
| CPU 完整物理矩阵 | [52 条摘要](cpu-physics/summary.json)：Jeans、三种积分阶、能量、动态 AMR/restart、孤立云、扩域、热扩散与真实核燃烧耦合 |
| GPU 原周期完整矩阵 | [32 条摘要](cuda-periodic-full/summary.json)：细化/粗化/无变化、1D–3D、时间阶、逐位重启 |
| 最终 CPU/CUDA 资格矩阵 | [42 条摘要](cuda-physics/summary.json)：独立物理检查、1D–3D 周期、3D 孤立/混合、三物理动态 AMR、跨后端重启和失败拒绝 |
| 共享算子 | [三维周期](composite-periodic-3d.log)、[Dirichlet 与直接质量和](boundary.log) |
| CPU 设备无关安全 | [数学 ASan/UBSan](math-sanitizers.log)、[场生命周期 ASan/UBSan](lifecycle-sanitizers.log)，含泄漏检测 |
| CUDA 内存与泄漏 | [周期](memcheck-periodic.log)、[孤立混合](memcheck-isolated-mixed.log)，各有实际生产进程信息，零错误/零泄漏 |
| CUDA racecheck | [周期](racecheck-periodic.log)、[孤立混合](racecheck-isolated-mixed.log)，零竞争错误/警告；不将该工具解释为所有全局内存竞争的证明 |
| 四档重复性能 | [原始记录与中位数/范围](performance/performance.json)，每档 CPU 1/4/8 线程预选、CPU/GPU 各三次正式测量 |
| 64³ 十个时间步 | [原始记录与范围](performance-steady/performance.json)，重新预选 CPU 线程、各三次正式测量 |

每个 campaign 目录的 `inputs-and-logs.tar.gz` 保留全部输入、修复账本、求解/计时/
重网格诊断和日志。小文件按功能封包，避免在 Git 中散落数百文件；没有删除原始记录。
包内大日志再以 gzip 压缩，路径相对 campaign 根目录；不含大型 HDF5 流场。
例如 `tar -xzf cpu-physics/inputs-and-logs.tar.gz -C <空目录>`，再按需要解压 `.gz`。
摘要中的 `/tmp/arch-p57-*` 是本机运行来源；其他机器按参数重跑并替换输出/EOS 表路径。

## 性能结论与限制

硬件：RTX 3060 Ti 8 GiB、i7-10700、16 逻辑 CPU、约 7.7 GiB RAM。
同一 Release 程序、同输入、同残差目标、同物理终点和求解次数；不以 CPU Debug 对比 CUDA Release。
速度均为 CPU 时间 / CUDA 时间，大于 1 才是加速。

| 负载 | CPU 线程 | 总求解 | Poisson | 端到端 |
| --- | ---: | ---: | ---: | ---: |
| 周期 64 单元，2 步 | 8 | 0.0043× | 0.0042× | 0.038× |
| 周期 32³，2 步 | 4 | 1.31× | 1.30× | 0.356× |
| 周期 64³，2 步 | 8 | 2.57× | 2.55× | 0.542× |
| 孤立 AMR 233472 单元，2 步 | 8 | 5.54× | 4.51× | 0.982× |
| 周期 64³，10 步 | 8 | 2.65× | 2.63× | **1.37×** |

十步端到端中位数为 CPU 59.71 秒（59.46–59.71），CUDA 43.48 秒（43.34–43.55）。
短算例保留负加速：大网格 CUDA 输出约 15.5 秒，CPU 约 0.38 秒；不能隐藏在“求解器加速”之下。
输出分项包含流体 Host materialization、验证及 HDF5，不含调用前的引力字段下载；总时间包含全部。
Poisson/边界/力的设备计时均以同步完成为边界。prepare 传输计数不包含拓扑初始化和显式输出。
峰值 RSS 约 1.09 GiB；四档整卡显存采样 1425–2437 MiB，含桌面基线，不是单进程精确峰值。

这套结果通过用户要求的 3060 Ti 基础加速验收，不承诺所有规模正加速，
也不把十步周期结果外推为所有孤立云、EOS/网络组合或长期天体模拟的精度认证。

## 保留的失败与解释

[跨燃烧阈值轨迹](strong-heating-threshold.json) 与
[强升温粗步长轨迹](strong-heating-smooth-coarse.json) 保留了未达到二阶的诊断。
前者存在源项开启不光滑点，后者在 1e-4 秒内升温超过一倍，4/8/16 步尚未进入渐近区。
不降低 1.8 门槛；真实网络的平滑短区间另给出 2.017/2.070 阶，并通过 64→128 步参考检查。
强升温的独立核能收支仍通过，不能将其二阶失败从记录中删掉。

一次并行工具调用报告 `Device not supported`，见 [原始报告](racecheck-initial-tool-failure.log)。
未把它算作通过；以原有单进程串行检查命令重跑未修改的 AMR 基线和正式程序后均通过。
该瞬时工具失败的根因未确定，没有因此修改程序、压制报告或降低数值预算。

## 复现入口

从仓库根目录，用已构建的程序及安装 numpy/h5py 的 Python：

```sh
python3 validation/gravity/run_self_gravity.py --arch <ARCH> --output <新目录>
python3 validation/gravity/check_cuda_compatibility.py \
  --cpu-arch <CPU-ARCH> --cuda-arch <CUDA-ARCH> --output <新目录>
python3 validation/gravity/check_cuda_compatibility.py \
  --cpu-arch <CUDA-Release-ARCH> --cuda-arch <CUDA-Release-ARCH> \
  --benchmark-only --output <新目录>
python3 validation/gravity/check_cuda_compatibility.py \
  --cpu-arch <CUDA-Release-ARCH> --cuda-arch <CUDA-Release-ARCH> \
  --benchmark-only --benchmark-case large-periodic --benchmark-steps 10 --output <新目录>
```

CI 仍用原 `--quick` 入口，对应现有测试覆盖更新为 `self_gravity_physics`；
重型矩阵与计时不是新 CI job。远端 hosted CI 的状态另行确认，本记录只报告已执行的本地验证。
