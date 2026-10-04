# O7 JENS CPU accepted-state 生命周期接线

前置 checkpoint：523bec6a21422432f8104d1433ad50ef9a19259e。
本次遵循 Core 08ae94684 批准的父态和精确阈值规则，不改 G、EOS 或误差门槛。

## 实现

Driver 初始化或恢复后，在 backend 启动与首次科学输出前完整检查 JENS；
每个完成的宏步在下一步或最终输出前重新检查。复用 DriverRuntime 的原拓扑、
迁移、状态 residency 和发布事务，未创建另一套 AMR 数值实现。

新增只细化 repair 模式，不在普通 regrid 间隔之间触发曲率指标细化/合并。
每次从当前 accepted conserved state 通过当前 EOS 求声速；没有缓存旧阶段结果。
循环在最大层级之后仍检查一次，不以 pass 耗尽掩盖 unresolved 状态。
CUDA JENS 在 backend 启动前明确拒绝；device regrid 也保留拒绝检查，
不能用陈旧 Host buffer 冒充设备 accepted state。

## 证据

- guarded CPU ARCH 与 shared scheduler 重建通过；未触发 guard，无 swap 增长。
- 最终 jeans_diagnostics、shared_stage_scheduler、configuration_api_contract、
  config_input_records、case_configuration、configuration_input、
  checkpoint_compatibility：7/7 PASS。
- 实际完整源树 architecture audit 与 git diff --check PASS。
- 实际 Runtime fixture 初始两叶细化后发布新 accepted state，触发四叶细化；
  resolved 状态保留身份，最大层级失败不发布部分身份，关闭 JENS 不执行事务。
- 原 DriverIO Cartesian/RZ t=0 native checkpoint、恢复、写出失败与索引/重试
  检查通过；预期 HDF5 create failure 诊断被保留，不视作成功科学写出。

最终 CPU ARCH SHA256：42a61aaf9a67430ab501ba20f1c57a45e22f143055d6f0d81004dedb6009f498。
原始 HDF5、ELF、编译及检查日志留在本地 studio/.local/integration/
o7-jeans-runtime-accepted-20261004 和 o7-jeans-driver-build-20261004。
提交处理摘要：validation/gravity/results/o7-jeans-runtime-20261004/summary.json。

## 覆盖边界与剩余任务

新 accepted state 是通过真实 scheduler publication 注入的夹具，不是已完成的 hydro
科学演化；t=0 DriverIO 回归也不是 JENS 演化 restart 验收。
公开 schema、解析及能力 JENS gate 尚未解除。还需运行时/输出消费者、
jeans_cells checkpoint 身份、独立低密度与演化参考、restart、CUDA 后端以及关闭路径
性能验证。Studio managed binary/profile 未自动替换为本轮 CPU binary。

RZ Lz finding 保持开放，离散设计与环体参考预算仍待独立 review。没有新增 Windows
适配，没有上传原始场数组或 checkpoint，没有宣布完整 O7 科学验收通过。
