# 实际 Driver checkpoint 几何身份验证

基线 119b173b65f1314f9202bcc0ade604eba4579e04。
fetch 后 compute/optim / codex/o8-boundaries 未变；未 merge、未覆盖独立工作区。
承接 RZCheckpointWriter 中“未链接 Driver 执行”的缺口，本轮编译当前 DriverIO、
DriverRuntime、Boundary、Regrid、ChkIO、CheckpointCompatibility、HDF5Writer 和 fixture；
依赖沿用现有可信 CPU build tree，不把旧 ARCH ELF 当作此次源码身份。

## 结果
真实 DriverIO -> write_chk -> HDF -> read_chk：
Cartesian 1D 与内部 cylindrical RZ 2D 共两例 PASS。
revision=1；chart 分别 existing/axisymmetric-rz，来自 Runtime immutable profile。
原生 rho=2、momenta=0、eng=5、ENUC=-.25、X=1 精确恢复。
成功 checkpoint index 从7到8一次；HDF/恢复保存边界的 chk index=7、plt=11。
dt_burn=.02、resume=true，time=0/step=0；未执行任何 timestep loop。
使用有效固定 EOS callback，仅测试 IO 机械链路，不是 EOS 独立科学 oracle。

首次 fixture 未初始化 registered-species repair ledger，正确触发 writer 拒绝。
随后只修正 fixture 初始化，不改校验或物理；失败原始记录保留。
本轮没有检查 checkpoint write/close 失败后的 Driver index 保持，不能由成功路径外推。

## 可复现和范围
validation/io/run_driver_checkpoint_geometry.py --build <existing CPU build>
--output-root <new ignored local directory>
新增 tests/host/io/test_driver_checkpoint_geometry.cpp；manual scoped runner，不扩展 CI 矩阵。
精确 recompiled sources / ELF SHA 见 RZDriverCheckpoint-20261004.Summary.json。
原始 checkpoint、编译日志、ELF 留在 studio/.local/integration/rz-driver-checkpoint-20261004
和 rz-driver-checkpoint-20261004-repair，不提交。

公共 RZ config/dispatch、完整 AMR 角动量转移、有限环重力、演化与真实 restart/CUDA 尚未完成。
此无演化两例不能替代 O7.5 或冻结科学场景验收。无 push/tag/main merge。
