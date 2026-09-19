# ARCH Studio Core Contract B — CellularDet 2D Preview

状态：范围和分工已获认可，作为下一阶段对接基础；扩展尚未实现，最终接口名称和测试后的上限待 Core 发布。Studio checkpoint：`43b381c3068824a373dd5477a92dc41627efca49` / `studio-phase2d-v0.8.0`。现有 Core API 基线：`4c0fd5c1242d9a48d2a75d7a2c546abb34f83627`。

## 1. 独立范围

本扩展仅增加 CellularDet 的二维 Cartesian init-only 场显示，不依赖参数 metadata 或可编辑 marker。保留既有 Sod 1D CLI、JSON 字段及意义；旧客户端继续可请求 Sod，只有协商二维能力的新 Host/UI 请求二维结果。

继续 CPU Preview、stdin 未保存配置、request/config/build provenance、取消、旧请求淘汰、失败保留上次结果，以及 EOS/grid/AMR/species 状态快照。不运行 simulation、网络演化或 timestep；不生成正式 output/Plotfile/checkpoint，不改项目 .par。实际 AMR hierarchy、三维、非 Cartesian 和图形化 AMR 编辑不在范围内。

## 2. 请求和能力协商

Core capabilities 应按 case 提供支持的 dimension、geometry、fields、采样默认值及上限。当前已发布能力只有 Sod 1D，不能因为存在 CellularDet 的 Setup/Init 就宣布二维 API 已可用。

建议二维请求使用独立的两个轴采样数量，例如以下候选语法（待 Core 定名）：

```text
ARCH --preview CellularDet --config-stdin --samples-x1 128 --samples-x2 128 --request-id <id>
```

保留现有 1D `--samples N`。二维两个轴都必须明确给出，或都使用 Core 声明的默认值；混用旧新采样参数应返回明确错误，不能静默猜测。

默认 128×128 和 8 MiB 响应限制已确认；每轴及总点数按下表候选上限实施测试后，由 Core 将最终值写入 capabilities，Studio 据此执行，不把候选数值写死为最终 contract：

| 项目 | 建议 |
|---|---|
| 默认采样 | 128 × 128 |
| 每轴 | 2..256 |
| 总点数 | Nx × Ny ≤ 65,536 |
| 输入 | 沿用最多 1 MiB UTF-8 .par |
| stdout JSON | 沿用最多 8 MiB |

点数限制与响应字节限制必须同时满足；满足点数上限不保证所有字段都能在字节预算内返回。Core 应在分配大数组前检查乘积/预算，并在序列化输出前保证最终响应未超限；超限返回结构化错误，并尽可能保留本次请求身份及已确认的状态。不截断 JSON、不静默降采样或丢字段；错误响应自身也必须满足字节限制。Host 独立执行相同资源上限。精确错误 code/退出码由 Core 发布，不能伪装成成功。

## 3. 二维数据约定

保留 schemaVersion `1.0` 的既有字段及含义，通过明确的模型能力声明启用二维。建议增加 `dimension: 2, kind: "grid"` 的 data 变体，具体名称待 Core 发布。新增类型须更新 Host 校验/能力协商；保留旧 line 变体。metadata 和 graphical bindings 使用独立版本的可选扩展，二维热图不依赖它们。

- axes 固定按 x1、x2 排列，分别含 Nx、Ny 个严格递增、有限的实际坐标。
- 均匀 bin-center 采样：Core 在每个域内采样点调用 authoritative Init，不做实际 AMR cell 构建。
- `shape = [Ny, Nx]`，`order = "x1-fastest"`，`count = Nx * Ny`。
- 每个字段为长度 count 的一维数组，`index = j * Nx + i` 对应 `(axes[0].values[i], axes[1].values[j])`。
- UI 横轴 x1，纵轴 x2 向上。Canvas 的屏幕 y 翻转只属显示变换，不转置科学数组。Inspector 返回对应 i/j、坐标、字段值和 provenance。
- 字段计划沿用现有字段并增加 VELY，最终列表由 Core 明确发布；输出 key/displayName/unit/values/min/max，UI 按实际能力显示。未知单位为 null。
- 二维压力、温度和能量等字段必须继续经过现有共享状态转换和 EOS 计算，保持与当前预览一致的数据含义，包括总能量密度与比内能的区分。不能新增平行转换公式或由前端推导替代；Core 测试需覆盖此一致性。
- shape、axes 长度、各 field 长度和所有数值须校验；不接受 NaN/Infinity、半截场或不一致 shape。

以下仅为数组顺序的合成示例，不是 CellularDet 物理解：

```json
{
  "dimension": 2,
  "kind": "grid",
  "sampling": {
    "kind": "uniform",
    "valueLocation": "init-sample",
    "position": "bin-center",
    "count": 6,
    "shape": [2, 3],
    "order": "x1-fastest"
  },
  "axes": [
    {"name": "x1", "unit": null, "values": [0.5, 1.5, 2.5]},
    {"name": "x2", "unit": null, "values": [0.5, 1.5]}
  ],
  "fields": [
    {"key": "DENS", "displayName": "Density", "unit": null,
     "values": [4, 1, 1, 4, 1, 1], "min": 1, "max": 4},
    {"key": "PRES", "displayName": "Pressure", "unit": null,
     "values": [8, 2, 2, 8, 2, 2], "min": 2, "max": 8}
  ]
}
```

已确认采用 Cartesian x1–x2 平面，非活动坐标固定为 x3=0，与当前网格代码一致。Core 必须在响应中记录该固定坐标，具体字段名随接口交付；上面的合成数组示例未展示完整 envelope 和固定坐标扩展，不能作为完整验收响应。

## 4. Cellular 的真实语义与图形表达

依据 `simulation/Cellular/Cellular.cpp` 的 `CellularDet` 注册模型：

| 参数 | 实际含义 | 可接受的显示 |
|---|---|---|
| shock_dir = 0 | 沿 x1 判断区域 | 分界线垂直于 x1 |
| shock_dir = 1 | 沿 x2 判断区域 | 分界线垂直于 x2 |
| radiusPerturb | 所选轴上的分界坐标；坐标小于它的一侧为扰动区域 | 标注“分界位置”，保留原 key；不是圆半径 |
| noiseAmplitude | 修改上述区域内部的密度/压力场值 | 展示 Core 返回场值/色标及参数；不画成界面波动幅度 |

二维热图基本能力不要求 marker。若 Core 另外提供明确的 interface 描述（轴、坐标、区域不等式和参数 key），Studio 可显示只读分界线及区域说明；没有描述就只展示场数据，不靠字段名推断。可编辑 Cellular binding 单独确认，不作为本二维扩展的前置条件。

已确认首版支持 shock_dir=0/1；shock_dir=2 返回明确的不支持信息，不能由 UI 猜测成 x1/x2。此项只限定 Preview 支持范围，不要求改写 scientific Core 的参数规则。

不在前端复写噪声公式、不根据振幅移动分界线、不渲染圆形热点。当前源码对 rho/p 的调整不同，图形必须来自实际字段，不能用一张人工纹理代替。

## 5. 生命周期、错误和 provenance

沿用一次请求一个受控进程、CPU、完整 stdin 文本散列及现有 identity/execution/state/diagnostics。Host 保留 executable SHA、Build Manifest 和 profile 来源；source 改变后不能沿用旧 binary 的 freshness。

新 Working Copy、采样分辨率或 case 变化均产生新请求身份。取消必须终止受控进程并释放资源；旧请求即使迟到，也不能替换当前场、坐标、Inspector 或快照。出错保留上一成功图并明确 stale，不能将其标为当前配置结果。

配置非法、unsupported dimension/geometry/case、EOS/Setup/Init 失败、采样/响应超预算应区分诊断，尽可能保留已有 identity 和确认过的 state。沿用现有错误阶段和退出约定，新增 code 在实现前确认。日志留 stderr，stdout 只包含一份完整 JSON。

Studio 复用 A 的简洁预览状态信息区，展示二维区域/坐标范围、EOS 加载状态、组分及 AMR 配置，并与当前图和 Inspector 按同一请求/revision 同步更新。旧结果留存时明确标注旧快照；本次失败的已确认状态另行标识，不能混入旧图或暗示已生成实际 AMR 布局。

## 6. 验收与 Core 交付项

- Core 提供独立二维 Cartesian CellularDet 参考 .par，以及所需 EOS 表文件的配置说明；当前仓库 Cellular.par 的 nblockx2/nblockx3 为 0，不能直接作为二维验收配置。
- 用 Nx≠Ny、非对称场验证轴顺序、展平索引、上下方向及 Inspector；验证两个 shock_dir 的真实分界方向。
- 验证噪声只影响区域内场，未把分界线画成波纹；具体物理数值一致性由 Core authoritative Init 测试保证。
- 验证响应记录 x3=0，shock_dir=2 明确不支持；按 capabilities 验证采样/字节预算、实际字段与单位。
- 验证二维字段使用共享状态转换/EOS，响应超限返回结构化错误并尽可能保留身份和确认状态；验证预览状态信息区不会混合不同请求的图与快照。
- 验证未保存配置、取消、迟到结果、失败恢复、EOS/grid/AMR/species 快照，以及无 timestep / scientific output 副作用。
- 旧 Sod 1D 调用和输出契约保持兼容。二维路径可先接入热图，不等待草案 A 的 metadata/dragging。

Core 先交付 A，再交付 B，每次提供 README、示例响应、测试结果和准确提交引用；Studio 分别接入和验收。剩余待发布项为具体扩展字段、模型能力声明、错误代码名称及测试后的最终上限。Cellular 可编辑分界线后续单独确认。

本次仅更新需求文档，不执行新的 Preview、Build 或下一阶段开发。

## 7. Core scoped tests / acceptance 与能力示例

以下是待 Core 交付的验收清单，不表示当前 binary 已支持：

1. registry 确认 CellularDet，支持 Cartesian dimension2；shock_dir0/1 成功、2明确 UNSUPPORTED_PREVIEW 或发布的等价稳定代码；其他几何/维度明确拒绝。
2. Nx!=Ny、非对称 Init 数据验证 axes、shape=[Ny,Nx]、x1-fastest、j*Nx+i、bin-center 和固定 x3=0；每字段长度必须 Nx*Ny。
3. DENS/PRES/TEMP/VELX/ENER/EINT 和新增 VELY 按实际 capability 返回，min/max 与数组一致、数值有限；单位未知 null。
4. 压力/温度/总能量密度/比内能经现有共享状态转换与 EOS，使用 scoped 转换测试和直接 Init 对照，不运行正式 simulation。
5. 验证默认128x128，整数边界、负数/零、溢出、每轴和总点数超限，以及字段数/8MiB 响应边界；不截断、不静默降低采样。超限结构化错误尽可能保留 identity/已确认 state，data=null。
6. missing EOS/非法配置/Init 非有限结果的 diagnostics 和非零退出码；确认没有 partial-success 场。
7. 未保存 stdin SHA、requestId、一进程一请求、终止取消；不写 .par、不创建 scientific output、不进入 timestep。
8. 旧 Sod1D --samples 请求和字段含义保持兼容；二维无需 metadata/marker 能力才能使用。

候选能力扩展片段（名称可调整；数值为测试前候选，最终由 Core 发布）：

```json
{
  "modelCapabilities": [{
    "caseId": "CellularDet", "dimensions": [2], "geometries": ["cartesian"],
    "previewBackend": "cpu", "supportedShockDirections": [0,1],
    "sampling": {"defaultShape":[128,128],"minPerAxis":2,"maxPerAxis":256,"maxTotalSamples":65536},
    "maxResponseBytes":8388608,
    "fields":["DENS","PRES","TEMP","VELX","VELY","ENER","EINT"],
    "maxFields":7
  }]
}
```

Host 必须有明确字段数上限；这里7只是上述计划字段集对应的候选值，Core 若调整字段集须同步发布限制。Core 不提供总数/字节限制时，不由 frontend 猜测安全预算。

第2节 --samples-x1/--samples-x2 为 samples-x/samples-y 的等价提案，最终拼写以 Core README 为准；禁止混用导致歧义。建议在 sampling 中增加如下片段记录固定坐标，具体字段名待发布：

```json
{"fixedCoordinates":[{"name":"x3","value":0,"unit":null}]}
```

完整响应沿用 schemaVersion1.0、identity、execution、state、data、diagnostics；第3节 JSON 仅展示 data 合成示例。Core 交付必须提供真实完整成功/错误示例，其中 identity 含实际 stdin 的 SHA-256，execution 确认 CPU/no timestep/no scientific output，state 含区域/EOS/species/AMR 确认状态。诊断使用 severity/code/message，Core 公布精确错误码与退出码，不能只供自由文本供 Studio 猜测。

接收材料：准确 commit/ref、README、独立2D参考 .par、EOS 表路径配置说明、完整示例响应及以上 scoped 测试结果。A/B 分别交付；Studio 完成并验收 A、独立 checkpoint 后再进入 B。本次不执行 Build/Preview，也不解除 M0 Stop Gate。
