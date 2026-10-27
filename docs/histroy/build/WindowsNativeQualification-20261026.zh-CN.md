# Windows 原生 CPU／Studio：W0–W3 验收与交接

日期：2026-10-26。原始冻结基线为 `04cb2f3f7881c20b7ee446d1b7adc020cb8feaf5`（v1.3.1），实现分支为 `build/win11`。本记录归档平台适配的范围与处理后证据，不创建新 release 或移动冻结标签；Zenodo 与 release 继续暂缓。

**推荐 Linux／WSL2。Windows 11 x64 同样提供完整的已适配 CPU CLI 与 Studio 工作流，但适配较新，仍可能有不稳定性与 bug。** “完整工作流”指配置、编译、初始场／AMR 预览、Run／Restart、配置保存和正式 Plotfile 查看，不意味着所有物理模型、硬件、文件系统或原生 CUDA／MPI 都通过验收。

## 共享实现与阶段结果

| 阶段 | Windows 原生结果 | Linux／共享边界 |
| --- | --- | --- |
| W0 工具链 | GCC/UCRT64、CMake、Ninja 及同 ABI HDF5／HighFive／KLU；选定精度契约探针 | 同一 CMake 图、数学库和既有误差预算；不混用 MSVC／Anaconda C++ DLL |
| W1 CPU CLI | 构建、安装、UTF-8 中文／空格运行路径、Sod 输出与 checkpoint 续算；所测 OpenMP1／4线程及续算比较为零差异 | 复用生产 ARCH／HDF5 实现；没有新增全仓逐位标准，也不从样本推导跨 OS checkpoint 资格 |
| W2 Host | Configure／Build、来源与文件绑定、配置原子保存、输出锁、Run／Restart、隔离 Plotfile 读取、真实初始化／AMR Preview | 共用原状态机、科学 parser、指纹、读写限额与失败检查；仅平台进程、句柄、路径和元数据边界不同 |
| W3 桌面 | 原生 Electron 窗口、项目与源码绑定、真实 Build／Preview、Open／Save Working Copy As、便携包、正式 Core 输出读取与人工显示 | 同一 Electron／Host／renderer／h5wasm 与打包脚本，分别携带原生 Host Node；Linux 回归及 Linux 便携包保留 |

参考工具组合为 GCC16.2、HDF5 1.14.6、HighFive2.9.0、SuiteSparse/KLU7.13.0。MSYS2 为滚动发行版，这些是实测记录，不是对后续所有版本的保证。具体配方在 [W0](../../guides/WindowsNativeW0.zh-CN.md)、[CPU](../../guides/WindowsNativeCpu.zh-CN.md)、[Host](../../guides/WindowsStudioPlatform.zh-CN.md)、[桌面](../../guides/WindowsStudioDesktop.zh-CN.md)；用户日常操作统一看 [Studio 指南](../../guides/Studio.zh-CN.md)。

数学库、方程、科学格式及数值判据保持原约定。已逐项审阅的平台例外集中在原 owner 的进程监督、运行映像身份、路径比较、证据发布、资源策略和少量来源标签，不建立 Windows 专用的科学读写或求解器。实现归属见 [ImplementationOwnership](../../development/ImplementationOwnership.md)。

## 完整 Core 产物链路

W3 首批窗口／打包和初值 Preview 不能独立证明正式计算输出可用。补验采用同一最终包启动的原生 Host，执行新的代表性 CPU Sod Run，从中途 checkpoint Restart，再经包内原隔离 worker／h5wasm 读取其正式 Plotfile，最后由用户在 Real Plotfile 页确认数值表与曲线正常并正常关闭窗口。

代表性运行使用64个单元、4个块和8步上限；设置 tmax0.05，但实际结束于 step8、时间 `0.015108299363338467`，没有声称达到 tmax。原比较器对完整运行／续算最终 checkpoint 给出 `max_abs=0`、`max_rel=0`；控制状态与输出索引相符。

| 证据 | 结果 |
| --- | --- |
| 正式文件 | `W3Sod_HLLC_plt_0008.h5`，93440字节，DENS／ENER／PRES／VELX |
| 文件 SHA256 | `ac8e6b1b7d781c347117b1b3d95eba0e56b500dc6efdd5a4f821ac6e80555fc1` |
| 对应 Core SHA256 | `5e9265a5f54ebad11a17a3d88af4d6837cd19167fa3354fdaa06e4630ecf445e` |
| 公开 Host 读取 | metadata、SHA 绑定 slice／global overview／point 通过原 renderer validator；错误项目、越界路径、额外工具参数及错误 SHA 均拒绝 |
| 独立原生读回 | Windows h5py／NumPy 对8个 DENS slice 样本、1个 point 样本及坐标完成 FP64 位模式比对，读取前后文件 SHA 相同 |
| 配置另存 | 用户 Save Working Copy As，1143字节副本通过原 reader／validator，SHA `9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843`，未覆盖原输入 |

这些结果证明所测样本的计算恢复与存储数值传输，不代表全部科学模型、分辨率或全部数值方法组合已验证。最终包未捆绑 Core、同 ABI DLL、编译器或运行终端；它是本地便携目录，版本仍为开发用0.0.0，不含签名、安装器或自动更新。

Electron 的 manifest、lock 两处记录、installed 包与实际运行引擎均为 **44.9.1**。打包脚本从依赖读取版本，不再另写固定值。Host 使用独立 Node24.21.0；Electron 内嵌 Node24.22.0 是另一运行时。222个生产文件与原生暂存内容逐项 SHA 一致，原 Host／renderer／桌面入口与工作区一致；Node、Electron／Chromium、h5wasm／HDF5 许可证保留。

## 精确的限制与失败记录

- Windows Preview 使用原 owner 中明确声明的 nested Job、process commit 和 Job user CPU 策略，不冒充 Linux RLIMIT_AS。原 CPU／wall／输出预算、会话续额、取消及后代回收检查保留。
- 完整构建新鲜度仍可为 `freshness-unknown`、`dependenciesComplete=false`；有限 `observedInputs=current` 只覆盖成功 Build 记录的输入。没有为解锁 Preview 声称完整 loader／LTRANS／动态依赖闭包已知。
- 本次合格的 Windows Run／Restart 路线显式配置 Windows Terminal。受限测试作业下，直接控制台兜底曾以 Win32 5 拒绝未确认的交接，未把它计为运行成功。编辑／Preview 无需终端。
- WSL 的 Computer Use 路由无法初始化，人工窗口操作与自动软件证据分开接受，没有用 HTTP 测试冒充 OS 点击。配置另存仅在实际落盘、独立 reader 与 SHA 检查后接受。
- 首轮 ESM/CJS 探针、测试启动器参数、DLL 搜索目录、临时窗口生命周期与资源超时失败均保留。修正的是相应探针／平台边界；没有放宽科学容差、删除失败检查或关闭 TLS／校验和。
- 本机 NTFS 的中文／空格运行路径不保证 Unicode LTO 构建、任意长路径、UNC／网络文件系统、所有编译器和任意负载的稳定性。原生 Windows GPU／MPI、AMD 与 Apple 仍另行规划。

## 最终文档与依赖整理

README、Build、Studio、环境与功能索引均按 Linux 在前、Windows 在后或并列表格说明。Studio 指南补充模型／参数分离、Help／Inspector、Working Copy／保存／下载区别、Plotfile metadata／raw／LOD 操作，以及 Linux `.par` 转到 Windows 后的路径检查。详细 Windows 配方作为同一套文档的补充，不另建第二套操作流程。

PR [#19](https://github.com/Shiro-Akane/ARCH/pull/19) 的 undici7.30.1 和 [#20](https://github.com/Shiro-Akane/ARCH/pull/20) 的 Electron44.9.1 已在精确 head CI 通过后提前合并。#19 改变 main 后，#20 由机器人更新基线并重新通过 CPU Release、Studio／Host、Tooling 与 CI required；没有绕过必需检查。两者只更新依赖；#20 合并本身不修复旧 main 的硬编码打包版本，该修复在本轮 Windows 分支中，合入前不从该中间 main 制作发布包。

最终依赖的330项许可证清单按 lockfile 同步，包括先前 Vite8.3.4 更新后的传递依赖版本。Linux 与 Windows 本地便携包已用共用打包设施重新生成，实际引擎均为44.9.1，最新 notices／README 一致；Core SHA 未变，沿用同一实现和此前人工窗口验收，不重复要求用户操作。Windows 首次最终目录 rename 返回 EPERM，确认无包进程／目标残留后一次有界重试通过；首轮失败保留，未关闭系统保护或改写源码。

最后一次本地检查使用 Linux Node24.21.0：锁定安装、lint、带类型检查的生产构建通过。早期全套报告为448 pass／40 Windows专属 skip，不能满足现有正式 CI 的零 skip 规则。最终在 npm 测试入口显式选择适用文件：5个纯原生文件列为 NOT_APPLICABLE，混合文件中的23个 Windows case 与2个 Linux case 按 OS 注册；所有原断言、名字、超时和缺失工具检查不变，共享／未知新增文件默认选入。相同注册调整在原生 Windows 的受影响组中取得47 pass／0 fail／0 skip。新增3个入口控制测试后，Linux 完整适用套件为 **451 pass、0 fail、0 skip**，原 `check_ci_results.py` 零 skip 门禁逐字节未改并实际通过；

首次托管 Tooling 的最小 CMake 身份测试 fixture 漏列新 `arch_run_identity`，仅补齐原占位 target 清单后，本地原回归和完整619项 Python Tooling 均通过且零 skip，生产身份规则及全部原断言未改；不把不适用项计为通过，也不使用名字过滤产生空文件 pass。

首次全套运行还曾因固定 Sod 样本为 LFS 指针而失败；仅按既有 OID／14336字节复用本地缓存后重跑，最后恢复原指针。

## 归档边界

本页及[处理后摘要](WindowsNativeQualification-20261026.json)作为可分发的验收入口。完整原始 log、失败结果、worker 产物、机器绝对路径和真实 H5／checkpoint 留在本机忽略的 `output/windows-*` 目录；本记录不承诺下载者拥有这些本机文件。AGENTS、项目 skill 与两份过期计划不随本轮源码分发。源码分支推送供用户审阅和合并，**不自动合并 Windows PR，不创建 tag、Zenodo 或 release**。
