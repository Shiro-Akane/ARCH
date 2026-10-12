# R2.1：基线身份、代表检查与运行边界

2026-10-12，`compute/optim`。本文件定义下一轮数值检查的起点，进度统一由[主计划](../../../ARCH_MODERNIZATION_PLAN.md)维护。**R2.1 已完成；R2.2 数值运行、R2.3 性能／编译／内存测量未启动。**

## 本轮出口

| 检查 | 结果 |
| --- | --- |
| 分支、源码与旧执行物身份 | 当前提交、Core 树、CPU/CUDA ELF 与原记录已核对；旧验收按原范围复用 |
| 代表范围与数值门槛 | 选定下方六组；独立／解析参考、求解器交叉核验及后端一致性分别判断 |
| 依赖与可运行入口 | CPU 76 项有现成命令；CUDA/cuDSS 143 项中 75 项命令存在，68 项未解析；没有执行测试 |
| 数据依赖 | 当前 Helmholtz 表由本地 LFS 恢复，60,242,514 字节，SHA-256 与原参考一致；未拉取其他数据 |
| 文档整理 | 前置及 R1 的五份记录集中归档，唯一 Cellular 报告按原字节纳入；主计划继续作为唯一进度入口 |
| 文档与清单验证 | manifest结构检查退出0（22个原case，未执行）；392个本地链接及锚点检查无缺失，归档机器证据和导入报告原字节一致，历史合同数值字面量保持 |
| 本轮验证界限 | 无数值轨迹、无 CUDA 编译、无 RZ 长程、无性能测量、无新运行 H5；没有新增科学 PASS |

## 身份与工作入口

| 项目 | 冻结身份与用途 |
| --- | --- |
| 当前调查基线 | `890d81a06afdbe6537e216234dbebab02b14366a`；本轮文档提交不反写为旧测量的来源 |
| 工作区 | `/home/shiroakane/.codex/worktrees/d7a6/ARCH`，`compute/optim`；[AGENTS](../../../AGENTS.md)保存恢复入口与 `work` 环境 |
| 保留的构建目录 | `/home/shiroakane/ARCH/build-release-cpu`、`/home/shiroakane/ARCH/build-rt-cuda`；主 checkout 停在 `9aaa5add…`，保留旧执行物，不在此切分支或重配缓存 |
| 当前源码比较 | `src/`、`include/`、`simulation/`、`cmake/`、`tests/`、`tools/`、`.github/` 与最终封包提交 `d5a9158746e6e85b09e778843ed1290d891a28c6` 相同；相较封包，仅 Studio 有已集成的 19 个文件变化。含 Studio 的全部上述区域与前置 `a0185bdf…` 相同 |
| CPU ELF | SHA-256 `a1c76c6ff4c1833b79ff692b460805013b2695d8e9d492e36abcabb039fd57e9`；最终增量构建＋单配置 owner 的通过身份 |
| CUDA ELF | SHA-256 `0828b3c8644333d446a044b555c15681e9277a10217d3fac6e129ccf14ba861c`；生产编译／链接＋只读配置身份，非公共四模块轨迹通过 |
| 历史 CUDA 源码组 | 176 项清单摘要 `e2bc6390…`；该清单的旧 Git 注释不是全部后续修复的唯一源码来源，按处理后记录和实际 Core 树共同核对 |
| 本机资源身份 | Intel i7-10700；WSL 内核 `6.6.87.2-microsoft-standard-WSL2`；可见内存约 7.7 GiB、Swap 2 GiB；RTX 3060 Ti 8 GiB，驱动 `610.47`。历史可选 4070 Ti 结果单独保留 |

已有缓存的 `CMAKE_HOME_DIRECTORY` 是 `/home/shiroakane/ARCH`。复用时将 `--source-root` 指向该目录，并另记当前目标提交与 Core 树相等的依据；不能把工作区 `.` 配上旧缓存来绕过 provenance 校验。完整源码树相等、某个执行物哈希相等、一次测试通过分别记录；它们不互相替代。涉及 Studio 的新检查使用当前工作区。

## 六组数值代表范围

默认先执行 CPU 的既有 owner，再按缺口执行设备检查。所有测试保留原断言；下表给出代表性门槛，完整适用条件来自链接中的现有契约。复用范围随实际改动确定，无需构造所有选项的笛卡尔积。

| 组 | 先检查什么、怎么检查 | 参考与原验收依据 | 条件设备检查及覆盖边界 |
| --- | --- | --- | --- |
| **G1 Hydro** | `ppm_limiter_math`、`conservative_flux_limiter`、`conservative_acceptance`、`shared_stage_scheduler`；应用代表为 PPM 平滑波与 Sod 各 N=64/128/256 | [原 manifest](../../../validation/backend/cases.json)、[Hydro](../../../validation/hydro/README.md)：平滑波解析有限体积均值，t=0.1、质量漂移≤1e-12、PPM L1≤1e-4、末对阶数≥2.7，后端 rtol/atol=2e-10/2e-12；Sod 精确 Riemann，t=0.2，各档 L1/L2≤0.05/0.10、0.03/0.08、0.02/0.06，阶数≥0.7/0.3、正性、激波位置≤2.5格，后端5e-9/5e-12 | `hydro_leaf_parity`、`cuda_conservative_acceptance`、`cuda_ppm_limiter_math`；叶函数一致性不能替代完整 Hydro 或全部 flux/EOS 路由 |
| **G2 EOS／burn／NSE** | `helm_components`、`native_tabular_eos`、`burn_mainline_reference`、`generated_nse`；需要应用核验时用已有短时 BE_NR／BD／ROS4 输入，不重跑旧长时 burn | [EOS](../../../validation/eos/README.md)、[burn](../../../validation/burn/README.md)：各自断言；短 burn t=1e-10 s，物种 Linf／总能量相对差≤1e-8、丰度和残差≤1e-12。收敛 BE_NR 是内部参考；EOS／NSE 独立参考按其 owner 判定 | `native_tabular_device`、`generated_nse_device`、`network_nse_device`、`burn_controller_parity`；前三者当前缺执行命令，需定点补建或明确后置，不能用跳过充当通过 |
| **G3 AMR／checkpoint** | `same_level_exchange_plan`、`amr_flux_surface_plan`、`topology_transaction`、`checkpoint_compatibility`、`checkpoint_temporal_comparison` | [AMR](../../../validation/amr/README.md)、[restart](../../../validation/restart/README.md)及原测试断言：检查守恒量度、拓扑／事务和存储身份；严格重启的逐位条件与后端范数容差分开 | `cuda_amr_exchange`、`cuda_regrid_transaction`；不授予完整多模块 Device fresh restart，不新增长程重网格矩阵 |
| **G4 RKL 扩散** | `rkl_repair_weights`；应用代表为 RKL1 N=64 和 RKL2 N=64/128/256，均至 t=0.1 | [扩散](../../../validation/diffusion/README.md)与 manifest：解析余弦衰减含单元平均 sinc；RKL1 L1/Linf≤1e-5、RKL2 L1≤1e-5／末对阶数≥1.8、分数有界、均值漂移≤1e-12；后端5e-10/2e-12 | `diffusion_rkl_parity`、`cuda_backend_diffusion_rkl1/2`；常系数 Ideal species 案例，不覆盖 Helmholtz species／粘性或完整 Native RZ 耦合 |
| **G5 重力／RZ 边界** | Poisson／composite 的 `contract` 和 `analytic`、`self_gravity_lifecycle`；时间允许再执行 `self_gravity_physics` quick；`rz_qualification_runner_contract`只验证 runner 契约 | [Poisson 原标准](../P2PoissonMultigrid.zh-CN.md)、[现有独立 Jeans runner](../../../validation/gravity/run_self_gravity.py)、[RZ 契约](../../../validation/core_contracts/rz/README.zh-CN.md)；保留制造解、离散参考及各原断言 | quick 会真实运行 ARCH 并写 H5，受本轮后续预算约束；runner 测试不等于物理 RZ 通过，完整 RZ 五次穿越及 fresh 继续后置 |
| **G6 配置／API 身份** | `configuration_api_contract`、`configuration_entry_contract`、`configuration_v3_contract`、`preview_api_contract`、`preview_full_model_contract`、`preview_cellular_2d` | [配置协议](../../../src/api/docs/ConfigurationProtocol.md)及原测试：配置 revision、准入、预览／inspection 语义，按当前生产 Core 判断 | 配置／Cellular 预览不验证燃烧速率、演化轨迹或原生 GUI UAT；Studio 自动化385项保持前置身份 |

现有 backend 验证器对每个 `qualification_group` 要求三档分辨率。不能只运行 `sod_ppm_n64` 后宣称该组验收完成，也不为减少工作量删除 manifest 的收敛门槛。G1／G4 共选择 **10 个 manifest case**，先分别运行完整的小组；任一异常慢则停止该组并保留未完成状态。

## 后续复现入口

这些命令是 **R2.2 的入口，尚未执行**。先确认目标执行物、输入、表和源码身份；命令存在只表示有入口。`work` 的自动化 Python 入口和护栏例外见 [AGENTS](../../../AGENTS.md)。

```bash
# 在当前工作区；复用保留的 CPU 缓存，先逐组检查，-j1 防止重负载争用。
CPU_BUILD=/home/shiroakane/ARCH/build-release-cpu
/usr/bin/python3 tools/run_memory_guarded.py \
  --min-available-mib 1536 --max-swap-growth-mib 256 --pressure-guard -- \
  ctest --test-dir "$CPU_BUILD" -j1 --stop-on-failure \
  -R '^(ppm_limiter_math|conservative_flux_limiter|conservative_acceptance|shared_stage_scheduler)$'
```

其余五组的精确 CTest 名称、可用命令、选定执行物／直接文件参数及输入 SHA-256 保存在本文件末尾的机器清单；以各组名称组成完整锚定正则，逐组执行，避免无意启动全部测试。现成 CPU 缓存76项命令均可解析；不因本轮文档改动先重建全部目标。R2.2 运行前仍须核对所用独立测试执行物的依赖与源码来源；仅有哈希和命令不能证明编译来源，无法对应时定点补建原目标并另记新身份。

应用双后端检查沿用已有验证器，示例仅选择 PPM 完整组三档：

```bash
CUDA_BUILD=/home/shiroakane/ARCH/build-rt-cuda
/home/shiroakane/miniconda3/envs/work/bin/python tools/validate_backend_results.py \
  --manifest validation/backend/cases.json \
  --case hydro_ppm_n64 --case hydro_ppm_n128 --case hydro_ppm_n256 \
  --arch "$CUDA_BUILD/bin/ARCH" \
  --checkpoint-validator "$CUDA_BUILD/arch_cuda_single_level_validation" \
  --build-dir "$CUDA_BUILD" --source-root /home/shiroakane/ARCH \
  --output-root "$PWD/output/modernization/r22-hydro-ppm"
```

执行前将应用命令放入同一现有资源护栏，使用独立新输出目录；已存在的非空目录不能覆盖。Sod 也选完整三档；RKL2 选完整三档，RKL1 可单独 N=64。该 runner 会运行 CPU 和 CUDA，不存在已确认的“加一个 CPU-only 参数”捷径。构建缺项仅构建所需原目标；仍不自动启动完整 CUDA 构建。

## 历史证据的复用范围

| 证据 | 可保留结论 | 仍缺什么／不能据此声称什么 |
| --- | --- | --- |
| [公共消费者与最终 CPU](../../../validation/backend/results/release-closure-20261009/native-public-consumer-summary.json) | 原 Host owner、历史68/68／749/749／72/72及最终 CPU 单配置 owner 各按原 ELF／源码身份；原 CUDA 消费者8/9 | 最终 CPU 单配置检查没有重跑68项；完整 CUDA Hydro 受控停止约125分钟，guard143／序列1；生产 Device 四模块三宏步＋fresh 未执行 |
| [O9 账本](../../../validation/backend/results/release-closure-20261009/o9-endpoint-ledger-summary.json) | RZ保存的1.25／2.5次穿越满足原2%能量／0.2%参考／1e-12质量／100信号预算；Cartesian严格重启21数据集及半CFL对照；L2补充3次真实AMR变化 | 原完整五次穿越与自身fresh后置；原资源中断125、重复primary终止241保留；Cartesian主轨迹1<2次AMR变化失败不被补充轨迹擦除 |
| [长时 burn](../../../validation/burn/results/release-closure-20261009/production-summary.json) | 原 t=1e-6 aprox13、物种／温度／能量1e-8及质量闭合1e-12，独立时间方法和端点EOS参考 | 反应 RHS 仍共享；短时 BE_NR 与长时独立时间方法是两种证据，不混用物理终点，不自动重跑 |
| [Cellular 唯一报告](../archive/modernization/CellularBurnRateCpuCudaAudit-20261008.zh-CN.md) | 报告提交`110e769dd…`与实际复现`ee091b35c…`已区分；首步／第24步精确同时间的速率筛查未过，根因未定位 | 第2步是同步数诊断；原5e-9／5e-12是事前筛查设置，尚非专门速率误差上界；不放宽、不宣称当前提交重现或已修复，也不作为所有现代化任务的自动阻塞项 |

早期 RZ reflux／memo 失败记录保留在原档案中，读取它们时须继续核对后续修复和当前 owner；不能只凭旧文件名含 `failure` 就新增当前缺陷。当前实际未关闭范围按上表与既有 release 出口判断。解析／独立参考通过与 CPU/GPU 相同是两种证据；两端相同不证明算法正确。

## 时间、资源与 H5 约定

- **数值正确性优先。** 首批操作预算：普通 owner 约2分钟，原 gravity analytic 上限10分钟、quick上限5分钟；单个代表批次先以约10分钟为观察上限。预算仅作为执行停止点，原 CTest／manifest 数值断言与物理终点保持不变；原超时更短者沿用更短者。发现明显异常慢可尽早停止，不必等满上限。
- 通过 `SIGTERM` 停止本任务的现有 guard，沿其 PID 及后代进程归属清理，不终止其他任务。记录输入和执行物、已到达时间／步数、墙钟、最后数值检查、退出与输出范围，标记 **未完成／待执行优化**。有限端点证据仅授该端点；中断、超时、跳过均不记完整 PASS，性能根因未测则保留假设。
- 重负载一次执行一个，CTest `-j1`；uniform案例沿用 `OMP_NUM_THREADS=4`，gravity quick沿既有`OMP_NUM_THREADS=1`。护栏参数复用[构建指南](../../guides/Build.md)的1536 MiB可用内存及256 MiB swap增长边界。WSL实际可见7.7 GiB，不能按16 GB工作站目标安排并行。
- WSL Ubuntu 的 VHD 实际位于 **`E:\WSL\Ubuntu`**。2026-10-12 04:46 UTC宿主E盘剩余**23,495,598,080字节，约21.9 GiB**；Linux根盘约894 GiB是虚拟可用量。该观测早于本轮60 MB表恢复，不是持续容量承诺。当前工作区的H5实际消耗E盘，`/mnt/c`输出则消耗C盘。
- H5批次开始前核对对应宿主挂载，预计明显增长或出现I/O异常才复查；约剩 **1 GiB** 时预警并停止新增大输出，不高频查盘。初始每个代表批次暂设256 MiB逻辑输出运维上限；遇到大输出先诊断是否重复写盘，不能删字段、降低必要输出或改终点换通过。确需增加上限时据实际工作负载记录理由。
- 输出写入已忽略的`output/modernization/`；只保留后续仍需比较的原始端点及简短失败证据，清理须确认消费者已完成。仓库只提交处理后摘要，不提交运行H5／checkpoint或全日志。本轮未产生运行H5，无容量预警。

## 文档结构与后续分工

| 位置／工作包 | 用途或下一步 | 负责人及边界 |
| --- | --- | --- |
| [主计划](../../../ARCH_MODERNIZATION_PLAN.md) | 唯一阶段、进度与工作包索引 | Codex维护；不再复制第二套当前总表 |
| 本目录 | 当前基线说明和机器身份清单；后续按实际工作包增补结果 | 保持可读摘要，避免给每个小检查新建文档 |
| [现有档案索引](../archive/README.md#modernization-checkpoints) | 前置、R1、原Cellular报告；源计数、失败及身份仍可追溯 | 五份完成记录移入`archive/modernization`；无已证实无用的目标文件，不强制删除 |
| [旧O总表](../ComputeOptimizationPlan.zh-CN.md#当前执行校准2026-10-08)／[release](../ComputeStudioReleasePlan-20261006.zh-CN.md#当前验收出口) | 继续维护原科学合同与未关闭出口，执行顺序指回现代化主计划 | 纠正“所有外部协作只限UI”的歧义，保留人类GUI交接范围；工程MCP仍按当前授权与能力路由 |
| **R2.2 数值与接口**，未启动 | W1核验G1／G2参考和指标；W2先运行现成CPU各组、按需补设备缺项；W3独立核对数值判定及新旧身份 | Codex唯一排程／集成，DeepSeek只读审查有界分工，Gemini仅冻结说明；CPU失败先定位，不扩成长程矩阵 |
| **R2.3 运行／编译／RAM**，未启动 | 只测已接受代表负载，记录缓存、端到端范围及资源；先评估异常慢和H5成本，再决定是否值得完整测量 | Codex串行测量；W2整理成本、W3审查公平性。正式CPU/CUDA配对仍缺测，不用本轮查询耗时替代 |
| **阶段3联合审计**，未启动 | 将可复现缺陷和实测高成本排序，决定修改、保留或后置 | Cellular速率／CUDA Hydro／RZ成本按各自证据处理，不提前归因或引入新抽象 |

本轮两项DeepSeek只读调查各有一次合并定点返修；采用范围经Codex核对CMake、manifest、原README、源码树及ELF。Gemini的冻结运行说明因数字邻接书写的校验问题返修一次，数值与条件保持后采用；没有改变网关。完整交付与失败元数据留在全局网关，项目只保留结论与引用。当前停止于R2.1，不自动开始R2.2。

<details>
<summary>机器身份与精确输入／命令</summary>

[BaselineIdentity-20261012.json](BaselineIdentity-20261012.json)保存树OID、完整ELF哈希、缓存身份、选定测试命令、10个manifest case的原预算及输入哈希。原176项源码清单摘要不替代当前完整Core树比较。协作回执：DeepSeek最终`ac3ccbaa51044e10b1e55cdfbfc8d2fc`（代表集，限定证据采用）、`e4228660e6244e02a3579504e5d24e04`（归档，限定证据采用）；Gemini最终`0a15f9c790d04e7ebbf13d60653f5e46`（运行说明，完整采用）。首轮交付及失败元数据均保留在同一全局网关。

本轮本地查询回执位于`output/modernization/r21-20261012/`，该目录忽略提交。

</details>
