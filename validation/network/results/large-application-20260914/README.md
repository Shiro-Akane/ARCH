# 150／200 核素完整应用构建与验证

当前已完成 **clean Release 完整构建、修复阶段增量整合、原六组完整应用数值矩阵和正式计时**。
这六个原定 32 单元算例的 CUDA 端到端耗时约为 CPU8 的 5.0–10.3 倍，性能尚未达到齐平目标。
不是 focused harness 的通过代替完整 ARCH 验收。

## 原始 clean 构建

生产源码为 `32cc416220a69cad5ab12030ba7b26db360bcd22`，工作树位于服务器
`/home/ubuntu/projects/ARCH-large-application-20260914`。
两个新计时脚本／测试是未提交文件，完整 provenance 已记录；生产源码没有未提交修改。
严格浮点、既有 SuiteSparse KLU 和 cuDSS／cuBLAS 库保持不变，目标 sm90。

八个 EOS／网络组合均成功生成，没有跳过失败 TU：

| 网络 | EOS | 编译墙钟（秒） | 单命令 peak RSS（KiB） |
|---|---|---:|---:|
| 150 | IdealGas | 3368.02 | 2714144 |
| 150 | Helm | 3438.08 | 2823740 |
| 150 | Tabular3D | 5875.71 | 3020496 |
| 150 | Tabular4D | 5067.86 | 3184264 |
| 200 | IdealGas | 7015.43 | 4004052 |
| 200 | Helm | 7429.00 | 4240728 |
| 200 | Tabular3D | 10800.80 | 4424572 |
| 200 | Tabular4D | 11478.57 | 4536864 |

共记录 116 次 CXX／CUDA 编译调用；SuiteSparse 的 C 编译未经过逐命令 launcher。
backend archive、sparse provider archive、完整 ARCH 和 checkpoint validator 都已生成，指纹见
`clean/artifacts.sha256`，完整命令、首轮配置失败和成功配置日志亦保留。
采用服务器并行 4 构建并有其他工作负载；此表不是编译加速比，也不是 16 GiB Debug 资格。
内置网络的 16 GiB 受限 Debug 验证另见 composition-fix 结果，不外推到超大网络。

## 备份和后续边界

`clean/summary.json` 绑定原始源码、生成网络、依赖库、build options 与产物。
compact 构建日志包已在服务器和本机分别核验：

- `large-clean-build-evidence-v1.tar.zst`：244,127 bytes。
- SHA-256：`c92ba070a934bbddea27ba6bb45c071f20bff511dda5fb41dab368e1c50cba09`。
- 服务器上述工作树的 `build/`；本机 `C:/tmp/ARCH-perf-20260909/build/`。

该包只包含构建证据，不包含二进制。整合前已保存四个原产物，再由 Ninja 按真实依赖重编。
整合明确标为“原 clean 构建 + 已记录的增量整合”，不冒称第二次 clean 构建。
完整应用原矩阵已通过；原 focused BE 长轨迹大容量超时继续保留，不以不同完整应用算例或更换 ODE 覆盖。

## 修复阶段整合

已 fast-forward 至双方固定分支提交 `8da9b23d5597c2c8bd91d9dfa07b971d2bb91bef`。
相对原 clean 基线严格限定 14 个生产／测试文件差异（S5 六个＋共享组分修复八个）。
Ninja 实际完成 34 次编译调用，最大单命令 RSS 为 4,133,840 KiB；完整 archive／ARCH 链接成功。
八个超大网络 object 的前后 SHA 相同，这是实际依赖解析结果，没有手动跳过或伪造构建。
guard 的进程组 RSS 峰值为 7,718,696 KiB，949.832 秒；没有 swap 增长或资源终止。

源码、生成网络及 cuDSS／cuBLAS 依赖校验均通过。实际产物：

- ARCH：`53d27a2f4fe574a784d9cd6bb7375dbdcd2cbb5c858485644e5d77314953fb1f`。
- backend archive：`13dc08d75f06246818a20d8522d6ac1461b1e2bf3454f9c459444e93866c5f77`。
- sparse provider：`57ca4790f79a63653915a5f02ac9d504be45b51340f44e5f81a1b136aeabce02`。
- checkpoint validator：`7dfe449db6f0cf8a9ae420ca7cb1c3689fdd1d7bff03a55b92b5245478eaa61f`。

`integration/` 保存完整重编命令、资源日志、前后 object 指纹及 provenance。
其 compact 已双端核验：`large-integration-evidence-v1.tar.zst`，235,783 bytes，
SHA-256 `7cd43b5ab00de22ebcd87508cecdf31bb9ea336e947b02d7bd90ffa94da733f1`。
初次快速计数误将 verbose 命令里的格式字符串计入；正式 parser 仅统计真实 metric 行，实际为 34 次而非 68 次。
这次整合没有采用另行测试的固定页 provider 候选，详情见 [调度诊断](../large-scheduling-20260914/README.md)。

## 完整应用数值结果

相同 ARCH 二进制，CPU 解析为 SparseKLU、CUDA 解析为 cuDSS，EOS 均为真实 Helmholtz。
audit150／audit200 × BE_NR／BD／ROS4，共六算例；每例对比第 1、2、5 步，以及
原终止时间 1e-10：**48 次运行、18 组检查点对照、6 组终点对照全部通过**。
整个矩阵的源码、构建、二进制、运行环境和输入前后身份校验通过。
`runtime/tested-helper-identity.json` 记录当时未提交的两个计时辅助文件与后来入库文件的
逐字节／规范化 Git blob 对应。实际受测服务器仍为 `8da9b23d` 加这两个已记录文件；
不能将后续证据提交号冒称实际二进制的构建提交号。

六算例终点均为 CPU／CUDA 各 20 宏步，原始归一化场误差如下；比较器原预算未改：

| 网络 | ODE | 最大归一化场误差 |
|---|---|---:|
| 150 | BE_NR | 4.4554e-15 |
| 150 | BD | 8.9100e-15 |
| 150 | ROS4 | 2.6206e-15 |
| 200 | BE_NR | 8.6445e-15 |
| 200 | BD | 2.0954e-15 |
| 200 | ROS4 | 1.0477e-15 |

额外检查真实初末 HDF5：12 条终点轨迹都有约 2.32e-5 的可测组分演化，密度、动量、
finite／组分边界／metadata／组分闭合门槛均通过，不接受“没有实际燃烧”的快结果。
弱反应网络不套用 aprox13 的电荷恒定假设；这不是独立弱反应能量或反应率 oracle。
原压力门槛（不同密度单元、16 步至 1e-9）仍按 [大网络长轨迹诊断](../large-scheduling-20260914/README.md) 单列，不能互相替代。

本轮 guard 没有资源终止或 swap 增长；进程组峰值 RSS 516,620 KiB，
全设备显存峰值 15,620 MiB、最低可用 4,143 MiB。该显存数包含设备上的运行时保留，
不是 provider 的估算 cache 大小。1643.196 秒为整条验证链墙钟，不是正式加速比。

`runtime/backend-validation-evidence.json` 保存完整判断，`runtime/cases/` 保存输入及运行日志，
`runtime/additional-trajectory-quality.json` 保存额外真实 HDF 资格。
Git 中采用浅目录投影，避免超过 Windows 路径限制；原 compact 完整布局另存于
本机 `build/large-runtime-unflattened-compact-20260914/`，原始包不改写。

原始包包括实际产物、全部 HDF5、日志、生成网络、Helm 表、构建与执行配方，共 595 个文件：

- raw：`large-application-runtime-v1.tar.zst`，285,199,568 bytes；SHA-256 `297de8027483181d63f7a975f910b68f63aa6da5d5260b85fdc743e5a51c0569`。
- compact：401,531 bytes；SHA-256 `6c79e0a062a6bfcf4926d0353e835199b1b3efedbb5575eb1b795450ffc5bb03`。
- 服务器上述工作树的 `build/`；本机 `C:/tmp/ARCH-perf-20260909/build/`。raw 和 compact 均已在两端核验完整 SHA。

原 clean 构建四个产物的独立备份也已双端核验：服务器 `fixed-integration-v1/base-products.tar.zst`，
本机 `build/large-base-products-20260914.tar.zst`，262,029,070 bytes，
SHA-256 `f27f6e9dd0344043f5112021d7754a2fc801ac78397950281d39dbd954a5414d`。

## 正式端到端计时

2026-09-14 06:54–09:23 UTC，原六算例、CPU1／8／16 和 CUDA/Host8，
每配置一次预热、五次交替正式运行，**144 次运行、138 次字段及宏步／regrid 对照全部通过**。
源码、输入、构建和产物前后身份不变；物理终止时间、网络、EOS、求解器和原预算未改。
完整样本表见 [正式性能汇总](formal/summary.zh-CN.md)，完整记录见 `formal/evidence/evidence.json`。

在这六组实测中 CPU8 的中位数均优于 CPU1／CPU16。CUDA 仍明显较慢，不能因为数值通过就称为性能通过，
也不能拿内置小网络的收益覆盖这一结果。适用范围限定为当前 H100-20C／20 GiB vGPU、
32 个物理单元、20 宏步至 1e-10；不是独占整卡或大规模全网格结论。
口径为进程启动到退出，包含初始化与初末输出，不包含事后资格检查；没有声称稳态或纯矩阵 kernel 性能。

诊断入口仍见 [调度观测与被否决候选](../large-scheduling-20260914/README.md)：
当前 executor 按 lane 调用同步的线性 provider；原长 BE 观测中 symbolic analysis 很少，
主要问题不能简单归结为反复 symbolic analysis 或传输字节量。
固定页状态试验没有显示明确端到端收益，未采用。真正合并多 lane 的提交／完成与批量求解，
需要独立实现和重新通过自适应 ODE、原残差、失败状态及有界生命周期门槛；本轮没有把它冒称为已完成能力。

guard 没有资源终止或 swap 增长；进程组峰值 RSS 544,064 KiB，
全设备显存峰值 15,620 MiB、最低可用 4,143 MiB，观测完整。
8939.185 秒是整个测试链耗时，不用于计算加速比。
另有两次只读进程环境／线程快照，未发现启用 `CUDA_LAUNCH_BLOCKING`；
它们只是补充抽查，不是整段时间的环境证明，也没有据此修改线程亲和性。

全部输入、HDF5、日志和后处理配方共 1488 个文件已双端备份并核验：

- raw：`large-application-formal-v1.tar.zst`，1,775,255 bytes；SHA-256 `34be7d84dad7c362a562bfabca2f80e86ec59513fde474ab52cacd294afc8f3e`。
- compact：`large-application-formal-v1-compact.tar.zst`，474,487 bytes；SHA-256 `cb34cd5d477a5ef1ce1ba467fc7e83460b06e0579e7235ef8a478314dae50330`。
- 服务器本工作树 `build/` 与本机 `C:/tmp/ARCH-perf-20260909/build/`。二进制在前述独立 runtime 原始包中，不重复放入此计时包。

双端 raw 核验后，逐一校验 tar 成员／manifest／展开文件 SHA，再清理了 576 个重复展开的 CHK／PLT HDF，
共 64,822,032 bytes；两份原始包完整保留。`formal/hdf-compaction.json` 记录每个成员。
因此证据中的服务器 HDF 路径是原执行路径，后续读取需从 raw 恢复，不能冒称它们仍全部展开存在。
本机后处理门槛用既有完整正式记录作正控，并拒绝 16 种损坏记录；执行时脚本与日志保存在 `formal/recipes/`，
原执行入口为本机仓库 `build/test-formal-archive-20260914.py`，不是一个新的物理 oracle。
