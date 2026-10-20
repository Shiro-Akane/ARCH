# 3D Jeans mixed-AMR coarse mesh：直接构造确认

## 实际证据

使用现有arch_composite_poisson新增coarse-diagnostic手动入口，
传入原失败campaign的真实root维数64/16/16和spacing1/64。
直接构造production CompositeMultigrid，用uniform root leaves进入原coarsening路径；
不执行物理simulation，也不声称此fixture覆盖完整mixed leaf/演化。

root validate_mesh通过，production constructor实际拒绝：
Composite coarse mesh cells=(4,4,4), spacing=(0.250000,0.062500,0.062500):
Poisson prototype spacing ratio=4.000000 exceeds limit=2

这将上一轮仅源码推导提升为真实构造证据：是coarse spacing ratio问题，
不是非有限diagonal。完整Jeans native-mixed-3d仍FAIL，科学gate不清除。

## 最小工程修改

CartesianPoisson.cpp只拆分现有guard的错误消息，finite diagonal和spacing ratio分开；
所有条件/数值上限原样保持。CompositeMultigrid只在invalid_argument时附上
失败coarse cells/spacing上下文，重新抛出；未更改operator/transfer/LU/粗化/预算。

test_composite_poisson.cpp复用原make_cells/真实constructor，
精确assert失败cells/spacing与guard文本。此模式不注册新CTest，不扩大CI inventory，
是目前已知不支持hierarchy的诊断复现，不是给失败科学路径置绿。
如果Core批准修正hierarchy策略，此诊断预期须同步按新contract更新。

## 检查

内存保护下定向arch_composite_poisson构建通过，两次均无swap growth/guard stop。
coarse-diagnostic和原contract入口通过；后者含原radial manufactured convergence，
数值参考/误差门槛原样保留。
首次fixture日志因newline multicharacter literal产生23662，格式已修正；
只重建测试并重测诊断入口，原失败/成功日志均本机保留，不重复无变化contract。
最终fixture/source hashes在同名Summary.json。

主ARCH executable未重编，不能称其已带新的diagnostic文本；
主ELF仍7d0360de…，先前完整campaign失败日志保持旧消息。
不改变CPU/science/CUDA/O9未通过状态；仍需Core确认内部coarse-mesh允许范围和算法策略。
原始fixture ELF/build日志留studio/.local/integration/poisson-coarse-diagnostic-20261004。
无新simulation输出、push/tag或Windows工作。
