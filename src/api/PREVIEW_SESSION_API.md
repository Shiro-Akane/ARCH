# 本地预览会话接口 v1

本增量接在 `97a2b50c440473dfe93ab82617c5631e8b9da24a` 后，交付分支为 `codex/studio-core-ui-contracts`。保留冻结 main 基线，不合入 Studio 分支历史。

目标是首次载入完成资源准备，后续编辑复用已验证的表资源。所有请求仍执行真实、完整的初始化；没有增加低精度求解模式，没有缓存上一份场值，也没有修改模型的迭代次数、公式和收敛条件。

## 分工

| 内容 | Core 本轮提供 | Host / Studio 接入工作 |
|---|---|---|
| 重复预览 | `--preview-session` 串行会话 | 为当前项目、工作目录和 binary 管理会话 |
| 表资源复用 | 一份解析后的 EOS 表；使用独立拥有的组分数据，给每次请求重新绑定视图 | 保持进程存活，不在每次改参时重启 |
| 文件内容一致性 | 每次请求前后读取完整内容核对；相同字节可复用摘要 | 表来源或构建改变时标记当前图过期；重建 binary 后重启会话 |
| 重复采样状态 | 每个坐标都执行 Init；同次请求内逐位相同的初始状态复用 EOS 转换 | 无须模型专用绘图公式 |
| 参数变化 | 新配置、新模型实例，重新 Setup/Init；不猜测 custom 参数依赖 | 发送编辑器当前完整 `.par` 文本 |
| 进度和诊断 | 阶段事件、总耗时、分阶段耗时、缓存统计、原有错误响应 | 显示阶段，保留取消入口；不将阶段换算成虚构百分比 |
| 更新频率 | 单次请求处理，不维护编辑队列 | 输入防抖、至多一个执行中请求和一个最新待处理版本 |
| 取消和超时 | 进程资源限制；可终止的独立会话 | 终止并回收会话，丢弃未完成结果，下次重新启动 |
| 图像一致性 | 请求身份、配置摘要和原有 EOS 来源指纹 | 旧图与旧 Inspector 保持配对，新结果整体替换；AMR 叠层核对身份 |
| 显示设置 | 返回原始值和 Log 统计 | Linear/Log、配色、裁剪、已有数据缩放仅重绘 |

## 启动和能力协商

```sh
ARCH --preview-capabilities
ARCH --preview-session
```

读取 `extensions.session`。v1 仅 Linux/WSL CPU，`supported=true` 时使用。旧单次 CLI 继续接受 stdin 原始 `.par`，退出时返回一个 JSON；会话使用下面的 NDJSON 协议。没有此能力的 binary 继续走旧流程。

Host 设置项目的运行工作目录，使用参数数组直接启动 binary，不通过 shell 拼接用户输入。该进程只用于预览。项目、工作目录、可执行文件或构建版本切换时结束旧会话并重新启动。源码改动仍按现有构建过期提示处理。

会话启动后立即返回一行 `preview-session-ready`，含 `version="1"`、`sequence=0` 和 `capability`。此时只表示可接收请求；尚未加载表、初始化模型或生成场图。

## 请求

每份请求是单行 JSON 对象，以 LF 结束；`configText` 中的换行使用 JSON 转义。使用 `JSON.stringify(request) + "\n"` 即可。**保持 stdin 打开**，不要沿用旧单次调用的写入后立即关闭方式。

```json
{"command":"--preview","caseId":"Sod","requestId":"edit-001","configText":"nblockx1=1\nnblockx2=0\nnblockx3=0\nnetwork_name=none\nx_pos=.35\n","samples":256}
```

共同必填字符串为 `command/caseId/requestId/configText`。caseId、requestId 非空且各不超过 128 UTF-8 字节；configText 非空，不超过 1 MiB，不含 NUL。Core 仍通过唯一的 RuntimeParams 解析配置，不要求用户保存 `.par`。

| command | 可选整数成员 |
|---|---|
| `--inspect-config` | 无 |
| `--amr-resources` | 无 |
| `--inspect-case` | 无 |
| `--preview` | Sod：`samples`；CellularDet：`samplesX1` 与 `samplesX2` 同时提供 |
| `--preview-amr` | `meshMaxBlocks`、`meshMemoryMiB` |

范围、默认值、维数、采样排列和模型支持范围与相应旧命令相同。整数字段不接受 JSON 小数或指数形式。未知字段、重复字段、错误类型、未知命令被拒绝。传输对象只接受字符串和整数成员；不接受嵌套对象、数组、布尔值或 null。

发现/schema 查询仍使用原有便宜的单次命令。新增会话不扩展场图或网格支持列表：模型检查覆盖 11 个现有模型，完整场图和 AMR 当前仍为 Sod 1D / CellularDet 2D。

需要主动释放资源时发送：

```json
{"command":"reset-resources","requestId":"reset-001"}
```

该请求只接受这两个成员。返回 `preview-session-reset`、`status="ok"`，释放表和文件字节缓存。下一份重计算请求重新准备资源。它只在会话空闲时提交，不承担取消功能。

## 响应和进度

stdout 每行是一个独立 JSON 事件。Host 必须持续读取并限制单行字节数，不能等待 EOF 后才解析。stderr 同时持续收集；底层库的诊断可能写入 stderr。

所有请求事件含 `version/sequence/identity`。sequence 在当前进程内单调增加；Host 另外维护自己的进程代号和 binary 身份，不能拿旧进程的 sequence 匹配新进程。

正常请求的 identity 包含 requestId、caseId，以及完整原始参数文本的 SHA-256 `configRevision`。它与嵌套 Core 响应的 identity 相同。requestId 应由 Host 为每次提交新建。

先返回若干 `preview-session-progress`，成员还有 command、stage、elapsedMilliseconds。可能的 stage 为 request、configuration、support、setup、eos、sampling、initialization、initial-refinement、source-validation、complete。各命令的阶段不同，某些阶段可以重复或不出现。

`complete` 阶段事件不是成功结果。必须等待最终 `preview-session-result`：

```text
kind: preview-session-result
version: "1"
sequence: 当前会话的请求序号
identity: 当前请求身份
command: 原请求 command
exitCode: 原单次接口的退出码语义
elapsedMilliseconds: 本次请求处理耗时，不含 Host 排队/界面绘图
stages: [{stage, milliseconds}, ...]
resources:
  tableLoads: 本次加载表的次数
  tableHits: 本次 EOS 查询复用表的次数
  retainedTables: 请求结束后保留 0 或 1 份表
  fileContentMatches: 本次完整文件字节比较相同的次数
  fileHashes: 本次实际计算文件 SHA-256 的次数
  retainedFileBytes: 请求结束后保留的文件字节数
  sampleEvaluation: 本次场采样计数，其他命令为 null
  clearedAfterError: 是否因请求错误清空资源
  resultReused: false
response: 原命令完整 JSON 对象
```

`response` 是对象，无须再次解析字符串。直接交给原有各命令响应处理器。exitCode 非零或 response.status=error 时不能采用 data；AMR 的 limited 状态继续遵守初始网格接口的规则。进程正常存活不代表每个请求成功。

tableHits 可以发生在同一个请求的多次 EOS 查询之间。确认跨请求复用应查看后续请求 `tableLoads=0` 且 `tableHits>0`。没有调用 EOS 的命令也可能 tableLoads=0，不能单独据此显示“缓存命中”。

JSON 或信封参数错误返回 `preview-session-error`，含 code/message/fatal。语法错误的 identity 可能为 null，其他输入错误可能只有 requestId；Host 用唯一执行中的请求对应该错误，不继续显示该请求的成功状态。完整一行的输入错误可恢复并清空资源。超长行、未以换行结束的残缺输入和会话级故障关闭会话，fatal=true。

stdin 正常 EOF 返回 `preview-session-closed`，reason=stdin-eof。处理完 256 份请求（含 reset 和无效完整行）后返回 reason=request-limit，正常退出；Host 按需启动下一会话。

## 缓存正确性和资源范围

只保留一份已解析的 EOS 表。命中条件包括 EOS 类型、表路径、完整来源摘要，以及有序组分的名称、A/Z、gamma/Cv 等值。组分变化、表内容变化或表类型切换时重新加载。每个请求的模型、配置和组分对象都重新创建；表 owner 自己拥有组分副本，不保存上一请求的悬空引用。普通模拟不安装此会话缓存。

每次完整初始化请求第一次使用 EOS 来源时读取完整内容，结束前再读取完整内容核对；初次加载还保留加载前后校验。相同文件字节可复用已有 SHA-256，**不以路径、文件长度、mtime 或文件监视器作为内容一致性的证明**。同长度、同 mtime 的文件内容修改仍被识别。

文件字节缓存总量最多 128 MiB、最多 8 个文件。超出预算的文件使用正常流式 SHA-256；空间不足时清理缓存，正确性不变。缓存没有命中或被清理只影响速度。所有资源都受会话进程 1 GiB 地址空间上限约束；该上限包含缓存、EOS 表、网格和临时内存，不等于可用物理内存承诺。

输入或初始化错误后清空缓存。取消、超时、进程退出、重编译也会失去资源，下次是首次准备。会话不缓存任意 C++ 外部副作用，不提供操作系统安全沙箱。用户新增模型仍需遵守每次 Setup/Init 独立初始化的约定；会话优化不推断 custom 参数依赖，不证明任意外部文件均被追踪。

EOS 文件变化发生在一次请求中时，原接口返回 EOS_SOURCE_CHANGED，数据不发布。场图与网格分开请求时，Host 除了核对配置/binary 身份，还要核对两份结果 `state.eos.sourceFingerprint`；不同来源版本的图与网格不可叠加。IdealGas 此字段为 null。

会话外层的 `resources.sampleEvaluation` 提供 initCalls、eosConversions、exactStateReuses、scope 和 maxRetainedStates；旧场图 response.execution 结构保持原样。每个坐标仍调用真实 Init；同一个请求、同一 EOS 下，只有密度、压力、温度、三个速度、温度输入标志和完整组分数组逐位相同，才复用转换结果。无近似比较、舍入或插值，不跨请求保留。最多保存 1024 个状态、1 MiB 键和值的数据负载，达到上限后正常转换。真实 AMR 的初始化与加密流程不使用此采样缓存。

## 预算、取消和调度

- 会话内一次只提交一份请求。编辑过程中由 Host 保留最新待处理版本，避免把每个按键都写入 stdin 排队。
- 模型检查与场图请求：300 秒 CPU / 360 秒 Host 墙钟上限；AMR 网格请求：30 秒 CPU / 45 秒 Host 墙钟上限。配置检查和资源估算使用 30 秒 CPU，Host 可沿用 45 秒上限。预算不是预计耗时。
- Linux CPU 计时累计整个进程；实现按已消耗 CPU 时间设置下一请求的截止值，且保留启动时继承的更严格生命周期限制，不让先前 AMR 请求永久降低后续检查预算。
- 取消或超时由 Host 终止、必要时强制杀死并回收会话。取消信号不通过 stdin 排队；下一请求启动新会话。丢弃被取消进程之后到达的所有结果。
- 阶段消息不重置墙钟截止时间。没有完整最终事件、异常退出、身份不匹配时，不采用结果。
- 正常连续编辑可等待当前任务完成后提交最新版本，保留资源。用户主动取消、切项目、切 binary 时立即停止。请求期间改参产生的新版本必须标记当前任务过期。
- 需要即时配置检查时，可独立调用便宜的 `--inspect-config`；不要把它排在较长的初始化任务后面阻塞表单。其结果仍按配置摘要对应当前编辑版本。
- 普通输入先以约 300 ms 防抖作为可调整的交互起点；拖动中的标记立即反馈，释放后提交。这由 Studio/Host 实现，Core 不设置输入延迟。

## 图像与 Inspector

保留上一张成功图，标记“参数已修改，正在更新”；Inspector 继续显示这张图的数据和身份。新场图成功后整体替换。失败保留输入与上一张成功图，并明确它不是当前参数的结果。显示设置仅变换现有数据，不改动原数组。

场图和 AMR 可依次返回，但只叠加同配置、同构建、同 EOS 来源的内容。新场已显示而新网格尚未完成时，隐藏旧网格或明确显示旧版，不能作为新场的有效加密布局。

## 维护位置与验证

- `PreviewSession.cpp`：传输、串行请求、身份、进度与会话生命周期。
- `SessionInput.h` / `RequestInput.h`：严格信封解析与共用 UTF-8 校验；不解析 `.par` 科学语义。
- `WorkerLimits`：单次工作进程和持续会话的独立预算策略。
- `physics/eos/InspectionEosCache.h`：只读表 owner 与请求视图绑定；普通 EOS 路径保持原行为。
- `core/files/VerifiedFileCache.h` / `FileFingerprint.cpp`：有界内容比较与统一 SHA-256 权威。
- `InitialSampleCache.h`：仅在当前场采样中复用逐位相同输入的转换结果；数值转换仍使用原 Core 实现。
- `Progress.h`：既有模型检查、场图与 AMR 阶段发布。

验收覆盖冷/热请求与独立进程数值一致、连续修改几何和温度、错误恢复、表文件同长度同 mtime 修改、组分顺序变化、3D/4D 表切换、缓存预算、CPU 预算恢复、非法协议输入、取消重启与请求上限。实际响应及耗时记录放在 `examples/preview-session/`；测试结果与准确同步范围见 [PREVIEW_SESSION_HANDOFF.md](PREVIEW_SESSION_HANDOFF.md)。
