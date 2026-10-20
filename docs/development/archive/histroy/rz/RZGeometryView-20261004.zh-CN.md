# RZ 共享 GeometryView 身份与数学分派

## 实现
GeometryView 新增内部 GeometrySemantics，默认 Existing，显式 AxisymmetricRz。
保持 Geometry 枚举、geometry_from_name、Grid 生产装载和 runtime capability 不变。
make_rz_geometry_view 只接受 cylindrical 2D，返回副本，不修改原 view。
显式 view 的 CellVolume/FaceArea/PhysicalSpacing 复用先前 full-ring Rz 数学；
generic geometric source 选择 mom_w 作为旋流，与显式 RZ adapter 同一叶函数。
PhysicalPosition(view,native) 对 RZ 返回 r,0,z 的代表性子午面位置。
此位置不是把环体压为点源的引力近似，正式环体边界仍须 Core 方案 review。

## CPU 验证
限定构建 arch_curvilinear_metrics、arch_amr_operation_plans；CTest 2/2 PASS。
轴/普通单元 full volume、径向/轴向面积、dr/dz 与共享叶函数精确一致，
负 z 坐标保持，源项 mom_u/mom_w 与独立显式 adapter 完全一致，
质量/z/能量源项为零；错误 geometry/dimension 被 factory 拒绝。
旧几何 metric/source、CFL/viscous 和 AMR/seam 回归通过。
没有放宽既有算术门槛、独立配置、完整 ARCH 构建或 simulation。
精确编译基线/dirty inputs/test ELF SHA 见 Summary；日志保存在
studio/.local/integration/rz-geometry-view-20261004。

## 迁移边界与下一步
这是内部 view 路径，不能让旧 Grid/API 装载器提前输出 RZ capability。
现有显式 geometry/dimension 标量调用尚不携带 semantics，必须逐一迁移；
扩散/诊断/CUDA cache、AMR measure/flux、elliptic、gravity、IO/checkpoint
均不能因本次 helper test 通过就认为已切换 RZ。
下一步检查运输和椭圆消费者对 GeometryView 的分量/面积/spacing 使用，
明确绑定 chart identity；随后完整 CPU 短验证与科学 review，
再统一 CUDA。原始旧二维 checkpoint 不得自动按新语义恢复。
本轮无 push/tag/main merge，无科学结果验收声明。
