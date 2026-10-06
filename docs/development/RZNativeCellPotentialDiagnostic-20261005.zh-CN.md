# RZ 实际 native cell-center 势的连续参考诊断

依据科学清单第7.1/7.2/7.4节，未知 Phi 为几何 cell center 点势；
继续使用批准的同源 piecewise-constant full-ring 势参考。
本节点补全真实已求解 mixed probe 的所有 cell 点势对照，
不新增接触力 oracle、科学预算或生产能力。

## 真实映射

现有 rz_matched_native_reference.py 增加 --observer-set cells；
默认 boundary 入口保持原有范围。新入口读取实际 potential 数组，
要求与真实叶单元一一对应且有限；用已核对 root/leaf 的原生 edges
确定几何中心，中心不能精确表示时明确拒绝，不用容差补齐。
ρ来自显式真实 density，不从RHS反推；保留同一source hash和依赖stamp。

实际FP64 G与Decimal数学G表示差异显式换算。
逐cell点势差按完整环体V归一化RMS；共同pi在分子/分母解析约去，
不用等cell权重、Cartesian体积或cell-average势替换点值。
该RMS是点势误差的原生体积加权范数，不是势体积平均值。

工具入口的6项测试PASS，新增覆盖全部cell映射、
不同径向体积1:3权重、实际potential改变不改变source身份、
缺失/非有限potential在求积前拒绝。architecture audit/diff check PASS。

## 全部真实样本结果

复用上次probe.json的准确SHA ca3135a23f6b9130f7373c8dd29b161e309398238d7e6421f6386de949a7cb8f。
两个静态mixed case各12个L0、16个L1叶，共56个observer；
每observer包含1个自身接触叶与27个源外叶，未删任何源贡献。
80位Decimal，16/32阶，Duffy tPanels=1。

| radial origin | 32阶最大点势差 cm²/s² | 原生V-RMS点势差 | 参考16→32阶最大变化 |
|---|---|---|---|
| 0 | 4.085977e-10 | 2.391530e-10 | 5.301465e-14 |
| .5 | 5.429294e-10 | 2.780378e-10 | 5.301465e-14 |

处理后 [summary](../../validation/gravity/results/rz-native-cell-potential-20261005/summary.json)
分列level最大差、实际source身份、代码/probe/原始诊断SHA。
原始reference每点值与probe数组保存在本机
studio/.local/integration/rz-native-cell-potential-20261005；
不上传数组或H5。复现命令：

    python3 validation/gravity/rz_matched_native_reference.py --probe-record <existing actual probe.json> --output <new local JSON> --observer-set cells --precision 80 --order 32 --t-panels 1

参考阶数差只是估计；本节点没有可靠quadrature误差界、空间细化序列
或批准的连续Phi预算。因此不把小residual或参考两阶接近当作科学PASS，
也不由以上观察值拟合新阈值。已通过的离散原请求证据继续独立保存。
相同源边界势、实际cell势、连续力与空间误差必须分开验收。

## 剩余接口与门槛

- 此source stamp属于静态numerical probe的抽象dependency，
  不是实际Runtime RZ所有block publication的认证。
- 未做continuous face force、side gather/cell acceleration及独立空间收敛。
- 接触求积参考仍是estimate；本节点没有把它升级成certified。
- 请Core按既有短设计确定连续Phi/force的参考误差预算与科学判据。
- 不改scientific Core、不重建production ARCH、不重复JENS receipt，
  不运行simulation/CUDA/长跑、不开放RZ gate，不开展Windows适配。

production CPU binary仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。
整体联合目标继续，不因这个诊断节点完成而收尾。
