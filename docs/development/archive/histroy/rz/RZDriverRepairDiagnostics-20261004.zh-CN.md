# 实际 Driver repair 诊断：RZ 语义与失败传播

## 问题与修改

基线3c81d32a的DriverIO::write_measurements把所有状态的三个repair动量积分
标为momentum_x/y/z，不能表达已冻结RZ局部正交r/z/phi分量。
文本报告只检查open，没有检查缓冲写入/关闭；实际/dev/full反例明确失败：
旧路径仍打印成功，异常未向调用方传播。

当前仅修改诊断序列化，不改变ledger值/浮点数组、EOS、推进、repair数学或checkpoint格式。
RZ依据不可变Runtime profile记录独立几何revision/chart、local-orthonormal-r-z-phi，
measure cm³/full_rotation、mass g、momentum g*cm/s、energy erg，
使用momentum_r/z/phi。phi动量积分不是r加权角动量，未声称解决AMR角动量finding。
默认Existing报告内容保持字节一致；没有把既有极平面改称RZ。

报告写完显式检查flush与close；失败抛出，之后才打印State摘要。
这不提供报告原子发布/fsync/崩溃持久性，也不改checkpoint发布机制。

## 真实验证

复用tests/host/io/test_driver_checkpoint_geometry.cpp及
validation/io/run_driver_checkpoint_geometry.py。
按既有CPU compile/link命令重编真实Driver/Runtime/IO；
不是mock writer或仅header编译。首次fixture换行转义错误保留本机，
修正后旧生产代码实际报repair report write failure swallowed。

补丁后Cartesian/RZ均通过：
- 使用1.25/-2.5/3.75不同ledger槽值验证分量标签，非全零烟雾测试。
- /dev/full真实缓冲写入失败向调用方传播，移除本地fixture symlink后同ledger恢复。
- 恢复报告原字节一致，原checkpoint SHA不变。
- 既有checkpoint raw state/controller/profile和失败序号恢复检查继续通过。
- 独立读回确认RZ全部测度/单位声明；Cartesian报告与baseline文本字节一致。

精确fixture ELF/source SHA见同名Summary.json；相关diff check通过。
time=0/step=0，无simulation evolution；原始H5、日志和ELF在ignored studio/.local。
主build-cpu/bin/ARCH尚未为此修改重编，不能把新fixture身份冒充主binary。
不重复没有相关改动的Studio或API全baseline。

## 阶段状态

关闭一个O7.5真实诊断接线与失败传播缺口。
公开RZ能力、regrid角动量、有限环体gravity、完整演化与CUDA验收仍未完成；
没有改科学定义、独立参考或误差阈值，没有push/tag。
