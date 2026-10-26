# Windows Studio 平台服务：W2

[English](WindowsStudioPlatform.md) · [原生 CPU CLI](WindowsNativeCpu.zh-CN.md)

W2 在既有 Host 状态机下补充 Windows 进程原语。当前已实测进程监督、Configure
完成、重复与取消；经 A2 批准的 Build 证据与 manifest 发布也已接入同一状态机。
经 A3–A6 批准的源码读取、配置保存、Plotfile、Run／Restart 已取得下述原生证据。
A7–A10 进一步接入明确的 Windows Preview 资源契约与有限范围构建准入；
W3 的原生窗口软件链路、A11 与便携包验证见[桌面构建指南](WindowsStudioDesktop.zh-CN.md)；
OS 对话框保存及真实 Core 输出显示已在 W3 单独验收，不混入本节 W2 证据。日常使用见两侧共用的 [Studio 指南](Studio.zh-CN.md)。
所有原生结论来自 Windows PE／Node 执行，不以 WSL 测试代替。

## 共享实现与构建

`studio/host/hostProcess.ts` 只处理进程所有权。Linux 继续使用原进程组；Windows
使用同一 CMake 图中的 `arch_windows_process`。Node 标准库没有 Job Object、
进程创建时绑定 Job 或进程创建时间 handle 接口，因此需要这个小型原生目标。
它不链接求解器实现，不增加工作流、GUI、科学读写或第二套配置判定。

辅助程序通过 Windows 10+ Job-list 属性在创建进程时绑定 Job，成功后才恢复
子线程；不提供未监督启动的回退。Job 与父进程 handle 不可继承；除标准流外，Run 仅通过明确的 handle
allowlist 继承输出租约和目录 pin。Job handle 关闭时回收任务子树；正常主进程结束也清理残留
后代。Configure／Build 取消作用于当前持有的辅助进程。Run Stop 走受控 stdin，
辅助程序确认 Job drain 后返回 130；未知退出不能记为 stopped。不用 `taskkill` 或 PID 枚举。

父进程 PID 与创建 FILETIME 必须匹配，再以同一打开的 handle 等待其退出。
身份查询也在同一 handle 上取得创建时间与存活状态。父进程退出的检测和终止
存在调度延迟，不承诺退出后子进程零指令；子进程始终已处于 Job 中。若父进程
与工具同时结束，保留失败判定，不在所有者消失后推导成功。

工具退出码完整转发；辅助程序自身失败使用 125 并带 `ARCH process supervisor:`
诊断。工具也可能返回 125，因此不能只凭该数值区分来源，需看日志。共享 Configure
将非零结果表述为命令或监督失败，不把未知状态写成成功。

沿用 CPU CLI 指南中的 `$ucrt`、`$build`、`$install` 和同 ABI DLL PATH：

```powershell
& "$ucrt/bin/cmake.exe" --build $build --target arch_windows_process arch_windows_build_publish --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Process supervisor build failed' }
& "$ucrt/bin/cmake.exe" --install $build --component Runtime --prefix $install
if ($LASTEXITCODE -ne 0) { throw 'Runtime installation failed' }
```

构建目标位于 `$build/arch_windows_process.exe` 和
`$build/arch_windows_build_publish.exe`，Runtime 安装复制到
`$install/bin/arch_windows_process.exe` 和 `$install/bin/arch_windows_build_publish.exe`。安装也沿用原 Runtime 的其他复制行为，
不是独立桌面发行包。本轮未创建 release 或安装包。

可信 Host 初始化可为 `ConfigureProfile.windowsTools` 提供绝对 `cmake.exe`、
`supervisor` 与固定 `path`；Build 还必须提供同 ABI 的 `ninja`、`objdump`、
`rc`（GNU windres）和 `publisher`。`localCpuProfile(root, windowsTools)` 可将这些可信
工具交给已有 Configure/Profile，`openProject` 再传给同一 BuildRunner。
它不是 HTTP 配置项，浏览器不能提供程序、argv、
环境或 shell。没有这个显式配置时，Windows Configure 拒绝启动。工具路径限于
本机盘符绝对 `.exe`；UNC／设备路径不由本轮结果推导。

## 路径与功能边界

启动选择将当前宿主绝对路径转换成既有 `/` 分隔的项目相对身份，再交给原
`selectedPath`。越界路径、依赖隐含工作目录的 `C:relative` 和 Windows 设备路径
被拒绝；内部路径准入保持。盘符路径两种斜杠可用于 Windows 入口；Linux／WSL
路径仍属于相应宿主，不能仅换斜杠就迁移身份或访问权限。

| 路线 | 当前状态与原因 |
| --- | --- |
| 原生 CPU CLI | 沿用 W1 限定证据；本轮没有重新签发全部 CLI／科学资格 |
| 进程原语 | 原生覆盖宽字符 argv、标准流、退出码、子树清理、父进程退出与创建身份 |
| Configure 取消 | 复用原状态机；原生 CMake 实际进入工作后取消，等待退出并保留 cancelled |
| Configure 完成 | 经批准的 A1 路径比较适配后，真实 ARCH 生产图完成首次与重复配置，记录实际输入 SHA；不等于完整编译／链接依赖证据 |
| Build | A2／A8 验证原生 Configure → ARCH Build → manifest → 重开，已记录输入可为 current；完整 freshness 保持 unknown |
| Preview | A7／A9 使用明确的 Windows commit／user CPU 策略和严格 capability，接入原 Host session；Linux 继续原资源契约 |
| Run／Restart | A4a／A5 共用原状态机，原生独立终端、输出互斥、Host 退出后继续运行、恢复身份与 Stop 已测；真实 Sod Run／CHK Restart 零阈值比较通过 |
| 保存、源码与 Plotfile 读取 | A3／A4b／A5 的 OS 边界已接入原 owner；h5wasm／科学解析不变；A6 只接受已存在的精确 Windows main-image 标签 |

Job process-memory 限制针对提交内存，不等于 Linux `RLIMIT_AS`。本轮没有用它
替换 Linux 地址空间限制。A7 明确批准 Windows 专属的 commit／user CPU 定义，
并保留 Linux 原预算。数学实现、科学阈值、科学数据 schema 与外部 HDF 库保持；
本页列明的 A1–A10 之外的 Studio 冻结读写例外仍须先审核。

A1 共用一个 CMake 路径比较函数：Windows 按宿主规则规范分隔符和点段，统一
盘符大小写，目录／文件名大小写仍精确；Linux 保留原字符串比较。真实 CMake
缓存会将 `C:` 写成 `c:`，重复 Configure 也使用同一比较规则，仍拒绝外来树、
错生成器及重复绑定键。原指纹、读取上限、缺失／越界／变动检测均保留。

## 验证与复现

原生 Node 使用核对官方 SHA-256 的 Windows x64 24.21.0 便携版，没有修改系统
PATH。在已设置同 ABI DLL PATH 的 PowerShell 中运行：

```powershell
$node = 'C:/path/to/node.exe'
$env:ARCH_TEST_CMAKE = "$ucrt/bin/cmake.exe"
$env:ARCH_TEST_SUPERVISOR = "$build/arch_windows_process.exe"
& $node --test --test-concurrency=1 "$repo/studio/tests/host-process.test.ts" `
  "$repo/studio/tests/host-cmake-paths.test.ts"
if ($LASTEXITCODE -ne 0) { throw 'Native process checks failed' }
```

实际 15 项通过，无跳过，含实际 Configure、重复配置、错绑定拒绝，以及 SHA、
读取上限、版本和越界控制。强制结束辅助进程会清理 detached 后代、保留无关
进程；父进程退出回收其 Job；独立 worker 在 Host 退出后继续存活、在自身退出时
回收任务。这是最初的进程原语证据；A4–A6 的 Run 交接证据另列于下文。

A1 的 Linux 证据／Configure／Build 定点回归 33 项通过、无跳过。真实生产图
首次配置记录 577 项输入，后续缓存配置两次各记录 491 项；保留实际集合与原始
reply SHA，不把不同配置阶段强行记成同一集合。CMake／Ninja 二进制指纹也核对。
TypeScript 与所改文件 lint 通过。以上不涵盖真实 Electron 窗口、完整
GUI、所有文件系统或科学模型；Zenodo 和 release 仍暂缓。

## A2 构建证据与发布

Build 复用原状态机和 manifest 内容 owner。可信 CMake/Ninja、GNU driver、
所选 linker、subprograms、specs 与实际 DLL 插件均记录 SHA。Windows RC 的 File API
描述缺少 id/version 时，仅对明确绑定的 `rc` 执行 `windres --version`，保留其实际
版本与驱动 SHA；C/C++ 身份缺失仍拒绝。Windows GNUld depfile 支持盘符及两种
分隔符，保留目标绑定、Make 转义与 ADS／扩展／通配符拒绝。

启用 `ARCH_EMIT_LINK_DEPENDENCIES=ON` 时，GNU windres 额外生成真实预处理 depfile，
补足 CMake 原规则的空 RC 记录；原 Ninja parser 仍拒绝零计数、缺项和 STALE。
`ARCH_RETAIN_LTO_LINK_INPUTS=ON` 保留实际 LTO 链接输入，不关闭 IPO 或改数学选项。
Host 在本构建树的 `.studio-link-inputs/<build-id>` 中设置受控 TMPDIR/TEMP/TMP。
本机 GNUld depfile 列出原始 IR 对象／库，未列出保留的 LTRANS 对象；保留文件存在
不等于其已纳入链接快照，覆盖仍未知。删除已记录输入会使证据失效，不能把缺失
对象排除后继续宣称匹配。

Linux runtime v1 及原字段保持；Windows 使用内部 PE v2，记录实际 objdump、工具与
helper、imports、搜索顺序依据、候选文件及前后身份。系统/API-set、未解析 delay
import、动态加载和未建模 loader 选择继续显式未知，`dependenciesComplete=false`。
这不是科学数据 schema，也不证明实际 DLL 加载穷尽。实测 cc1plus 的 `objdump -p`
输出约 35.6 MB，包含大量 unwind 表；PE 限制为每次 64 MiB、全图 256 MiB，ELF
原 1 MiB 不变。图仍受 32 roots、256 nodes、8192 edges、每项 256 candidates 限制。

`arch_windows_build_publish` 只接收既有 owner 生成的不超过 4 MiB 的 manifest 字节。
它 pin 每层非 reparse 父目录，独占创建 owner/SYSTEM 私有临时文件，flush 并检查
close，通过保留的源文件句柄进行同目录原子替换；不使用路径 rename 回退。
采用 Native API 的 simple-name/NULL-root 同目录契约，参见
[微软 FILE_RENAME_INFORMATION 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information)。
测试明确使用不同 cwd。目标被 reader 占用可能拒绝替换；失败保留旧完整文件并清理
本次临时文件。提交后的错误会报告 committed，不承诺回滚。未承诺断电持久性、
完整 ACL 模拟、业务 CAS 或在初始 pin 前消除所有并发变化。

## A2 复现与资格范围

沿用上面的原生工具和 DLL PATH，设置新增测试入口：

```powershell
$env:ARCH_TEST_NINJA = "$ucrt/bin/ninja.exe"
$env:ARCH_TEST_CXX = "$ucrt/bin/g++.exe"
$env:ARCH_TEST_RC = "$ucrt/bin/windres.exe"
$env:ARCH_TEST_OBJDUMP = "$ucrt/bin/objdump.exe"
$env:ARCH_TEST_PUBLISHER = "$build/arch_windows_build_publish.exe"
& $node --test --test-concurrency=1 "$repo/studio/tests/host-link-windows.test.ts" `
  "$repo/studio/tests/host-windows-runtime.test.ts" `
  "$repo/studio/tests/host-windows-build.test.ts"
if ($LASTEXITCODE -ne 0) { throw 'Native Build evidence checks failed' }
```

在已按 CPU 指南配置 `BUILD_TESTING=ON` 的原生测试树中，发布控制可独立构建运行：

```powershell
& "$ucrt/bin/cmake.exe" --build $build --target arch_windows_build_publish_test --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Publisher test build failed' }
& "$ucrt/bin/ctest.exe" --test-dir $build -R '^windows_build_manifest_publication$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Publisher controls failed' }
```

本轮实际原生证据：A1 的 15 项回归通过；A2 的 11 项 Node 检查无跳过，覆盖真实
Configure/Build/重开、源变动、链接输入缺失、编译失败保留旧 manifest、畸形证据与
工具绑定拒绝；13 项原生发布控制通过。真实 ARCH 生产图两次 Build 和重开均取得
可验证记录：491 项 CMake 输入、912 个 compiler 输入文件、48 个 PE runtime 节点，
链接缺失数为零。已匹配的记录继续标为 freshness-unknown，不将系统边界或不完备
依赖升格为就绪。Linux 同范围 62 项通过、10 项 Windows 专属跳过；TypeScript/lint
通过。这不是重新签发全部科学测试或 W2 完全关闭。

实测 GCC 16.2 在中文 retained-LTO 临时路径下找不到 resolution 文件。本轮只认证
ASCII source/build 路径（空格已测），未关闭 LTO 或放松缺失输入检查；中文安装／
运行目录沿用 W1 的限定证据。建议把 LTO 构建树置于 ASCII 路径，后续另行解决
工具链的 Unicode 能力。源码／保存／Plotfile／Run 的新增资格见下文。Preview 限额继续冻结；
新增例外需审核，Zenodo 和 release 继续暂缓。


## A3–A6：原 owner 中的平台适配

`windowsFilePublish.ts` 复用原生 `arch_windows_build_publish` 的 OS 操作；历史目标名
保留，不新增第二套配置或 HDF 库。`WindowsDirectoryPins.h` 由文件操作与 Run 输出
租约共用。各内容 owner 仍负责自己的验证、状态和格式：

| 操作 | 保留的契约与 Windows 边界 |
| --- | --- |
| 源码读取 A3a | 256 KiB、严格 UTF-8、BOM／CRLF 原字节、SHA 与路径准入；Windows 对比打开／读取／重开后的文件身份，Linux 保留 `/proc` 检查 |
| Save／SaveAs A3b | 原配置解析与指纹冲突检查；原生独占私有临时文件、flush／检查 close、保留句柄的同目录发布；SaveAs 不覆盖 |
| Plotfile A4b | 原 h5wasm、原科学解析和 64 MiB 上限；Windows pin 非 reparse 目录并持有只读共享的文件租约，解析期间阻止替换／写入 |
| Host 集成 A5 | `configuration`、隔离 metadata worker 与项目入口只传递既有可信工具上下文；不新增 HTTP 命令、环境或工具参数 |
| 来源标签 A6 | 只增加 `Windows mapped main executable`，仍接受原 Linux 标签；64 hex SHA、main-executable-only、null 一致性、正式记录哈希和未知标签拒绝保留 |

配置写能力由 helper 的 `windows-file-1` capability 实测探针决定，不因路径存在就
宣称可写。Save 保留同 owner 目标的 DACL ACE 与保护状态；NULL／缺失 DACL、异 owner、
readonly／reparse 目标保守拒绝。没有承诺完整 security descriptor／SACL 模拟或业务 CAS。
准备阶段的临时文件有内核 delete-on-close；解除该标记至 rename 的很小提交窗口、
提交后强制终止与断电结果仍可能未知，不承诺一切崩溃均清理或回滚。提交后错误明确
报告已提交或需重新读取。参见 [微软 disposition 扩展契约](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information_ex)。

read lease 的 15 秒限制仅用于取得租约；取得后维持到原解析结束并显式释放，已实测
持有 15.5 秒仍拒绝写入。隔离 reader 的原 15 秒 wall、64 KiB 响应与 256 MiB Node heap
约束保留；heap 不是对 WASM／进程 RSS 的硬上限，也不等同 Core Preview 的限额。

## Run、终端与可信入口

Run 的 Windows 输出互斥位于 Known Folder `LocalAppData/ARCHStudioRunLocks-v1`，
不依赖各 Host 的 TEMP。目录／锁文件只授权当前用户与 SYSTEM；不删除锁文件来
释放身份。Core 继承受控租约，辅助程序等待整个 Job 清理后才发布终态。恢复检查
PID、创建 FILETIME 与存活状态；已保存记录本身不是进程仍存活的证据。

默认原生 console 使用独立 console／Job breakaway。若调用环境的外层 Job 禁止
breakaway，会明确失败。本机 WSL 执行器复现了 Win32 5；实测交接改用已安装的
Windows Terminal 1.24.12741.0，并由可信 `windowsTools.terminal` 指定 `wt.exe` 绝对路径。
helper 编译不要求安装 Terminal。Terminal 使用 `-w new new-tab`；项目与固定工具上下文
以 base64url bootstrap 传给原 worker，固定 executable／script 路径拒绝终端分号命令
分隔符。参见 [Windows Terminal 命令行](https://learn.microsoft.com/en-us/windows/terminal/command-line-arguments)。

工具设置属于 Host 程序初始化，不属于项目配置、job.json 或 HTTP 参数。当前桌面／
开发 CLI 没有自动发现这些工具的产品入口；本页资格针对显式可信 Host 上下文，
不据此宣称普通 Windows Electron 启动已经完成。已有 API 可复用同一个 profile：

```typescript
const profiles = localCpuProfile(root, tools); // tools: WindowsHostTools
const reader = await openProject({
  project: root, buildProfile: profiles.build.id,
  trustedBuildProfile: profiles.build, configureProfile: profiles.configure,
  config: 'simulation/Sod/Sod_beginner.par',
});
```

`root` 应先 `realpath`；`tools` 包含本页前述固定 ABI 路径。配置所需的 C/C++ compiler、
依赖 prefix 与 HighFive 来源仍沿 CPU 指南，通过可信 Configure definitions 指定。
不会从 HTTP 或 PAR 文件推导可执行程序。Build freshness 保持 unknown 时，原工作流
不能据此自动声称 current Build；实际 Run 测试使用显式选定且验证 SHA 的编译程序。

## A3–A6 复现与证据范围

沿前述环境增加测试工具与终端（需要执行真实交接用例时）：

```powershell
$env:ARCH_TEST_TERMINAL = "$env:LOCALAPPDATA/Microsoft/WindowsApps/wt.exe"
& $node --test --test-concurrency=1 --test-reporter=tap `
  "$repo/studio/tests/host-process.test.ts" "$repo/studio/tests/host-cmake-paths.test.ts" `
  "$repo/studio/tests/host-windows-run.test.ts" "$repo/studio/tests/host-windows-handoff.test.ts" `
  "$repo/studio/tests/host-source-stability.test.ts" "$repo/studio/tests/host-windows-config.test.ts" `
  "$repo/studio/tests/host-windows-plotfile.test.ts"
if ($LASTEXITCODE -ne 0) { throw 'Native platform checks failed' }
& $node --test --test-concurrency=1 "$repo/studio/tests/plotfile-windows-identity.test.ts"
if ($LASTEXITCODE -ne 0) { throw 'Platform source identity checks failed' }
& "$ucrt/bin/cmake.exe" --build $build --target arch_windows_build_publish_test --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'File operation test build failed' }
& "$ucrt/bin/ctest.exe" --test-dir $build `
  -R '^windows_(build_manifest_publication|config_and_read_operations)$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'File operation controls failed' }
```

当前原生平台组 43 项、Build／PE／链接与 A6 组 12 项，均通过且无跳过；文件操作
2 个 CTest 含 22 项控制通过。Linux 最终 I/O／Run 组 121 项通过、5 项 Windows 专属
跳过，Build／Configure／入口组 63 项通过、9 项跳过；启动失败释放修复后，隔离读取／HTTP／
正式来源再回归 41 项通过。Linux 共用 CMake 图与 4 项 Core/I/O CTest 通过；TypeScript／lint 通过。
真实 Windows 中文／空格目录下，Sod Run 与第 1 步 CHK 到第 8 步 Restart 完成；
原比较器四个阈值全部为 0，max_abs／max_rel 为 0。A6 对原已完成的输出只读复验
通过，未以重跑隐藏原标签拒绝。该样例验证工程链路，不重新签发全部科学资格。

A3–A6 检查点时，Preview session／初始化／AMR 尚未认证；当时
`--inspect-case Sod --config-stdin` 返回 Linux-only 限制错误。A7–A10 已按下述明确
Windows 契约处理此阻断；
未以 Job 的 committed memory 替代 RLIMIT_AS。真实 Electron／安装包、UNC、Unicode
LTO 构建、其他文件系统与全面科学矩阵不在本次资格内。Zenodo、release 仍暂缓。


## A7–A10：Preview 资源与已记录构建输入准入

资源限制仍由 `WorkerLimits.cpp` 负责。Windows 在既有监督 Job 内建立私有资源
Job，限制每进程 **1024 MiB committed memory**，CPU 使用资源 Job 的累计
**user-mode CPU**。地址空间预留不计作提交；它不等于 RLIMIT_AS、RSS 或 working
set。若产生后代，其 user CPU 也消耗该资源 Job 的配额。实现不修改父 Job，不使用
breakaway，不在资源 handle 关闭时杀死当前进程。Preview OpenMP 保持单线程；
Configure、Build、Run、metadata worker 不统一套用这组 Preview 限额。

会话的 field／case 请求每次续设 300 秒 user CPU，mesh／资源查询每次 30 秒。
一次性 worker 使用含启动时间的累计进程 ceiling，重复调用只收紧。Windows
按周期检查 CPU 配额：本机 1 秒负控制实际在数秒后由内核以 quota 状态终止，
因此不承诺亚秒级精确终止。既有 Host 360／45 秒 wall 限额、帧大小、序列、队列、
取消和进程所有权回收均单独保留。参见
[微软 Job CPU／内存语义](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-jobobject_basic_limit_information)。

Windows session、inspection 与 AMR capability 明确携带以下精确策略，并省略
`processAddressSpaceMiB`：

```json
{"kind":"windows-job-commit-user-v1","memoryKind":"committed","processCommitMiB":1024,"cpuClock":"user","cpuScope":"resource-job","budgetRenewal":"per-request"}
```

Host 拒绝缺失、未知或放宽的 Windows 策略。纯谓词放在既有
`workflowValidation.ts` 供 Host 共用；A9 只兼容 registry 平台元数据。
A10 仅更新 Core registry 平台宏及逐 case 平台／策略元数据，原源码摘要、已审核
单位表和全部科学数据校验保留。小型值构造函数 inline 于既有 PreviewSession
声明头，独立 inspection probe 不因此链接 session 运行实现。Linux 响应保持原格式。

A8 仅为短暂的 `BuildSnapshot` 增加可选 `observedInputs`，scope 为
`observed-build-inputs-v1`，state 为 `current`／`changed`／`unknown`。
只有可信 Windows CPU profile 明确启用；Linux 和旧 profile 不自动启用。
原 `BuildRunner` 在同一次扫描中基于现有指纹、CMake／compiler／link／runtime
证据及严格稳定性／error 检查生成三态。RC 仍使用精确的 `GNU windres` 身份。
Preview 通过同一 owner 的 `requireBuildEvidence` 准入，并在能力查询后复查当前
状态；并发扫描发布 unknown 时，不能发布过期 readiness。

**已记录输入 current 不代表完整依赖 freshness 已知。**
`binaryState=freshness-unknown`、`dependenciesComplete=false`、系统／API-set
未闭合边，以及 dynamic loader／未记录 LTO 中间物的限制均保留。每个已记录的
物理候选仍检查 SHA。缺失／变动、错误、稳定性未建立或 delay import 未解析时
拒绝正向准入。没有新增 manifest 版本、持久化 schema、completeness 值或
BinaryBuildState token。旧 profile／manifest 保留兼容；新增 scope 进入原有
profile fingerprint，必须有与新指纹匹配的成功 Build。

仍用相同 ABI 工具链、原 CMake 图和 IPO／LTO 选项构建 ARCH 与既有 helper；
没有新增运行库或科学序列化实现。独立资源验收程序的原生构建命令如下：

```powershell
Push-Location $repo
try {
  & "$ucrt/bin/g++.exe" -std=c++20 -fopenmp -O2 -Wall -Wextra -Wpedantic -Werror `
    -Isrc tests/api/preview/test_windows_worker_limits.cpp src/api/resources/WorkerLimits.cpp `
    -o "$build/windows-worker-limits.exe"
  if ($LASTEXITCODE -ne 0) { throw 'Resource harness build failed' }
  foreach ($mode in @('info','startup-info','reserve2gib','commit-over-1gib','single-info',
      'renewal','tighter-parent-memory','cpu-over','tighter-parent-cpu')) {
    & "$build/windows-worker-limits.exe" $mode
    if ($LASTEXITCODE -ne 0) { throw "Resource check failed: $mode" }
  }
  & $node --test --test-concurrency=1 studio/tests/build-observed-contract.test.ts `
    studio/tests/host-preview-windows-platform.test.ts
  if ($LASTEXITCODE -ne 0) { throw 'Metadata/platform contract checks failed' }
} finally { Pop-Location }
```

沿用前述同 ABI DLL PATH 和 `ARCH_TEST_SUPERVISOR`。资源检查包括真实 CPU
超额终止、十二次续额、commit 拒绝、reserve／commit 区别和更紧父 CPU／内存
限制，不创建或触碰多 GiB 数据数组。实际生产 Build 记录与这些小型原语分开
验收；仅编译成功不能推导 Preview、GUI 打包或所有物理模型已合格。


A7–A10 原生验收覆盖最终生产 Build／重开、九个资源模式、十二项记录证据／并发
控制，以及原有独立 inspection probe。最终真实 Host HTTP 的 registry、字段
Preview、case inspection、资源估算和初始 AMR 全部通过。会话内三次 Sod 参数
切换与单次 Core 数据精确一致。原生平台组 56 通过／1 项 Linux 专属跳过；新增
生命周期／元数据组 14 通过／1 跳过，两组有重叠，不相加。Linux Host 两组分别
67／61 通过，9／4 项 Windows 专属跳过，五个定点 Core CTest 通过。这是有限的
Host／初始化证据，不是全部科学模型或 Electron 资格。最终生产记录仍保留
552 条 runtime 边界，独立哈希读取核对 1,313 个文件身份、无不匹配。

重建后的首次能力查询曾在并行重读文件时触发保留的 3 秒上限，按失败关闭；
在无并行重读负载时，以相同上限复测通过。该失败收据保留，不保证任意机器或
负载下的启动始终低于 3 秒。
