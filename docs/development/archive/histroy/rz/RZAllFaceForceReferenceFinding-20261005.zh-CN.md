# 全量 native 面力参考诊断与未解析 finding

结论：208 面诊断完成；RZ-CONTINUOUS-FORCE-REFERENCE-01 OPEN。
不判定 Core 面力科学 PASS/FAIL，不解除 RZ production gate。

## 真实数据与参考

复用已发布 quartet 节点四组真实 uniform/mixed solved 数据，88 native cells，
radial origin 0/0.5，全部208面。原始输入hash与已发布receipt逐项一致；
当前新生产ARCH ELF不是这些旧静态数组的producer，不能冒称它生成了这些场。

实际g=-face_gradient，对照同一face.center法向点力；不是cell平均力。
逐bit stored-field映射已由f20a1eef节点核对。独立参考不导入生产kernel：
对matched常密度源计算Decimal full-ring Phi，再固定中心差分，
h=相邻最小cell-width/8、/16、/32，Gauss16/32，precision80/100，t-panels2。
轴面g_r严格0，不使用小半径阈值或epsilon。

四组源同rho、无overlap，Fraction验证叶面积总和等于共同包围矩形：
独立参考可对同一精确常密度并集积分。所有native cell/face/source身份仍保留，
没有改求解网格、密度或物理源；非均匀源仍逐叶参考。
这种等价积分不提供quadrature认证。

## 结果（单位 cm/s²）

| radial origin | mixed | faces | area RMS diagnostic delta | max diagnostic delta | max order16→32 drift |
| --- | --- | --- | --- | --- | --- |
| 0 | False | 36 | 8.813045e-10 | 2.253487e-09 | 2.536085e-09 |
| 0.5 | False | 40 | 1.171891e-09 | 4.282878e-09 | 2.552986e-09 |
| 0 | True | 64 | 9.028419e-10 | 2.427210e-09 | 2.536085e-09 |
| 0.5 | True | 68 | 1.124784e-09 | 4.590722e-09 | 2.552986e-09 |

最大step /16→/32变化约4.08e-10；precision80→100变化约2e-82。
积分阶数变化约2.5e-9，与若干实际场偏差同量级；因此不能把final delta
当作已解析的真实force误差。精度稳定不是积分收敛或可靠余项证明。
全部观察点保留，没有裁剪坏点、松阈值、abs/softening或静默丢源。

## Finding、职责与下一步

问题定位于独立连续参考的求积/差分解析度，尚无证据据此判定Core力算子错误。
原>=1.8制造解标准与residual请求保持；本组不是refinement序列，不能反推空间阶。
Core需按已有RZContinuousPotentialForceReferenceContract明确接触/源内force
参考、域/分区/norm/可靠界要求与预算；不从本次观测值倒推验收条件。
后续可按其裁定实施Duffy导数或明确限定的诊断参考。
该finding与axis/viscosity、Runtime身份、CUDA及长跑门槛独立。

## 交付与复现

validation/gravity/rz_matched_face_force_reference.py
  --probe-record LOCAL_INPUT.json --output NEW_LOCAL_RESULT.json

提交处理后摘要validation/gravity/results/rz-all-face-force-20261005/summary.json。
全部实际/参考数组、逐面序列、full log保留
studio/.local/integration/rz-all-face-force-20261005/，不上传。
1309.406秒，peak owned RSS19832KiB，swap0，guard未停止；
期间并发了其他构建/短检查，不能作为正式benchmark。
