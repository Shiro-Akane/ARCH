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
- Linux 工具测试 335 项：334 通过、1 跳过。Windows 全套因 symlink 权限、文件锁、PATH 与编码等环境项存在失败，不能称为跨平台全通过。
- Canonical 7 算例通过：RKL1/RKL2 各 64/128/256 解析解/原收敛要求，以及双块 BD 燃烧。
- 原 aprox13/Helm 三 ODE 完整应用交叉求解器门槛通过。
- 内置网络 NSE on/off 原应用矩阵 16 算例通过。
- 六组 BurnGradient＋Helm 物理组分扩散＋Hydro＋动态 ENUC-AMR（BE_NR/BD/ROS4 × RKL1/RKL2）通过，原场对照及质量/电荷/含核能源项能量门槛不变。见 [耦合记录](coupled-v3.json)。初次误设 Helm 常数系数和误用 uniform qualifier 的失败测试记录均保留；修正为物理系数和 AMR 逐单元检查，并补非有限/闭合/正性失败测试。
- 四个内置网络的独立时间积分/冻结端点 EOS 参考通过；反应率来源仍共享，不能宣称独立核反应率验证。
- 150/200 网络 32/33 单元存储、8/32 lane 池、BD/ROS4 原四步全部通过；原场 2e-10 / limiter 2e-8 门槛不变。见 [容量记录](sparse-capacity-v2.json)。
- 新 provider 缓存恢复、token 更换、失效、>32 owner 淘汰、原残差/错误合同通过。见 [provider 记录](sparse-provider-v2.json)。该 focused 构建复用不可变的大网络工厂对象，不冒充完整 cuDSS ARCH 重编。

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
- 应用回归入口：`validation/backend/run_microphysics_validation.py`；规模计时入口：`validation/backend/run_microphysics_timing.py`。
- 待完成：完整 AMR/曲线坐标/长期/restart 回归，正式规模计时与剩余热点判断，最终 cuDSS 全应用集成、长期资源/安全门槛。vGPU 的 memcheck/racecheck 调试限制仍未解除，不给安全通过标签。
