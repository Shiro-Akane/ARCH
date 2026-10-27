# Windows 原生 Studio 桌面构建（W3）

当前完整 Windows CPU／CUDA／Studio 状态与逐项对应见[2026-10-27 最终验收](../histroy/build/WindowsNativeCudaQualification-20261027.zh-CN.md)。本页的 W0／W1／W2／W3 结果保留各自当时范围；CUDA 使用独立 [MSVC 构建及工具上下文](WindowsNativeCuda.zh-CN.md)。

[English](WindowsStudioDesktop.md) · [两侧共用操作指南](Studio.zh-CN.md) · [W0–W3 验收归档](../histroy/build/WindowsNativeQualification-20261026.zh-CN.md)

状态：W3 在已测 Windows 11 x64／GCC UCRT64 CPU 环境闭合。原生窗口、Build／Preview、便携包和配置保存已验收；2026-10-26 补验通过最终包的真实 Core Run／Restart、正式 Plotfile 读取与独立 FP64 传输比对，用户确认同一产物的数值表与曲线显示正常并正常关闭窗口。窗口工具的 WSL 路由问题仍存在，人工操作证据与自动验收分别记录。数学、其他 Studio 读写、Zenodo 与 release 仍冻结／暂缓。

## 前提

先完成 [W0 同 ABI 依赖](WindowsNativeW0.zh-CN.md)、[GCC/UCRT64 CPU 构建](WindowsNativeCpu.zh-CN.md) 和 [W2 平台服务](WindowsStudioPlatform.zh-CN.md)。使用原生 x64 Node 24+、CMake、Ninja；不要把 Linux Node 或 MSVC／MSYS 的另一套 ABI 混入 UCRT64 Core。打包使用官方 Node 分发目录并保留其完整 LICENSE。Electron 包包含独立 Host Node 与 h5wasm，Core、工具链、同 ABI DLL 与可选的 Windows Terminal 留在外部。

## 同一组 CMake 目标

在已经按 CPU 指南配置的 Ninja 树上追加以下选项。路径仅作示例，可用 Windows 盘符路径的 `/` 或 `\` 分隔符；PowerShell 中将带空格的参数整体引用。

```powershell
cmake -S . -B build-studio-cpu -DARCH_BUILD_STUDIO=ON "-DARCH_STUDIO_NODE=C:/tools/node/node.exe"
cmake --build build-studio-cpu --target arch-studio-assets -j2
cmake --build build-studio-cpu --target arch-studio-runtime -j2
cmake --build build-studio-cpu --target arch-studio-package -j2
```

`arch-studio-assets` 使用 Node 同目录下的 npm-cli.js 执行锁定安装与生产构建，不需要运行 npm.ps1。`npm ci --ignore-scripts` 不隐式下载 Electron；`arch-studio-runtime` 随后显式运行官方安装器。它核对依赖声明、锁文件两处记录、已安装包，并让官方安装器核对 dist/version，旧 executable 的存在不会跳过更新。Linux 沿用同一目标和现有 shell 入口，Windows 生成 bin/arch-studio.cmd。

在目标 OS 上打包：Windows 生成 studio/.local/desktop-release/arch-studio-bin-win32-x64/，启动其中 arch-studio-bin.exe；Linux 生成 linux-x64 目录并保留 arch-studio 包装器。保持整个目录完整。两侧共用生产资产、Host、src、h5wasm 与许可证复制流程；分别携带 runtime/node.exe 或 runtime/node。版本从依赖读取，不再单独写死 Electron 版本。保留 Node、Electron/Chromium、ARCH 和 HDF/h5wasm 的完整许可证。

这是本地便携目录包，不包含签名、安装器、自动更新或发布步骤。开发包默认版本仍为0.0.0；不得据此创建 release。若受限网络需要离线缓存，只使用官方发行文件并校验锁定 Electron 包内的 checksums.json；electron_config_cache 可指定官方安装器与打包器共用的缓存目录。不得跳过 TLS／校验和来使下载通过。

## 原生 Windows 的私有工具上下文

启动前由可信本机环境设置 ARCH_STUDIO_WINDOWS_TOOLS。它不是项目文件、HTTP 参数或 renderer 表单。

```powershell
$repo = (Resolve-Path .).Path
$ucrt = 'C:\msys64\ucrt64\bin'
$deps = 'C:\arch-deps'
$build = Join-Path $repo 'build-studio-cpu'
$tools = @{
  cmake = (Join-Path $ucrt 'cmake.exe')
  supervisor = (Join-Path $build 'arch_windows_process.exe')
  ninja = (Join-Path $ucrt 'ninja.exe')
  objdump = (Join-Path $ucrt 'objdump.exe')
  publisher = (Join-Path $build 'arch_windows_build_publish.exe')
  rc = (Join-Path $ucrt 'windres.exe')
  terminal = (Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\wt.exe')
  path = "$deps\bin;$ucrt;$env:SystemRoot\System32;$env:SystemRoot"
}
# Run/Restart 使用已安装的 Windows Terminal；若别名位置不同，调整 terminal 的绝对路径。
$env:ARCH_STUDIO_WINDOWS_TOOLS = $tools | ConvertTo-Json -Compress
$env:ARCH_STUDIO_NODE = 'C:\tools\node\node.exe' # 源码入口；便携包默认优先使用包内 Node
& "$build\bin\arch-studio.cmd" --project "$repo" --build-dir build-studio-cpu --binary build-studio-cpu/bin/ARCH.exe --case Sod --config simulation/Sod/Sod.par
```

调整示例路径以匹配本机。未知字段、相对工具、非 .exe 或不完整 PATH 会明确报错。显式 ARCH_STUDIO_NODE 无效时不会静默改用其他运行时。原生 Windows 项目默认使用直接 Node；明确 --distro、绝对 Linux 项目／源码或 WSL UNC 项目保留原 WSL 路线。源／包内 Main 都保留同源导航、sandbox、contextIsolation、禁止 Node renderer 与 loopback token。

Open/Save 的原生入口只返回项目内 .par 相对路径；真正读取、保存和科学校验仍由既有 Host owner 执行。没有新增科学格式或 Windows 专用读写实现。A11 的五个 Host owner 仅做已批准的私有平台上下文与路径绑定兼容；其他读写 owner 保持冻结。原生 Main 等待 Host 就绪上限90秒，Linux／显式 WSL 保持25秒；Core 的原资源限额不变。

可选 Windows Terminal 支持 App Execution Alias；Main 不要求该别名可被 stat，实际启动校验仍由原 W2 Run owner 执行。

编辑／Preview 无需 Windows Terminal。本次最终包的 Run／Restart 使用显式 `tools.terminal` 与既有 W2 Windows Terminal 路线。在当前受限测试作业中，省略该设置后，直接新建独立控制台的兜底返回 Win32 5，并按原策略拒绝未确认的终端交接；这一路线未通过本次验收。不应取消 Job 约束或把此失败视为计算成功。

原生对话框返回的路径由既有 desktop/arguments.mjs 转为 Host 规定的正斜杠相对路径，再交给原 Open／Save As owner。界面中的 Save Working Copy As… 写入项目；Download Copy… 是下载导出。两者不应混作落盘验收。当前资格限于已测 Windows 11 x64、GCC/UCRT64 CPU 路线，不扩展到 Windows GPU／MPI、签名或安装器。

## 完整计算链路验收

窗口启动或初值 Preview 不能代替正式输出的端到端验收。使用最终便携包的独立 Host Node，通过原公开 Run／Restart 服务完成一个小型 CPU 算例，再由同一包读取其正式 Plotfile。检查 metadata、SHA 绑定的 raw slice／global display LOD／point 查询，并在 Real Plotfile 工作区打开同一文件；科学解析、renderer 校验器和既有 worker 限额保持原实现。

本次代表性 Sod 运行推进8步；原 checkpoint 比较器对完整运行与中途 Restart 的最终 checkpoint 给出 `max_abs=0`、`max_rel=0`。独立 Windows h5py 对包内 reader 返回的8个 DENS 样本、1个 point 样本及坐标完成 FP64 位比较，源文件 SHA 在读取前后相同。这些结果限定于此样本，不能外推为全部物理模型或全部文件系统已验证。可分发的范围与摘要见 [W0–W3 归档](../histroy/build/WindowsNativeQualification-20261026.zh-CN.md)。完整日志保留在本机忽略目录 `output/windows-w3-20261026/`，不随源码分发。
