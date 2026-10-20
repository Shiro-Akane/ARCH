# ARCH 工作入口

工程实现、审查、验证或性能工作使用项目 `arch-engineering` skill；需要外部协助时结合全局 `multimodel-engineering`。沿用现有物理定义、数值标准和验收，安装这些规则不授权源码或架构优化。

## 本机路径与执行入口（2026-10-12 已核实）

- 本项目在 WSL Ubuntu 中执行，shell 为 `/bin/bash`。Windows C 盘文件通过 `/mnt/c/Users/26428/...` 访问，切勿将 `C:\Users\26428\...` 直接交给 Linux 命令。
- 当前工作区为 `/home/shiroakane/.codex/worktrees/d7a6/ARCH`，主仓库目录为 `/home/shiroakane/ARCH`。恢复上下文后先读取本文件和 `ARCH_RELEASE_PLAN.md`，需要现代化结案依据再查 `ARCH_MODERNIZATION_PLAN.md`，再用 `git status --short --branch`、`git worktree list` 确认实际分支和目录，不凭旧聊天推断。
- 若普通 `exec_command` 在启动进程前报 `CreateProcess ... No such file or directory (os error 2)`，不要反复替换 bash／PowerShell 路径。已验证的恢复入口为：`shell="/bin/bash"`、`login=false`、真实 WSL `workdir`，并通过工具审批使用 `sandbox_permissions="require_escalated"`。审批拒绝时按拒绝理由处理；本记录不授权绕过审批或永久关闭沙箱。
- Windows `node_repl` 会拒绝 `file:///home/...` 的 `sandboxCwd`，这不证明文件不存在。WSL 项目文件须通过上述 WSL 命令入口读取，不在该 Windows Node 会话中反复尝试 Linux 路径。2026-10-19本轮普通沙箱的`/bin/bash`、`login=false`和真实WSL工作目录已可启动，优先使用普通入口；仅复现启动错误时再按上条恢复，不宣称全局兼容问题已永久修复。

## Python 与 Studio 工具环境

- Python 默认使用已有 conda `work` 环境。交互 shell 直接输入 `work`（已核实为 `conda activate work` 的别名，对应 Python 为 `/home/shiroakane/miniconda3/envs/work/bin/python`）。缺包可在这个环境内安装，不要重复搜寻或新建替代环境。
- 自动化命令需要别名时使用 `bash -ic 'work; python -c "import sys; print(sys.executable)"'`（非交互 bash 不保证加载该别名）；也可按既有环境指南显式初始化 conda 并激活 `work`。
- 已核实 `work` 的 Python 3.12.14、NumPy、h5py 可用。该 Python 当前缺少 `os.pidfd_open` 和 `signal.pidfd_send_signal`；资源护栏及必须使用这些 Linux 进程接口的工具沿用 `/usr/bin/python3`（已核实提供这两个接口）。此例外不改变科学 Python 默认使用 `work`，也不能用安装普通包代替标准库接口检查。
- Studio 使用 Linux Node 24+。本机现成入口为 `/home/shiroakane/.local/opt/node-studio/bin`（已核实 Node 24.21.0），运行 npm 前将整个目录前置到 `PATH`。切勿只调用该目录的 npm 而仍让其选中 `/usr/bin/node` 18。详细环境要求见 `docs/guides/StudioEnvironment.zh-CN.md`。
- 本机未安装`tsx`；本轮Studio Host只读`.ts`脚本已由上述Node24原生入口直接执行通过，不要假定存在`studio/node_modules/tsx/dist/loader.mjs`。UI／JSX沿现有项目构建入口。

## 进度恢复

- **REL-5已闭合，REL-6已由用户启动，REL-7未启动。** 当前恢复`output/release/rel6-20261020/progress.json`；旧REL-5记录保持原签收范围。 唯一计划`ARCH_RELEASE_PLAN.md`；收据`validation/backend/results/release-readiness/REL5DocumentationAndTestConsolidation-20261019.json`，恢复`output/release/rel5-20261019/progress.json`。分支`compute/optim`，完整源码签收`21ca1cc6721a`／真实运行`39243412776`，Tooling／完整CPU库存＋低密度矩阵／完整Studio测试与lint、typed build及汇总均通过。随后只同步记录的提交不冒称重新编译。原失败及其定点修正留收据；323份归档、157份冻结历史与3475份原结果保持。用户已授权提交、正常推送和main草稿PR；不合并／强推／打标签／正式发布或自动进入REL-7。PR[#12](https://github.com/Shiro-Akane/ARCH/pull/12)已由用户创建并关联当前任务；Draft／Ready状态由GitHub实时核对，不凭旧截图推断。2026-10-20四处CodeQL定点修正及受影响CPU／CUDA测试见整理收据的`post_closure_followups`，托管验收只认PR当前提交。Linux无GitHub API写凭据，Windows Node的WSL入口失败；不反复操作登录／安全设置或下载需鉴权日志。
- CTest仅将八个Python wrapper收为唯一`tooling_contracts`，CPU75→68、CUDA142→135，其余命令／属性逐项相同。完整Tooling当前619项（原618全部保留，新增当前Plot身份／三速度拒绝控制）、Studio383项零跳过、lint／typed build通过；原593个工具ID及9个迁移方法均保留，新增17个控制。`npm test`唯一持有Node完整选择、TAP与文件并发1。当前文档／架构／actionlint本地通过；非适用范围的`NOT_APPLICABLE`不是测试PASS。成本统一查本轮`cost-ledger.json`；完整CI成本已记录，尚无同条件提速证明。
- 136个Studio改动文件生成JS语法树等价，20文件仅排版；593份C++／CUDA及inc有归属索引，20份C++／CUDA改动的非注释token相同。本地保留的原CPU／CUDA应用`fe09bf2e251a…`／`b1143d8aa6a2…`和Host比较器`13e91fba6e38…`未重编；托管CPU已按上述完整源码提交干净构建并完整测试通过；注释改变源字节／BuildId，不能把旧无动作构建或旧ELF冒称新源码编译签收。REL-4完整身份和原72.11／3040.45秒增量构建成本只查原收据；新本地CPU测试入口为`output/release/rel5-20261019/hosted-admission/current-cpu/bin/ARCH`（ff8da693…，编译前脏源码冻结身份），CMake的ARCH输出已指向该路径；旧`build-modernization-cpu/bin/ARCH`仅保留原证据。79动作GNU237.16秒（护栏235.18秒），4个既有CTest和短fresh通过；不能将其冒称提交后的干净构建。
- REL-3／4及二维柱坐标重植保持其原范围闭合；依次查`REL3CurrentCpuClosure-20261019.json`、`REL4CudaDeviceAndIntegration-20261019.json`及CYL-0至6七份收据。原私有Cellular24步／25CHK ENUC与两后端自身fresh、受影响旋转AMR已按原门通过，不重跑长程。原高压近零动量parity `NOT_MET`、SNIa main AMR `FAIL`／thermal `GAP`仍限制发布声明，不由设备或缩放正例转签。
- 保持O7及原`5e-9`＋`5e-12`源项门、`1e-12`角动量、质量／电荷等各自原标准。`SimConfig::Get`第二参数是审计元数据，不授权关键缺项回退；未活动显式值仍为input，省略且无默认才为null。新chart为revision1 `cylindrical-rz-pwc`、轴`r,z,inactive`，`mom_w`为方位动量，`J=sum(mom_w*W)`、全环质量`2*pi*V`、Poisson`-4*pi*G*rho`；几何／可选引力解耦，关闭时零求解／专属数组，不恢复Rz生产设施／AGM／memo／savepoint或GlobalDefs字段。模型使用Grid的`(r,0,z)`点，`center_y=0`、`center_z`显式必需；旧chart／非法活动路由拒绝。原生CHK不新增build/source/profile gate。核质量／能量保持`preserve_total_precision=True`与`native_long_double_roundtrip_decimal`。
- 冻结157份历史及原科学产物、协议夹具和两个旧ELF保留原字节／权限；3475份入场结果文件均已重新对照独立备份。不运行旧REL-4组装／最终核验脚本覆盖其收据。新CI固定资产owner在`.github/actions/lfs-asset/`：Helm旧cache key保持，Studio14KiB Sod夹具同样定向物化／验大小和SHA；本地五项控制及实际离线物化通过，托管资产准备通过；日志无权下载，具体冷／热命中与RAM未知。本机仅复用已校验数据，禁止自动LFS拉取。`run_memory_guarded.py --log`是子进程正文加护栏的文本，外层stdout主要是资源摘要；不要因旧私有`.guard.json`后缀误按JSON解析或拿它当TAP。
- 两个build的真实CMake源均为本工作区；CPU Release／GCC13／CUDA OFF，CUDA GCC12／NVCC12.3／sm86严格FP。LTO库不可跨图混用，旧主目录CTest不签当前源码。重型j2、CUDA heavy pool1及现有护栏，科学OMP4并尊重owner明确OMP1；计时窗口不编辑相应源码。本地图沿用主仓库SuiteSparse生成头目录和用户ccache，普通沙箱会拒绝它们的写入；定点编译应通过工具审批执行原图，运行时普通入口仍可用，不能把该拒绝记作源码编译失败。前置本批输出预算因已有独立备份＋512MiB预留由512调至1024MiB；宿主1GiB边界不变。

REL-6本轮恢复：`output/release/rel6-20261020/progress.json`。公共柱坐标／rphi常压抵消定点修正已过CPU曲线坐标及真实CUDA叶测试，原高压应用门尚待冻结候选重验；Ideal CUDA仅这些chart启用已有P/c缓存（16字节／存储单元），其余不扩张。Runtime安装规则与实际生成网络已准备；Studio参数分隔符和已销毁窗口关闭修正已过实际包及388项测试。不得把这些预检写成候选签收；SNIa联合准入问题等待用户决定，原FAIL保持。不自动进入REL-7。

## 本地数据与执行证据

- 2026-10-13收到LFS流量预警后，三张表`helm_table.dat`／`eos2.tab`／`eos4.tab`及`studio/tests/fixtures/sod-1d.h5`均已从现有对象离线展开并核对当前指针SHA-256／字节数。独立普通文件副本固定在`/home/shiroakane/.local/share/arch-data/`下的同名仓库相对路径，`manifest.json`保留身份；不放入Git、build或output，不由LFS prune管理。新工作区先核对所需版本指针，再复用匹配副本／本地对象。
- 本仓库已配置`filter.lfs.smudge = git-lfs smudge --skip -- %f`与`filter.lfs.process = git-lfs filter-process --skip`，覆盖关联worktree，未改全局配置。读取本地表不消耗LFS流量；默认不自动下载。对象缺失明确报告路径与大小，不自动退回`git lfs fetch/pull`；确需下载另行限定路径和流量范围。保留LFS指针、来源与许可。离线命令见`docs/guides/Build.zh-CN.md`；当前CI资产实现见`.github/actions/lfs-asset/`及`.github/workflows/README.md`，托管资产准备已通过，具体冷／热命中率未采集。
- 保存可执行基线须保留执行位并核对SHA；启动失败不算数值或性能样本。CTest枚举会覆盖可变`LastTest.log`，先保存旧引用原字节，新包使用独有日志。
- 本轮所有原始日志、集成前字节、失败与多模型产物保留在本机输出目录；历史冻结记录只链接，不重写。源码签收身份与提交号分开记录，不能把提交前执行物冒称提交后干净构建。

- 本地提交凭据检查使用公共Git目录中`arch-security/default-rules.toml`的可信策略；2026-10-18仅为两份历史科学收据的13条已重算SHA-256登记精确路径与完整行例外。恢复时见本包签收记录的`precommit_review`及本机`output/release/rz-retirement-implementation-20261016/security/`；新匹配需独立核实，不跳过钩子或扩大文件级排除。

## 运行与存储边界

- 运行明显异常缓慢时，记录已到达时刻／步数、耗时、退出原因和已得到的数值证据，标为待执行优化；可通过现有护栏受控停止本任务进程。截断不能记为 PASS，不改变原终点、误差门槛或物理定义。
- H5／checkpoint 使用实际宿主盘容量。本机 WSL Ubuntu 的 VHD 位于 `E:\WSL\Ubuntu`，对应宿主挂载 `/mnt/e`；Linux `/` 的虚拟可用空间不能替代宿主余量。开始 H5 批次前、预计明显增长或出现 I/O 异常时核对；约剩 **1 GiB** 时预警并停止新增大输出，不高频反复查盘。路径／机器改变时重核映射。
- 2026-10-12 实查 WSL 可见内存约 7.7 GiB，GPU 为 RTX 3060 Ti 8 GiB；历史 16 GB 工作站目标及可选 4070 Ti 记录不等于本轮实际可用资源。沿用现有资源护栏；重型构建、GPU与正式计时分时，独立轻任务按实测预算并行。

## 文件归属与注释

- 公共数值／状态头检查传递依赖和实际解引用消费者：DiffFlux不再通过helper导入具体引力owner，IHydroSolver不含RuntimeParams，FluidState只前声明边界storage；实际消费者直接include定义。现有架构审计覆盖这些边界。前声明不等于附件所有权已迁移；三槽共享／回滚／regrid生命周期仍保持，配置parser与共享求积归属收窄按主计划后置，不为形式整齐扩大重构。
- 几何与引力职责严格分离：GridMetrics／GeometricSources／HydroGeometryBinding只管度量、坐标／边界与几何源项；引力的实际场、来源选择、patch claim、做功、能量修正与receipt完成归physics/gravity。Host Hydro组合层保留既定时序，AMR只通过可选通用Energy receipt登记，不识别具体引力帧或势公式；Device的场借用与执行适配仍由后端组合层负责。关闭引力必须能独立运行几何路径，仍执行EOS／边界与守恒检查；未知活动引力必须拒绝。旧二维柱坐标专属文件、AGM控制栈和boundary memo已退役，不恢复；所需全环Newton数学只扩展现有gravity owner，其余坐标的物理守恒耦合保持。

- 后续现代化改动无需保持向后兼容：旧接口、配置格式、脚本、构建入口及重复设施可直接退役，不为旧调用方保留兼容层、别名或双轨实现；用户已授权，无需为兼容性再次请求批准。同步更新当前消费者、构建、测试与文档；不再支持的输入／重启产物明确拒绝，禁止静默转换或补默认。数值正确性、物理定义和验收标准保持；历史证据保留来源与适用身份。详见`ARCH_RELEASE_PLAN.md`“发布目标与判定”。
- 新增、拆分或移动文件前，按 `docs/development/ImplementationOwnership.md` 核对目录、文件职责、调用方和构建归属；优先复用现有所有者，不以文件数或文件大小决定拆分。
- 冻结行为的源码注释按 `docs/development/CommentAndDocumentationStyle.md` 说明职责、流程、数学约定和资源生命周期。原始结果、签收记录及锁定字节的文件保留原文；已授权的历史说明可纠正、合并或归档，并保留来源、数值与失败记录；注释改善不授权数值、接口或验收变化。
