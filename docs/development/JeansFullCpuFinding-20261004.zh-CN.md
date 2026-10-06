# 完整 JeansWave CPU campaign：3D mixed AMR coarse-mesh finding

## 当前结果

原 Campaign.full 实际 FAIL，在 native-mixed-3d 的首次 gravity求解停止。
此前24条验收记录完成；后面的原Restart独立执行7条PASS，未把它并入整轮绿色结果。
生产CPU build source clean b82205e2，ELF SHA
7d0360de4ac9a3429d2a8ffec3908ca4424f2c7716096f2c44ad189662a9d8c4。
无需重build；本轮代码仅显式补原多维x2_min/x3_min=0。
这些原GridConfig默认已核对8fc0dd25；4份原多维inputs inspect由缺项exit3恢复exit0。

## 已按原冻结预算完成

空间32/64/128密度相对RMS .0065685/.00159579/.00046516；
64-cell原<=.02通过。Euler/RK2/RK3最低时间阶1.01187/2.001995/3.002163，
分别通过原.9/1.8/2.7；参考细化变化1.03273e-5，原reference-resolution检查通过。
两份dynamic-AMR到原t=.4，refine/coarsen/no-change与mixed-level覆盖通过；
maximum net force8.95608e-8/6.78972e-9，原<=2e-3且改善通过。
原mass/energy预算、density lease、Poisson residual及零floor检查均按runner执行。
uniform2D/3D和mixed2D通过；mixed3D明确失败。

原独立Restart到原t=.1，checkpoint原dataset比较通过；
额外核对本例实际14个dataset均为numeric，14份raw bytes一致；没有按其他用例的dataset数量套用。
retired G配置拒绝与3个checkpoint gravity controls不一致拒绝均通过。
Restart独立结果不能清除full FAIL。

## 实现证据与最小重现

已有native-mixed-3d原配置：nblockx1=4,x2/x3=1；
domain1/.25/.25，root64/16/16，spacing均1/64，max_blocks32，
lrefinemax1、phaseπ/4、threshold2e-5/5e-6、tmax.02/maxsteps2。
实际启动经过initial AMR，首次求解报：
Poisson prototype requires valid spacing and finite diagonal。
完整run.log/input及已生成初始H5留在本机；不重设更小场景冒充通过。

源码路径：
- CompositeMultigrid.cpp constructor：移除leaf refinement后，当size>64，
  只粗化cells>4的轴，直到实际bounded DenseLU粗层。
- CompositePoisson constructor调用validate_mesh。
- CartesianPoisson.cpp validate_mesh：Cartesian spacing ratio必须<=2。

按实际root输入和该源码推导（不是新增runtime trace）：
64/16/16→32/8/8→16/4/4→8/4/4→4/4/4；
对应spacing ratio1→1→1→2→4，末层size64但ratio4触发原guard。
finite coefficient初值正常；疑点是coarse aspect ratio，尚未用插桩宣布最终根因。
没有放宽ratio、不改coarse capacity/算法、domain、max_blocks或验收阈值。

## 待 Core review

请确认生产root spacing ratio<=2是否也必须适用于内部preconditioner coarse mesh；
若允许内部anisotropic coarse mesh，应提供/确认稳定性与独立测试预算；
否则需确认保留root约束时的coarsening/bottom solver策略。
此项关系数值方法，不为取得通过自行删除guard或放宽科学约束。

完整CPU gate仍失败/待审，历史radial G=1e-20迁移另待Core。
原full CTest69/71失败JUnit保留，不合成全部通过。
原始H5/checkpoint/日志/编排脚本在studio/.local/integration/jeans-full-cpu-20261004，
只提交生成器迁移、处理后summary及本finding；无CUDA/Windows/push/tag。
