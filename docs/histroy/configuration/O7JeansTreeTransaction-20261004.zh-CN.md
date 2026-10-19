# O7 JENS CPU AMR 事务阶段证据

依据 Core review 08ae94684d433ddbb7398a9de88c4fb9d09070b4；前置 checkpoint c3b8d6c219d3fc35b4cc78d270c68df456bfde1e。

本次仅开放内部 CPU 事务验证：借用当前 EOS，根据真实 conserved state 求绝热声速，不缓存旧阶段结果。普通指标保持原逻辑，JENS 不作为曲率指标；未开启时不调用 JENS EOS。jeans_cells 内部缺失值为 0，必须显式有限且 >=4；没有新增模型默认值或降低阈值。

细化使用 NJ < jeans_cells；合并以共享 AverageToCoarse 得到的真实父态再次求 EOS，NJ >= jeans_cells 才允许。拒绝合并不消耗池块；最大层级不足明确失败，容量不足沿用原事务回滚。

## 实际验证

重建并运行 jeans_diagnostics、refinement_indicator_math、amr_operation_plans、amr_flux_surface_plan：4/4 PASS。实际完整源码 architecture audit PASS。

新增真实 Tree 测试覆盖细化、父态合并 veto、升温后的合法合并、精确相等/相邻 FP64 阈值、最大层级失败、池容量失败、非法目标和关闭路径零 EOS 调用。旧数值与六个父态参考仍通过；父态最大相对差 2.7056947525674304e-16。

原始日志在 studio/.local/integration/o7-jeans-tree-20261004；提交的处理摘要为 validation/gravity/results/o7-jeans-tree-20261004/summary.json，不提交原始科学输出。

## 未覆盖与后续

这不是完整 JENS 交付：运行时 schema/能力声明继续关闭；生产 ARCH executable 本轮未重建。尚需 Driver 初始及 accepted macro/regrid 生命周期、restart 身份、CUDA 当前设备态和统一父态政策、正式输出消费者，以及对应冻结科学 gate。内部 RZ 父态静态参考不能代替完整 RZ 生命周期验证。

RZ Lz finding 保持开放；共享离散设计与环体预算仍独立待审。本次未改 RZ 物理定义、共享 G、验收阈值、Plotfile/checkpoint 原始数组或 Windows 适配。
