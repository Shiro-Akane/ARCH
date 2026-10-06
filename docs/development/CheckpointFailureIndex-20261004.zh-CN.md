# checkpoint 失败序号传播修复

基线 73be293adbe68483d2cd866ea09cee8f351c36de。
实际 DriverIO 在 write_chk 参数中使用 ctrl.chk_file_index++，serializer 拒绝后仍消耗编号。
当前 CPU Driver/Runtime/IO 源码重编的负向 fixture 实测退出1：
failed checkpoint consumed Driver index。失败前后源码/ELF 身份见 Summary。

## 修复和验证
仅将 checkpoint index 递增移到 write_chk 成功返回后；异常继续向调用者传播。
不改变 checkpoint format、字段、FP64、物理状态、timestep 或保存边界语义。
修复后实际 Cartesian/RZ 两例 PASS：
repair-ledger 形状错误拒绝、编号保持7、无文件；恢复合法 ledger 后同编号7成功，增到8。
regular-file parent 导致 HDF errno20/Not a directory；异常传播、编号保持8；
先前 checkpoint SHA 不变，恢复合法路径后仍以8成功，增到9。
原生 FP64/controller roundtrip 仍通过，time=0 step=0，未 simulation。
HDF stderr 是预期负向证据，不隐藏或绕过；完整原始日志留本机。

## 范围
本轮不改造 checkpoint 原有直接写入发布流程；不能声称 atomic publication、
checked close、ENOSPC、fsync、断电持久性已经验证。对已成功返回的 writer 实现按原契约递增。
Plotfile 的独立 checked-close/atomic-rename 语义不受此改动影响。
可复现脚本 validation/io/run_driver_checkpoint_geometry.py，raw H5/ELF/logs 位于
studio/.local/integration/checkpoint-failure-index-20261004-before 和 -after；
提交仅处理后摘要、脚本fixture和最小源码修复。
公共 RZ 演化、AMR 角动量、有限环重力、科学验收和 CUDA 仍未完成。未 push/tag/main merge。
