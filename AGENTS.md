# ARCH 工作入口

工程实现、审查、验证或性能工作使用项目 `arch-engineering` skill；需要外部协助时结合全局 `multimodel-engineering`。沿用现有物理定义、数值标准和验收，安装这些规则不授权源码或架构优化。

## 本机路径与执行入口（2026-10-12 已核实）

- 本项目在 WSL Ubuntu 中执行，shell 为 `/bin/bash`。Windows C 盘文件通过 `/mnt/c/Users/26428/...` 访问，切勿将 `C:\Users\26428\...` 直接交给 Linux 命令。
- 当前工作区为 `/home/shiroakane/.codex/worktrees/d7a6/ARCH`，主仓库目录为 `/home/shiroakane/ARCH`。恢复上下文后先读取本文件和 `ARCH_RELEASE_PLAN.md`，需要现代化结案依据再查 `ARCH_MODERNIZATION_PLAN.md`，再用 `git status --short --branch`、`git worktree list` 确认实际分支和目录，不凭旧聊天推断。
- 若普通 `exec_command` 在启动进程前报 `CreateProcess ... No such file or directory (os error 2)`，不要反复替换 bash／PowerShell 路径。已验证的恢复入口为：`shell="/bin/bash"`、`login=false`、真实 WSL `workdir`，并通过工具审批使用 `sandbox_permissions="require_escalated"`。审批拒绝时按拒绝理由处理；本记录不授权绕过审批或永久关闭沙箱。
- Windows `node_repl` 会拒绝 `file:///home/...` 的 `sandboxCwd`，这不证明文件不存在。WSL 项目文件须通过上述 WSL 命令入口读取，不在该 Windows Node 会话中反复尝试 Linux 路径。常规沙箱启动与 Windows Node 路径兼容问题尚未根治。

## Python 与 Studio 工具环境

- Python 默认使用已有 conda `work` 环境。交互 shell 直接输入 `work`（已核实为 `conda activate work` 的别名，对应 Python 为 `/home/shiroakane/miniconda3/envs/work/bin/python`）。缺包可在这个环境内安装，不要重复搜寻或新建替代环境。
- 自动化命令需要别名时使用 `bash -ic 'work; python -c "import sys; print(sys.executable)"'`（非交互 bash 不保证加载该别名）；也可按既有环境指南显式初始化 conda 并激活 `work`。
- 已核实 `work` 的 Python 3.12.14、NumPy、h5py 可用。该 Python 当前缺少 `os.pidfd_open` 和 `signal.pidfd_send_signal`；资源护栏及必须使用这些 Linux 进程接口的工具沿用 `/usr/bin/python3`（已核实提供这两个接口）。此例外不改变科学 Python 默认使用 `work`，也不能用安装普通包代替标准库接口检查。
- Studio 使用 Linux Node 24+。本机现成入口为 `/home/shiroakane/.local/opt/node-studio/bin`（已核实 Node 24.21.0），运行 npm 前将整个目录前置到 `PATH`。切勿只调用该目录的 npm 而仍让其选中 `/usr/bin/node` 18。详细环境要求见 `docs/guides/StudioEnvironment.zh-CN.md`。

## 进度恢复

- **REL-3.5已按二维柱坐标全面退役的新范围签收；REL-4未启动。** 先读`ARCH_RELEASE_PLAN.md`与`validation/backend/results/release-readiness/RzRetirementAcceptance-20261018.json`，不要凭旧聊天重启RZ性能调查。当前源码已删除RZ专用文件、公共分支／传参／缓存及专用测试、输入和构建入口；不重新实现RZ。
- 所有`Cylindrical&&dim2`明确拒绝，含旧polar解释。保留八种合法组合、真Sphere2 Rphi和Cyl1／3，尤其不误删3D `CylindricalRZPhi`基底、物理`r_cy/z_cy`、三动量、三槽、通用mean P/c／EOS附件及普通守恒与资源生命周期。Cyl3轴线拼接误排除已经修复并通过原正例。
- 当前CPU `build-modernization-cpu/bin/ARCH` SHA256为`d3d8e07037627e83adcaa967716701e9a6d033efc913c521884e38a75f9da487`；同build根的Host比较器`arch_cuda_single_level_validation`为`bed49e522814100530599457ed8484f5c0d5c1d18fce9fd12d04cb2a3595845a`。本机原始记录和1207份源码冻结见`output/release/rz-retirement-implementation-20261016/`；最终为`source-freeze15.json`，推送结果见`push-receipt.json`。
- 本包CPU20项、GPU8项、Studio104项及212项相关工具短测通过；七组旧新两步Data／Blocks逐位一致、零修复。CUDA21个生产单元已编译，完整CUDA应用／网络矩阵／Store及公共fresh仍未签收，不能把本包当REL-4或完整release。
- REL-3、现代化R1–R8及旧RZ科学结果各保留原源码／执行物身份，索引分别见发布计划、`ARCH_MODERNIZATION_PLAN.md`和对应机器收据。42份冻结证据原字节、107份`docs/development/archive/histroy/rz/`归档的迁移冻结版本均未变（8份历史文档曾仅重定位链接）；旧NOT_MET／FAIL／GAP不改签，不重复旧RZ长程。
- 当前CPU／CUDA CMake源均为本工作区；CPU用GCC13／CUDA OFF，CUDA用GCC12／NVCC12.3／sm86，严格FP。CPU与CUDA的LTO库不能混用；旧主目录CTest只属于历史来源。重构建j2、CUDA重型pool1及护栏；科学短例OMP4并尊重owner明确OMP1。编译、科学运行和只读核验分开记账，冻结测量期间不编辑源码。
- 保持O7：活动关键键缺失或显式错误必须拒绝；25项登记辅助默认、具名模型和显式auto来源可追溯。未活动显式值仍为source=input，只有省略且无默认才为null。两份登记定义本包未改；不恢复旧Hydro／Cellular默认。Plot的source/build/profile身份检查保留；原生CHK没有build/source/profile gate，不推断新增规则。
- 原核质量／能量核对继续使用`preserve_total_precision=True`与`native_long_double_roundtrip_decimal`，不降精度或放宽原门槛。旧历史空复选框不等于待重跑项目；按原身份复用、分别记录失败／截断／未覆盖。

## 本地数据与执行证据

- 2026-10-13收到LFS流量预警后，三张表`helm_table.dat`／`eos2.tab`／`eos4.tab`及`studio/tests/fixtures/sod-1d.h5`均已从现有对象离线展开并核对当前指针SHA-256／字节数。独立普通文件副本固定在`/home/shiroakane/.local/share/arch-data/`下的同名仓库相对路径，`manifest.json`保留身份；不放入Git、build或output，不由LFS prune管理。新工作区先核对所需版本指针，再复用匹配副本／本地对象。
- 本仓库已配置`filter.lfs.smudge = git-lfs smudge --skip -- %f`与`filter.lfs.process = git-lfs filter-process --skip`，覆盖关联worktree，未改全局配置。读取本地表不消耗LFS流量；默认不自动下载。对象缺失明确报告路径与大小，不自动退回`git lfs fetch/pull`；确需下载另行限定路径和流量范围。保留LFS指针、来源与许可。离线命令见`docs/guides/Build.zh-CN.md`；当前CI缓存补丁见R3.1记录，尚未执行托管验收。
- 保存可执行基线须保留执行位并核对SHA；启动失败不算数值或性能样本。CTest枚举会覆盖可变`LastTest.log`，先保存旧引用原字节，新包使用独有日志。
- 本轮所有原始日志、集成前字节、失败与多模型产物保留在本机输出目录；历史冻结记录只链接，不重写。源码签收身份与提交号分开记录，不能把提交前执行物冒称提交后干净构建。

- 本地提交凭据检查使用公共Git目录中`arch-security/default-rules.toml`的可信策略；2026-10-18仅为两份历史科学收据的13条已重算SHA-256登记精确路径与完整行例外。恢复时见本包签收记录的`precommit_review`及本机`output/release/rz-retirement-implementation-20261016/security/`；新匹配需独立核实，不跳过钩子或扩大文件级排除。

## 运行与存储边界

- 运行明显异常缓慢时，记录已到达时刻／步数、耗时、退出原因和已得到的数值证据，标为待执行优化；可通过现有护栏受控停止本任务进程。截断不能记为 PASS，不改变原终点、误差门槛或物理定义。
- H5／checkpoint 使用实际宿主盘容量。本机 WSL Ubuntu 的 VHD 位于 `E:\WSL\Ubuntu`，对应宿主挂载 `/mnt/e`；Linux `/` 的虚拟可用空间不能替代宿主余量。开始 H5 批次前、预计明显增长或出现 I/O 异常时核对；约剩 **1 GiB** 时预警并停止新增大输出，不高频反复查盘。路径／机器改变时重核映射。
- 2026-10-12 实查 WSL 可见内存约 7.7 GiB，GPU 为 RTX 3060 Ti 8 GiB；历史 16 GB 工作站目标及可选 4070 Ti 记录不等于本轮实际可用资源。沿用现有资源护栏；重型构建、GPU与正式计时分时，独立轻任务按实测预算并行。

## 文件归属与注释

- 公共数值／状态头检查传递依赖和实际解引用消费者：DiffFlux不再通过helper导入具体引力owner，IHydroSolver不含RuntimeParams，FluidState只前声明边界storage；实际消费者直接include定义。现有架构审计覆盖这些边界。前声明不等于附件所有权已迁移；三槽共享／回滚／regrid生命周期仍保持，配置parser与共享求积归属收窄按主计划后置，不为形式整齐扩大重构。
- 几何与引力职责严格分离：GridMetrics／GeometricSources／HydroGeometryBinding只管度量、坐标／边界与几何源项；引力的实际场、来源选择、patch claim、做功、能量修正与receipt完成归physics/gravity。Host Hydro组合层保留既定时序，AMR只通过可选通用Energy receipt登记，不识别具体引力帧或势公式；Device的场借用与执行适配仍由后端组合层负责。关闭引力必须能独立运行几何路径，仍执行EOS／边界与守恒检查；未知活动引力必须拒绝。二维柱坐标专属环核／AGM／boundary memo已退役，不恢复；其余受支持坐标的物理守恒耦合仍归实际physics owner。

- 后续现代化改动无需保持向后兼容：旧接口、配置格式、脚本、构建入口及重复设施可直接退役，不为旧调用方保留兼容层、别名或双轨实现；用户已授权，无需为兼容性再次请求批准。同步更新当前消费者、构建、测试与文档；不再支持的输入／重启产物明确拒绝，禁止静默转换或补默认。数值正确性、物理定义和验收标准保持；历史证据保留来源与适用身份。详见主计划“目标与硬约束”第4项。
- 新增、拆分或移动文件前，按 `docs/development/ImplementationOwnership.md` 核对目录、文件职责、调用方和构建归属；优先复用现有所有者，不以文件数或文件大小决定拆分。
- 冻结行为的源码注释按 `docs/development/CommentAndDocumentationStyle.md` 说明职责、流程、数学约定和资源生命周期。原始结果、签收记录及锁定字节的文件保留原文；已授权的历史说明可纠正、合并或归档，并保留来源、数值与失败记录；注释改善不授权数值、接口或验收变化。
