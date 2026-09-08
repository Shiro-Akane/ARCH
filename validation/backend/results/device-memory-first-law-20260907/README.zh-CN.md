# CUDA 分配器容量验证

[English](README.md)

第 926 轮已在冻结的发布源码上通过全部四组工作负载。采集 CUDA 内存申请量时，
正常的重网格、稀疏求解、核反应积分和 CPU/GPU 检查点校验仍然执行。每个选中进程
的记录结束时，观测到的动态分配均已释放；执行文件、分析器及其启动器的身份也
通过了运行前后的核对。

[完整证据](release-926/evidence.json)对应容量专项验收，
[最终验收索引](../final-acceptance-20260907/)负责汇总其他发布条件。
本专项结果本身不单独宣布整个版本完成发布验收。

## 工作负载与分配峰值

| 工作负载 | 采集期间保留的检查 | 动态设备内存申请峰值 | 最大单次分配 |
|---|---|---:|---:|
| 重网格事务 | 4、41 核素；保留块、细化、限制、回滚及主机旧数据处理 | 531,212 B（0.507 MiB） | 52,480 B |
| cuDSS 稀疏容量 | 16,384 行、49,150 个非零元；求解、复用、重新分解及错误输入契约 | 190,708,688 B（181.874 MiB） | 78,640,800 B |
| audit31 生成网络积分 | 三种 ODE 方法、2/3 两种存储规模，CPU KLU 与 GPU cuDSS 一致性 | 9,401,860 B（8.966 MiB） | 8,388,608 B |
| AMR 完整应用 | 三维 RK3 流体第 1/2 步；五阶段 RKL2 扩散第 1/2/5 步 | 300,773,640 B（286.840 MiB） | 5,406,720 B |

AMR 一行取五个独立 CUDA 进程中的最高值，不将它们各自的峰值相加作为同时占用。
1 MiB 等于 1,048,576 字节。这组稀疏矩阵用于实测容量，不表示程序最多支持这么
大的矩阵；分解所需内存也与稀疏结构和填充量有关。

核反应积分将**同一个总物理时间区间 1e-10 分成 64 段**，密度、温度、组成与容差
均保持原设置。场量与限制器的最大误差分别为 2.099e-14 和 2.102e-14，满足已有的
2e-10 和 2e-8 误差预算。五组 AMR CPU/GPU 检查点比较全部通过，CUDA 执行记录中
没有未完成传输或过期 ghost 数据发布。详见
[网络积分证据](release-926/sparse-validation/evidence.json)与
[AMR 证据](release-926/amr-validation/backend-validation-evidence.json)。

## 如何理解内存结果

表中记录的是进程向 CUDA 分配器申请的内存，并非物理显存驻留量。分析器还记录了
加载 CUDA 模块时创建的少量静态设备对象：重网格 272 B、稀疏容量 305 B、audit31
577 B，AMR 每个进程 816–1,088 B。这些对象与动态缓冲区分别统计。记录没有覆盖
它们的销毁过程，因此证据保留原始 `all_allocations_released=false`，并单列静态
残留，不补造释放事件。所有观测到的动态设备缓冲区在记录末尾的存活字节数均为零。

内存保护器正常完成了本轮运行，没有触发停止。Linux 最低可用内存为
5,354,144 KiB，所属进程的 RSS 峰值为 1,903,980 KiB；swap 从 197,664 KiB
升至 200,480 KiB。整张 GPU 的占用从 1,527 MiB 升至 2,306 MiB。这是另一项
设备级观测，包含桌面、上下文与驱动分配，不能与上表的申请量直接等同。本轮带
分析器的总耗时为 293.606 秒，不作为运行性能基准。原始保护器输出保留在本机的
[capacity-final-926.log](../../../../build/capacity-final-926.log)。

## 身份校验与复现

四组结果均已根据保存的数据独立重放身份与分配生命周期判定。每个选中的 CUDA
进程通过实际观测的完整执行文件路径、文件对象及冻结文件的 SHA-256 建立对应。
Nsight 给出的短进程名原样保留，但不用于猜测执行文件。重网格与稀疏容量进程
记录到了已明确指定的 Nsight 启动器向工作负载切换；其他进程直接观测到工作负载
映像。启动器例外只接受事先固定的那个文件对象，而且其全部观测必须早于唯一的
工作负载映像。未知文件、文件变化、时间重叠或身份歧义仍会被拒绝。

科学任务定义、执行文件清单和 Nsight 调用与归档的 908、921、922、925 轮一致。
本轮修正仅涉及结果侧的分配生命周期分类、执行文件观测和明确声明的分析器配置；
共享内存读取器与 ARCH 数学物理、后端实现均未因这些修正而变化。

以下为记录的调用方式，输出目录已换成新的复现目录。执行时使用该机器上对应的
Python、Nsight 与启动器路径。内存保护参数是本轮采集配置，不是应用参数，也不是
针对某台机器修改的数值设置。

```bash
env OMP_NUM_THREADS=4 python3 -B tools/run_memory_guarded.py \
  --min-available-mib 1536 --max-swap-growth-mib 256 \
  --pressure-guard --gpu-memory-device 0 \
  --nvidia-smi /usr/lib/wsl/lib/nvidia-smi \
  --log build/capacity-replay.log -- \
  python3 -B validation/backend/results/device-memory-first-law-20260907/replay.py \
  --build-dir build/release-core-throughput-cmake \
  --output-dir validation/backend/results/device-memory-first-law-20260907/release-replay \
  --scientific-python /home/shiroakane/miniconda3/envs/p311/bin/python \
  --profiler /usr/local/bin/nsys \
  --profiler-launcher /opt/nvidia/nsight-systems/2023.3.3/target-linux-x64/nsys-launcher
```

同一时间只运行一组 GPU 验证，输出目录不能已有结果。原始 Nsight 数据库可能带有
无关的环境元数据，因此留在被 Git 忽略的 build 目录；交付的 JSON 只保留选中的
CUDA 进程及相关内存统计。

## 审计记录

此前各轮仍保留为失败且未完成，不因后续成功而补记为通过。归档包含对应原始脚本
及诊断：[908：静态生命周期分类](attempt-908/README.md)、
[921：被截断的进程名](attempt-921/README.md)、
[922：未保存失败观测](attempt-922/README.md)、
[925：已观测的分析器启动器切换](attempt-925/README.md)。

926 启动前，结果侧的 39 项进程身份测试和 19 项分配生命周期测试均已通过。
检查涵盖动态泄漏、缺少真实动态分配记录、未知类别、错误文件对象、PID 复用以及
错误的启动器时间顺序等拒绝条件。

| 冻结项目 | SHA-256 |
|---|---|
| 源码工作树范围 | `73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171` |
| [采集脚本](replay.py) | `36437cb84a410b72bc2b8a33b325317d8384980515095ee9ddd9dbb0a4ff4936` |
| [本轮证据](release-926/evidence.json) | `5458ae9f5a26bd7beebbea5d2a271f20acb8c425aadb32ed730eced8af124e8c` |
