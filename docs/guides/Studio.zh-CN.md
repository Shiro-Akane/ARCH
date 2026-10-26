# ARCH Studio：配置、预览、运行与查看输出

[English](Studio.md) · [首页](../../README.zh-CN.md) · [环境准备](StudioEnvironment.zh-CN.md)

Studio 是可选桌面组件，Core 可独立运行。**推荐 Linux／WSL2；原生 Windows 11 x64 同样提供完整的 CPU Studio 工作流，但适配较新，可能存在不稳定性与 bug。** 两侧共用参数编辑、科学预览与 Plotfile 服务。Windows CUDA／MPI 不在本轮资格内；已有 CUDA 路线使用 Linux。

## 构建与启动：先 Linux，再 Windows

先按[构建指南](Build.zh-CN.md)准备 Core，再按[环境指南](StudioEnvironment.zh-CN.md)准备相应系统的 Node24+、Electron 和运行终端。所有示例从 ARCH 项目根目录开始。

Linux／WSL2：

```bash
cmake --preset studio-cpu-release -DARCH_STUDIO_NODE=/absolute/path/to/node
cmake --build build-studio-cpu --parallel 2
build-studio-cpu/bin/arch-studio --project "$PWD" --build-dir build-studio-cpu \
  --binary build-studio-cpu/bin/ARCH --case Sod --config simulation/Sod/Sod_beginner.par
```

原生 Windows：先完成 [GCC/UCRT64 CPU 构建](WindowsNativeCpu.zh-CN.md)，再按[原生桌面指南](WindowsStudioDesktop.zh-CN.md)开启 Studio 并设置可信工具／DLL 上下文。在 PowerShell 启动：

```powershell
& .\build-studio-cpu\bin\arch-studio.cmd --project "$PWD" --build-dir build-studio-cpu `
  --binary build-studio-cpu/bin/ARCH.exe --case Sod --config simulation/Sod/Sod_beginner.par
```

命令中的构建目录必须对应实际配置；已有树通过 `--build-dir` 显式选择，不会静默改写其他缓存。生成的源码启动器依赖当前 checkout。便携包保留整个目录：Linux 启动 `arch-studio`，Windows 启动 `arch-studio-bin.exe`。包内带 Host Node 与 h5wasm，Core、系统依赖和运行终端仍需另备。连接由程序管理，无需启动 Vite 或填写端口。

## 界面导航

先看顶部 **MODEL / PARAMETERS**，确认当前模型与参数文件；再用右上角 **Workspace** 选择 **Config** 或 **Plotfile**。Config 中左侧是参数，中间是 **Real initial condition** 预览，选中参数后在右侧 **Parameter Inspector** 查看解释。上方可展开 **Project / Local Host**，核对项目、源码、二进制与构建状态；下方工作流区域用于运行和重启。

![Studio 的 Plotfile 界面，显示已记录的势场与原生 AMR 叶块轮廓](images/studio-plotfile.png)

*示例为已输出文件的只读查看，不是初始场 Preview。*

## Config：引入模型与参数文件

1. **先选项目和编译好的模型。** 启动页可填写 Project、Existing build directory、Binary、Registered model、Source；也可用上面的启动命令。连接后，在中间 **Real initial condition → Model** 选择当前二进制发现的注册模型，例如 Sod。模型发现失败可点击 **Retry model discovery**。
2. **源码与参数是两件事。** `--source simulation/Sod/Sod.cpp` 或启动页 Source 绑定已注册源码，不是导入任意 C++ 后自动编译。选定源码时 Model 固定为其唯一注册模型；源码身份待确认、发现未完成或工作流忙碌时，下拉框不可用。换另一源码请重新打开项目。新增模型先按[算例指南](SimulationCase.zh-CN.md)注册并编译；选文件本身不能更新旧二进制。
3. **再打开 `.par`。** 左侧 **Config file → Open Parameter File (.par)…** 选择参数；已关联项目可点 **Load Project Parameter File**。先试 `simulation/Sod/Sod_beginner.par`。顶部模型与文件名应对应；文件名相似不证明物理配置兼容。展开 **Source & parameter association** 可核对来源。

## Config：查看、调整与保存参数

从左向右看：左侧按 Grid、EOS、Network、Gravity、Diffusion、Runtime 等组展开；用搜索框查名称、原始键或说明。常用项先显示，额外设置在 **Advanced** 或 **Inactive settings** 中；隐藏字段并不消除错误。

每项 **Help** 提供原始键、类型、单位、允许值、适用条件与默认值是否允许。点击参数后，右侧 **Parameter Inspector** 可对照 Working Copy、Saved value、解析值与诊断；没有元数据时保持 Unavailable，不凭猜测补齐。完整定义查[参考手册](../Reference.zh-CN.md)。输入输出统一采用 CGS；例如长度 cm、密度 g/cm³、压力 erg/cm³、温度 K、角度 rad。

直接修改输入框、下拉框或开关即可调整 **Working Copy**。先修正 View issues／行内错误与路径检查，再在中间请求 **Generate Real Preview / Update Preview**。它可读取未保存的工作副本，但仍要求模型、编译身份、参数和采样条件有效。预览显示真实 Setup／Init；**Initial AMR** 是单独的有界请求，不代表已经运行时间推进。字段、坐标轴和颜色的 linear/log／范围只影响显示；log 不能使零或负值变成正值。

保存与计算分别确认：

- **Save** 写回已关联文件；**Save Working Copy As…** 在项目内另存 `.par`，建议先保存副本。磁盘有外部变动时保留工作副本，按提示重载或另存。
- **Download Copy…** 是导出下载，不等于在项目内保存并建立可运行的文件关联。
- 修改 C++ 后，在 **Project / Local Host** 内显式 **Configure / Build**，查看输出与身份检查；刷新状态不等于编译。依赖证据不完整仍显示 unknown。
- 最后到工作流区域选择 **Run / Restart**，检查已保存参数、二进制、输出目录和 checkpoint，再确认。独立终端中的已启动作业可在 Studio 关闭后继续；Stop 只针对记录的本次作业。

## Plotfile：调入结果与看图

右上角 Workspace 选 **Plotfile**，页面标题为 **Real Plotfile**。优先看上方 **Project Plotfile Viewer · read-only**：

1. 在 **Project-relative file** 输入项目内结果路径，例如 `output/first_sod/SodBeginner_HLLC_plt_0000.h5`，点击 **Read metadata**。选择 `_plt_` 场输出，不是 `_chk_` 重启文件；名称和编号以实际磁盘结果为准。
2. 先核对 Time、Geometry / dimension、Stored shape、完成状态与来源，再在 **Stored field** 选 DENS、PRES 等实际存在的场。
3. 需要原始数据时，选择 **Block index、Start、Count → Read raw samples**。下方出现数值表与支持的曲线／原生视图；点行内 **Inspect** 查看该存储单元、坐标和单位。
4. 需要全域总览时，点击 **Read global display LOD · scans leaves**。当前图形总览覆盖具有原生网格证据的笛卡尔1D／2D及柱坐标 r-z 2D；其他几何／维数可用的有界单元查询不代表已有三维渲染。用 Fit、缩放或拖动查看；总览是显示降采样，精确数值回查 raw samples／Inspector。

下方 **Local file · legacy 1D inspection → Open Plotfile…** 是独立的旧式本地1D入口，不提供正式完成／来源核验，不能和上方项目读取混为一谈。完整 `.h5` 始终是权威数据；所有查看均只读。初始化 Preview、正式时间推进输出和数值科学验收也分别判断。

## Linux 与 Windows 的路径

**先区分“当前 OS 的路径”与“Studio 的项目相对路径”。**

| 位置 | Linux／WSL2 | 原生 Windows |
| --- | --- | --- |
| 项目根／外部工具绝对路径 | `/home/user/ARCH`、`/usr/bin/cmake` | `C:/work/ARCH`、`C:\tools\cmake.exe` |
| Core | `build-studio-cpu/bin/ARCH` | `build-studio-cpu/bin/ARCH.exe` |
| Studio 的 config／Plotfile／Save As 相对路径 | 两侧均用 `simulation/Sod/Sod_beginner.par`、`output/first_sod/xxx.h5` 这样的正斜杠路径 | 不在这些输入框填写盘符、`/mnt/c` 或 `..`；原生文件对话框会转换项目内选项 |
| `.par` 内的输出／重启／表路径 | 相对路径从计算工作目录解析，或写真实 Linux 绝对路径 | 相对规则相同；绝对路径需改成真实 Windows 路径，建议 `C:/work/ARCH/...` |

Studio 启动 Core 时以项目根目录为工作目录；自行运行 CLI 时以终端当前目录为准。`out_dir = output/first_sod` 因此通常可跨两侧共用，**并不表示输出到 `.par` 旁边**。从 Linux 复制 `.par` 到 Windows 后，逐项检查 `out_dir`、`restart_file`、EOS／网络表等路径，不要只改文件名或全局替换斜杠；写实际值，不依赖 shell 环境变量展开。

WSL 中的 `/mnt/c/work/ARCH` 与原生 Windows 的 `C:/work/ARCH` 可指向同一磁盘位置，但 `/home/...`、`/mnt/c/...` 不能直接传给原生 Core。Windows 路径也不能直接交给 Linux Core。PowerShell 的命令参数遇到空格要整体引用；`.par` 使用 UTF-8，保留原有语法，不照抄 PowerShell 的引号／反引号或 Bash 的转义。不要共用两侧的 CMake 缓存、Node 可执行文件或 ABI 依赖；UNC、网络文件系统和任意长路径未由本次本机 NTFS 验收覆盖。

## 更多说明

[构建](Build.zh-CN.md) · [平台与环境](StudioEnvironment.zh-CN.md) · [科学功能范围](../Features.zh-CN.md) · [验证入口](../../validation/README.zh-CN.md) · [Host 工程测试](../../studio/tests/README.md)。数学与物理定义、原科学格式及既有误差预算不因平台变化而放宽。
