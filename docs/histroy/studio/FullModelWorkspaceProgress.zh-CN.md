# 全模型工作区与三维显示进度

2026-10-03，实施基线5bd1e780。沿联合交付计划全模型初态/AMR出口推进；不是完整阶段封箱。

## 本次实现

- 根据当前输入、project/case/build/binary身份匹配的 inspect-config 坐标维度选择 runtime Profile。
  不按文件名、模型固定名单或未提交文本猜维度。当前检查尚未完成时明确等待。
- Provider接受UI所选case身份，只向Host提交profileId、文本、revision、采样shape。
  acceptance case不匹配立即拒绝，不允许浏览器提供程序/命令。
- 二/三维采样预算来自当前Profile；三维shape仍[Nz,Ny,Nx]/x1-fastest。
- 三维原生轴切片，可分别固定x1/x2/x3。切片缓冲是副本，记录globalIndices。
  Inspector使用完整原数组索引和Core坐标/单位，不读取clipped/display值。
  切面外选择保留原Inspector并明确不绘制当前切面marker。
- Core native coordinate metadata驱动图轴，固定非活动坐标逐项显示，不硬写x3=0。
- Core uniform-state以单区初始状态表达，不伪装为空间演化曲线。
- 切片选择不加入Preview scheduling key、不编辑文本、不Save、不改变revision。
  现有已授权编辑自动Preview策略保持；slice操作独立于该策略。

## 验证

最终Studio/Host240/240、lint、typecheck、production build、diff check PASS。
新增回归覆盖非立方体三切面、全部raw索引、原数组不变、当前检查/身份失配拒绝、
generic Provider未知case及acceptance错配拒绝。

复用维护的test_full_model_preview.py Gaussian输入，实际build-cpu/bin/ARCH CPU
init-only生成Cartesian/Spherical/Cylindrical的[2,3,5]数据；
通过当前TS validator，八字段每个切面与原数组逐项一致，Inspector坐标/索引一致。
每次request/config SHA校验通过、timeStepping=not_executed、
scientificOutput=not_created，独立cwd前后无新增文件。
原始JSON/stderr/完整检查日志仅留studio/.local/integration/full-model-workspace-20261003，
本提交不包含数组或科学输出。

## 未完成与边界

本次是Core实际响应到显示adapter的集成证据，不是真实HTTP Host/桌面UAT。
生产UIbinary仍旧，未伪造Manifest或替换binary。需要按正式Host Build更新并真实验证。
三维/曲线AMR契约与显示、实际细化mixed hierarchy、全模型production desktop UAT、
独立plt出口仍待实施。三维切片当前不叠加未校验AMR；不能称AMR完整。
JENS/RZ/CUDA/冻结科学性能验收仍未完成，历史G/architecture审批仍独立待处理。
不push/tag/main merge；当前增量不改变科学物理定义和误差阈值。
