# Plotfile 原生叶块轮廓与 Inspector（2026-10-03）

本项属于联合计划 3C 后 plt 排期。完成全域候选 LOD 上同文件的 leaf outlines 增量，不作为整体 O7/O9 或完整 Linux UAT 的完成记录。

## 实现

- overview 可选 nativeBlocks 扩展 version=candidate-leaf-outlines-1；明确 kind=stored-active-leaf、identityScope=file-local、totalBlocks/limit/complete。
- 在现有 bounds 扫描中收集各块原生存盘单元边界的包络；level 来自 Grid/level，logical coordinates 来自 NativeGrid/logical_x1/2/3。block index、firstCellIndex、x1-first no-ghost cellShape 都与同文件存储位置绑定。
- 输出前128个 block 的轮廓；更多块时 complete=false。这个限制仅影响轮廓输出，domain 和字段 LOD 仍扫描全部单元。不把 first128 宣传为完整 hierarchy。
- shared validator 拒绝重复 key、负/非法 level、shape/firstIndex 不一致、domain 外或 inactive bounds 非零、虚假的 complete 状态。Host worker 与 client 校验使用同一函数并匹配外层 blocks/cellShape。
- SVG 轮廓沿用字段轴的 X/Y physical mapping、同一 clipPath、zoom/pan/Fit。1D 显示 block x1 区间边界，2D 显示原生 bounds 矩形。
- 原生 level 复选框仅过滤轮廓，不修改 LOD 字段、不发起读取/AMR/Preview。UI 明示 field LOD still contains all scanned leaves。
- 可展开 Block Inspector 通过列表选块、突出其轮廓，并显示 level/logicalKey/bounds/cellShape/firstCellIndex 和精确 file digest。只记录原生 leaf，不生成 parent/coarse block，也不显示缺失的 coarse field。
- candidate/unknown units/科学审核待定仍保留；原始字段、科学 Core、writer、checkpoint 不修改。

## 验证

296 项 Studio/Host 测试 PASS，lint/typecheck/build PASS。新增制造数据负向和129块限制检查；限定128轮廓不截断129单元字段/全域。
真实既有t=0文件走 HTTP→client→SVG SSR→原生 Inspector：
- Sod：12 blocks，level 0/1/2/3；HTTP 4669 bytes。
- CellularDet：20 blocks，level 1/2；HTTP 23190 bytes。
- SVG emitted outlines 数与 stored block 数一致，Complete stored leaf set 与过滤语义文字存在。
- 独立 h5py verifier 核对所有 emitted key 对应 checkpoint/Preview、bounds 与存盘 NativeGrid min/max 精确一致、shape/firstIndex 一致、原始 H5 SHA 未变化。
- 本次 block extent 对 Preview 的最大差值均为0。这不覆盖/消除此前 per-cell bounds 1.776e-15 的独立 review finding，不自行新增科学容差。
- 独立工具 validation/io/verify_plotfile_blocks.py，依赖本机 h5py，参数 --plot/--checkpoint/--preview/--response/--output。只读原始数据，仅输出处理后的摘要。

## 仍未验收

SVG SSR 和纯映射检查不代替 Linux native window 手工使用。层级toggle/zoom/pan在真实窗口的实际操作尚需UAT。
仍需坐标点精确 native cell 回查（当前 LOD click 是 representative）、viewport finer query/索引/有界cache、大型读取取消/RSS、owner单位/测度/完整身份/显示语义review。
complete 仅指该文件存盘leaf输出完整，不表示完整parent hierarchy或科学认证。最多128 outlines，文件仍限定64MiB audit。无新的simulation/CUDA/Windows工作。
H5、完整HTTP响应、HTML和日志留在本机 studio/.local/integration/plotfile-leaf-outlines-20261003；提交只有实现、测试、工具与处理摘要。
