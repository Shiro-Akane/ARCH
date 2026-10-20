# Plotfile Core IO 最小契约草案

日期：2026-10-03。状态：**候选实现依据；允许小范围试做，科学语义待 Core review；不是已实现 API 或科学验收**。
审计基线：6eef34f5cd0f793df7d9b8c18c9a0b114d9aaaac。
依据：StudioConfigurationHandoff.zh-CN.md 的正式 plt 只读结果接口、LOD、原生单元 Inspector 出口。
原始草案仅整理契约。更新依据为联合计划提交 35c5b7b114069621901386bfc4bc2a656e65af06 及用户最新分工确认：允许在共享 IO 层试做最小 writer 扩展、只读查询与 Viewer；保持科学场值、精度、数值推进及 checkpoint 语义。
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
以下字段名与 HDF5 映射可由实现方提出并试用，必须标记候选版本；正式支持范围以 Core review 后的契约为准。

- 实现方复用 Core 已有 case/config/EOS/网格来源，提出存储、发布、查询及显示候选实现；Core review 单位、坐标、积分测度、身份、发布完整性与原生数值一致性。
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
候选实现需明确并验证：临时文件失败保留／清理、Linux rename 语义、冲突、错误返回／异常、调用方退出状态与关闭失败注入。允许提出小范围实现供 review；不能把未经验证的发布流程称为完整性保证。

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
首版可接入已知候选 metadata 版本的 Viewer，明确标记候选语义、已知支持范围和未知项；legacy raw audit 不自动升级科学身份。
LOD 与 raw 是不同数据层；显示降采样不用于科学范数，不替代 Inspector 原值。
首版仅 Sod 1D 与笛卡尔 2D AMR；提出明确的 LOD 取样／聚合与覆盖规则并交 owner review，不静默改变原值。曲线坐标、3D、XDMF 后续单独扩展。
单元积分测度复用 GridMetrics::CellVolume；低维每单位长度／立体角约定交 Core 核定。当前叶块输出不能冒充已存粗层场值。
固定图像尺寸仅限制返回数据量；首次全域总览可能扫描大量叶块。实测读取字节、响应大小与峰值内存，缓存受明确预算约束。
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

## 8. Core review 项（不阻塞首版候选布局试做）

1. 是否已有准确 IO contract/commit？有则以其为准替换草案。
2. close 后同目录原子发布、final collision、失败传播是否接受？
3. metadata 版本／HDF5 映射、正式结果必需身份及 unknown-mode 边界？
4. field unit/basis/definition 与 native volume 由哪个共享所有者提供？
5. bounds/volume 显式存储还是 Core 只读接口？
6. LOD 聚合／权重／重叠规则、独立参考与预算？

无需等待完整布局规范，按三个小交付推进：

1. writer 最小扩展、metadata 读取与原生单元查询；附准确 SHA、数组映射、发布失败处理和本地验证摘要。
2. 全域显示、zoom/pan、AMR 轮廓、Inspector；区分 Native AMR 与 Displayed LOD，实测 I/O、响应与内存成本。
3. Core review 后按 finding 修改并逐片冻结，再扩大范围。

此授权替代本草案旧版“等待完整 IO 布局决定后才实现”的门槛；不替代科学 review，不扩大到曲线坐标、3D 或 XDMF。原始 H5 留本机；提交处理后的摘要。
