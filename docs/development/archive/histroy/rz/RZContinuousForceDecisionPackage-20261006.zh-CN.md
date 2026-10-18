
# RZ 连续面力参考独立决策包
状态：待验证 / 待 Core 确认。RZ-CONTINUOUS-FORCE-REFERENCE-01 OPEN。

## 现有证据和限制
RZAllFaceForceReferenceFinding-20261005.zh-CN.md：四组 uniform/mixed、轴线/离轴，
88 cells、208 faces；真实比较对象为 face.center 法向点力 -face_gradient，
不是 cell 平均或面平均。原始数据 producer 身份沿旧 receipt，不冒称新 ELF。
独立 Decimal 环体势中心差分使用 h=width/8,/16,/32，Gauss16/32，
precision80/100。阶数漂移最高约2.55e-9 cm/s²，与实际差同量级；
差分漂移约4.08e-10。精度漂移约2e-82 不证明求积误差已解析。

## 可推进候选
扩展既有 matched-source 工具，不导入生产积分 kernel：
1. 对 source 内部、面/边接触进行 Duffy 分区与奇异主项处理。
2. 分别改变 quadrature 阶数、分区数、差分步长、高精度位数，
   保留全部 native faces，包括坏点；记录每个因素的收敛证据。
3. 如用势导数构造力，先独立推导可交换积分/微分的条件与接触项；
   既有 Duffy 势验证不能冒称力验证。
4. 轴线径向力=0 使用解析对称极限；源分割、平移、对称性作独立反例检查。
5. estimate、observed drift、certified bound 分开；WorkLimit/未解析必须传播失败，
   不加 softening/epsilon 或丢源以获得通过。

## 待 Core 确认
源内/接触的参考定义、observer 点值/平均、域、求积/差分验收界限，
与空间离散、AMR、边界截断、solver residual 的独立预算分配。
原 >=1.8 制造解和 physical residual 门槛保持。
当前仅诊断，不据未解析参考判断生产面力科学 PASS/FAIL。
即使另三项 RZ 决策落实，此 finding 仍独立阻挡完整 RZ 签收。
