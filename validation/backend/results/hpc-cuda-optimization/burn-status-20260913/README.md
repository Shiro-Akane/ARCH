# 开启燃烧后的 CUDA 大网络状态检查

日期：2026-09-13。按用户要求检查燃烧和大矩阵求解；本轮为诊断，不修改生产实现或依赖。

## 当前结论

CUDA 燃烧不是空接口。小系统的 DenseLU 路径已经在 S4 数值回归中运行；大系统有实际 cuDSS 实现。不过，Sedov 测速未开启燃烧，不能把其加速比当作燃烧加速比。

| 检查 | 本轮状态 |
|---|---|
| 既有 S4 线程扫描 | 1/2/4/8/16 五档均为 passed，各 24 次运行；先确认结束才启动本轮燃烧诊断 |
| 真实 cuDSS provider | 通过；默认合同包含 32、151、201 阶制造矩阵、分解/求解/复用及负向检查 |
| audit150 / audit200 GPU 数学测试 | 两项通过；与 provider 合计 CTest 3/3、1.71 秒，无 skip |
| audit150 四步真实燃烧 | 失败：BE_NR 的四步与 2/3 单元均完成；BD 在 2 单元、外部第 0 步的 ENUC 比对超差，ROS4 未到达 |
| audit200 四步真实燃烧 | 通过：BE_NR/BD/ROS4、2/3 单元各四步；原预算不变，前后身份校验通过 |
| 完整应用级大网络燃烧及正式加速比 | 本轮尚未验证；不由小规模 typed-factory 测试替代 |
| Sanitizer | vGPU 禁用 GPU debugging 的既有阻塞未解除，不作安全通过声明 |

原始诊断位于服务器 `/home/ubuntu/projects/ARCH-perf-20260909/build/burn-status-20260913/`。日志由原 runner 在子进程结束或超时后写入；运行中没有轨迹日志不等于没有执行。

## 实际求解路径

总 ODE 方程数包含核素、温度及可选辅助能量状态。不超过 31 方程时 `Auto` 选择共享 DenseLU；更大系统 CPU 选择 KLU，CUDA 选择 cuDSS。显式 CUDA+KLU、CPU+cuDSS 或缺少已链接 provider/网络代码会被拒绝，不静默代算。

当前两个生成包没有辅助方程：audit150 为 150 核素、151 阶系统；audit200 为 200 核素、201 阶系统。它们是每个燃烧单元的稀疏系统，不是把整张 AMR 网格拼成一个全局矩阵。

数值状态、CSR 系数、右端项和修正量留在 GPU；Host 调用 cuDSS API 并交换请求/完成状态。适配层显式关闭 hybrid CPU numerical execution 和 Host factor spill。BE_NR / BD / ROS4 的 ODE 数学仍共用，原矩阵残差检查保留。

性能上有明确的待测点：`SparseOdeBatchExecutor` 在 Host 逐 lane 处理求解请求，每轮请求回传后同步；只有一个 cuDSS factor owner，换 lane 后可能重新分解先前矩阵。这限制了显存中的分解填充量，但不保证速度，不能仅凭使用 GPU 就预言收益。

## 本轮产物与边界

使用原大网络专用 Release 构建 `build/large-network-20260909/release`，源码冻结于 clean `0266d96f20b184d4b17ebc6a066ac3b9021f1642`，启用 KLU/cuDSS，注册两个原始网络。它不是当前 S4 的 Sedov 二进制；后者关闭 KLU/cuDSS，也未注册这两个大网络。

本地 Git 对比确认 `src/cuda/microphysics`、`src/cuda/runtime/burn`、`src/numerics/burnsolver`、`src/numerics/linalg` 在该旧基线到 S4 之间无代码 diff。此事实不能代替 S4 的完整大网络构建/集成验证。

未触发重编或 CMake 重新配置。`ninja -n -d explain` 报告 VerifyGlobs 强制检查链；独立只读解析发现其 7 组 glob 列表均匹配实际文件。此检查解释了 dry-run 不足以断言需要重编，不将构建图另标为已验收。cuDSS、CUDA runtime、cuBLAS 的动态链接均解析到既有隔离路径，没有修改库。

原四步输入保持：rho=1e7、T=3e9、cv=1e8、interval=1e-10、rtol=1e-7，C12/O16 各 0.5；原 2/3 单元存储切换，三种 ODE，场误差预算 2e-10、limiter 2e-8。每网络超时 1200 秒，保留至少 16 GiB 可用 Host RAM，不允许 swap 增长。不扩大容量、不延长轨迹、不放宽预算。

启动包装第一次因 CTest 4.4 的摘要格式与字符串预检不符而退出，未启动燃烧程序；随后按三条具体测试的 Passed 记录与无 skip 核对，通过后才启动。未改变测试或科学通过条件。

## audit150 已确认的失败与性能现象

原始错误见 [stderr](evidence/audit150-fourstep/trajectory/arch.stderr)，逐步记录见 [stdout](evidence/audit150-fourstep/trajectory/arch.stdout)，资源保护和退出见 [运行日志](evidence/audit150-fourstep.log)。

BD、2 单元、外部第 0 步在 `field 5 = ENUC` 首次失败：CPU `3.2232544593769839e22`，GPU `3.2232544549325635e22`，相对差 `1.3788611696231555e-9`；既有预算 `2e-10`，超出约 6.894 倍。是已被接受的燃烧状态在应用量比对中失败，不能直接诊断为 cuDSS 原生矩阵求解报错。未发生超时、OOM、保护器停止或新增 swap；总诊断约 391.3 秒，程序退出 1。

注意：当前共享 handoff 明确使用 ODE `report.energy_change / burn_dt` 产生 ENUC，并未用两个巨大 EOS 能量之差反推热量。不能仅凭终点总能差很小就把失败归咎于该减法。具体来源需要沿 BD 能量源积分、外推/接受路径与线性求解残差进一步定位；本轮没有实施修复或放宽预算。

BE_NR 已完成原 2/3 单元各四步。场最大误差 `6.0569681805083994e-15`，limiter 最大误差 `6.1936727584750394e-15`；实际组分演化 `2.5734306900671022e-5`，不是没有燃烧的空通过。

这部分诊断计时合计 CPU `12.7820 s`、GPU `374.2679 s`，GPU/CPU 约 `29.28`；GPU 显式 kernel 计数 `93856`、同步计数 `46930`。这是仅 2/3 单元、CPU 串行 cell 循环的 typed-factory 微测试，不是多网格单元的完整应用计时，也不是 8 核 CPU 对照或可推广的正式加速比。

## 资源观察（非归因结论）

audit150 运行数分钟时，GPU 利用率采样为 99%，显存占用 10526 MiB（约 10.28 GiB），进程 Host RSS 约 332 MiB。最终保护器报告 whole-device 观测峰值 10825 MiB（约 10.57 GiB）、共 372 次采样。两三单元测试中的这一设备占用值得调查，但不能等同于矩阵本身的字节数，也不能据此认定泄漏、OOM 或死锁；编译生成代码、运行时及 provider 工作区尚未拆分归因。GPU busy 百分比也不是所有计算单元并行利用率。

当前大网络整体门槛仍为未通过，不能扩大 audit150 的容量/轨迹并标成验收。优先定位 BD/ENUC 超差；之后按 RHS/Jacobian、分解/求解、Host 同步及跨单元复用拆解开销，并用有代表性的全网格 CPU/GPU 燃烧实验评价性能。不与原 Sedov 计时混算。

## 2026-09-14 收尾与定向复现

audit200 的 [原四步证据](evidence/audit200-fourstep/evidence.json)确认完整 focused gate 通过，仍非独立物理 oracle、全应用或安全验收。总运行 568.547 秒；显存 whole-device 峰值 14528 MiB，保护器未停止、无 swap 增长。

| ODE | 最大场相对误差（预算 2e-10） | CPU / GPU 诊断秒 | 显式 kernel / 同步 |
|---|---:|---:|---:|
| BE_NR | 5.315e-15 | 18.1471 / 526.7206 | 87868 / 43927 |
| BD | 1.852e-14 | 0.1375 / 4.4048 | 4108 / 2047 |
| ROS4 | 1.191e-14 | 0.5114 / 14.2845 | 7556 / 3771 |

这些是 2/3 单元微测试，不是完整网格的公平 CPU8/CPU16 加速比。数值通过不能掩盖明显调度开销。

audit150 单选 `--ode bd` 在原参数下独立复跑，ENUC 首个失败值和误差与原四步完全一致，退出 1。单选 `--ode ros4` 的 2/3 单元各四步通过，最大场误差 `8.5175130142838974e-15`，limiter 误差 `6.408566701459062e-15`；单选成功不冒充三 ODE 矩阵通过。

一次尝试对旧二进制使用新测试的 `--pool-cells` 选项，被参数解析器拒绝（`Expected species=fraction`），未启动 GPU，原日志保留。这不是新的物理失败：旧 `0266d96f` 产物尚不支持本地 S0 新加的池大小参数。下一步若需要该诊断，必须单列重编后的测试身份，不能伪称旧二进制支持新接口。

### 首个差异的隔离实验（非生产修复）

[shadow 记录](evidence/bd-host-shadow-v3-150/record.json)使用相同生成网络、共享 BD 数学和旧构建的原始编译/链接选项，单独编译诊断程序；没有重建原二进制或修改 KLU/cuDSS 库。实验限定原外部第一步、三个 density，不能替代四步全矩阵。测试源码见 `validation/network/diagnose_sparse_bd.cpp`，执行器见 `run_bd_diagnostic.py`。

在首个失败单元 rho=1.05e7：

| 诊断路线 | ENUC | BD 尝试 / 拒绝 | 观察 |
|---|---:|---:|---|
| 原 CPU KLU（仅加残差观察） | 3.2232544593769839e22 | 4 / 1 | 原值精确复现，最大 componentwise backward error 5.374e-16 |
| CPU KLU 每次重新 numeric factor | 与原值相同 | 4 / 1 | 不支持“复用旧 KLU 因子导致本例超差”的假设 |
| CPU KLU 两次测试用残差修正 | 3.2232544593769738e22 | 4 / 1 | 仅约 3e-15 相对变化 |
| **Host 网络/BD＋原 cuDSS 线性解** | **3.2232544549325525e22** | **7 / 2** | 原矩阵残差额外拒绝 1 次；最坏 backward error 3.869e-9 |
| Host 网络/BD＋cuDSS＋有界原矩阵残差修正 | 3.2232544593769709e22 | 4 / 1 | 合计 2 次额外 correction solve，无额外残差拒绝；与原 CPU ENUC 相对差约 4e-15 |

完整 GPU 原值为 `3.2232544549325635e22`，与 Host-BD＋cuDSS shadow 的相对差约 3.4e-15。因此本例的可复现触发链是：cuDSS 某次修正解未通过原矩阵残差门槛 → BD 多拒绝/多子步 → ENUC 轨迹差异。库的 native success 本身不是准确性证明；现有拒绝检查正在发挥作用，不应删除它。

这个诊断尚不能宣称生产已修复。shadow 中的额外残差在 Host 用 long double 计算并上传，仅用于隔离实验；真正生产候选必须使用共享、显式 double 的设备可用残差数学，保持数据驻留、有界重试和原最终接受门槛，再复跑 150/200、错误路径及完整应用。若候选不能过原预算，仍标失败，不能因 shadow 有效就放行。
