# RZ 原生 checkpoint writer 接线

基线 287b0c8c15876a15551496ef3d95228d5d11d697；承接独立 geometry_semantics_revision/chart。
DriverIO 读取 Runtime 的 immutable GeometrySemantics，显式传给 write_chk；不根据 cylindrical 名字推断 RZ。
write_chk 在创建目录前验证身份，将身份连同原始 FP64 场序列化。默认 existing 保持旧调用方兼容。
公共 SolverDispatch 尚采用 existing，完整公共 RZ dispatch、输入与续跑迁移仍未完成，不能称 RZ production restart 可用。

## 证据
CPU arch_checkpoint_compatibility / checkpoint_compatibility 1/1 PASS（0.15秒）。
新增真实 2D native block write_chk -> HDF -> read_chk：
rho/mom_u/mom_v/mom_w/eng/ENUC/两组 X 按原值精确读回；
time/step/dt_old/dt_burn/chk/plt/resume 状态一致。
未来 revision 在写前拒绝、旧正式文件 SHA 不变；非法 Cartesian/RZ 组合不创建输出目录。
这是无时间步进的内部状态序列化 fixture；constant 非零径向/方位动量不是轴正则物理 IC，不能用于科学演化验收。

实际 DriverIO.cpp 使用现有可信 CPU compile command 单独编译成功；没有复用旧 DriverIO 对象支持编译结论。
这里只做调用方 TU 编译，未将其链接进 Driver 执行 checkpoint。
source fingerprints 和 scope 见 RZCheckpointWriter-20261004.Summary.json。
完整日志/对象留在 studio/.local/integration/rz-checkpoint-writer-20261004；
原始 H5 留在 build-cpu/checkpoint-compatibility-data，不提交。
无完整 ARCH rebuild、simulation、CUDA、push/tag。

## 后续
仍需完整公共几何/API/IO 切换和实际 evolution/restart 验证；
角动量 AMR 传递与有限环重力遵循 Core 确认，不能自行换物理/放宽误差预算。
