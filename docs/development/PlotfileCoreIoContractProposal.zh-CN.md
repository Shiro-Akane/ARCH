# Plotfile Core IO 最小契约草案

日期：2026-10-03。状态：**草案，待 Core 负责人确认；不是已实现 API 或科学验收**。
审计基线：6eef34f5cd0f793df7d9b8c18c9a0b114d9aaaac。
依据：StudioConfigurationHandoff.zh-CN.md 的正式 plt 只读结果接口、LOD、原生单元 Inspector 出口。
本次仅整理契约，不修改 writer、科学 Core、checkpoint 格式，不运行 simulation。
当前 audit 接口继续 completion unknown / renderEligible false，不自动升级认证。

## 1. 源码事实

| 位置 | 实际行为 | 接入含义 |
| --- | --- | --- |
| src/io/hdf5/HDF5Writer.cpp:186–219 | 最终路径 Create/Truncate；根属性只有 time/dim/geometry；Grid x/y/z/level/morton、Data 字段 | 可读内容没有 authoritative 发布／身份契约 |
| 同文件:214–218 | Saved 日志在 File RAII 关闭之前；HDF5 异常打印后返回 | 日志不能证明关闭成功，调用方没有明确失败状态 |
| src/io/plot/PlotIO.cpp:32–42,69–79 | shape 为 [blocks,Nx] / [blocks,Ny,Nx] / [blocks,Nz,Ny,Nx] | x1-fastest，不能当作全域均匀网格 |
| 同文件:60,85–119 | GetActiveBlocks 顺序逐 block、k/j/i；GetPhysicalCoords 的 p.x/p.y/p.z | 字段与 Cartesian 物理中心对齐；不能借 checkpoint 注释假定 Morton 排序 |
| 同文件:159–225 | shared state/EOS/diagnostics 提取字段 | Viewer 不重算物理 |
| 同文件:228–235 | extra_fields 校验名称、长度、冲突、有限性 | 仍缺单位、定义和来源 |

Cartesian 中心不能独立确定原生边界、曲线体积或矢量基底。
当前二维 cylindrical 仍为 polar(r,phi)，不能在 O7.5 完成前标为 RZ。
checkpoint provenance 是独立契约，不自动赋予 plt 完整身份。

## 2. 兼容与职责

保留当前 Grid/Data 原始数值、shape、存储顺序。建议增加独立版本的 PlotMetadata 扩展。
以下字段名是逻辑候选；HDF5 映射由 Core 定案，不能作为已发布名字接入客户端。

- Core 提供实际 case/config/EOS/字段/网格语义及发布状态。
- Host 提供实际持有的 job/build/binary 指纹并声明关联来源。
- Studio 不根据文件名、当前模型或 binary 补齐旧文件身份。
- 缺失信息为 unknown；legacy 文件继续 raw audit。
- metadata 不是数字签名证明，仍需 file digest 与请求身份检查。

## 3. 完成发布候选契约

在同目录唯一临时文件写完整 payload/metadata；成功 flush/close 后原子发布最终路径。
final collision 的 replace/reject 策略由 Core 明确，不无声覆盖成功结果。
失败传播到调用方，保留原因，不发布 partial、不输出 false success。
文件内 complete=true 自身不能证明关闭／发布成功。
正式读取至少校验已知 writer/发布版本、布局一致性、读取期间文件身份稳定。

原子可见性与断电耐久性不同；是否 fsync 文件与目录需要明确，不默认保证。
待定：临时文件失败保留／清理、rename 平台语义、冲突、错误返回／异常、
调用方退出状态与关闭失败注入。草案不授权直接实施这些未确认的 IO 语义。

## 4. 最小 metadata 结构

候选字段：

| 组 | 最小内容 |
| --- | --- |
| version/publication | plotMetadataVersion、publicationVersion、state、writer identity |
| run identity | runId、caseId、rawConfigSha256、effectiveConfigSha256 |
| build identity | sourceGitHead/dirty、buildId、profileFingerprint、executableSha256；逐项来源 |
| EOS | 实际 type、table content SHA、species 顺序／身份 |
| layout | dimension、shape、x1-fastest、stored block order、cell centering、ghost inclusion |
| geometry | name、semanticVersion、nativeAxes、storedCenterBasis、velocityBasis、volumeConvention |
| fields | name、quantity、unit、basis、definitionSource |

没有证据时 null/unknown，不伪造默认科学身份。正式结果必需项与 unknown-mode 的能力边界待 Core 定案。
无 Host 的 CLI 不能伪造 buildId；可记录 binary digest 和实际取得的构建记录。
raw/effective config hash 的规范化、模型派生值及来源分别定义。
EOS 路径仅审计，身份以实际内容为准；组分沿共享状态，不从文件名推断。
仅 hash 明确相关输入，不递归扫描整仓库／整台机器。

示例逻辑记录（非已发布响应）：
    plotMetadataVersion: candidate-1
    publication: {state: complete, contractVersion: candidate-1}
    identity: {caseId: Sod, rawConfigSha256: null, effectiveConfigSha256: null,
               buildId: null, executableSha256: null, eos: {type: null, tableSha256: null}}
    layout: {dimension: 1, shape: [4,16], order: x1-fastest,
             centering: cell, ghostCellsIncluded: false, blockOrder: stored-order}
    geometry: {name: cartesian, semanticVersion: null,
               storedCenterBasis: cartesian-physical, velocityBasis: null, volumeConvention: null}
    fields: [{name: DENS, quantity: mass-density, unit: null, definitionSource: Core}]

示例 null 不表示正式认证允许缺失，必需条件尚待确认。

## 5. 字段与原生单元

| 字段 | 已核对来源 | Core 要确认 |
| --- | --- | --- |
| DENS | conserved rho | 单位体系 |
| PRES/TEMP | shared EOS 与真实 Xi | 单位、EOS 身份及有效域 |
| ENER | conserved U.eng | 总能量密度定义／单位；不标成比内能 |
| VELX/Y/Z | shared recover u/v/w | 曲线几何分量基底／单位 |
| ENTR | PRES / rho^gamma1 | 此诊断定义／单位；不直接称热力学熵 |
| ENUC | state.enuc_rate | rate 定义／归一化／单位 |
| VORT/DIVV | shared VelocityDiagnostics | 几何、符号与单位 |
| species | registered state.X | mass fraction、注册身份／顺序 |
| extra_fields | Core 传入向量 | quantity/unit/basis/来源 |

未知单位保留 unknown，不猜 CGS/code units；NaN/Inf 保留原始异常，不 abs/floor/epsilon/静默删除。

每个 stored block 应关联稳定 logicalKey、level、native bounds、cellShape，
叶／内部块关系、ghost inclusion、活动轴。logicalKey 编码／epoch 由 AMR 所有者定义，
不假定 morton 单个整数全局唯一。

原生 cell Inspector 应关联 file digest、stored block、logicalKey、local i/j/k、global stored index，
显示 raw value/单位、native center/bounds、Cartesian center、authoritative volume/convention。
Core 选择显式存 bounds/volume，或存 native edges/参数并由共享 Core 度量提供只读计算；
Host 不另复制曲线公式，也不由中心差分猜体积。
完整环／单位角／立体角约定及非活动轴需要明确版本。
未来 RZ 使用语义版本，不凭旧 cylindrical 字符串自动迁移。

## 6. 只读与 LOD 边界

沿用 project/path/digest 绑定、隔离 worker、bounded slice、取消及旧响应淘汰。
正式 metadata 结构／版本验证后才进入科学 Viewer。
LOD 与 raw 是不同数据层；显示降采样不用于科学范数，不替代 Inspector 原值。
聚合／曲线权重／AMR overlap／覆盖状态先由 owner 定案，不默认平均或插值。
zoom/pan 重绘已有数据，不改 Config、不运行 Core、不写文件。

## 7. Core scoped acceptance（待实施，不是本轮 PASS）

使用既有 fixture、合成 IO fixture 和已授权 t=0 输出，不因此运行新演化轨迹。
预算与独立科学参考由 Core 提供。

1. 成功 close 后发布；未知版本／不完整布局被拒绝。
2. write/flush/close/rename 失败与进程终止：无 false success，既有最终文件不损坏。
3. legacy raw audit 不伪认证 completion/identity/unit。
4. 非方形 2D／3D 多块 field/coords/native-cell 索引一致；block reorder 不改变 logical identity。
5. shared state/EOS 独立参考；ENER/ENTR/ENUC/velocity 语义不被 UI 改名改变。
6. 已知 Cartesian／曲线 native bounds/volume 独立参考；polar/RZ 可区别；checkpoint 不被破坏。
7. config/binary/EOS 内容变化改变对应身份；无关 studio 修改不冒称 Core input changed。
8. cancel/limit/corrupt/replacement 后 worker 清理／恢复；Node heap 不冒称 WASM/RSS 硬限。
9. LOD/Inspector 标明 original/derived，选择回读 raw，非有限值不静默修复。

## 8. Core 最小决定项

1. 是否已有准确 IO contract/commit？有则以其为准替换草案。
2. close 后同目录原子发布、final collision、失败传播是否接受？
3. metadata 版本／HDF5 映射、正式结果必需身份及 unknown-mode 边界？
4. field unit/basis/definition 与 native volume 由哪个共享所有者提供？
5. bounds/volume 显式存储还是 Core 只读接口？
6. LOD 聚合／权重／重叠规则、独立参考与预算？

收到明确决定或实现引用后才接线受影响 Core/Host/Studio 与 scoped tests。
本草案不解除正式科学文件语义门槛；其他已批准工作可继续。
