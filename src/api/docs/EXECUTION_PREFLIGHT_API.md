# 运行准备接口

`--check-run` 使用正式运行的配置解析、模型 `SetupChecked` 和后端选择，返回当前计算将使用的后端与线性求解器。它在独立进程中检查运行依赖，不构造 AMR 网格、不执行 `Init`、不推进时间，也不写入运行记录。配置编辑与 Preview 使用各自的接口，不调用本查询。

## 调用

从项目根目录提交原始 UTF-8 配置字节：

```sh
build/linux-core-with-cuda/bin/ARCH --check-run Sod --config-stdin --request-id run-001 < simulation/Sod/Sod.par
```

Windows 使用对应构建目录的 `ARCH.exe`。从 `cmd.exe` 调用前，先执行该目录的 `arch-env.cmd`，再使用同样的参数和输入重定向。相对数据路径以查询进程的工作目录为基准。

stdin 上限为 1 MiB；单个 JSON 响应上限为 8 MiB。正常完成返回 0，配置、后端或运行库检查失败返回 3。进程使用独立的 300 秒 CPU、360 秒 wall 限额；Windows 的 CPU 计量为 Job user time。此查询不施加 Preview 的地址空间／提交量上限，以便按需加载 GPU 运行库。资源耗尽或进程异常退出时可能没有完整 JSON，客户端应报告查询失败。

MPI 构建可以在单进程中预检计划的进程数：

~~~sh
build-mpi/bin/ARCH --check-run Sod --config-stdin --mpi-ranks 4 --request-id mpi-001 < run.par
~~~

省略 --mpi-ranks 时为 1；必须为正 int。该选项仅属于 --check-run，不属于物理配置，也不启动 mpiexec。正式运行从 MPI_COMM_WORLD 读取实际大小。两者共用 ExecutionPreflight，检查固定笛卡尔网格、X 根块数量、边界配对、全局块容量及未支持的源项和 checkpoint/restart。多进程时 Auto 明确选 CPU，显式 CUDA 被拒绝；MPI 关闭的构建拒绝大于 1 的请求。

## 响应

外层字段为 `schemaVersion: "1.0"`、`kind: "execution-preflight"`、`version: "1"` 和 `status: "ok" | "error"`。

| 字段 | 含义 |
| --- | --- |
| `identity.caseId` | 本次查询的注册模型 |
| `identity.configRevision` | 原始输入字节的 SHA-256 |
| identity.requestId | 调用方提供的请求标识 |
| identity.mpiRanks | 本次查询的计划进程数，默认 1；调用方应一起核验 |
| data.mpiEnabled | 此程序是否编译了 MPI 支持 |
| `data.requestedBackend` | 输入请求的 `cpu`、`cuda` 或 `auto` |
| `data.resolvedBackend` | 实际选定的 `cpu` 或 `cuda` |
| `data.linearSolver` | `none`、`denselu`、`sparseklu` 或 `cudss` |
| `data.fallbackReason` | Auto 选择 CPU 的原因；没有回退时为空字符串 |
| `diagnostics` | 带 `severity`、`code`、`message` 的诊断 |

失败响应的 `data` 为 `null`，包含至少一条 error 诊断。非法 UTF-8 标识返回空字符串；有效标识和已读取输入字节的 SHA-256 保留。`execution.meshConstruction` 和 `execution.timeStepping` 均为 `not_executed`。客户端应同时核验退出码、响应状态和完整输入身份。

## 后端选择与运行确认

查询和正式启动共用 Core 的能力检查与后端选择。普通 CUDA 流体计算和 DenseLU 不加载 cuDSS；需要 GPU 稀疏求解时才检查配置时选定的 cuDSS、cuBLAS 和 cuBLASLt 库。显式 CUDA 或显式 cuDSS 的请求不能在缺少能力时静默替换。后端及求解器均为 Auto、且 CPU KLU 路线可用时，可在网格构造前选择 CPU，并返回原因。

Windows 直接链接 cuDSS 的构建还依赖进程启动前的 DLL 检查；生成的 CLI 环境脚本和 Studio 启动环境负责这一检查。缺失 DLL 时，Core 本身可能无法进入本接口。

Studio 在准备 Run／Restart 时显示选定后端和回退原因；确认时重新查询，并检查已保存配置、程序与重启输入的身份。结果变化会要求重新准备。正式启动仍重复原有检查，随后执行网格分配、初始化和 checkpoint 兼容性检查。

上述两个字段是 version 1 的增量信息，已有单进程字段保持语义。Studio 当前仍按单进程准备和启动；此变更不表示 Studio 已具备 MPI 启动或分片读取能力。预检只检查当前程序和配置能力，不证明集群网络、节点环境或资源可用。
