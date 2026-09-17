# 显存归因与 ODE 工作窗口：合同通过，新 factory 构建中

2026-09-17；leaf-inline 对照已完成并双端归档。窗口封装的 18 项真实 GPU 合同通过，
现在单独构建两份新的 CUDA factory；真实窗口 ODE 尚未运行，没有性能或生产资格。
候选保持互相隔离，没有把 leaf-inline 叠加到本项，也没有并行运行服务器实验。

## 不能把整卡峰值直接归因于 cuDSS 因子

使用 agent-reach 的 Exa／Jina 核对 NVIDIA 官方定义：
`CUDSS_DATA_MEMORY_ESTIMATES` 返回 16 个 int64；下标 0、1 分别是设备常驻和峰值估计，
2、3 为 Host 对应值。必须在 analysis 后查询，输入矩阵、RHS 或配置变化会影响其有效性。
这是库的估计，不是整个 CUDA 进程／设备实际分配量的测量。
来源：[NVIDIA cuDSS 数据类型](https://docs.nvidia.com/cuda/cudss/types.html#c.cudssDataParam_t.CUDSS_DATA_MEMORY_ESTIMATES)。
只查公开文档，没有安装、升级库或改变服务器环境。

本轮已双端归档的 API-cost 原 stderr，三种 ODE 中各一次同值输出：

| 实际网络，pool2 | 常驻设备估计（bytes） | 峰值设备估计（bytes） |
|---|---:|---:|
| audit150 | 7,424,772 | 8,729,412 |
| audit200 | 10,072,372 | 11,809,012 |

见 [API-cost 证据](../results/native-wave-20260917/api-cost-v1/README.zh-CN.md)。
整轮 guard 的 14,520 MiB 是整设备峰值；不能把它当成上述 factor 估计，
也不能把相减后的差额直接判定为 local memory、stack 或 runtime 的任一项。
需用分阶段分配／资源证据继续区分，估计值本身不保证未来大容量不会 OOM。

现有 native-wave provider 将 native 峰值估计与自身私有字节合计限制在 256 MiB，
并检查实际剩余设备内存；这个预算与整卡峰值是不同的口径。不得为了测试扩大容量静默提高预算。

## 活跃 ODE 窗口当前与 warp／因子 cohort 绑定

已核对实际冻结源码以及当前共享实现：

- `src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh`：owner 容量取 caller max_cells、device warp width 和按 lane_bytes 计算的 fitting 三者最小值。
- `src/cuda/microphysics/SparseBurnCells.cuh`：按该容量逐 chunk 完成燃烧。
- 冻结 v4 `SparseOdeBatch.cuh`：advance 的 block width 同为 warp；provider 容量取 batch.capacity。
- native-wave provider 当前限制 capacity≤32；批量缩放／残差 kernel 的按值描述符也限制 32 lanes，并保留旧的 4 KiB 参数兼容边界。

因此当前≤32活动单元一般只启动一个 advance block。已完成的 launch-shape ABBA
固定总单元数不变，只把 32 lanes 拆成更多不满 warp 的 block；其变慢结果
不证明“同时处理更多真实单元”也一定变慢。反过来，更多活动单元也不保证更快。

## 当前隔离候选及后续验收

优先评估有界地分开 ODE continuation 工作窗口与 native factor cohort：
例如更大的 ODE 窗口、仍至多 32 个 native 因子槽。先做资源／排程合同，随后才是真实网络。
这不是直接扩大全部因子缓存；也不能通过伪造硬件 warp width 绕过 owner 上限。

必须检查：

1. 共享 ODE、RHS/Jacobian、EOS、反应网络和数值预算原样；只改变 Host 调度与 kernel/memory 组织。
2. 每个逻辑单元唯一 matrix token，槽位复用时不能复用别人的因子；记录由 cohort 切换引起的额外 factor。
3. 数值拒绝、EOS 失败、已完成和待响应状态分别保持正确；不能把“尚未服务”误当作线性求解失败。
4. 原 256 MiB provider 预算、设备余量与参数大小约束保留；ODE workspace 单独报告，保持明确上限。
5. 覆盖 cohort/window 两侧的尾部与存储更换、三 ODE、长轨迹、真实 Helm；不把局部性能当作全应用成绩。
6. 对比更高 ODE 并发的收益与失去因子复用、额外 Host API 的代价，保留不利结果。

当前状态及证据入口见 [windowed](windowed/README.zh-CN.md)。Host 封装合同已实际通过；
ODE owner/executor 隔离接入仍在新编 CUDA 对象，不能使用旧 `.o` 冒充新路径证据。
构建后先双端保存，再执行原 2→3 单元、pool2 的六组 focused 轨迹；该小输入即使通过，
仍不能替代多页 ODE、容量、长轨迹、Helm 或正式性能。没有修改生产库／注册／调度参数。
生产仍逐 block 调用 owner，因此更大窗口不等于跨小 block 聚合，不得改变原算例来宣称同输入提速。
