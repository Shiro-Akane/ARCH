# RZ 实际 Runtime fatal rollback 节点 — 2026-10-05

结论：CPU 内部事务工程验证 PASS；仅 t=0，不解除生产或科学能力门槛。

## 修改与职责

原 DriverRuntime 的显式内部 CPU RZ candidate 入口接受一次借用的 finalizer 验证回调。
回调在原 staged topology activation、真实 BC/ghost 工作之后、ledger publication 之前，
异常仍走原 backup/abort/topology transaction 恢复路径。
生产入口禁止带回调；没有 SimConfig/API/Studio 映射，没有替换迁移算法或物理公式。
不修改 Driver 演化顺序、阈值、共享 G 或 CUDA 实现。

## 实际验证

原两块/512 cells、两组分、非均匀正密度、非零角动量输入。
连续三次在真实 fallible finalizer 内破坏保留源块字段并抛出固定异常：
每次恢复原 active topology、handles、Current versions、ghost readability；
rho、三 momentum、eng、enuc_rate、mass_fractions 全数组逐 bit 与备份一致。
峰值池 10（2 原块 + 8 staged），失败后均回 2，无 staged 泄漏，无成功记录。
RepairBudget 等非数组元数据不在该 bitwise 断言覆盖范围。

恢复后实际源数组地址共变化 6 次，不承诺旧借用指针有效。
真实 GravityStage 再次 gather 时逐块核对指针等于当前源数组，并核对实际 Current
handle/slot/version。原完整 native 请求残差上界 4.4674101534278339e-15，
低于原安全预算 1.4586002329335141e-14。invalidate 后旧场拒绝读取。
随后无回调合法重试 2→8，池回 8，新 ledger 可读、旧 handle 拒绝。
默认生产 RZ perform_regrid 仍拒绝。

实际 Cartesian 默认 Runtime 回归 4→8→4、7 gathers PASS；
非首版本和 device-only lease 拒绝保持。架构审计 PASS。
Driver 生命周期只读检查显示失败异常向外传播，没有失败后本地演化消费；
未据数组地址变化臆造或修改 Driver 物理生命周期。

## 身份与证据

标量、测试源码 fingerprints、独立 ELF 身份见
validation/gravity/results/rz-actual-rollback-20261005/summary.json。
本节点 ELF SHA-256：
c118c304a7fafe777691518ed7cc1724eb8e754ee7936c60c875bcce9cd1ecda。
CPU 编译加实际恢复场验证总计 121.227 秒，peak owned RSS 1350784 KiB，
swap 0，memory guard 未停止；此数不是生产性能基准。
原始 stdout/stderr、ELF、全日志保留于
studio/.local/integration/rz-actual-rollback-field-20261005/；
早期 rollback-only 与默认 Cartesian producer 保留，未覆盖。
未重建或宣称生产 ARCH ELF 当前。

## 覆盖边界与下一步

这是注入 fatal Host finalizer 故障，区别于先前 parent-group veto；
不是实际物理失稳、Device rollback 或科学演化验收。
持续 Phi/force 独立参考、Hydro torque/axis/viscosity、原空间阶、
完整 CUDA 和长跑仍开放。JENS public CUDA gate 候选仍等待明确审批，
不借本节点绕过。下一节点按科学清单 §§6–8 继续消费者与独立参考验证。
