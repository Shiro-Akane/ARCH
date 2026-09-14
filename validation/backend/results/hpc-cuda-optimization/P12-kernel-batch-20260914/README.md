# P1/P2 跨块 kernel：内置燃烧与扩散验证及正式计时

本目录承接同步批量版 P12，记录已验证的内置燃烧/扩散跨块版本，
**不是 150/200 核素完整应用、安全资格或全部多物理优化完成声明**。
不改 EOS、反应网络、ODE、容差、严格浮点、AMR 决策、restart schema 或外部 KLU/cuDSS。

## 候选内容

- Dense burn 用 `grid.y` 绑定不同 AMR 块，仍调用原单元 kernel/共享 ODE，每个块独立 workspace、EOS 状态及确定性归约。每波最多 1024 块。
- Diffusion 在原 face、dt、RKL kernel 中绑定各块的 view；方向内保留原块顺序及 `tilde_mu → gamma` 的通量注册顺序。新增 kernel 只清空、遍历和复制，不复制物理数学。
- RKL slot copy 改为跨块设备复制 kernel；EOS 失败不跨块污染。超过本地组分 scratch 容量时仍按单块 wave 使用全局 arena，避免别名竞争，不声称该路径已实现跨块并行。
- CTest 增加 3/1024/1025 块与 33 组分的逐位对照；显式检查测试清单，跳过不计为通过。

## 已完成的燃烧单独候选预实验

先只接入 burn kernel 批量化，保留同步版扩散。完整 Release archive/ARCH 链接成功；构建 guard 的 owned-process 峰值 RSS 为 2,494,188 KiB，耗时 2,385.806 s，无 swap 增长或保护停止。这不是 16 GiB clean Debug 验收。

三种 ODE 双块生产 backend 对照逐位一致。表格 EOS focused witness 的 3D/4D × BE-NR/BD/ROS4 六组通过，覆盖单块、批量、稀疏路径的失败不提交和有效复用；详见 [原始身份与命令](burn-eos-failure-v1.json)。该 witness 链接既有有界 provider，不等于完整 cuDSS ARCH 集成。

[无插桩全应用预实验](burn-fused-pilot-v1.json) 共 16 次运行、12 次跨版本/后端比较，全部通过原数值、含核反应源项的能量/电荷预算及接受步数/regrid 工作量对齐检查。每配置只有一次测量，不能代替预热后至少五次正式样本；统计口径是 ARCH 启动到退出（含初末 I/O），不是稳态 kernel 时间。

| 模块 / 块数 | S4 GPU（s） | 跨块 burn GPU（s） | 同轮候选 CPU8（s） |
|---|---:|---:|---:|
| BD / 8 | 7.940405 | 3.065424 | 3.554022 |
| BD / 32 | 25.004101 | 3.315398 | 6.820783 |
| ROS4 / 8 | 5.803196 | 2.788342 | 2.735683 |
| ROS4 / 32 | 16.514491 | 3.054759 | 5.108124 |

8 块 ROS4 仍未超过 CPU8，保留此结果；32 块的初步改善不能外推至大核素稀疏网络。

燃烧单独候选不可变副本在服务器 `build/p12-20260914/burn-fused-binaries/`：

- ARCH SHA-256：`9937c7c95d6da348cb3d1be1cf4e1a8db7db55ab41f1aa0d12983e063c96cef0`。
- archive SHA-256：`b4c430cd3f2457cf620a6acfb0c153f9efb84b94f4ca4a3188f61af849ab1a9e`。

## 联合版本的构建与回归

联合 burn/diffusion Release archive 与 ARCH 已完整链接，ARCH SHA-256 为
`3a04af7da6e67d0682ba4bb36f7d53f6cd5422cd9ffb525badaecff8677c7989`，
archive 为 `1a025fb3da8ac2f315e6fe65b7c4d872350a621403b94319a7d9c73322ed39a6`。
在已完成 burn-only 构建上增量构建耗时 198.200 s，owned-process 峰值 RSS 1,507,484 KiB；
这两个数不是 clean build 成本。原合同测试全部重新链接至该 archive。

当前已通过：新增批量合同 10/10、原合同 24 项、原扩散解析/收敛与燃烧基准、
四个内置网络的独立时间积分参考、三种 ODE 的第一定律检查、16 项 NSE 矩阵、
两组六项燃烧/扩散/流体/AMR 耦合矩阵、Cartesian AMR、24 项柱/球坐标矩阵、
长步 refine/derefine、基础 restart、六项燃烧＋组分扩散 restart、
六项全输运耦合 restart，以及原 tails 故障检查。
这里的独立时间积分参考仍共用反应速率，不能称为独立核反应率验证。

补充混合 EOS 失败/正常块并交换顺序后，批量合同再跑 10/10 通过，ARCH/archive SHA 不变。
Linux 同轮工具测试为 347 项、346 通过、1 跳过。新增进度诊断工具后，相关 26 项工具测试也通过；
后续新增脚本仍需分别记录验证，不能把旧全量计数当作新全量运行。
Windows 全量工具运行的 347 项出现 9 个平台/运行环境错误及 24 跳过
（含 `os.sysconf`、symlink 权限、SQLite 临时文件、POSIX 路径及编码），不计为全量通过，
也不据此修改物理代码或放宽测试。
补齐进度/长轨迹辅助脚本后，本机 Windows 全量再跑 352 项，仍为 9 个环境/平台错误、24 跳过，
完整原始日志见 [windows-tooling-v3.log](evidence/windows-tooling-v3.log)。相关 26 项 microphysics
及 2 项 sparse recipe 单测单独运行通过；服务器新一轮全量结果另行记录。

全部正式计时退出后，同步长轨迹辅助脚本，服务器新全量 352 项：351 通过、1 跳过，
耗时 16.897 s；详见 [Linux 原始日志](evidence/all-batch-tooling-v3.log)。未重编生产二进制。

## 正式计时与性能边界

扩散正式扫描已经通过，共 216 次运行、210 次结果比较。每配置一次预热和五次正式测量，
候选 CPU 扫描 1/8/16 线程，GPU 的 Host 固定为 8 线程；下表是中位数（秒）。
原始 JSON 会与本阶段完整归档一起保存；不以预实验替代正式数据。

| 模块 / 块数 | S4 GPU | 候选 GPU | 候选 CPU1 | 候选 CPU8 | 候选 CPU16 |
|---|---:|---:|---:|---:|---:|
| RKL1 / 8 | 0.613533 | 0.503934 | 0.050202 | 0.077209 | 0.101352 |
| RKL1 / 32 | 2.666353 | 0.886029 | 0.339384 | 0.562583 | 0.765070 |
| RKL1 / 128 | 52.861389 | 7.800511 | 7.447012 | 12.647046 | 16.372297 |
| RKL2 / 8 | 0.656453 | 0.510175 | 0.058327 | 0.101157 | 0.140985 |
| RKL2 / 32 | 3.189738 | 0.966104 | 0.450746 | 0.912510 | 1.265309 |
| RKL2 / 128 | 79.357805 | 10.013550 | 13.009476 | 28.626860 | 39.827433 |

128 块 RKL1 相对最快 CPU1 仍慢约 4.7%，不能宣称稳定超越；GPU 样本范围
7.773643–7.946289 s，CPU1 为 7.395106–9.019927 s。
128 块 RKL2 相对最快 CPU1 的比值为约 1.30，GPU 范围 9.919509–11.025804 s，
CPU1 为 12.975356–13.038589 s。8/32 块两种方法均落后于最快 CPU，保留全部结果。

燃烧 BE-NR、BD、ROS4 的正式扫描均已完成，每模块各 108 次运行、105 次比较全部通过。
五模块合计 **540 次运行、525 次比较**。完整数据见 [正式汇总表](formal-summary.md)，
其中保留旧 GPU、新 GPU、CPU1/8/16 的全部 15 个规模点及原始 JSON 链接。
128 块 BE-NR 的候选 GPU 中位数为 4.492721 s、最快 CPU16 为
8.508508 s（约 1.89 倍）；128 块 BD 为 4.942416 s 对 11.712192 s（约 2.37 倍）；
ROS4 为 4.548395 s 对 8.171037 s（约 1.80 倍）。
BE-NR 的 8 块为 2.795571 s 对最快 CPU8 的 2.817785 s，只能视为基本持平；
BD 的 8 块 GPU 样本范围 3.091545–3.821494 s，也不能隐藏其波动。
ROS4 的 8 块为 GPU 2.859749 s 对最快 CPU8 2.790637 s，仍略慢。
上面的 burn-only 单次计时不能代替这一步。全应用启动到退出与进度日志观察的
“启动＋首步／首步后推进／末步后输出和退出”分列；后者是带观测的诊断，
不是无扰动 kernel 时间、纯冷启动或经过认证的稳态性能。
150/200 有界 factor cache 的剩余长轨迹/完整 cuDSS 应用集成也不能由上面的 aprox13 数据代替。vGPU 调试限制仍阻塞 sanitizer 安全资格，不标记 memcheck/racecheck 通过。

## 源码身份和原始归档

服务器冻结目录为 `/home/ubuntu/projects/ARCH-microphysics-20260914`；实际 Git 基点和
overlay 见 [server-head.txt](server-head.txt)、[源码 patch](server-tracked.patch) 及各报告 provenance。
该目录是较早 checkout 加已记录的 overlay，不能冒称它当时 checkout 了本阶段发布提交。
提交前另外逐文件比对本地索引：
[原始字节检查](evidence/core-index-identity-v1.log) 保留换行符差异；
[LF 内容核对](evidence/core-index-identity-v2.log) 全部匹配，唯一额外差异是
`CudaBackendBurnSparseImpl.cuh` 的两行缓存说明注释，详见
[完整差异](evidence/core-comment-only-difference.patch)。该差异不改可执行代码，
也没有为追求“字节相同”而事后改写服务器冻结源码或产物。

原始 HDF5、初末检查点、`.par`、日志、各阶段报告、失败记录、overlay 和实际 Helm 表
已合并归档；Git 保存紧凑报告/命令/指标，巨大 raw 不展开进 Git。
以下为服务器 `build/` 中已完成归档的 SHA-256；本地 `build/` 下载副本也须逐包核对该值，
不以开始传输当作备份完成：

- `p12-fused-validation-timing-v1.tar.zst`：`fe0bbe45e885121b7d816eff380f06415f46b6c73b7f48e220764ac08550d0b8`。
- `p12-fused-compact-v1.tar.zst`：`83f36798651d168d80d07d3edc33f1fe91548a483a9b59c5f2a39f6d9aa53509`。

逐文件清单见 [raw-files.sha256](raw-files.sha256)，生产产物见
[production-artifacts.sha256](production-artifacts.sha256)。Helm 的真实表 SHA 与提交 LFS oid
一致，单独进入 raw 归档；不会把约 60 MB 的表内容写成源码差异 patch。

## 后续独立验收

保留此源码/构建原位不变作为后续性能基线。S5 AMR 指标批量化在另一个真实 worktree
完整构建并重复科学、生命周期、restart 和性能门槛，不混入本阶段提交。
大网络应用也单独构建所有注册 EOS/network 路线，并补 BE-NR 原四步与更长容量轨迹。
任何后续失败或性能不足均单独保留，不撤销或冒用本目录的既有证据。
