# ARCH Studio Core Contract A — Parameter Metadata + Sod Binding

状态：范围和分工已获认可，作为下一阶段对接基础；扩展尚未实现。具体字段、能力声明和错误代码名称以 Core 后续交付为准。本文描述 Studio 的数据需求，不要求 Core 采用某种内部实现技术。

共同 Studio checkpoint：`43b381c3068824a373dd5477a92dc41627efca49`，tag `studio-phase2d-v0.8.0`。Core 已实现接口基线：`4c0fd5c1242d9a48d2a75d7a2c546abb34f83627`，`src/api/README.md` / `src/api/Preview.cpp`。本文与草案 B（CellularDet 二维预览）可分别实现、协商和验收。

## 1. 范围和兼容性

- 保持现有 CPU init-only stdin/JSON 调用、未保存配置、requestId/configRevision、取消和旧请求淘汰约定。
- 不运行 timestep，不写 scientific output，不写项目 .par；继续返回 EOS/grid/AMR/species 状态快照。AMR 配置不等于已构建 AMR 布局。
- 第一批只要求 Sod `x_pos` 有可靠 metadata 和轴向绑定。其他参数按 Core 实际可提供信息逐步增加，不要求任意 C++ 自动生成 UI，也不要求使用者额外维护声明文件。
- metadata 与绑定分别作为可选能力。旧 binary 没有该能力时，Studio 保持现有编辑和预览，不能根据参数名猜绑定。
- 保留现有 schemaVersion `1.0` 的字段及含义，metadata 和 graphical bindings 使用独立版本的可选扩展，并在 capabilities 中显式声明。Studio 接入时更新 Host 校验和能力协商，不能假设当前校验器会自动接受扩展。本文新增字段名仍为提案。
- 保留现有 `parameterTracing` / `markers` 能力字段原含义，不把局部 metadata 宣称为完整参数追踪。

## 2. Studio 需要的数据

每个已覆盖参数：原始 key、实际读取类型、实际读取值、值来源；默认值、单位、约束和说明在可靠时提供。

来源必须可区分显式赋值、缺失后采用默认值、解析失败后的回退，以及未知。候选表达为 `valueSource: explicit | default | unknown` 配合 `sourceReason: missing-key | parse-failure`；默认来源已知时必须给出具体原因，最终字段/枚举名由 Core 发布。Core 必须依据实际读取和初始化过程报告，不能仅凭文本出现该 key 就认定 explicit。未知默认值直接省略，不能把当前值当默认值。

单位可为 null，约束可缺省；缺省表示未知，不表示无约束。参数全集不完整时必须明确覆盖范围，未返回某参数不代表该参数未被使用。若同一 key 的多次读取无法归并成可靠值，应给出诊断并不提供可编辑绑定。

建议的可选扩展示例（加在原有响应上，非完整响应）：

```json
{
  "parameterMetadata": {
    "version": "1",
    "coverage": "observed-case-setup-reads",
    "complete": false,
    "parameters": [
      {
        "key": "x_pos",
        "type": "float",
        "explicitValue": null,
        "effectiveValue": 0.5,
        "description": null,
        "valueSource": "default",
        "sourceReason": "missing-key",
        "defaultValue": 0.5,
        "unit": null,
        "constraints": {
          "min": 0.0,
          "max": 1.0,
          "minInclusive": false,
          "maxInclusive": false
        }
      }
    ]
  },
  "graphicalBindings": {
    "version": "1",
    "items": [
      {
        "id": "Sod.x_pos",
        "parameterKey": "x_pos",
        "kind": "axis-position",
        "axis": "x1",
        "coordinate": 0.5,
        "min": 0.0, "max": 1.0,
        "minInclusive": false, "maxInclusive": false,
        "clamping": "none", "invalidBehavior": "retain-input-and-report",
        "editable": true
      }
    ]
  }
}
```

该示例以域 [0,1] 为例。实际 min/max 必须来自本次配置。当前 `simulation/Sod/Sod.cpp` 中默认 x_pos 为 0.5，验证要求 `x1_min < x_pos < x1_max`；不能将默认值改称域中点，也不能把开区间改成闭区间。

扩展数据继承同一响应 identity，不单独拼接其他 revision 的 metadata。绑定坐标必须与该参数实际值、同一响应的 x1 坐标系一致；单位未知时不显示推测单位。枚举、布尔、范围控件仅用于 Core 明确声明且实际成立的类型/约束。配置失败时只返回已确认的信息；没有 Init 结果不得生成假曲线。

## 3. 图上交互

1. 当前成功预览显示真实曲线、实际 x_pos 标记及对应 configRevision。缩放和平移只改变屏幕映射，不改变物理坐标。
2. 拖动时显示候选位置，旧曲线明确为上一成功结果；不能通过挪动旧曲线模拟物理结果。
3. 松开后更新当前 Working Copy 的 x_pos，形成一次可撤销操作并标记 Preview stale；不自动 Preview、不自动 Save。用户点击 Update Preview 后才发送完整 UTF-8 Working Copy。Esc 取消拖动，不提交编辑。
4. 使用 Core 提供的约束解释非法位置，不偷偷 clamp 或发明 epsilon。输入错误时保留可修正的 Working Copy 和上一成功预览，显示错误。
5. 新请求取消旧进程；只有 requestId、configRevision 和当前状态均匹配的结果能成为 current。旧响应的 metadata/marker 同样不能覆盖当前状态。
6. 不自动 Save，不改变文件绑定，不修改无关行、注释、重复 key 的非生效条目或换行格式。

当 x_pos 缺失而 Core 使用默认值时，首次拖动需要向 Working Copy 安全插入显式赋值。现有替换已有值的 serializer 不能被当作已支持插入；这是未来 Studio 接入的独立验收项。在安全插入完成以前，此情形只能只读显示并说明原因。

## 4. 分工和独立验收

Core 负责实际读取值/默认来源/约束、binding 语义、capability 及与真实 Init 一致的结果。实现可在现有配置读取或 case 层提供，不要求用户新建声明文件。

Studio 负责显示、坐标变换、Working Copy 编辑/撤销、状态反馈、请求生命周期和严格响应校验；不复制 Setup/Init 或物理公式。

验收覆盖：显式/缺省 x_pos、无效值或默认回退、非 [0,1] 域、开区间边界、未知单位/约束、拖动和撤销、缺失 key 的安全插入、取消和迟到响应、失败保留上次结果。每次通过真实 Core preview 校验新 configRevision；既有 .par round-trip、Sod 预览和无输出副作用回归继续通过。

## 5. 预览状态信息区

Studio 增加简洁的信息区，展示本次预览的区域/坐标范围、EOS 加载状态、组分和 AMR 配置。内容使用 Core 返回的确认状态，不从当前表单推断成功加载，也不把 AMR 配置显示为实际细化布局。

信息区与图、metadata、绑定和 Inspector 按同一请求/revision 同步更新。失败或取消保留上一成功图时，原状态快照明确标为上一成功预览；新请求的错误及已确认状态单独标明，不与旧图混合。迟到响应不得覆盖状态区，未知信息显示未知。

该信息区随 A 首次交付，B 复用并扩展到二维；验收包含成功更新、修改后 stale、失败、取消和旧请求淘汰。

## 6. 交付顺序与剩余确认

Core 先交付 A，再交付 B；两部分分别交付和验收。每次提供 README、示例响应、测试结果和准确提交引用。收到 A 后即可从共同 checkpoint 开始 Studio 接入，无需等待 B。

首批已确认包括 x_pos 实际读取值、默认值、上述来源区分、当前区域约束及 x1 位置绑定。Core 仍需发布最终扩展字段/能力声明/错误代码名称，以及未知或歧义的具体表达。缺失 x_pos 的安全插入继续作为独立验收项，不以只读降级替代该项完成。

Core 负责接口实现，Studio 负责界面、工作副本编辑和请求管理。本次只更新对接文档，等待 A 的交付引用，不提前实现假定接口。

## 7. 最小 schema、请求和来源示例

本文字段名为提案，Core 可调整名称并在 README 发布准确契约。以 Phase 2E Target 为准：拖动提交一次 Undo，不自动 Preview；旧草案中的松开自动 Preview 已废止。

候选请求沿用现有 CLI，不新增任意命令入口：

```text
ARCH --preview Sod --config-stdin --samples 512 --request-id contract-a-001
```

stdin 是完整未保存的 Sod .par 文本；例如在有效配置中设置 x_pos=0.35，而非仅发送该赋值行。Host 固定 program/args/cwd。响应沿用 schemaVersion=1.0、identity(requestId/caseId/configRevision)、execution、state、data、diagnostics，增加第2节可选扩展。该节 JSON 是响应扩展片段，不是完整响应。

| metadata 字段 | 最小语义 |
|---|---|
| key / type | 原始 key；Core 声明 float/int/bool/enum/string |
| explicitValue | 成功解析的显式值；缺失/解析失败为 null |
| rawValue | 可选原始 token，解释解析失败，不冒充有效值 |
| effectiveValue | Setup 实际读取值；未确认时 null |
| defaultValue | 该读取调用使用的默认值；未知省略或 null |
| valueSource / sourceReason | 区分 explicit、default+missing-key、default+parse-failure、unknown |
| unit / description | 可靠信息，否则 null；不猜单位 |
| constraints | 可选 min/max、端点是否包含、allowedValues；省略表示未知 |
| diagnostics | 可选参数级 severity/code/message |

来源示例是参数片段；仅当实际读取行为符合时返回：

```json
[
  {"key":"x_pos","explicitValue":0.35,"effectiveValue":0.35,"defaultValue":0.5,"valueSource":"explicit","sourceReason":null},
  {"key":"x_pos","explicitValue":null,"effectiveValue":0.5,"defaultValue":0.5,"valueSource":"default","sourceReason":"missing-key"},
  {"key":"x_pos","rawValue":"bad","explicitValue":null,"effectiveValue":0.5,"defaultValue":0.5,"valueSource":"default","sourceReason":"parse-failure"}
]
```

若真实 Core 拒绝 token，不得虚构默认回退。explicit/working/effective/default 是不同概念：Studio 保留 loaded、working、saved text；effective 属于 Core 响应 configRevision。用户编辑 pending marker，不会修改旧响应的 effectiveValue。

binding 最小项是 parameterKey、kind=axis-position、axis=x1、coordinate、min/max 和开闭区间语义，来自当前域而非 viewport。invalidBehavior=retain-input-and-report：手动非法输入保留，marker 标越界或隐藏，不静默 clamp；Core 返回真实诊断。Setup 失败 data=null，只发布确认过的 metadata，不宣称 Init 成功。

## 8. Core scoped tests 与接收条件

以下为待交付测试要求，本次未执行 Core 测试：

1. 显式、缺失默认、解析失败回退/拒绝，验证实际值和来源；保持重复 key 的既有生效规则。
2. 非 [0,1] 区域、严格开区间两端及越界输入，验证本次区域 min/max 和诊断；不改输入、不 clamp。
3. metadata/binding 与真实 Setup/Init 的 x_pos 一致，axis=x1；不靠日志或源码 regex 提供语义。
4. 未知单位/说明/约束、局部覆盖、无法确认的状态如实表示；失败不发布假曲线。
5. 旧 Sod 1D 请求和 schema1.0 字段兼容；可选扩展与 capability 单独版本。
6. stdin SHA-256/configRevision、requestId、未保存输入、进程终止取消；无 timestep、科学输出或 .par 写入。
7. 使用现有共享转换/EOS；仅 metadata/binding/preview scoped tests，不运行 simulation oracle 或完整 baseline。

Core 提供 README、完整成功/缺省/回退或拒绝/越界响应示例、scoped 测试结果和准确 commit/ref。上述条件通过后，Studio 单独验证 default安全插入、拖动一次Undo、手动Update Preview、取消/race和状态同步。A 可独立交付，不等待 B。
