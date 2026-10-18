# RZ 实际面梯度导出映射检查

结论：四组88 cells、全部208 faces 的 stored-field 数值映射 PASS。
没有改 Core、生产能力或科学阈值。

## 方法与结果

复用已记录的真实 native uniform/mixed solved probe，不重跑求解：
radial origin 0 / 0.5，各 uniform16cell、mixed28cell。
输入逐叶密度、bounds、source stamp、solved Phi、boundary Phi、
最终 face stencil 和导出 gradient 全部保持。

独立 Python 按原 anchored 差值与固定 Neumaier 顺序重建导出 gradient；
所有208面 FP64 bits 一致。精确 Fraction 单独计算同一 stored 输入的数学
stencil 值，记录逐组最大算术偏差。该值不包含 ideal coefficient 或连续 PDE 误差。
力符号是 g=-gradient；200个非零面的错误符号反例可被辨别。
零面的符号不能提供该反例，不算入200个反例。

这只验证数组/stencil/符号映射，不是独立物理算法验收。
四组完整源和面仍用于正在运行的独立连续 Phi 差分诊断；
不会把映射 PASS 自动升级为 contact force 或空间收敛 PASS。

## 复现与身份

python3 validation/gravity/rz_face_gradient_mapping_audit.py
  --probe-record studio/.local/integration/rz-all-face-force-20261005/input.json
  --output NEW_LOCAL_OUTPUT.json

处理后摘要：validation/gravity/results/rz-face-gradient-mapping-20261005/summary.json。
原始输入/数组/日志保留本机 studio/.local/integration/rz-all-face-force-20261005/。
原求解 producer 保留于 studio/.local/integration/rz-quartet-reference-20261005/。
输入 hash、源码 hash、source identity 随 summary 提交；没有重建生产 ARCH 或声称 freshness。

完整连续势/力、axis/viscosity、RZ 演化/CUDA/long-run 门槛保持。
