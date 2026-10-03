# Plotfile 原始 FP64 的 JSON 传输反例

基线 f86f9706756277550d43c5ab0b035140abfa358f。对照 owner contract
23ff77c4f08419de2b3c5eadee214da2af25784e；本轮不重新 Build Core、不运行 simulation。

实际临时 HDF5 的负零经直接 slice 保留，但原隔离 worker / HTTP JSON 后变为正零。
修复前真实 HTTP 测试 3 PASS / 1 FAIL；完整日志仅本机保留。
Host 在 worker 成功响应与 Plotfile HTTP 响应两个边界使用 Node 24 的 JSON.rawJSON('-0')，
最终仍为合法 JSON numeric token；客户端 JSON.parse 恢复 number 的符号位。
只对 Plotfile 响应启用，不改变其他 Host endpoint 序列化、查询 schema 或科学数组。
原始样本表和 Inspector 使用保留负零的文本表示。

真实 HDF5 → isolated worker → HTTP 测试覆盖 1D [1,8] 和非正方形 2D [1,2,4]。
负零/正零、最小正负子正规数、最大正负有限值、1 两侧相邻 double 全部 uint64 位级一致，
文件读前读后 bytes 一致。反例是合成传输夹具，不是新的科学输出或完整科学验收。
NaN/Infinity 原有显式字符串规则不变；不宣称保留 NaN payload bits。

相关 18 项回归通过；扩展二维测试后完整 316/316 通过。
lint、typecheck、production build、git diff --check 通过；现有 large-chunk 提示保留。
本轮未重新进行 native desktop UAT；原 Sod/Cellular 真文件与界面证据见既有报告。
原始 HDF、完整日志留在 studio/.local/integration/plotfile-fp64-wire-20261003。
本轮不改变 checkpoint 格式、场值、物理定义、误差预算或来源认证级别。
