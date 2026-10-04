# O7 JENS 条件输入与 restart 身份

前置 checkpoint：13c673c7b2ce91fa2003559f79db463c688b6229。
依据 Core 08ae94684 与 4d9f38ac uniform-lifecycle-1；本轮不是完整 JENS 激活。

## 条件配置

唯一 Core 注册表增加 jeans_cells，float、Grid/AMR、unit=1，无 allowedDefault，
有限且 >=4。只有 refine_var 包含 JENS 才必填，混合列表同样适用；
仅 plt_variables 请求 JENS 与完全关闭时不要求目标。显式非法值即便暂不适用
仍返回 INVALID_RANGE；合法未消费值保持输入记录，不自动写回或变成科学默认。

API 元数据完全来自 Core 注册表，真实 --config-schema 返回 95 个可用标准键
（表内另有 retired gravity_G）。新增字段的条件、units、范围和 presentation 已核对。
不将本次 count 当作永久生产常量。

未完成科学 qualification 的显式 JENS AMR/plot 请求改为明确拒绝，
不再 warning 后静默关闭或选择其他指标。公开 availability 仍 false，
原因更新为 lifecycle qualification incomplete。ALL 当前继续过滤 JENS；
最终 CPU 激活/三通道验证之后再调整能力。

## Checkpoint 身份与兼容边界

state_controls 升至 revision 3、20 值，在原字段之后记录：
JENS AMR 是否启用；仅启用时记录 jeans_cells，关闭时目标固定为未消费的 0。
输出选择和合法未消费目标不改变 accepted trajectory 身份。

真实 HDF round-trip 验证 active JENS identity；启用状态变化或目标 160→161
拒绝 read_chk。新增记录的 enabled 必须为 0/1，active target 必须 >=4；
false target 必须为 0。原 conserved/native/controller 数据及 checkpoint file
format v6 未改写。

**旧 state_controls revision 1/2 明确拒绝，不做隐式迁移。**
已保存 checkpoint 保持原文件不动；如需续算应使用其原 executable。
这是身份 schema 变更，不能把当前 scoped round-trip 写成历史 checkpoint 全兼容。
完整实际 JENS 演化 checkpoint 仍需冻结包中的 0.01→0.02 s 验证。

## 验证

guarded CPU ARCH 和相关 scoped targets 构建 PASS，无 swap 增长。
最终 configuration_api_contract、config_input_records、case_configuration、
configuration_input、jeans_diagnostics、checkpoint_compatibility：6/6 PASS；
实际 source-tree architecture audit 与 diff check PASS。
API 首次因旧固定 94-key 断言失败；按真实唯一注册表核对 key set/count，并补
新字段的无默认/条件/范围/单位断言后通过；未削弱科学门槛，初次失败日志保留。

CPU binary SHA256：49ca838d7617c15fc0fc27caa0c3552e995da50b7e6738b97945b3fa58ea7044。
原始日志/schema/checkpoint 测试输出留本地；处理摘要见
validation/gravity/results/o7-jeans-input-identity-20261004/summary.json。

## 后续

统一开放经过 CPU 验证的 Runtime/API/Studio 消费路径，沿现有 GravityBox campaign
执行 uniform-lifecycle-1 的三个通道、1D/2D/3D、真实分割时刻续算、零源势力、
守恒与 repairs gate；之后统一处理受影响 CUDA。
本轮不宣布该科学包通过，不关闭 RZ Lz finding，不新增 Windows 适配。
