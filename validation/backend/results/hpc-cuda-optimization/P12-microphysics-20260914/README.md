# P1/P2 燃烧与扩散执行层：验证进行中

这是开发和复现实验记录，**不是所有物理优化完毕、正式加速比或完整发布验收声明**。保留既有 ODE、EOS、网络、严格浮点、restart schema 和数值预算；不改外部 KLU/cuDSS。

## 已实施的执行层改动

- 扩散 dt、slot copy、RKL stage 和两个半步燃烧按当前有效块批量提交，在原有 stage 发布边界统一完成回传与等待。
- 保留每块状态、错误、确定性 reduction key 和返回顺序；regrid 后重新绑定 handle/storage，不缓存旧拓扑访问。
- CUDA 内置燃烧仍使用同一单元数学和原 kernel；本阶段减少同步，尚未融合所有逐块 kernel。
- cuDSS 保留一个必需 factor owner，按 `(values address, matrix token)` 缓存至多 31 个额外 owner；子 owner 不递归缓存。失败显式失效，淘汰前完成流工作。额外 native-estimated factor/workspace 上限为 256 MiB，并检查可用显存。这不是整个进程显存上限。

## 已完成的门槛

- 完整 Release CUDA backend archive 和 ARCH 链接成功，保留所有内置 EOS/network TU。串行构建进程峰值 RSS 4,125,868 KiB；未触发 OOM 或压力保护停止。
- 真实 GPU：批量/逐块扩散 RKL1/RKL2、燃烧三种 ODE 的所有字段逐位一致；两块 summary/stage 完成等待从 2 次减到 1 次。
- 后端/AMR/生命周期合同 24/24 通过。
- Linux 最新工具测试 340 项：339 通过、1 跳过（`tooling-coupled-v5.log`）。v4 在辅助文件尚未同步完时提前启动，读到旧接口而失败，日志保留；同步完成后的完整重跑通过。Windows 全套因 symlink 权限、文件锁、PATH 与编码等环境项存在失败，不能称为跨平台全通过。
- Canonical 7 算例通过：RKL1/RKL2 各 64/128/256 解析解/原收敛要求，以及双块 BD 燃烧。
- 原 aprox13/Helm 三 ODE 完整应用交叉求解器门槛通过。
- 内置网络 NSE on/off 原应用矩阵 16 算例通过。
- 六组 BurnGradient＋Helm 物理组分扩散＋Hydro＋动态 ENUC-AMR（BE_NR/BD/ROS4 × RKL1/RKL2）通过，原场对照及质量/电荷/含核能源项能量门槛不变。见 [耦合记录](coupled-v3.json)。初次误设 Helm 常数系数和误用 uniform qualifier 的失败测试记录均保留；修正为物理系数和 AMR 逐单元检查，并补非有限/闭合/正性失败测试。
- 四个内置网络的独立时间积分/冻结端点 EOS 参考通过；反应率来源仍共享，不能宣称独立核反应率验证。
- 150/200 网络 32/33 单元存储、8/32 lane 池、BD/ROS4 原四步全部通过；原场 2e-10 / limiter 2e-8 门槛不变。见 [容量记录](sparse-capacity-v2.json)。
- 新 provider 缓存恢复、token 更换、失效、>32 owner 淘汰、原残差/错误合同通过。见 [provider 记录](sparse-provider-v2.json)。该 focused 构建复用不可变的大网络工厂对象，不冒充完整 cuDSS ARCH 重编。
- 原 Cartesian AMR、24 个圆柱/球坐标二维和三维算例、动态 refine/derefine 长期测试、原 CPU↔GPU restart 与尾块门槛均通过。分别见 [AMR](amr-v1.json)、[曲线坐标](curved-v1.json)、[生命周期](lifecycle-v1.json)、[restart](restart-v1.json)、[尾块](tails-v1.json)。
- 六组耦合输入进一步同时开启真实 Helm 热传导、黏性和组分扩散，全部通过原数值及源项守恒门槛，见 [全输运耦合](coupled-all-transport-v1.json)。没有设置常数输运系数或更改 ODE 容差。
- 六组组分扩散＋燃烧＋AMR 的原 split-run / 跨后端 restart 协议全部通过，并检查恢复步号后的两条 RKL lane、缓存 generation 与负 gamma。见 [完整 restart 详情](coupled-restart-details-v1.json)。这不自动等价于全输运六组也跑过 split-run。

## 全应用预实验：同步优化尚不足以解决小块燃烧

输入、终止物理时刻、原场预算、接受步数和 regrid 工作量对齐全部通过。每个配置仅一次，是诊断，不是正式速度验收；部分样本期间还在归档传输。完整结果见 [无插桩预实验](pilot-v1.json)，插桩单独保存在 [API 观察实验](observer-v1.json)。

| 模块／32 块 | 旧 S4 GPU（s） | P12 GPU（s） | 同轮 P12 CPU8（s） |
|---|---:|---:|---:|
| BD 燃烧 | 25.127622 | 25.108967 | 6.638544 |
| ROS4 燃烧 | 16.588421 | 16.557384 | 5.175118 |
| RKL1 扩散 | 2.615887 | 2.095619 | 1.040386 |
| RKL2 扩散 | 3.109700 | 2.165661 | 0.840182 |

API 观察确认：32 块 BD 的燃烧 kernel 发射仍为 1,280 次，批量同步从 1,280 减到 40 次，但没有让不同小块的核反应计算并行。RKL2 仍有 9,728 次首 stage、19,392 次递归 stage 发射和 136,192 次 device-to-device 小拷贝。下一项候选因此是跨块 kernel/slot-copy 批量化，而不是更换物理或依赖库。原始 Host API 时间不能当作 GPU kernel 耗时，也不能把同步时间相加推算加速上限。

## 因子缓存诊断

150 核素 / BD / 32 lane，32/33 单元四步：

| 同一插桩口径 | 无 lane 缓存 | 256 MiB 有界缓存 |
|---|---:|---:|
| native analysis 次数 | 1 | 32 |
| native numeric factorization 次数 | 16,234 | 1,702 |
| native solve 次数 | 15,304 | 15,304 |
| native stream sync 次数 | 32,331 | 17,923 |

初始 64 MiB 版本在 32 lane 上发生循环淘汰（analysis 15,889 次），数值通过但性能恶化；已保留失败设计的完整日志。实测单个 150 核素 factor native 峰值估计为 4,332,244 bytes，不能用 64 MiB 容纳 31 个额外 owner。

256 MiB 容量诊断整轮耗时 352.127 s，whole-device 显存观测峰值 16,710 MiB、最低空闲 3,053 MiB。该观测含全部设备占用，不是仅 cache 的实际分配量；不能把 native estimate 当作进程显存。没有 swap 增长或保护停止。

以上是插桩/容量诊断，不是与 CPU8 的正式全应用加速比。仍需匹配物理终止时间、输入、步数和合理线程扫描，分别报告启动到退出与稳态。

## cuDSS 原生 batch 兼容性

官方 [types](https://docs.nvidia.com/cuda/cudss/types.html) / [advanced features](https://docs.nvidia.com/cuda/cudss/advanced_features.html) 文档限制 uniform batching 与 BTF_COLAMD 的组合，因此未为启用 uniform batch 更换 pivot/reordering。独立 non-uniform API 探针在现有 0.8.0.10 上以 BTF_COLAMD、GPU-only、IR=2 验证 1/2/8/32 个 151/201 阶制造解系统，最大相对误差 2.409e-16；它只是 API 能力证据，尚未接入生产 ODE，不能视为大网络物理验证。

## 身份、原始证据与待办

- 应用源：`/home/ubuntu/projects/ARCH-microphysics-20260914`，基底 `635fc37b` 加记录中的候选改动；build：`build/p12-20260914/release`，Release / sm90 / GCC11.4 / NVCC12.8.93 / KLU OFF / cuDSS OFF。
- ARCH SHA-256：`41b40db5b91dd914ca613acf11a44d15e3e3a55edfb58fba78ada533f1d4539b`。
- backend archive SHA-256：`368153af5fb042d2184ec00564910d043e7fc5a04cba7ce6fdaaf81499972aa0`。
- focused provider SHA-256：`24bb9be952ef34c68cd4ea65de498f0cd0de8bb70dc5419d6ef9d33ecf6b4fcd`。
- 因子原始证据包（源 overlay、命令、hash、全部失败/成功日志；不含可执行文件）：本地与服务器 `build/factor-cache-20260914-evidence.tar.zst`，366,971 bytes，SHA-256 `fbc0eca0225239a422181e8960d489bbba58f3163761bd244564ecafbd304d09`。可执行文件仍保留于服务器独立候选目录。
- 原应用完整证据包（包含 HDF5、输入、日志及失败测试）：本地与服务器 `build/p12-application-validation-v1.tar.zst`，1,500,981,790 bytes，SHA-256 `1615498bcad0cfcf2c0863e4ba61604d965c9a17c8f6f3ca71fced9362bbff74`。
- 联合 restart 与两轮预实验包：本地与服务器 `build/p12-coupled-restart-pilot-v1.tar.zst`，1,383,643 bytes，SHA-256 `a16abc7c668f11a3d71b8723631437d57877f66827d0d0cc849c62155221459e`。
- 上述应用二进制与 archive 的不可变副本另存服务器 `build/p12-20260914/unfused-binaries/`，哈希与本页一致。工作目录后续用于新候选重编，不能把旧报告自动套到新二进制。
- 应用回归入口：`validation/backend/run_microphysics_validation.py`；规模计时入口：`validation/backend/run_microphysics_timing.py`。
- 待完成：新增跨块 kernel 候选的构建与全部相关回归、正式规模计时，最终 cuDSS 全应用集成、长期资源/安全门槛。vGPU 的 memcheck/racecheck 调试限制仍未解除，不给安全通过标签。
