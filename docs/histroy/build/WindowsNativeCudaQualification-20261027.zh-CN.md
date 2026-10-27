# Windows 原生 CPU／CUDA／Studio 最终功能对照

2026-10-27：本轮 Windows 兼容工程在下列实测配置与既有科学范围内闭合，提交到 `build/win11` 供用户合并。**推荐 Linux／WSL2；Windows 具有对应的完整 CPU／CUDA CLI 与 Studio 功能，但适配较新，可能存在不稳定性与 bug。** 本记录是源码分支验收，不发布新 tag、release 或 Zenodo 包。

Linux 当前源码通过72项 CPU CTest、12项受影响 CUDA／真实 EOS fixture 回归，以及453项 Studio 测试。Windows 通过原矩阵141项及补齐的初始 AMR 拓扑 oracle 1项，共142项不同科学 CTest；均无失败或跳过。与 Linux 名称核对，71项科学契约全有 Windows 对应执行，Linux 专用 tooling 契约单独保留。Windows Studio 的16文件95项测试及7项链接证据测试通过。

37个规范后端算例与9个分辨率组按原阈值通过；8条中途／终止 CPU↔CUDA AMR restart 路由及9项比较通过，执行前后来源和二进制身份保持。真实 Host 完成 MSVC/CUDA Build、16模型发现、初始场／检查／资源／AMR、独立终端 GPU Run／Restart。正式 GPU Plotfile 的 metadata、SHA绑定 raw slice／point／LOD 通过；8个 DENS 样本及1个 point 与原生 h5py 相同，文件 SHA 保持，代表性同后端续算使用0／0阈值。原完整几何目标的 Compute Sanitizer 报0错误。

两侧便携包均重新构建，实际 Electron44.9.1、独立 Host Node24.21.0；各225项生产文件比较一致，许可证保留。最新 Windows 包的真实 Main／独立 Host／renderer 已就绪。新的 GPU Host 验收不冒充人工对话框或曲线操作；W3 用户 Open／Save Working Copy As／正式输出看图与正常关闭证据保留在前一份归档。

| 功能 | 两侧共用范围及平台对应 | Windows 验收 |
| --- | --- | --- |
| 构建与安装 | 共用 CMake／Ninja 的配置、构建、安装；Linux 使用 GCC／NVCC，Windows CPU 用 GCC/UCRT64、CUDA 用独立 MSVC/NVCC 依赖树。 | GCC 与 MSVC/NVCC 真实生产构建通过 |
| 路径与输入 | 输入格式与相对路径规则共用；工作目录为路径基准。Windows 支持本机斜杠、UTF-8 与空格，跨系统绝对路径需调整。 | 中文空格及双分隔符路径；原生文件与进程契约 |
| CPU／OpenMP | 共用 CPU 数值实现及 OpenMP；检查1／4线程功能，不承诺 SIMD 性能相同。 | 两种程序的 OpenMP 1／4线程、CPU／auto 实际运行 |
| 后端选择 | 同一 CUDA 构建选择 cpu／cuda／auto；CPU-only 的 auto 保持 CPU，显式不支持的 CUDA／设备请求必须拒绝。 | CPU／CUDA／auto、无效设备及不支持构建拒绝；37算例 |
| 流体求解 | 原 SW／VL／Roe／HLL／HLLC、PCM／MUSCL／PPM、四类 limiter 与 Euler／RK2／RK3；保留原模型、EOS、粗细面与耦合阶次限制。 | 原流体叶函数、路由／积分器、保守性矩阵与规范算例 |
| 几何 | 原 Cartesian／cylindrical／spherical 1D–3D 与原生2D r-z；坐标顺序、奇点拼接及后端资格沿用原记录。 | 原 Host／GPU 完整几何检查；memcheck 0错误 |
| 流体／势边界 | 周期、流出、反射、给定流入、用户流体边界；势值、外法向梯度、Robin 与用户势边界保留原兼容条件。 | 原边界计划／Host-Device 对照及耦合几何检查 |
| AMR | 同一指标、细化／粗化、守恒迁移、ghost、reflux 与拓扑事务；Host 决策、Device 数值工作分工保持。 | 交换／reflux／迁移／事务与8条真实重启链 |
| 状态方程 | IdealGas、Helm、3D／4D free-energy，以及支持的 EOSDriver／baryon 原表；保留声明域、分量、反演和拒绝检查。 | 原 Helm、规范／原生表与 Host-Device 对照；70个失败槽 |
| 扩散 | 原热、黏性、组分通道和 RKL1／RKL2；各通道取决于材料模型，Helm 恒星分支的导热范围保持。 | 原 RKL1／RKL2 与后端测试；规范扩散收敛组 |
| 烧核与 NSE | iso7、aprox13／19／21，BE_NR／BD／ROS4 及已验收网络的 NSE；保留网络／EOS 组合限制。 | 原网络／ODE／策略矩阵与规范烧核算例 |
| 生成网络 | 注册生成网络与 weak tables 的同一包契约；GPU 使用者仍需提供可设备调用的数学和有效包。 | 原生成网络／NSE 的 Host 与真实 Device 测试 |
| 烧核线性求解 | 总 ODE 方程数≤31的 DenseLU、CPU KLU、可选 CUDA cuDSS0.8；显式跨后端错误组合继续拒绝。 | 原 DenseLU／KLU／cuDSS 与32／151／201方程及重试测试 |
| 外部／自引力 | 外力阶段与复合自引力沿用原周期／孤立／曲线几何、流体面、势边界和耦合物理的适用范围。 | 原引力 stage／生命周期／物理测试与规范外引力算例 |
| Poisson／复合多重网格 | 共用自引力 Poisson 的 composite multigrid；烧核线性系统与 Poisson 方程继续分别验收。 | 原解析／composite Poisson 契约与引力检查 |
| Jeans／JENS | Jeans／JENS 的 CPU 演化／重启限原均匀周期、常比热 IdealGas 背景；CUDA 仍仅有原 Cartesian／原生 r-z 事务资格。 | 原 Jeans 诊断与受限事务；不新增完整GPU科学演化资格 |
| HDF5 输出 | 共用 HDF5 Plotfile／checkpoint／诊断格式和来源标记；平台只替换必要的文件／进程原语。 | 原发布／兼容契约与真实CPU／GPU checkpoint／Plotfile |
| 重启 | 同格式的相容状态与 CPU↔CUDA 重启；EOS、网络、几何、引力及控制身份检查保持。 | 8条中途／终止路由、9比较；真实Host GPU续算 |
| 模型与参数发现 | 同一 Setup／Init 用户接口、16个注册模型、声明参数与已观察单位证据；选择源文件不等于热编译任意 C++。 | 16个注册模型的源码SHA／单位证据匹配；完整初态API |
| Studio Config | 模型／参数绑定、Inspector、Working Copy、Open／Save／Save Working Copy As 共用；下载导出另计。 | Linux453／Windows95测试；保留W3人工Open／Save证据 |
| Studio Build | 相同 Configure／Build／指纹／绑定检查；GNU 可用观察范围保持，完整 CUDA 依赖新鲜度在两侧均不冒称已知。 | 真实MSVC/CUDA managed Build；完整新鲜度仍unknown |
| 初始化 Preview／AMR | 初始场／AMR 的同一有界 Preview；CPU／wall 预算保留，Linux RLIMIT 与 Windows Job commit/user 计量明确区分。 | 真实Host场／检查／资源／AMR；两侧t=0拓扑oracle |
| Studio Run／Restart | 共用 Run／Restart 控制器、输出锁、进程身份与取消；Linux xterm/flock 对应已配置的 Windows Terminal／原生 helper。 | 真实Host＋Windows Terminal GPU Run／Restart |
| Plotfile | 只读 metadata、SHA绑定 raw slice／point／global display LOD；保留原几何、FP64、文件身份与读取限额。 | 真实GPU metadata／slice／point／LOD；原生h5py一致 |
| Electron 桌面／便携包 | 同一 Electron44.9.1、独立 Host Node、生产资产和许可证复制流程；分别生成本机包和入口。 | 两侧44.9.1包各225文件比对；最新Windows真实窗口 |
| 开发工具 | 构建／来源证据共用原则并使用本机解析；Linux 内核 profiling／PSI 等工具与计数保持系统专用，不伪造等效指标。 | Linux完整tooling／架构审计；Windows链接证据7项 |

曾出现首次加载超出既有短元数据查询时限，最终匹配工具配置在原限额内通过；原失败保留，不保证所有冷启动稳定。

数学例外仅限已批准 A12–A18：分类入口、struct 声明、Host／Device ABI 注解、与原 Linux 逐位相同的742个 Helm 节点及13处失败数组填充；公式、容差、拒绝检查、池分配／容量／所有权均未改变。249份受保护文件中43份仅匹配批准候选，零越界。最初4项 SHA 测试和未初始化 Grid 夹具失败均保留记录，按测试平台修正通过；加强 EOS 断言已确认实际重编译。

功能对应沿用原科学组合范围；Jeans／JENS 的完整 GPU 科学演化仍未新增。Windows 资源限额与 Linux 不是同一个 OS 指标，MSVC/CUDA 完整依赖新鲜度仍为 unknown。当前合格 Run／Restart 使用显式 Windows Terminal；独立控制台兜底的 Win32 5 失败保留。仅接受本机受测 ABI／文件系统，不扩展到所有硬件、UNC 或任意编译器。原14.38编译器未通过 NaN 门，当前实测14.44不使用 unsupported-compiler 覆盖；官方12.9表明确列193x。MSVC 对3个超出 binary64 的下溢字符串保留格式化读取失败，不发布零值。MPI、HIP／Metal 与一般辐射输运未新增。

[构建与路径](../../guides/Build.zh-CN.md) · [Windows CUDA](../../guides/WindowsNativeCuda.zh-CN.md) · [Studio 使用导航](../../guides/Studio.zh-CN.md) · [W0–W3 人工与 CPU 记录](WindowsNativeQualification-20261026.zh-CN.md)

[Machine-readable record](WindowsNativeCudaQualification-20261027.json). Raw HDF5, local logs, machine AGENTS/skills and obsolete plans are excluded from source distribution.
