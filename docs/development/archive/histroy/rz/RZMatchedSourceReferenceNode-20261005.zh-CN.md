# Matched full-ring reference composition node

基线：9768426c4856ae36a35d83876e6a855de1961cc0。
状态：TOOL_TESTS_PASS；不是连续物理／生产 Runtime 科学验收。

## 实施与身份

新增 validation/gravity/rz_matched_source_reference.py，消费显式 sourceId、
唯一 leaf id、逐叶 r/z bounds 与原密度，叠加完整方位角的常密度有限环体。
拒绝重叠体积、负密度、非有限值、无身份；零密度为精确零源。
不从 source RHS 反推 rho，不调用生产 GridMetrics／重力核／EOS／求解器。
当前 sourceId 是显式标签，不是自动核验生产 provenance 的凭据。

轴点复用 Decimal 解析 primitive；轴内／接触使用同一连续极限。
离轴仅复用源外 K/E 参考：任一有质量叶单元 inside/contact 就拒绝该 observer，
不得删掉贡献后继续相加。CLI 保留每 observer 的失败而不写全局科学 PASS。
设置 order/precision 的界限沿用既有诊断工具，不成为生产参数。

axis_reference 新增 keyword-only decimal_output=False，默认旧 FP64 接口保持。
组合时选 Decimal 输出，避免每叶先舍入 FP64 再相加。
G 沿用十进制 6.67430e-8；比较生产 FP64 G 时必须显式处理常数表示差异，
本节点没有悄悄调整常数或科学定义。

## 检查与证据

7项新测试 + 5项轴线旧测试 + 8项离轴旧测试，共20项PASS：
轴线解析分区、默认接口兼容、源外分区、非均匀密度逐叶叠加、
接触贡献不静默丢弃、源身份／重叠／非法密度拒绝、
零源／非法设置、CLI失败保留和禁止覆盖已有结果。
新测试的数值条件只是独立工具恒等式检查，不替代冻结科学阈值。

独立非均匀2叶fixture，rho分别2/7，计算axis/exterior/contact观察点。
order16→32与precision80→100分别变化；输出Decimal标量差，
接触结果明确UNSUPPORTED_OR_FAILED。
这些差是估计，不是认证误差界，不通过观察差反推新budget。
summary记录输入、脚本与测试SHA256。

处理后摘要：validation/gravity/results/rz-matched-source-reference-20261005/summary.json。
输入、逐点结果及完整测试日志留：
studio/.local/integration/rz-matched-source-reference-20261005。
没有H5/plt/checkpoint、simulation、configure或build；
Core/production ELF无改动，沿用已有匹配冻结JENS9+9，不重复。

复现：
    python3 -m unittest discover -s tests/tooling/validation -p 'test_rz_*reference.py' -v
    python3 validation/gravity/rz_matched_source_reference.py --input <explicit-leaf-source.json> --output <new-summary.json> --order 32 --precision 80

## 仍需完成

此节点只是matched-source组合工具：当前诊断输入是独立fixture，
不是实际Runtime density/stage/AMR publication。
已有solved record未直接导出rho；没有从Poisson source猜密度来替代真实接线。
后续必须导出当前原生叶density和真实身份，再对接observer/生产势与面力，
保留每个不支持或失败点。

源内／接触离轴力、连续Phi/force预算、原空间收敛、真实Runtime失效、
RZ-AXIS-01/RZ-VISC-01及完整角动量消费科学gate均未关闭。
对应短设计见RZContinuousPotentialForceReferenceContract-20261005.zh-CN.md。
不启用生产RZ或新RZ长跑；CUDA按批准CPU gate后统一数学路径。
