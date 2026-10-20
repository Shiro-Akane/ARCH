# RZ 独立参考近接触补参数修正

## Finding 与最小修改

RZ-REFERENCE-COMPLEMENT-01：旧独立 exterior/contact 参考先计算
m=4Rr/s²，再在 AGM 中用 1-m。有限精度近接触可将 m 舍入为 1，
使一个真实正距离被误判为数学奇点。80 位算术、R=1、
dr=1e-60、dz=2e-60 的明确反例已经复现。

按科学清单 §7.2，独立参考直接由距离计算 q=d²/s²，并以 sqrt(q)
初始化 AGM。exterior Phi/force 与 contact Phi 的实际积分调用均使用新入口。
显式 m 输入的旧兼容函数仍保留；q=0/负值/大于1/非有限全部拒绝。
没有 epsilon、softening、abs 或静默丢数据。生产 Core 完全未修改。

## 验证与身份

脚本：validation/gravity/rz_reference_complement_audit.py。
处理后结果：validation/gravity/results/rz-reference-complement-20261005/summary.json。
原始 producer：studio/.local/integration/rz-reference-complement-20261005/result.json。

q=1、0.5、0.25、1e-20、1e-100、1e-300 共六例，
40/80/120 位 K 与180位 Machin-pi AGM 对照，E 跨精度一致。
算术误差检查按所请求位数设置保护位，不是新增物理阈值。
五个非法 complement 反例全部拒绝；旧舍入奇点反例保留。
两个实际 exterior Phi/force 与三个 contact Phi 积分调用，
相同8阶 Gauss 下60/100位 FP64结果一致。

## 覆盖边界

这是参考工具的算术修正，K 高精度对照仍使用 AGM 数学恒等式；
E 跨精度一致不是独立 force oracle。
相同积分阶数的精度一致不证明积分收敛、可靠余项或空间准确度。
contact force、连续 Phi/force 科学签收、原空间阶、
RZ 全消费者、CUDA 和长跑门槛继续保留。
RZ-REFERENCE-COMPLEMENT-01 在上述参考调用范围关闭。
