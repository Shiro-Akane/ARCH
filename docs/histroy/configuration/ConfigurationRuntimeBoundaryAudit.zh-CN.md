# O7.0 运行配置边界复核

更新（2026-10-02）：用户已明确恢复 G 接线与指定 t=0 对照授权；下文为 b6536533 的历史审计快照。G 可写成员/UniformGravity 入口现已迁移，共享常数 CPU 检查及 t=0 拓扑对照已执行，见 StudioIntegrationProgress 与 O7ConfigurationT0AmrSummary.json。运行配置构造边界及历史科学输入迁移仍未完成。

基线 b6536533，分支 studio/compute-optim-integration。本次仅只读审计，未执行 Driver 或科学输出。

## 已存在的防护

PreparedConfiguration 私有构造并拥有 const config/species 快照和模型实例身份。
DispatchSolver 校验该实例身份及 Evolution purpose，main 通过此入口启动。
Driver 要求 resolved plan/requirements/backend/startup order 并执行 ValidateControls。

## 未封闭的入口

src/driver/dispatch/bindings/DispatchImpl.h 的 launch_run 和 src/driver/Driver.h 的
run_simulation 仍直接接受公开 SimConfig/SpeciesManager。数值校验不证明输入经过
声明解析、checked Setup 或来自同一模型。顶层防护不等于底层构造约束。

DispatchSolver 又复制准备快照并用 resolve_nse_request 合法解析 use_nse=auto。
简单要求运行配置与 loaded snapshot 完全相同会误拒该合法路径。
下一步应区分输入/checked Setup 快照、允许的策略派生、受控构造的最终只读运行结果；
全部 dispatch bindings 与 CPU/CUDA 入口须一致迁移，不能删除 auto 或允许任意可信标记。
该改造尚未实现。

## G 退役缺口

输入层已拒绝 gravity_G，但 GravityConfig.G_const 仍公开可写。
SelfGravity RHS/边界/限步、GravityStage identity、checkpoint、GravityBox、JeansWave
仍读取该成员；UniformGravity 仍接受任意 G 参数。
radial_1d 的 1e-20 与 FLASH ARCH 输入的 6.67408e-8 不能直接替换后复用旧结果，
需按 Target 由维护者确认等效输入和科学参考。

移除 UniformGravity 任意 G 参数的操作此前被自动审批以旧 Phase 3B 范围拒绝，
未执行。生产 t=0 AMR checkpoint 对照同样未获放行，未执行，不绕过审批。

## 后续验证

编译期拒绝未经受控构造的入口；模型身份、组分和 checked Setup 快照不可替换；
InitialState 不得进入演化；auto 保留请求/解析结果且不修改输入快照；CPU/CUDA
共享同一语义。生产验证需解除权限问题后执行，原始结果只留本机。
本审计不代表 O7.0 或完整目标完成，不缩小 3C 与后续科学验收范围。


后续更新：实际 Driver/dispatch bindings 已改用私有构造 RuntimeConfiguration，auto 解析后冻结；CPU 构建、配置入口和 t=0 对照通过。上文为改造前审计，完整 O7.0 仍待其他条目关闭。
