# Plotfile 全叶单元覆盖与边界 finding

基线 17d5536fc29a643236b39af4fec7d60cacb999c4；上游 codex/o8-boundaries 仍为 23ff77c4f08419de2b3c5eadee214da2af25784e，compute/optim 为 8fc0dd25eefd2243e8c36f85440bac46994e2e73。仅 fetch，不 merge。

## 方法与覆盖

新增 validation/io/verify_plotfile_coverage.py。只读所有 Cartesian 1D/2D leaf-cell native bounds，按文件原序分析。通过坐标压缩与精确 Fraction(binary64 endpoint) 计算全域覆盖、缺口、重叠重复测度、域外测度；没有数值容差。二维压缩网格最多 2,000,000 节点，超过拒绝，不宣称通用大型 AMR 支持。

另以文件外原始输入 domain/root counts 与 HDF level/logical index 构建 dyadic 逻辑单元分区，独立检查覆盖；该证据不替代所有 field 数据、EOS 物理或 checkpoint 认证。坐标端点以实际 binary64 输入表达，未把十进制字符串当无限精度 Core 物理值。每单元存盘端点和理想逻辑端点的差异只报告，不修正原值。

新测试覆盖混合层划分、缺单元、重复、越界、单 ULP gap、不正方形 [1,2,3] HDF 的 x1-fastest 顺序及单元 bounds 损坏、压缩预算；5/5 PASS。全部合成 H5 在临时目录。既有 320 项 Studio/Core 实现未变，不重复无关 baseline。

## 真实 t=0 输出

| 对象 | Sod | CellularDet |
|---|---|---|
| leaves / cells | 12 / 192 | 20 / 5120 |
| shape | [12,16] | [20,16,16] |
| 外部 domain | [0,1] | [0,25.6] × [0,12.8] |
| active roots | [4] | [2,1] |
| logical dyadic partition | 恰好覆盖一次 | 恰好覆盖一次 |
| stored FP64 bounds partition | 恰好覆盖一次 | FAIL：极窄 gap/overlap |
| exact gap measure | 0 cm | 2.359001882723533e-13 cm² |
| exact overlap-excess measure | 0 cm | 1.3073986337985845e-13 cm² |
| outside measure | 0 | 0 |
| stored endpoint vs logical 最大差 | 0 | 3.3306690738754696e-15 cm |

精确有理数、最多六个反例区间、文件尺寸与 SHA 都在 Summary。Sod diagnostic exit=0；Cellular exit=1。逻辑覆盖通过不能把实际 stored bounds finding 改成 PASS。压缩 bin 数量不是坏单元数，重叠测度是 multiplicity excess，不是科学积分误差预算。

文件外 input SHA：
- Sod：8a2d92fed96382017dbca79dedbcebbf870d8e9c3e15c6efe4c3f829f0353e69。
- Cellular：e280f011586d02906d2bed1f65faeb980748be54deb29733eb55faf971f8a0c0。

实际 plot SHA：
- Sod：913d30637d4c416e84984dd1c1bfb4ac627cf9896db394f040c614446e93874c。
- Cellular：e233a0f5918889d31ba790555a4b1ef4ff92d8eedec4516c1b80730ef6be2e9c。

来源 binary 325ef7806af16e85c6dbfc545937da5e5bab32115e57ed5167a788296be4f91b。其构建来源仍按 PlotfileEosConstituents Summary 的基线与 dirty inputs 记录，不冒充当前 binary/source HEAD。全部原始数据留既有本机 amr-production-oracle 持久目录。

## production Inspector 复现

新增 validation/io/verify_plotfile_coverage_points.mjs，用 production isolated reader 查询同一实际文件：
- [0.1,1.2]：独立 h5py 半开 bounds 匹配 0 单元，reader 返回 NO_NATIVE_CELL。
- [0.1,2.6]：匹配 2 单元，reader 返回 AMBIGUOUS_NATIVE_CELL；原索引 1216、1232，同一 block=4 相邻 j=12/13、i=0。

复现退出0表示已证明错误行为，与 Cellular geometry diagnostic exit1 分开；不能写成 geometry 通过。两次查询后原文件 SHA 不变。不是原生 GUI 点击 UAT。

## 路径与后续边界决策

PlotGridMetadata.h 的 y upper 使用 y_lower + dx2，下一单元 lower 使用 x2_min + (j-ng)*dx2。两条运算路径在非二进制精确 spacing 下会舍入不同。x 方向复用 Grid::GetFacePosL/GetFacePosR；不能据此宣称所有跨 block/跨 level 的浮点面已一致。

下一步应核对共享面坐标定义，先关闭同块相邻 y-face 的算术不一致，再检查跨 block/level 及整个 stored partition。若改动要触及 Core geometry 或重新定义原生面坐标，需要明确科学语义 review；本次未修改 writer、科学 Core、checkpoint 或阈值，也没有 snap/epsilon、静默选第一 match 或把 bounds 从中心推断。

复现示例：
python validation/io/verify_plotfile_coverage.py --plot <local-Cellular-plot> --domain '[[0,25.6],[0,12.8]]' --roots '[2,1]' --output <local-summary.json>
node validation/io/verify_plotfile_coverage_points.mjs <local-queries.json>
python -m unittest discover -s tests/tooling/validation -p test_plotfile_coverage.py -v

本机 evidence：studio/.local/integration/plotfile-full-coverage-20261004；提交仅脚本、反例测试和处理后的摘要。最初 system Python 因无 h5py 未执行 point 检查，随后用既有 venv 正常复现；未安装新依赖。没有 Build、simulation、新 Plotfile、CUDA、push 或 tag。完整联合交付目标仍未完成。
