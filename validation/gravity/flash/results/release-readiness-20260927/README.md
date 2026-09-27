# O6／O6+ 发布收尾与三维 CPU 补测

本记录接续 [冻结实现的完整验收](../../O6ControlsAcceptanceReport.zh-CN.md)。
保留面向 main 的 PR 触发 CI 策略；本轮只推送 `compute/optim`，合并和发布由维护者控制。
没有引入数值近似、修改物理误差门槛、改变 AMR/CFL 或减少任何 Strang 燃烧半步。

## 检查点：以当前有效格式完成补测

现行格式为 v6、控制身份修订 2。原有 `checkpoint_compatibility` 增补了：

- 当前控制身份、原生组分和控制器状态的正常往返与主机状态恢复。
- v6／修订 1／15 项的旧身份、未知修订、错误长度、非有限值和空身份的明确拒绝。
- 负修复计数、非有限值、错误账本长度以及缺少必要数据集的拒绝。
- 读取拒绝后活跃 AMR 拓扑和字段保持不变；写入前验证失败不截断已有检查点文件。

这些拒绝场景是通过的负向测试，不是待补的功能或“不可用测试”。正常用例消费当前身份；
旧身份仅作为拒绝用例，不引入迁移默认值或第二套旧读取器。诊断区分身份不兼容与数据
损坏，保持读取器／写入器原有异常类型。既有生产耦合和跨后端重启入口也用当前程序重跑。

## 发布包与复现入口

实际下载了 GitHub 的 `51f982ab` 源码归档。它已包含 60,242,514 字节的 Helmholtz
实体表，摘要与该版本的 LFS 对象一致；未发现这份归档缺表。无 `.git` 的干净解压目录
覆盖本轮修复后，以 CPU preset 从头构建，依赖源码复用现有 HighFive/SuiteSparse，
ARCH 对象文件没有从开发构建目录复制。已有 Helmholtz、检查点测试和默认路径 Sod
入口均通过。Sod 是安装冒烟，不作为性能样本。

`run_comparison.py` 的 CPU 默认路径改为 `build-cpu/bin/ARCH`，与 preset 一致。
未指定亲和性时继承调用进程的可用 CPU 集合并记录；不默认套用本机的编号。
完整本地 CPU 参考生成和 CUDA 对照命令已补入
[复现说明](../o6-controls-20260927/README.md)。README 和构建指南同时区分 Git 检出
与 ZIP／tar：只有指针的其他归档应从同版本检出取得实际表，不能在无 `.git` 目录
直接执行 `git lfs pull`。本轮没有改动远端归档设置或重新分发外部求解器。

## 三维归因与执行修复

重建 `ca1fdcd3` 的原编译配置，并保留 `51f982ab` 原执行物，在相同输入、线程数、
绑核和实际终点下交替各运行三次。三维柱坐标四模块 AMR 的结果为：

| 源码／批次 | CPU8 中位数 s | CPU16 中位数 s |
| --- | ---: | ---: |
| 上一轮已存档的 ca1fdcd3 验收 | 40.625 | 30.878 |
| 本轮同机重建 ca1fdcd3 | 41.717 | 32.572 |
| 本轮与之交替的 51f982ab | 44.529 | 34.167 |

本轮对照复现约 6.7%／4.9% 的版本相关差距，不能把之前跨批次约 10%／20% 全部
归因于某一项代码变化。各样本的物理终点、场、AMR 拓扑、模块活跃性和零修复检查
均按原预算通过；没有通过关闭模块或换用较宽的容差减少工作量。

孤立引力边界静态平均分配面数，但每个面的树遍历／近场积分成本不同。诊断程序中，
八个线程各处理 480 个面，首个阶段有效工作时间却约为 0.57–1.36 s。动态领取
连续 16 面的小批任务后，约为 1.14–1.17 s，等待最慢分区的现象明显减轻。
正式实现仅对 `EvaluateBoundary` 改用动态分配，其他低成本任务继续静态调度；
每个面内的公式、树打开判据、近场积分和补偿求和顺序完全保留，CUDA 数学不另写。

该静态分配在旧版中也存在，因此它是已确认、可修复的执行瓶颈，不能单独解释最近
全部版本差异。同一诊断程序的水动阶段也有波动，不把每个阶段的差值都当作物理
工作量变化。取消补偿求和强制内联的单因素筛查没有改善成本，已撤销；保留现行
O3/LTO、严格浮点合同和共享数学实现。临时计时／环境分支已经移出生产代码。

## 最终验证与计时

正式样本使用 i7-10700（8 核／16 线程）、RTX 3060 Ti 和 WSL2。CPU8 绑定
`0,2,4,6,8,10,12,14`，CPU16 绑定 `0-15`；CUDA 主机线程绑定 `0`。
CPU 保留 GNU 13.3.0、O3/LTO、严格浮点及 OpenMP；CUDA 为现有 12.3 Release
配置。具体二进制摘要、源文件摘要和环境见 [verification.json](checks/verification.json)。
只有两个生产 `.cpp` 改动：检查点诊断和主机引力执行。共享数学头、配置和 CI 未改动。

计时范围是从启动到退出的完整进程，包括初始化和输出，比较同一物理终点；不是
用单步平均值或配置中的 `tmax` 代替总成本。最终计时开始前已经完成编译和下述
正确性检查；样本串行运行，没有与编译、其他测试、剖析或另一个模拟重叠。
先比较三维 CPU，再运行二维 CPU 与细网格回归，最后运行 CUDA。

### 三维 CPU 正式交替对照

相同线程数内，新旧执行物交替运行；第二轮交换先后顺序。所有三轮均保留，未删除
首轮样本。这里的基线是冻结的 `51f982ab`，不混用其他批次或 FLASH 的时间。

| 配置 | 三个完整进程样本 s | 中位数 s | 相对基线耗时降低 |
| --- | --- | ---: | ---: |
| 基线 CPU8 | 44.674 / 43.739 / 43.735 | 43.739 | — |
| 修复 CPU8 | 42.174 / 40.446 / 40.370 | 40.446 | 7.53% |
| 基线 CPU16 | 32.701 / 33.050 / 32.891 | 32.891 | — |
| 修复 CPU16 | 31.531 / 31.478 / 31.564 | 31.531 | 4.14% |

引力阶段中位数分别为 CPU8 的 10.279 → 8.954 s、CPU16 的 7.116 → 6.508 s，
下降约 12.9%／8.6%。水动、扩散和燃烧分项也一并存档；水动时间随批次与执行布局
变化，但公式和工作量未改，不能把其差值再宣称为新的算法收益。

收益以完整进程的上述配对结果为准。归因证据支持“边界面成本不均导致静态调度
等待”，还不能将此前所有版本差异独立分解到 O3、强制内联或链接布局。
取消强制内联的筛查未显示改善，没有采用。这个局部修复无需更改 CFL、EOS、
燃烧次数或自引力精度，并在两种线程配置下都有重复收益。

全部 12 个三维样本使用原来的场误差、物理时间、AMR 拓扑、模块活跃性和零修复
检查。原始样本和分项见 [final-pairs.json](timings/final-pairs.json)，归纳数值见
[summary.json](timings/summary.json)。

### 当前 CPU／CUDA 耦合成本

两套原有算例都运行 5 步，均开启四模块与 AMR。二维为笛卡尔，三维为柱坐标；
CPU 终点分别为约 `1.1555193003008242e-11 s` 和 `1.7615751690104147e-11 s`。
CUDA 终点和全部场采用原有 `compare_pair` 预算逐次核对，没有改变测试门槛。
“最快 CPU”仅指此次实测 CPU8、CPU16 中的较快者，不是全线程数搜索。

| 算例 | CPU8 中位数 s | CPU16 中位数 s | CUDA 三个样本 s | CUDA 中位数 s | 最快 CPU / CUDA |
| --- | ---: | ---: | --- | ---: | ---: |
| SNIaCoupled 2D | 4.791 | 5.302 | 3.071 / 2.970 / 2.920 | 2.970 | 1.61× |
| SNIaCoupled 3D | 40.446 | 31.531 | 42.942 / 44.345 / 43.141 | 43.141 | 0.73× |

二维保持正收益；三维在本机仍然较慢，相对 CPU16 耗时约为 1.37 倍。只与 CPU8
比较得到的 0.94× 也不是正收益。CPU 改进不会自动缩短设备任务成本；本轮没有
修改设备数学或声称获得三维 GPU 加速。Windows 端先前重负载已经由用户确认结束，
本轮启动前设备利用率为 0%；保留全部六个样本，未筛去最慢项。

额外一次 Cellular 细网格默认 CPU8 为 29.197 s，原检查点场预算通过。它仅用于
修复后的回归检查，不是新的三轮性能结论，也不与另一批次 FLASH 时间重算达标率。

### 工程补测结果

| 现有入口 | 结果 | 范围 |
| --- | --- | --- |
| CPU CTest | 61/61 通过 | 含当前检查点、错误身份、损坏输入、EOS 和引力 |
| CUDA CTest | 135/135 通过 | 当前 CUDA 构建的已注册组合和公共路径 |
| 生产耦合／重启验证 | 42 条记录通过 | CPU/CUDA、引力边界、扩散／燃烧／AMR、失败拒绝及跨后端重启 |
| Python 工具检查 | 359 项实际覆盖 | conda work 通过 346 项；其缺少 pidfd 而跳过的原 13 项由系统 Python 全部通过 |
| 架构审计 | 通过 | 原入口，无新增规则或容差 |
| 干净归档 | 2/2 + Sod 通过 | Helmholtz、检查点，以及默认路径 Sod 场对照 |
| 当前物理场补测 | 全部通过 | 12 个三维配对、6 个二维 CPU、6 个 CUDA 样本及单次细网格回归 |

本轮没有设备内存／设备数学改动，未重复上一轮的设备 sanitizer；原记录仍在
[冻结验收](../../O6ControlsAcceptanceReport.zh-CN.md)中。以上短程见证不能替代长时
演化或 FLASH 共同科学误差曲线。原统一两倍成本目标与 RTX 的负收益场景保留；
没有新增 FLASH 或 H100 测量，也不以尚未安排的 H100 试验阻塞本轮工程交付。

## 证据与复现

- [checks/verification.json](checks/verification.json)：当前源码／程序身份、全部检查状态和设备前后快照。
  同目录保留 CPU/CUDA CTest、两套 Python 日志和 42 条生产记录。
- [timings/summary.json](timings/summary.json)：本轮全部正式样本的中位数、分项和比值；
  `cpu8.json`、`cpu16.json`、`cuda.json` 保留每次记录与场检查，`regression.json`
  保留二维 CPU 和单次细网格检查。三维配对的完整场证据在 `final-pairs.json`。
- [diagnostics/baseline-pairs.json](diagnostics/baseline-pairs.json)：重建旧源码的对照；
  同目录保存配置／构建日志、单因素筛查及线程工作量记录。`probes.json` 中保留
  两个已撤销探针的原始补丁文本作为证据，不应应用到正式源码。最初错误使用 CPU/CUDA 比较器检查两个
  CPU 结果的拒绝日志也保留，随后以 CPU 对照检查原有效样本，没有放宽 CUDA 身份检查。
- [package/archive-inspection.json](package/archive-inspection.json)：实际下载归档的身份和表大小／摘要；
  同目录保存干净配置／构建日志、两项 CTest 与 Sod 结果。构建使用归档加本轮源码
  修复，四个受影响的源码／脚本文件与最终工作区逐字节一致。

原始 HDF5 和执行物保留在本机 `output/release-readiness-20260927/`，不提交到仓库。
归档构建原文件在记录中的 `/tmp` 路径，可能由系统清理；上述轻量证据随源码保留。
JSON 中的绝对路径用于追溯当时运行，其他机器应生成新输出，不把这些路径用作本地参考。

完成 CPU/CUDA preset 构建后，以下命令沿用现有入口生成当前版本的同机参考。
从仓库根目录执行，每轮使用新的输出目录；亲和性默认继承本机允许的 CPU 集合。
要复现本机绑核配置，给两条 CPU 命令分别加 `--affinity 0,2,4,6,8,10,12,14`
与 `--affinity 0-15`，给 CUDA 加 `--cuda-affinity 0`；先核对机器的 CPU 编号。
自定义输出位置时显式传入 `--arch-cpu`／`--arch-cuda`。

```bash
python validation/gravity/flash/run_comparison.py \
  --routes arch --cases snia2d snia3d --threads 8 --repeats 3 \
  --prefix cpu8 --output output/release-check \
  --manifest output/release-check/cpu8.json
python validation/gravity/flash/run_comparison.py \
  --routes arch --cases snia2d snia3d --threads 16 --repeats 3 \
  --prefix cpu16 --output output/release-check \
  --manifest output/release-check/cpu16.json
python validation/gravity/flash/run_comparison.py \
  --routes cuda --cases snia2d snia3d --repeats 3 \
  --prefix cuda --output output/release-check \
  --manifest output/release-check/cuda.json
python validation/gravity/flash/run_comparison.py --report-only \
  --manifest output/release-check/cuda.json \
  --reference-manifest output/release-check/cpu8.json \
  --reference-manifest output/release-check/cpu16.json
```

重做源码归因时，在独立目录保留 `51f982ab` 与当前版本的执行物，显式传入各自
`--arch-cpu`，保持原输入，并按本报告交替顺序运行。旧 `ca1fdcd3` 重建使用其
自身编译选项；不能把它重新套用当前选项后仍称作原配置。所有科学对照复用现有
检查函数与误差预算，CUDA 身份检查继续只接受实际解析为 CUDA 的候选。
