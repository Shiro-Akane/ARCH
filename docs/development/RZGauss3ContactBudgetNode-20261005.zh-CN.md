# RZ 三点可靠积分与接触预算节点（2026-10-05）

## 结论

MATCHED SOURCE TARGET PASS；production RZ gate 保留。
三点 tensor Gauss 的六阶可靠余项与原 Darboux/两点 Gauss/contact log bounds
取交集后，原三个全源接触样本在相同 maximum_boxes=16384、
relative target=1e-10、absolute target=0 下由 WorkLimit 转为 Bounded。
这关闭的是这些数学样本的工作预算不足，不是完整 RZ self-gravity 或全域科学签收。

## 数学规则

仍使用共享 FiniteRingBoundaryMath owner。三点节点为0、±sqrt(3/5)，权重
8/9、5/9、5/9；irrational节点、rational权重、geometry、kernel和归约全部
outward enclosure，不依赖 libm cos 生成节点或 Gauss 阶次差。

同一 Newton/Legendre 导数界 |∂^m(1/D)|<=m!/d_min^(m+1)，m=6。
h=r*K/s 的两向六阶界分别为 (pi/2)*6!*(rh/d^7+1/d^6) 与
(pi/2)*6!*rh/d^7。长度L的三点Gauss余项系数L^7/2016000，
配合正权重tensor telescoping，得到：

    area*pi/5600 * ((dr/d)^6*(1+rh/d) + (dz/d)^6*rh/d)

这个系数也可由三点rule对x^6的误差8/175、除6!后映射区间核对。
source接触处不套用非接触导数界；仍使用此前解析log-distance integral与
K complementary-root可靠上下界。既有axis连续解析、完整环体、point potential、
shared CGS G、源和输出身份不变。

两点/三点/Darboux bounds取交集；任意不相交仍明确失败，无法可靠表达时
不使用该候选界。没有hidden floor、softening、新用户参数或科学阈值调整。
实际K调用/AGM迭代计数贯通原有source/native-face接口。

## 非接触工作量

同一4个源外样本、原1e-10目标：
- (2,0)：5761 -> 104子矩形
- (0.75,2)：2985 -> 58
- (0.01,2)：2377 -> 44
- (2,-0.7)：4007 -> 76

每个非接触矩形现在最多计算两点和三点规则及原range界，实际全部样本
28次K interval调用/矩形；总量仍显著下降，原始实际counts见summary。
这是确定性算法工作量对照，不是正式CPU/CUDA performance benchmark。
此前4096-cap特定失败已因改进而不再应强制失败；低cap=8、零目标和
contact小预算等拒绝路径保留。没有为保留旧失败而故意限制新算法。

## 接触严格目标与独立诊断

完整源r=[0.5,1]、z=[-0.375,0.375]、rho=1、shared CGS G，
edge/corner/inside observer原样保持。相同16384工作上限：

| observer | old status | new status | new boxes | potential absolute bound cm²/s² |
| --- | --- | --- | --- | --- |
| (1,0) | WorkLimit | Bounded | 8007 | 1.3572723598e-17 |
| (1,0.375) | WorkLimit | Bounded | 3733 | 1.1912327263e-17 |
| (0.75,0) | WorkLimit | Bounded | 15664 | 1.6471402692e-17 |

这3个样本严格沿relative target=1e-10，没有把旧观测误差改写为容差。

独立reference沿Core批准Duffy source triangle映射，不导入production K/区间/节点。
在ro>0接触点，t→0时r/s→1/2，解析消去leading -(t/2)*log(t)，
该项积分为1/8/triangle；剩余项用独立Decimal AGM与64/96阶、
80/100位积分诊断。所有诊断落在可靠区间内。ro=0不适用这一消去，
工具显式拒绝，不把轴线极限偷换成正半径接触路线。

阶次与精度差依旧只作参考稳定性诊断，不宣称它们本身是可靠reference
余项或所有场景的完整物理验收。原reference默认20个primitive、3个小接触
诊断仍PASS；非接触12/16阶、128/256角向诊断也PASS。
旧失败日志/producer身份仍留存，没有覆盖旧证据。

## 回归、构建与身份

baseline b2e082b1db20c2b9b17dedefaa453c79ae16a146；
构建时本节点source dirty，实际source与scoped test ELF hashes见summary。
复用唯一工作区和CPU Release build-cpu，仅标准增量ARCH、
arch_composite_poisson、arch_self_gravity、arch_gravity_stage_contract；
没有configure/newtree/新workspace。

ring-contact-gauss3、ring-separated-gauss、ring-contact-log、ring-enclosure、
ring-native-face、ring-far-leaf、ring-axis-enclosure、boundary-ledger通过。
4项CTest、architecture audit、diff check及上述独立参考通过。
memory guard未停止，swap增长0。

production ARCH ELF仍为d83186386a3dbcb403f473426da7dd42d1fc42840b09f7ddc43a7977fb36d95e，
内部gated path以新scoped ELF验证；匹配的JENS 9短演化+9真实重启结果沿用，
无相关变化不重复运行。

复现：

    build-cpu/arch_composite_poisson ring-contact-gauss3
    python3 validation/gravity/rz_ring_contact_log_reference.py --probe build-cpu/arch_composite_poisson --strict-contact --output <local-output.json>

## 下一依赖与留存

单源的这些严格预算成功不是general parent far/translation认证，也不是完整
source tree的实际AMR身份与原始RHS/residual请求证明。后续贯通完整
coefficient/assembly/evaluation ledger、source/tree/AMR generation与solve
consumer；角动量A→B→C→D、RZ-VISC-01/RZ-AXIS-01参考和预算仍独立保持。

raw H5、plt、checkpoint、完整日志本机留存：
studio/.local/integration/rz-gauss3-budget-20261005。
只提交处理后标量、独立脚本和报告。不开production RZ、CUDA/长跑或Windows
适配，不改tag、不合并main。
