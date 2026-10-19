# Plotfile 候选原生单元只读查询

日期：2026-10-03。基线 198aa96907f9291ba671ad9559015e3f2554cfa5。
依据：联合交付计划 35c5b7b114069621901386bfc4bc2a656e65af06 §4.3。
状态：writer→query 候选链路接通；完整身份、真实模型验证和 Viewer 尚未完成。

## 查询 contract

沿用 audit-1/audit-slice-1 与已有字段，新增可选 candidateNativeGrid header、
payload.nativeCells（legacy 为 null）。
精确核对 candidate-cartesian-1 NativeGrid 属性、candidate-1 发布属性及 dataset
长度/类型。未知版本、writing state、缺失组/数据、非法 bounds/measure 拒绝，
不自动 fallback 伪装为有效 native 数据。

metadata 查询只读 header 与文件摘要，不加载 NativeGrid/场值完整数组。
slice 使用原请求 block/start/count 与相同 linearIndices，逐 x1 连续行 hyperslab
读取六条 bounds 与 cell_measure；logical coords/level 只读一个 block 元素。
输出 version、file-local logicalKey、level/logicalCoordinates、
lower/upper、cellMeasure、共享来源/数值约定，measureUnit=null。
logicalKey 仅是当前 file digest 内的 level/x1/x2/x3，不宣称跨 run 唯一。
active bounds 必须有限正宽，inactive bounds=0，measure 有限且正。
不根据 center 推算 bounds/measure，不加载 EOS、Driver 或 CUDA。

现有 pinned FD、SHA、路径/Project绑定、最大512 samples、64KiB worker stdout、
15s worker timeout、单任务容量、取消/旧请求保护保持不变。
新数组可能更早触发既有响应预算；没有为了测试通过放大额度。
NativeGrid 是候选语义，不自动提升 renderEligible；科学身份、单位和 completion
仍明确 unknown/review pending。客户端 nativeCells 校验/Inspector 显示尚未接入。
nativeCellGeometry 旧正式能力字段保留 unavailable；候选信息单列，避免误认证。

## 验证

定向12项 PASS；新增非方形多行读取，禁用 Dataset.value 全数组 getter，
原值/index/bounds/measure/logical 映射、文件不变、未知版本/partial/坏bounds/NaN measure拒绝。
既有隔离与HTTP全回归通过，Studio277/277、Host127/127。
lint/typecheck/build/diff check PASS。build 保留既有 large chunk warning，未扩大重构。

额外真实 interoperability：读 C++共享writer生成的本机 native-1.h5/native-2.h5。
分别回查3/6原生样本，field=index+.25、共享 measure 与独立 Cartesian期望一致，
logicalKey=0/1/0/0，读前后字节相同。响应摘要见同名 Summary.json。
这些是 manufactured IO fixture，不是真实 Sod/Cellular AMR科学验收。
原始 H5/数组/完整日志留在 build-cpu、studio/.local/integration，不提交。

## 成本与剩余项

查询每次仍 streaming hash 全文件，因此首次总览不具有低读取量保证；
仅返回数组 bounded 不代表底层 HDF5 read bytes 或 peak RSS 已测得。
当前 audit 输入限制64MiB，不冒称已支持生产规模数据。
响应字节数是实际 JSON 长度，不代替 I/O/RSS。
共享 writer metadata 增量还需 case/config/build/binary/EOS 与 field unit/source接线、
真实 Sod/Cartesian2D AMR 输出对照、发布故障注入及 owner review。
接着完成客户端验证和 native Inspector，再实现全域/局部 Viewer，
明确区分 Native AMR 与 Displayed LOD。曲线/3D/XDMF 后续。
本轮没有运行 simulation、替换production ARCH、CUDA、push/tag/main merge。
