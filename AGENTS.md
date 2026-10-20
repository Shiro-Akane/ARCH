# ARCH 工作入口

工程实现、审查、验证或性能工作使用项目 `arch-engineering` skill；需要外部协助时结合全局 `multimodel-engineering`。沿用现有物理定义、数值标准和验收，安装这些规则不授权源码或架构优化。

## 本机路径与执行入口（2026-10-20 Windows 原生模式已核实）

- Codex 工具宿主当前为 Windows 原生，执行入口用 `powershell.exe`、`login=false` 和真实 Windows 工作目录，例如 `C:/Users/26428`。`C:/home/shiroakane/.codex/worktrees/d7a6/ARCH` 是错误映射，不存在；不要创建同名镜像或将其当作仓库。
- 实际仓库仍在 WSL Ubuntu：`/home/shiroakane/.codex/worktrees/d7a6/ARCH`，主目录 `/home/shiroakane/ARCH`。Windows 可通过 `\\wsl.localhost\Ubuntu\home\shiroakane\.codex\worktrees\d7a6\ARCH` 访问；Linux 命令显式使用 `wsl.exe -d Ubuntu -u shiroakane --exec ...`。Windows 工具宿主不代表 ARCH 已获 Windows 原生计算资格，科学运行沿用已验收 Linux／WSL2。
- 恢复后先读本文件与 `ARCH_RELEASE_PLAN.md`，再显式在真实仓库核对 Git 状态、分支及 worktree。普通沙箱若对 WSL／UNC 报 `Wsl/E_ACCESSDENIED` 或返回不可访问，按工具审批后重核，不据此认定仓库丢失；不在 Windows 反复尝试 `/bin/bash` 作为工具 shell。
- 多行脚本和含 Markdown 反引号、美元符号的文本，通过 UTF-8 无 BOM 脚本／数据文件传递，再由 `wsl.exe --exec /usr/bin/python3 /mnt/c/.../script.py` 执行。不要用跨 shell 的 `wsl.exe -- python -c` 内嵌这类文本；本轮已复现默认 shell 对反引号的误解析，修正草稿后才派发。
- 全局多模型 MCP 未出现在本窗口时，Windows 显式调用既有 WSL `gateway.py`，不建立第二套队列。旧桥接固定使用 Windows PowerShell 5.1；本轮原生宿主的模块环境会使它找不到 `Get-FileHash`。调用该桥接的子进程可将 `PSModulePath` 限定为现有 `C:/WINDOWS/system32/WindowsPowerShell/v1.0/Modules` 并用 `WSLENV` 转发，保留原严格 profile／agent SHA 检查；只改本任务进程环境，不修改全局注册、登录、权限或计费策略。证据 `output/release/rel7-20261020/Gemini-process-environment-recovery.json`。

## Python 与 Studio 工具环境

- Python 默认使用已有 conda `work` 环境。交互 shell 直接输入 `work`（已核实为 `conda activate work` 的别名，对应 Python 为 `/home/shiroakane/miniconda3/envs/work/bin/python`）。缺包可在这个环境内安装，不要重复搜寻或新建替代环境。
- 自动化命令需要别名时使用 `bash -ic 'work; python -c "import sys; print(sys.executable)"'`（非交互 bash 不保证加载该别名）；也可按既有环境指南显式初始化 conda 并激活 `work`。
- 已核实 `work` 的 Python 3.12.14、NumPy、h5py 可用。该 Python 当前缺少 `os.pidfd_open` 和 `signal.pidfd_send_signal`；资源护栏及必须使用这些 Linux 进程接口的工具沿用 `/usr/bin/python3`（已核实提供这两个接口）。此例外不改变科学 Python 默认使用 `work`，也不能用安装普通包代替标准库接口检查。
- Studio 使用 Linux Node 24+。本机现成入口为 `/home/shiroakane/.local/opt/node-studio/bin`（已核实 Node 24.21.0），运行 npm 前将整个目录前置到 `PATH`。切勿只调用该目录的 npm 而仍让其选中 `/usr/bin/node` 18。详细环境要求见 `docs/guides/StudioEnvironment.zh-CN.md`。
- 本机未安装`tsx`；本轮Studio Host只读`.ts`脚本已由上述Node24原生入口直接执行通过，不要假定存在`studio/node_modules/tsx/dist/loader.mjs`。UI／JSX沿现有项目构建入口。

## 进度恢复

- **REL-6已闭合；REL-7已由用户明确授权，v1.3.0 发布准备进行中。** 唯一计划 `ARCH_RELEASE_PLAN.md`；原科学依据为 `validation/backend/results/release-readiness/REL6CandidateAcceptance-20261020.json`，当前恢复 `output/release/rel7-20261020/progress.json`。生产源码 `7e1dca167bd5a348fbc1ba252de581a548cf40fa`，入场记录提交 `7fa1b7a78ca0a459c8d88e79782142fcb3335bcb`；实际分支以 Git 核对为准。本轮用户授权正常提交／推送、合并 PR#12 至 main、创建 v1.3.0 标签并按历史源码模式正式发布，取代 REL-6 当时的停止边界；不授权强推、改签旧证据或降低数值标准。PR[#12](https://github.com/Shiro-Akane/ARCH/pull/12)已关联当前任务；合并／发布状态必须实时核对。旧科学产物与后续记录提交分开记身份，托管只认当前 head 的适用 job／汇总门／CodeQL。Windows 当前 GCM 尚无既有 GitHub 写入凭据，已提供官方 GitHub 连接入口；不重复自动登录或索取令牌。 main 的公开规则要求 PR／CI 并禁止删除和非快进更新；正常经 PR 合入，不改规则或走管理员绕过。本次受控 workflow 可在完整验收后用托管临时凭据调用正常 PR 合并、显式 main CI、标签与源码发布；不导出凭据、不绕过保护规则。main 的全文件树与已扫描 head 必须完全相同，安全证据明确继承，实际 main CI 另记身份。
- **当前实际候选入口：** 从归档`output/release/rel6-20261020/ARCH-candidate-7e1dca167.tar.gz`在`/tmp/arch-rel6-20261020/source/ARCH`干净配置、无缓存构建；CPU／CUDA图分别为其中`build-cpu`／`build-cuda`。1117份源码／测试／配置及25份外部生成网络文件在末尾一致，无`.git`目录；使用归档／清单溯源，不冒称执行了需要Git的验证CLI主入口。实际持久安装在`output/release/rel6-20261020/relocated-cpu`／`relocated-cuda`，各14文件，分别从自身`share/arch`运行；CPU安装SHA`3cfa5b15f10f…`，CUDA`7378407973aa…`，构建ELF分别`c9a41a1b1518…`／`f645036d5075…`。安装只移除RUNPATH，代码等其他文件节完全相同，不能要求安装前后整个ELF摘要相同。临时目录丢失先从持久归档和匹配本地资产恢复，不回退旧ELF充当候选。
- 原高压近零动量失败已用公共stage压力平衡定点修正；原输入与rphi对照在两后端均精确零动量，原比较门通过，旧`NOT_MET`保持历史身份。Cyl1/2/3与rphi复用已有P/c缓存，Ideal CUDA仅这些chart增加16字节／存储单元；完整面通量、reflux、T/W和引力做功不改。旋转角动量必须按原`DeltaJ+outward_torque-body_torque`预算验证，不能对有壁面粘性扭矩的算例错误要求`DeltaJ=0`；当前最大归一残差`7.31e-17`，原`1e-12`保持，四组fresh差异零。Helm反应算例实际热通道非零，不能由三个开关推断粘性／组分都活动。
- **SNIa用户决议：** 2026-10-20直接授权“允许联合证据，保留旧 FAIL”。候选采用原主终点／守恒／fresh与已获准L2重复重网格机制联合证据；旧主算例一次重网格的`FAIL`及thermal `GAP`仍原样保留。当前安装CPU20步／自身fresh及另两步4个接受热扩散half只是补充，不冒称重跑原主终点。原私有Cellular24步／25CHK ENUC及两后端自身fresh沿REL-4适用证据复用，不重跑长程；16模型逐项归属与适用范围查REL-6收据，不能从旧输入目录自行扩大声明。
- 实际`custom:nse_alpha`（pynucastro2.12.0）三ODE／NSE启停20次执行、10对比较与能量／电荷闭合通过。完整轨迹使用CUDA安装包的CPU／GPU模式，独立CPU包另验Helm短跑，不混淆编译器／ELF范围。受影响原生PPM／RKL解析终点与原收敛门通过；热点4.1ns功能／fresh不等同独立点火机制证明。原O7、`5e-9`＋`5e-12`源项及各自`1e-12`角动量／质量／电荷标准保持；`SimConfig::Get`第二参数为审计元数据，未活动显式值仍为input，无默认省略才为null。
- Studio当前388项零跳过、lint、typed build、实际Linux包Configure／Build→真实Preview／初始AMR→Run→Restart→Plot／Inspector及拒绝／关闭恢复通过；10→104步到原终点12组物理／网格逐值相同。160份包内第一方文件核身份，56份GUI数据／运行记录已持久保存；UI没有获得科学资格判定权。`npm test`仍唯一持有完整选择、TAP和文件并发1。REL-5历史383项、CTest CPU75→68／CUDA142→135及619Tooling不反写；原593ID／9迁移方法／新增控制保持，`NOT_APPLICABLE`不是测试PASS。
- 成本只查REL-6`cost-ledger.json`：CPU干净GNU172.62秒、代表重编译34.67秒、纯重链26.86秒；CUDA初次3600秒护栏截断124，原图续建686.63秒完成，初次不记PASS。GNU3689／704秒与护栏差异、续建与27秒约94MiB CPU诊断重叠保留，不能假报隔离连续cold样本或提速。CUDA代表重编译／纯重链外层42.24／30.56秒，未观察到swap增长；完整全路由编译吞吐为独立后续优化项。原生／rphi匹配成本CPU约1.06–1.10、GPU1.01–1.07，通过原2倍和CPU绝对锚点；小规模方差不支持稳定提速声明。
- 原CPU／CUDA ELF`fe09bf2e251a…`／`b1143d8aa6a2…`、Host比较器`13e91fba6e38…`及REL-5本地CPU`ff8da693…`末尾核摘要／执行位不变。旧`build-modernization-*`只作原证据，不给当前源码签收；REL-4完整身份／原72.11／3040.45秒增量成本查原收据。原323归档、157冻结历史、3475结果及协议保持，不运行旧REL-4组装／核验脚本覆盖旧收据。`run_memory_guarded.py --log`为子进程正文加护栏文本，stdout主要是资源摘要，不因旧`.guard.json`后缀按JSON解析或当TAP。
- 当前候选CPU为Release／GCC13／CUDA OFF，CUDA GCC12／NVCC12.3／sm86严格FP；重型j2／CUDA heavy1，科学OMP4并尊重owner明确OMP1，LTO对象不跨图。旧本地图的共享SuiteSparse生成头和用户ccache普通沙箱可能拒绝写入，不能记源码失败。Runtime安装不是任意机器独立闭包，HDF5／OpenMP／CUDA／可选cuDSS仍按指南具备；本轮只声明Linux／WSL2单RTX3060Ti及可选Linux Studio，不扩大Windows原生、多GPU或未验架构。
- 新chart仍为revision1 `cylindrical-rz-pwc`、轴`r,z,inactive`，`mom_w`为方位动量，`J=sum(mom_w*W)`、全环质量`2*pi*V`、Poisson`-4*pi*G*rho`。几何／可选引力解耦，关闭时零求解／专属数组，不恢复Rz生产设施／AGM／memo／savepoint／GlobalDefs字段。模型用Grid的`(r,0,z)`点，`center_y=0`、`center_z`显式必需；旧chart及非法活动路由拒绝。原生CHK不增加build/source/profile门；核质量／能量保持`preserve_total_precision=True`与`native_long_double_roundtrip_decimal`。

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
