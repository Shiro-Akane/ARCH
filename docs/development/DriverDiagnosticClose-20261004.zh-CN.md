# Driver 诊断流 buffered failure 传播

现有真实 DriverIO time=0 fixture 复现 run_timings.tsv 和 cpu_stage_timings.tsv
指向 Linux /dev/full 时，生产写入被缓冲，旧检查均未拒绝。
前置实际 fixture exit1，两文件 propagated=0；完整失败/编译证据本机保存。

最小修复：DriverIO.cpp 原所有者中统一 close_diagnostic，
显式 flush → check → close → check，错误含诊断类型。
六个现有诊断 stream 均调用；state_repairs 已有行为合并进同一入口。
不改 timing schema、repair ledger、科学数组、输出序号、checkpoint 格式或物理输入。

当前源码独立实际 Driver/Runtime/IO 重编 fixture：
Cartesian/RZ × run_timings/cpu_stage_timings 四种 /dev/full 错误均向调用者传播；
去除故障后同一对象恢复写出，repair 序列化和之前 checkpoint SHA 不变。
既有 profile/native/controller/checkpoint拒绝/create/retry及t=0 Plot检查通过。
fixture ELF：4ff5396358835adb543d269307981fca4f8fbaea4717631caae751f47e09a225。

这是 time=0 IO 工程验证，不是演化、科学或设备验收。
regrid/CUDA stream 复用同一 helper，但本次未逐项 fault-inject；
不以代码复用宣称其真实设备故障覆盖。
未证明真实磁盘耗尽、fsync、断电或科学输出回滚。
主 ARCH 尚未为此修复重编；实际 binary 构建身份需后续记录。

原始 H5/plt/checkpoint/ELF/log：
studio/.local/integration/driver-timing-flush-20261004-before 和 -after。
只提交处理后证据。Jeans/RZ 未确认科学决策和批准 O9 包仍待。
