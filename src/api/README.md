# ARCH 本地应用接口

本目录集中管理 GUI 等本地工具调用 ARCH 的接口。当前提供 **1.0 版初始状态预览**，由现有 `ARCH` 可执行程序提供，不需要单独的服务进程。

## 当前提供什么

- 支持已注册的 **一维 Cartesian Sod** 与 **二维 Cartesian CellularDet**；直接调用各模型的 `Setup/Init`。
- 使用 CPU 生成显示采样。配置中的 `compute_backend=cuda` 不会触发设备检测或 CUDA 初始化。
- 接收尚未保存的 `.par` 文本，返回坐标、密度、压力、温度、速度和能量。
- 同时返回 EOS、基础网格、AMR 配置、已注册组分及各阶段状态。
- Core B：返回 CellularDet 的二维坐标和真实场数据，包括两个方向的速度；提供独立二维参考配置。
- Core A：返回 Sod `x_pos` 的实际读取值、默认值、来源和当前域约束，以及与真实初始化一致的 x1 位置绑定。
- 复用 ARCH 的配置解析、EOS 和初始能量转换，不在接口中复制模型公式。
- 不进入时间推进，不建立 AMR 层级，不生成日志文件、backend sidecar、plotfile 或 checkpoint，也不创建临时配置文件。

当前没有实际 AMR 细化布局或完整参数追踪。参数扩展目前只覆盖 Sod `x_pos`；CellularDet 本次仅提供二维场，不提供参数 metadata 或可编辑分界线。未返回某参数不表示其未被使用。其他模型或维度返回错误，Host 可以继续保留旧图并标记过期。

这里的“初始状态”是初始化函数在指定坐标上的取值；显示采样不是实际计算单元，也不是完成初始 AMR 细化后的网格状态。预览成功仅说明此次初始采样成功，不代表整个模拟的求解器、反应网络或计算后端已经验证可用。

## CPU 构建

使用现有 ARCH CPU 构建流程即可。例如在项目根目录：

```sh
cmake -S . -B build-studio-cpu \
  -DARCH_ENABLE_CUDA=OFF \
  -DARCH_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-studio-cpu/bin"
cmake --build build-studio-cpu --target ARCH -j 1
```

依赖与普通 ARCH CPU 构建相同：C++20、HDF5、HighFive，以及所选构建选项需要的依赖。仅验证预览时，可在配置命令中增加 `-DARCH_ENABLE_KLU=OFF`，避免为大反应网络准备 KLU。已有 CPU 构建目录可以直接更新和重新编译。

该命令入口在创建科学输出目录、启动持久日志和解析计算后端之前分流。GUI 预览固定使用 CPU；它不修改配置中对正式计算后端的选择。

## 调用方式

### 查询当前程序能力

```sh
build-studio-cpu/bin/ARCH --preview-capabilities
```

返回一个 JSON 对象，包括 `schemaVersion`、支持的模型和维度、EOS 输入名称、字段列表、数量限制，以及 `amrHierarchy`、`parameterTracing`、`markers` 等能力开关。

**新 Host 使用 `modelCapabilities` 按 case 协商。** 每项包含 `caseId/dimensions/geometries/previewBackend/fields/maxFields/maxResponseBytes/sampling`。CellularDet 另有 `supportedShockDirections=[0,1]`。`sampling.defaultShape` 使用与响应一致的 `[Ny,Nx]` 顺序（一维为 `[N]`），其余限制为 `minPerAxis/maxPerAxis/maxTotalSamples`。为保持旧客户端兼容，顶层 `cases/dimensions/fields/defaultSamples/minSamples/maxSamples` 仍是原有 Sod 视图，不表示所有模型；不要混用两个层级的采样限制。

`eosTypes` 是 `.par` 接受的名称：`ideal`、`helmholtz`、`tabular`。表格 EOS 由实际数据确定为 `tabular3d` 或 `tabular4d`。能力列表说明可调用的现有 CPU 路径；具体数据是否加载成功、输入是否在 EOS 的适用范围内，以本次请求结果为准。

### 生成预览

```sh
build-studio-cpu/bin/ARCH --preview Sod --config-stdin \
  --samples 512 --request-id preview-001 < simulation/Sod/Sod.par
```

Host 应直接启动进程，以 stdin 写入编辑器当前的 `.par` 文本，然后关闭 stdin。上面的重定向仅用于手动试验，不要求用户先保存文件。

- `--config-stdin` 必须提供。输入是原始 `.par` 文本，不是 JSON。
- Sod 的 `--samples` 可省略，默认 512，允许 2–4096。CellularDet 的二维参数见下文。
- `--request-id` 可省略，原样回传，最多 128 UTF-8 字节。
- 配置最多 1 MiB，必须为不含 NUL 的 UTF-8；参数名称、重复键和默认值规则沿用现有解析器。
- 参数文本不会自动保存，也不会改变当前文件关联。
- **工作目录由 Host 设为所管理项目的运行目录。** EOS 相对路径仍相对于该工作目录解析，不相对于临时文件目录。接口不自动切换目录，也不改写配置中的路径。
- stdout 只返回一个完整 JSON 对象及换行。应同时收集 stderr；底层库可能向其报告错误。
- 每个进程处理一次请求。Host 负责超时、取消和终止进程；未正常退出或未收到完整响应时，不接纳结果。

接口不需要 WebSocket、HTTP 或 SSH。本地 Host 可以使用已有的进程管理方式调用。

### CellularDet 二维调用

```sh
build-studio-cpu/bin/ARCH --preview CellularDet --config-stdin \
  --samples-x1 128 --samples-x2 128 --request-id cellular-001 \
  < simulation/Cellular/CellularPreview2D.par
```

- `--samples-x1` 与 `--samples-x2` 必须同时提供或同时省略。省略时为 128×128；每轴 2–256，总数最多 65,536。只能是整数，不能混用一维的 `--samples`。
- 要求 Cartesian，且沿用配置解析器得到的维度为 2。参考配置使用 `nblockx1>0`、`nblockx2>0`、`nblockx3=0`；支持 `shock_dir=0/1`。`shock_dir=2` 返回不支持。
- [CellularPreview2D.par](../../simulation/Cellular/CellularPreview2D.par) 是独立参考输入，使用 Helmholtz 和 aprox19。已有 `Cellular.par` 不是此二维参考配置。
- 参考输入中的表路径为 `EOS_toolkit/tables/helmholtz/helm_table.dat`，从仓库根目录运行即可解析；Host 在其他工作目录运行时，应由配置提供有效绝对路径或相对于该目录的路径。缺失表会返回 `EOS_FAILED`。
- 参考输入保留 Helmholtz 与核素组分的配套选择。仅将 `eos_type` 改成 `ideal` 会因这些组分没有 ideal 所需的热参数而产生无效初值；接口报告 `INITIALIZATION_FAILED`，不自动更换 EOS。
- 组分由所选 `network_name` 的 Setup 准备；参考输入虽含 `use_burn=true`，此路径不启动反应演化。配置中的求解器、时间步与 CUDA 选择不参与初始预览。
- 点数和响应字节限制同时生效。即使点数合法，复杂数值在高分辨率下仍可能超过 8 MiB；Core 返回结构化错误，由用户选择较低分辨率后重新请求。

## 响应结构

成功和执行失败使用同一份快照结构：

```text
schemaVersion: "1.0"
kind: "initial-state-preview"
status: "ok" | "error"
stage: input | configuration | support | setup | eos | sampling | response | complete
identity: { requestId, caseId, configRevision }
execution: { previewBackend, simulationReadiness, timeStepping, scientificOutput }
state: { configuration, setup, grid, amr, eos, species, computeBackendRequested }
data: { dimension, kind, sampling, axes, fields } | null
diagnostics: [{ severity, code, message }]
parameterMetadata?: { version: "1", coverage, complete, parameters }
graphicalBindings?: { version: "1", items }
```

`configRevision` 是 **实际收到的原始 UTF-8 字节**的 SHA-256。换行、注释或空格变化也会改变摘要。Host 可与提交前的摘要比较，用于丢弃过期结果。

可直接查看实际程序生成的响应样例：

- [CellularDet 完整成功、EOS 失败、方向拒绝与超限响应](examples/core-b/README.md)：包含对应输入与复现命令。
- [Sod 成功响应](examples/sod.json)：使用 `simulation/Sod/Sod.par`，4 个采样点。
- [EOS 文件缺失响应](examples/missing-eos.json)：同一输入末尾追加 `eos_type = helmholtz` 和 `eos_table_path = missing-eos-table.dat`。保留已确认的网格和 AMR 配置，场数据为 `null`。

命令参数、编码、输入大小等错误可能发生在请求建立之前，此时 `identity/state/data` 为 `null`，也可能没有 `execution`。配置解析失败时，`state.configuration=not_loaded`；解析成功后的错误尽量保留已确认的状态，`data` 仍为 `null`。`status=error` 不发布部分曲线。

### Core A：参数与位置绑定

调用方式不变；执行到 Sod Setup 后，响应自动附带两个可选扩展。已有一维字段及含义保持不变，扩展与曲线共用同一份 `identity`。能力声明与完整示例见 [Core A 示例](examples/core-a/README.md)。

能力查询的 `extensions.parameterMetadata`、`extensions.graphicalBindings` 分别提供 `version="1"` 及覆盖的 case/key 或 binding ID。客户端只启用自己支持的扩展版本；扩展不存在或版本未知时，仍可使用基础一维预览。顶层 `markers=true` 表示当前支持集合中已提供位置标记；其精确范围由扩展声明决定。`parameterTracing=false` 继续表示没有完整参数追踪。

`parameterMetadata` 的 `coverage="observed-case-setup-reads"`、`complete=false` 表示仅报告明确覆盖的 Setup 读取。本版只覆盖 `Sod/x_pos`，不解析 C++ 文本，也不从日志提取参数。

| 参数字段 | 含义 |
|---|---|
| key / type | `x_pos` / `float`；数值为 double 精度，不表示 float32 |
| explicitValue | 由原有解析器得到的显式值；缺失或该类型解析失败为 null |
| effectiveValue | Setup 实际读取值；无法归并时为 null |
| defaultValue | 该次 Get 调用传入的默认值；本例为 0.5，不是域中点 |
| rawValue | 存在时返回原解析器保留的有效 token；重复 key 采用最后一项 |
| valueSource | `explicit`、`default` 或 `unknown` |
| sourceReason | 显式值为 null；默认值为 `missing-key` 或 `parse-failure`；其他见下文 |
| unit / description | 本版均为 null，不推测单位 |
| constraints | 本次域的 min/max；两个 Inclusive 字段均为 false |
| diagnostics | 参数级 severity/code/message 列表 |

读取行为保持原解析器的含义。例如 `x_pos=bad` 与超出 double 表示范围的 token 会采用 0.5，来源为 `default/parse-failure`；`x_pos=0.35suffix` 当前解析器接受数值前缀，故实际值为 0.35、来源为 explicit。`nan/inf` 则在预览基础检查阶段拒绝，尚未发生 Setup 读取，不返回参数扩展。

重复读取若类型、默认值、值或来源不一致，报告 `AMBIGUOUS_PARAMETER_READ`，type/explicitValue/effectiveValue/defaultValue 为 null，来源为 `unknown/ambiguous-reads`。若程序改写参数值而无法归因于原输入，报告 `PARAMETER_SOURCE_UNKNOWN` 和 `unknown/untracked-value`。两者均不提供可编辑绑定。解析失败后回退提供 `PARAMETER_DEFAULT_FALLBACK`。这些参数级诊断使用 warning，不改变原有 Setup 的成功/失败决定。

`graphicalBindings.items` 在完整成功响应中提供 `Sod.x_pos`：

- `parameterKey=x_pos`、`kind=axis-position`、`axis=x1`。
- `coordinate` 来自 Sod 内部实际分界值，且必须与记录的 effectiveValue 一致。
- min/max 来自本次区域；`minInclusive=false/maxInclusive=false`，与 Sod 验证共用范围定义。
- `clamping=none`、`invalidBehavior=retain-input-and-report`、`editable=true`。

Setup 越界失败时，保留已经读取的值及约束，`data=null`，绑定 items 为空。EOS 或采样失败也只保留已确认的 metadata，不提供本次成功绑定。Setup 之前的失败没有这些扩展，不能把缺失理解为“使用了默认值”。

Studio 拖动只修改工作副本，形成一次撤销并标记预览过期；不自动保存或重新预览。用户点击 Update Preview 后提交完整文本。未填写 x_pos 时，Studio 负责安全插入赋值。旧曲线、旧 effectiveValue、位置绑定和状态快照必须保持同一请求身份；候选位置不得改写旧响应。

### 场数据

Sod 返回 `dimension=1`、`kind=line`：

- `axes` 是坐标轴列表，目前只有 `x1`，包含 `name/unit/values`。
- `sampling.kind=uniform`、`valueLocation=init-sample`、`position=bin-center`。
- 采样坐标为 `x1_min + (i + 0.5) * (x1_max - x1_min) / count`，不采两端边界。
- `shape=[count]`、`order=x1-fastest`；字段与坐标按相同顺序排列。

CellularDet 返回 `dimension=2`、`kind=grid`：

- `axes` 按 x1、x2 排列，长度分别为 Nx、Ny；每轴使用相同的均匀 bin-center 规则，数值有限且严格递增。
- `sampling.shape=[Ny,Nx]`、`count=Nx*Ny`、`order=x1-fastest`。字段的 `index=j*Nx+i` 对应 `(axes[0].values[i], axes[1].values[j])`。
- `sampling.fixedCoordinates=[{"name":"x3","value":0,"unit":null}]`。非活动坐标固定为零，沿用真实网格的坐标转换，不取配置 x3 范围的中点。
- UI 横轴 x1，纵轴 x2 向上；屏幕 y 方向转换不能改变数组排列。Inspector 直接按上述索引取值。

两个变体中，`fields` 是列表，每项包含 `key/displayName/unit/values/min/max`。前端按 `key` 匹配，不依赖列表顺序。

| key | 含义 |
|---|---|
| DENS | 密度 |
| PRES | 由初始守恒状态和实际 EOS 得到的压力 |
| TEMP | 由同一状态和 EOS 得到的温度 |
| VELX | x 方向速度 |
| VELY | y 方向速度，仅二维提供 |
| ENER | 单位体积的总能量，包含动能 |
| EINT | 单位质量的内能 |

当前所有 `unit` 均为 `null`，表示接口没有提供可靠单位标签。前端保持原始值，不自行标为 SI、CGS 或无量纲。数值以 double 精度输出，所有成功样本均为有限数值；出现无效数据时整个请求失败。

CellularDet 的 `radiusPerturb` 是 `shock_dir` 选定轴上的分界坐标，坐标小于该值的一侧为扰动区域。`noiseAmplitude` 调整该区域内的场值，不移动分界。Studio 直接显示返回的场；本次没有 Cellular 的图形绑定描述，不从参数名称猜测圆形或波动界面。

### EOS 状态

`state.eos` 包含：

- `requested`：配置选择的 EOS 名称。
- `resolved`：本次解析出的策略名称，解析前为 `null`。
- `status`：`not_loaded/ready/error`。`ready` 表示 EOS 已构造，不等于之后所有采样成功，仍要检查顶层 `status`。
- `configuredGamma`：配置中的 gamma；并不表示所有 EOS 都使用一个固定 gamma。
- `tablePath/componentTablePath`：配置中的表文件选择。
- `loadedTablePath`：已成功加载的主表路径，去除配置引号；相对路径仍相对于进程工作目录。理想气体为 `null`。
- `sourceFingerprint`：加载器绑定的数据身份；理想气体为 `null`。组合表的摘要可能包含多个文件与解释规则，不应一律当成单个文件的摘要。

`state.species` 返回本次 Setup 实际注册的 `{index,name}` 列表。尚未确认注册内容时为 `null`；Cellular 的 EOS 在 Setup 内加载，加载失败时可保留已经完成注册的组分列表。当前不返回完整组分场。

### 基础网格状态

`state.grid.status=configured` 表示描述来自已解析配置，`hierarchy=not_constructed` 表示本次没有分配实际网格。

`geometry/dimension` 描述模型区域。各轴返回：`min/max`、`rootBlocks`、`activeCellsPerBlock`、`rootCells`、`coordinateSpacing`、两端边界条件，以及尚未提供的 `unit=null`。

基础单元数使用 **当前编译程序**的有效块尺寸计算，不包含 ghost 或内存填充单元。调整 `--samples` 或二维轴采样数量只调整显示采样，不改变这些网格设置。

### AMR 状态

`state.amr` 返回配置是否开启细化、最低/最高级别、阈值、重建间隔及块数量上限。

- `requestedIndicators`：解析后的请求名称文本。
- `parsedIndicators`：经过现有解析器开关筛选后的指标；尚未执行实际指标求值，组分指标也尚未绑定到实际细化网格。
- `maxBlocks`：原配置值；`effectiveMaxBlocks`：按照现有调度规则解释后的上限。
- `indicatorEvaluation=not_executed`。
- `initialRefinement=not_executed`。
- `actualHierarchy=null`。

前端可以显示“AMR 配置已读取；本次未生成细化网格”。不要把 `null` 显示成零块或零级。

## 错误与日志

| 退出码 | 主要错误码 | 含义 |
|---|---|---|
| 0 | — | 完整响应成功 |
| 2 | INVALID_REQUEST / SAMPLING_LIMIT_EXCEEDED | 命令、编码、输入大小或采样语法错误；已解析整数超出采样范围使用后者 |
| 3 | INVALID_CONFIGURATION | 配置解析或当前预览所需的基础检查失败 |
| 4 | UNSUPPORTED_PREVIEW | 未支持的模型、维度、坐标系或 restart 请求 |
| 5 | SETUP_FAILED / EOS_FAILED | 模型准备或 EOS 加载失败 |
| 6 | INITIALIZATION_FAILED | 初始采样、数据转换或数值检查失败 |
| 7 | RESPONSE_TOO_LARGE | 完整 JSON（含结尾换行）超过 8 MiB |

`diagnostics` 中 `severity` 为 `info/warning/error`。`CORE_LOG` 保存现有核心的文本报告，不作为前端解析接口；每个日志通道最多保留 16 KiB，截断时提供 `LOG_TRUNCATED`。完整响应限制为 8 MiB。

响应超限时 `stage=response`、`data=null`，保留请求身份和可容纳的已确认状态；不截断 JSON、不降采样、不丢字段后假装成功。若状态本身也超限，则 `state=null` 并附加 `STATE_OMITTED_FOR_SIZE`，请求身份仍保留。任何错误都不能发布本次图形绑定。

部分现有解析规则会使用默认值，预览与正式配置读取保持一致。Core A 提供上述有限范围的来源信息，不提供完整模拟配置审查。错误文字用于展示，前端逻辑按退出码、`status/stage` 和稳定错误码处理。

## Host 与前端接入

1. 使用最后一次成功编译且与已跟踪输入匹配的 ARCH 程序。
2. 查询能力后提交当前参数文本，并记录配置摘要和请求 ID。
3. 给结果关联 Host 自己管理的 project/profile/build ID 和可执行文件指纹。本接口没有另一个 helper，不新增另一套构建身份。
4. 检查退出码、JSON 版本、请求 ID 和配置摘要，再接纳结果。
5. 参数、源码或构建发生变化后，把旧图和旧状态一起标记过期。旧请求较晚返回时丢弃；不能将旧 EOS 状态与新参数混在一起。
6. 失败或取消保留编辑内容及旧图。只有完整成功响应才能成为新的当前预览。

本协议与 Studio Host 的协议版本独立。前端可以先使用曲线和基础摘要，其余信息按需显示。客户端应忽略未知可选字段；不支持的主版本应明确拒绝。后续增加模型、字段或可选元数据时优先保持 1.x 的现有含义。

## 文件与验证

| 文件 | 负责内容 |
|---|---|
| Preview.h | 请求、结果和数量限制 |
| Preview.cpp | 初始化、状态快照、数据检查和结果组织 |
| PreviewCommand.cpp | 命令选项、stdin 和 stdout 边界 |
| Json.h / Response.h | 有字节限制的 JSON 输出与超限错误响应 |
| Sampling.h | 一维/二维采样计划及分配前数量检查 |
| ParameterMetadata.h / .cpp | 将实际读取记录与模型位置描述组织为 JSON 扩展 |
| ../interface/PreviewMetadata.h | 独立于 JSON 的读取记录与轴向位置数据 |
| ../core/InitialStateConversion.h | 正式网格初始化和预览共用的数据转换 |

测试入口：

```sh
cmake --build build-studio-cpu --target ARCH arch_preview_initial_conversion \
  arch_preview_parameter_reads arch_preview_sampling_limits arch_preview_cellular_reference -j 2
ctest --test-dir build-studio-cpu -R '^preview_' --output-on-failure
```

测试需要配置 `-DBUILD_TESTING=ON`。`preview_api_contract` 使用实际 ARCH 程序测试配置修改、默认值、CPU 预览与 CUDA 请求分离、EOS 加载和错误、AMR 状态、输入限制，以及无文件输出。正式模拟输出对照默认跳过；只有显式设置 `ARCH_PREVIEW_SIMULATION_ORACLE=1` 且安装 `h5dump` 才运行该独立对照。

`preview_initial_conversion` 检查压力和温度两种初始化输入共用的转换，以及内存参数解析。测试源位于 `tests/api/`。

`preview_parameter_reads` 检查重复读取歧义、程序改值后的未知来源、普通配置不启用记录及严格开区间。`preview_parameter_metadata` 通过实际 CLI 检查来源、真实字段与绑定一致、动态域、越界、重复 key、数值前缀、EOS 失败和 Setup 前的错误。

`preview_sampling_limits` 检查分配前数量限制、真实网格的非活动坐标、JSON 字节边界及超限状态保留。`preview_cellular_2d` 检查真实 CLI 的二维排列、两个方向、噪声影响、直接 Init/EOS 对照、实际 Helmholtz 输入、采样/响应限制、错误、取消及无文件输出。直接对照程序仅供测试，不是 Host 需要管理的另一个生产程序。

各阶段基线与验证结果见 [Core A 交接](CORE_A_HANDOFF.md) 和 [Core B 交接](CORE_B_HANDOFF.md)。CPU 预览的验证范围不包括 CUDA、完整模拟或 Studio UAT。
