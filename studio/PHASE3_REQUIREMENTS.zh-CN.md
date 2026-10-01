# ARCH Studio：Linux/WSL 本地全 Core 工作流需求

状态：下一阶段统一需求稿，供 Core、Host/Desktop、Studio 协作与逐项验收。本文件描述目标及交付边界，不表示这些功能已经全部实现。

## 1. 目标与基线

ARCH Studio 要成为本地科研工作站上使用 ARCH Core 的完整图形工作入口。用户可以从 Linux/WSL 终端直接打开独立窗口，选择或编写 case，载入 `.par`，检查和修改参数，查看真实初始状态及 AMR，受控构建，然后在独立终端运行或从 checkpoint 重启；运行结果再由只读 Plotfile 视图检查。Studio 应覆盖**当前 Core 实际支持的全部配置、模型、计算选项和本地工作流**，并持续跟随 Core 新增能力。完成几个示例模型的演示不等于完成全 Core 支持。

本轮以 **Linux/WSL 个人 PC 与 workstation** 为交付环境。Windows 启动、SSH/cluster、调度器、服务器后台运行及运行中 in-situ 流场传输不进入本轮。Preview 可以使用 CPU；真正 Run 依照所选 Core 二进制及其能力运行，GUI 打包不得让普通 headless Core 构建强制依赖 Electron/Node，也不得自动触发耗时 GPU 编译。

基线核对时间：2026-09-28。远端 `origin/main` 与本地 main 均为 `25adec4224497981a0c124a3485f786194975be4`；Studio review checkpoint 是 `studio-phase2h-v0.19.0`（`840ab538f676f9b898a4680acaabc80201b13647`）。当前 main 不追踪 `studio/`，因此新实现分支应以**最新 `origin/main` 为基础**，再引入已审查的 Studio 内容；不要把旧 Studio 分支的 Core 文件整体覆盖新 main。每次交接前重新 `git fetch origin main`、记录新的 `origin/main` SHA，并复验接口和能力；本文的 SHA 只是本次核对记录，不是永久固定版本。

当前 Core 二进制报告 **95 个标准参数、14 个注册 case**。注册模型都可发现并执行初始化检查；完整初始场和初始 AMR 目前只开放 Sod 1D 与 CellularDet 2D。当前 Studio 有真实 `.par` 编辑、未保存内容 Preview、Controlled Build、初始 AMR 及状态恢复，但 Linux/WSL 独立窗口 CLI、真实 Run/Restart 与大型 AMR Plotfile 查询尚未完成。当前 WSL 的 `arch-studio` 脚本实际转调 Windows `.exe`；这个 `.exe` 是 GUI 启动器，不是原生 Windows Core。

## 2. 全 Core 支持的含义

“全功能支持整个 Core”按下列清单验收，不以手写两个示例的控件替代通用能力：

| 范围 | 最终可见行为 | 权威来源与验收方式 |
|---|---|---|
| 标准参数 | 每个当前 Core 标准键均可查找、查看默认/显式/有效值、按类型编辑、保存并重新载入；兼容别名汇入同一目标控件 | 运行时读取 `--config-schema` 和 `--inspect-config`；把 UI 可达键集合与 Core schema 比对。新增 Core 键至少有通用控件，不因 Studio 未写专门页面而消失 |
| 配置分组 | Grid、EOS、Network、Gravity、Diffusion、Runtime 和其内的 AMR、输出、restart 等内容均可找到，适用条件随当前 Working Copy 更新 | Core 的 group、presentation、options、applicability、diagnostics 为依据；UI 只决定显示顺序与交互形式 |
| 已注册模型 | 当前二进制 `--list-cases` 返回的每个 case 都可选择、核对源码/二进制身份、检查初始化，并得到与其物理维度相符的初始状态呈现 | 不在前端硬编码模型总数或名单。完整图像需 Core 提供通用或逐模型的真实采样能力；非空间模型应使用适当的状态视图，不伪造二维图 |
| 初始 AMR | 凡 Core 能实际构造初始网格的注册模型，都可显示真实块层级、位置和完整性；限制预算导致截断时清楚标记 | Core AMR 结果是权威；图上叠加必须匹配同一 model、配置、构建和 EOS 身份。资源估算不是 OOM 预言 |
| Core 计算能力 | 用户可以配置 Core 已支持的求解器、积分器、EOS、网络、重力、扩散、AMR、CPU/CUDA 计算后端、输出和 checkpoint/restart | 可选项与适用性来自当前 Core，禁用原因可见；配置检查不冒充运行就绪，Preview 不初始化 CUDA |
| 本地执行 | GUI 可以受控 Configure/Build、在独立前台终端 Run、从 checkpoint Restart，并查看构建和运行输出 | 真实 Core CLI 仍是 `ARCH <ProblemType> <ParFile>`；Restart 由配置与 checkpoint 契约控制，不发明一个 Core 不认识的命令行开关 |
| 输出检查 | Studio 可只读打开当前 Core 写出的完整 Plotfile，并按实际字段、单位、AMR 和时刻查询 | 完整 HDF5 是科学数据权威，Studio 显示可降采样，Inspector 回查原始 cell；实现依赖后续 Core/IO/Host 接口 |

全覆盖是**最终验收目标**，可分批实现，但每个里程碑都需报告尚未覆盖的 case、维度、参数或 Core 能力，不以“registered”“可导入 `.par`”或“可打开旧图像”代替完整 Preview/Run 支持。未注册或尚未编译的任意 `.cpp` 可以被阅读、编辑和构建；在新二进制确认注册与能力之前，不能宣称已有真实场图。

## 3. 本地启动、项目与文件身份

主入口应为 Linux/WSL 命令 `arch-studio`，从当前目录向上发现 ARCH 项目，也可显式指定：

```bash
arch-studio --case Sod --config simulation/Sod/Sod.par
arch-studio --project /path/to/ARCH --case CellularDet --config simulation/Cellular/CellularPreview2D.par
arch-studio --project /path/to/ARCH --source simulation/NewCase/NewCase.cpp --config simulation/NewCase/NewCase.par
```

还应允许显式 `--binary`。`ARCH --studio` 可以作为日后别名，但不得为打开 GUI 而初始化 simulation、CUDA 或输出目录。命令直接打开独立窗口；Host、前端资源和随机 loopback 端口由 launcher 内部管理，不要求用户运行 npm/Vite、复制 IP/端口或指定默认浏览器。可选 CMake Studio 构建/安装目标应产出互相匹配的 Core、Host、前端资源与 Linux launcher；普通 Core/headless 构建不受 GUI 依赖影响。用户安装版不得依靠某台开发机的固定目录或 Build Profile。

窗口醒目显示当前项目、注册 case、case 源码、`.par` 的完整文件名及路径、所用二进制和构建状态。`.par` 可独立选择；文件名/位置与 case 不符时给出提示和正确文件的切换入口，但不靠命名规则禁止用户继续。覆盖保存必须显示明确目标并获得本次确认。编辑 `.cpp` 后立即标记二进制可能过期；未完成编译时仍可编辑 `.par` 和看配置诊断，但真实 Preview/Run 必须按能力与身份判定。源码查看/编辑/保存、`.par` 工作副本与 Build 身份应能衔接，不由 GUI 猜测源码已经进入旧二进制。

Studio 只关闭它拥有的 Host 和 Preview worker。Run/Restart 的独立终端与进程组不属于 Preview 取消范围；关闭 Studio 或折叠日志不能暗中终止仍在可见终端运行的 simulation。当前阶段没有服务器后台作业、守护进程或远程调度功能。

## 4. 工作区布局与参数编辑

建议布局：顶部是项目/模型/文件/构建身份；左侧是参数；中央是初始预览和 AMR；右侧是选中项 Inspector；底部是一条始终可见的小型工作流栏。Real Config 不应因为切到真实模式就丢失原先在 Mock 页底部的操作入口。

左侧按 `Grid → 本组字段 → EOS → 本组字段 → Network → Gravity → Diffusion → Runtime` 的顺序就地排列。每组可折叠并保留展开状态，收起时显示一句摘要和错误数；搜索命中时可展开并定位字段。Grid 的活动轴 x1/x2/x3 blocks 同层显示，未活动的轴只收起 min/max/边界字段；几何为圆柱或球坐标时，用 Core 坐标目录提供用户熟悉的轴名称，同时保留 `.par` 原始键供查找。AMR 控制在 Grid 中有明确的二级区域。

一行参数主要显示易读名称、控件、必要单位和一句短说明。详细定义、来源、默认值、适用条件和解析诊断可在帮助或 Inspector 中展开，错误仍贴近字段。`RK2`、`muscl` 等是选项，不应写“Unit not provided”；Core 标记 `not-applicable` 的单位不显示缺失警告。已知物理量按 Core 的 CGS 元数据标注；`ode_atol` 之类混合状态容差不能伪造一个统一单位；未知 custom 参数仅显示真实可得的单位证据，绝不凭名字猜测物理意义。显示标签可以去掉 `_`，保存仍使用 Core 标准键。别名只显示一个规范选项，完整文字可用于解释，但写回 Core 认可的值。

Runtime 需独立于 Core 声明顺序排序：计算后端、时间积分器、通量求解器、重构/限制器、CFL、终止条件优先；输出与 checkpoint/restart 随后；数值修复、设备号和较少使用的时间控制可放 Advanced。因此 `time_integrator=RK2` 不会因 Core 定义靠后而排到页面末尾。`-1` 表示关闭的输出间隔等可用开关加数值框呈现，但必须遵循 Core 返回的 toggle 契约并保留未编辑的原文。EOS 的 `eos_table_path` 与 `eos_helm_table_path` 在最新 Core 中**含义不同，不可合并成同一输入**；按当前 EOS 和 Core 适用条件分别展示。

Gravity 先选择 `gravity_type=none/external/self`：none 收起后续设置；external 只展示活动轴的加速度；self 展示边界类型及 `gravity_G`，将 `gravity_rtol`、`gravity_atol`、`gravity_max_cycles` 放 Advanced。Core 对维度、几何与边界的实际约束必须继续显示。配置重力不等于初始 Preview 已解出引力势或加速度场。

Diffusion 以 Core 标准键 `use_diffusion` 为总开关。关闭时折叠明细但不静默删除已有值；开启后显示积分器及 `use_thermal_diff`、`use_viscous_diff`、`use_species_diff` 三个通道。每个常量系数只在 Core 元数据允许时显示，单位为 `cm^2/s`；`diff_cfl`、`diff_max_stages` 等放 Advanced。当前 Core 报告 `modeEditable=false`，不能凭 UI 构造一个可任选的“解析/常量”模式。若当前 EOS 路径禁止显式 `alpha_therm`、`nu_visc` 或 `D_spec`，只隐藏字段不会消除 Core 报错；提供带 Undo 的明确移除操作。Network/Burn 同样用总开关、常用项与 Advanced 整理，保留 RTOL/ATOL 等全部标准键。

## 5. 底部操作、构建与运行

底部固定操作栏至少覆盖 `Configure`（需要时）、`Build`、`Update Preview`、`Run`、`Restart from checkpoint` 及状态摘要。CMake 的 `Build` 通过 `cmake --build` 使用实际生成器；若生成器是 Make，它已经包含 make 阶段，不需两个功能重复的 Build/Make 按钮。Build/Configure 的命令、进度、警告、退出码与有界输出进入可展开的小型终端抽屉；关闭抽屉不会取消构建。失败、过期或当前无权限时按钮明确显示原因。

Run 只对用户确认的**已保存 `.par`**、已注册 case、匹配且可执行的二进制开放。启动前核对当前编辑副本是否未保存、case/配置关联、构建新鲜度、输出位置和后端选择；需覆盖已有输出时单独确认。Run 在独立可见终端以前台方式运行 Core，保留原始 stdout/stderr 与退出码；终端有独立任务身份和进程组。Preview 或 Build 的取消不能向它发送终止信号。用户可以显式停止 Run；不得在服务器上静默转成后台作业。

Restart 是**从 checkpoint 继续 simulation**，不是“把失败的进程重新点一遍”。用户选择 checkpoint，看到兼容性检查结果，并使用经确认的 restart 配置；Core 现有 `.par` `restart`/`restart_file` 和 checkpoint 契约为权威。Restart 同样在独立终端运行，不能自行挑选“最新文件”后无提示启动。

## 6. 初始场、AMR 与图形交互

真实初始 Preview 使用当前未保存 Working Copy，由 Core 初始化返回字段、坐标、单位、参数来源和身份；它不推进 timestep、不写科学结果、不初始化 CUDA。第一次加载允许较慢的完整 Setup；之后参数改动优先复用有界会话和旧请求淘汰，先显示仍有效的上次结果与“正在更新”状态。输入不完整、取消、Core 错误与过期构建必须保留明确状态，不把旧图标成当前图。

现有 Sod 1D 和 CellularDet 2D 是起点。最终应覆盖二进制注册的所有 Core case：Core 提供通用或逐模型的初始化采样与 AMR capability，Studio 按其实际维度、几何、字段和可用性绘制。未注册 `.cpp` 在编译注册前只能检查/编辑。对一维给曲线与位置标记；二维给场图；三维 Core case 需要可选择切片或相应视图；没有空间场的模型给真实状态视图。可编辑标记只在 Core 提供明确图形绑定时显示；不能仅靠参数名推断任意 C++ 的位置或单位。

AMR 的图上表达参考本轮提供的 Sedov `t=2.000000e-03` 图片：清楚的块边界、各 level 图例、场图叠加，缩放后能辨认实际细化区域。该图是**运行结果时刻**，不能用来证明 Sedov 的初始 AMR 已支持。初始 Preview 需调用真实 Core 初始加密接口；当前 Sod/Cellular 已能显式生成且身份匹配时叠加，下一步先把层级与开关移到图附近。更新参数后只有同一 model、Working Copy、构建和 EOS 身份的网格可标为当前；受块数/内存预算截断则写“受限/不完整”，不能把零块误作无加密。初始资源提示给数量级和假设，不报必然 OOM，也不自动运行完整 simulation 试探内存。

绘图控件保留坐标轴与场值各自独立的线性/对数设置、自动或手动范围、上下限裁剪和少量配色。对数遇零/负值时遮罩并提示，不能生成误导数值。缩放和平移必须使图像、AMR 线框、标记和 Inspector 使用同一物理坐标变换。图上颜色可能是有限分辨率的初始化采样；点击 Inspector 显示 Core 返回的原始样本与来源，不伪称 AMR cell average。

## 7. Plotfile 与运行结果：后续接口工作

完整精度的 `plt_XXXX.h5` 保持科学分析权威；Checkpoint 负责重启。Studio 只读 Plotfile，不维护另一套默认的 FP8/FP16 缩水科学数据。Core/IO 先整理主 HDF5 的格式版本、step/time、几何、字段名/单位/centering/shape/排列，以及块 ID、level、bbox、leaf 和数据行关系。写出使用受限内存流程和完成后原子发布；Studio 忽略临时文件。可重建 `.archidx.h5` 仅作查询索引，`.xmf` 用于第三方工具桥接；两者都不能替代主 HDF5。

Host 再提供本地、只读、可取消、有读取/缓存上限的 viewport 查询 worker。Studio 总览使用有限分辨率和明确 LOD，放大后查询相交块；点击图像回查 HDF5 中覆盖该位置的原生叶块及 cell，显示原始值。固定 512²/1024² 输出只限定返回图像，不保证第一次全域 HDF5 读取量固定；读性能应实测。若文件只存叶块，不可在停止读取细块时假装父块仍有场数组。图上同时显示原生 AMR 范围与当前显示 LOD。首先完成本地 1D/2D，随后把 Core 已支持的其他维度纳入全覆盖验收；远程 data-local worker 与 in-situ 留待独立阶段。

这部分由 Core/IO 与 Host **先提供、验证接口和示例数据**，再交给 Studio 实现展示。当前小文件均匀 1D Plotfile 页不能作为大型 AMR Plotfile 功能的验收替身。本机 simulation 运行时，Plotfile 查询默认低并发和有界缓存，提示额外 I/O 负载；不强制给 Studio 预留 CPU core。

## 8. 分工、顺序与交付检查

| 阶段 | Core / IO | Host / Desktop | Studio | 完成条件 |
|---|---|---|---|---|
| A. 最新 main 对齐 | 保持 schema、case registry、inspection 与现有 Preview/AMR 契约可核对 | 从最新 main 建集成分支，导入 Studio，修订项目/构建身份及固定路径 | 核对 95 键、14 case、表达式（包括 `exp(number)`）、保存和现有两模型 | Core/Studio/Host scoped checks 通过，未把已注册误报为可完整预览；记录 main SHA 与 Studio 来源 |
| B. 参数与工作区 | 必要时补充真实适用性/单位证据，避免仅靠前端猜测 | 配置读写与构建身份保持一致 | 折叠组、短描述、完整标准键、Gravity/Diffusion 条件、Runtime 排序、底部 Build 抽屉 | 任意标准键可查找编辑；切模式不丢 `.par` 内容；错误和旧结果不被隐藏 |
| C. 本地启动与执行 | 保持 Core CLI、注册与 checkpoint 验证权威 | Linux/WSL 单命令 launcher、可选 CMake 包装、独立 Run/Restart 终端与进程隔离 | 显示项目/文件/构建身份、动作可用性和运行状态 | 用户无需浏览器/端口/npm；Run/Restart 前核对配置；取消 Preview 不影响运行终端 |
| D. 全模型初始视图 | 将注册 case 的真实初始化状态/AMR 能力扩到适用维度，返回 capability、字段和明确限制 | 有界任务、取消、身份与资源限制 | 各 case 按真实能力出图或状态视图，AMR 线框直观叠加 | 对完整注册表逐项验收，不留无说明的 inspection-only 模型；不能用 Mock 补位 |
| E. Plotfile | 完整 HDF5 元数据、原子发布、示例、必要索引和第三方桥接 | 本地 viewport/native cell 查询、缓存和 I/O 限额 | 场图、AMR、LOD、Inspector 与显示控件 | 大文件不整份传入 GUI；原生 cell 数值与 HDF5 一致，取消/缩放有界 |

每阶段交接给对方时提供：准确分支/commit、所基于的最新 main SHA、接口与示例、支持/不支持能力表、局部测试与人工验收结果。最终验收必须比较**当时** Core schema/registry 与 Studio 覆盖表，逐项关闭缺口；不能因为旧版本的 95 键/14 case 已通过，就默认未来 main 新增的能力自动完成。
