# Core RZ v1 接入与私有 source-integral 候选节点
权威 Core ref 4639774fe3c94ae27b3e831d0f0b9d6340de34fd；
本地 cherry-pick 6af9849d，无冲突；未 merge main。
CoreRZDecisions-20261006 §§1–9、解析工具与 README 全读。
原四项RZ设计待确认部分现按该v1决定执行；工程/科学通过仍需分别交付。

## 本节点实际证据
Core Fraction oracle 的10项 unittest通过，仅解析fixture。
独立私有 source snapshot 基线326626cb；public源码/门槛/两个publicELF均保持。
inert patch：validation/gravity/candidates/rz-source-integrals-20261006.patch。
原 IGravityPolicy 增加 Unknown/ExternalNativeOrthonormal/FiniteRingCurrentState 和 chart；
默认Unknown，External保留Existing chart，不因一个enum开放RZ。
在原共享 GravitySource owner 增加已积分阶段源的保守更新 leaf：
radial/axial rate除V，angular rate除W，energy rate除V。
不新增重力kernel/数学定义；只消费阶段owner提供的真实积分。

实际 C++ 编译/运行用Core r=[1,3],dz=2,rho=2,m_phi(r)=2r,g_phi=-1/40,dt=1e-4：
full2pi V=8*2pi,W=(52/3)*2pi；delta_m_phi=-1/200000，
delta_E=-13/1200000。错误 Wmean*g*V 替代明确不等；
unknown默认与原非RZcell源行为保持。参考来自Core Fraction常数，不导入production作oracle。
8epsilon仅限这组标量运算舍入检查，不新增演化科学公差。

状态：source arithmetic 已通过；stage-integral producer/typed chart+epoch preflight、
真实Current/Next/Scratch/ledger拒绝保护、24组RK演化/续算均待实现/验证。
本patch未应用于正式Core或正式binary；没有把private叶函数通过写成外源finding关闭。
原HydroGeometryBinding RZ gate保留，finite-ring场租约门槛不替代。

## 下一批实施依据
- 外源：以真实阶段重构分别得到V线动量/能量及W力矩，不能用m_phi_W作V功。
  typed来源之外检查config/stage以及finite-ring density/topology/potential epoch。
- 粘性：nu为运动粘度，mu=rho*nu；tau_rphi=mu*(d_r u_phi-u_phi/r)，
  tau_zphi=mu*d_z u_phi，torque face/register与F_E=-u·tau_n配对。
  完整证明旧phi连接项与新散度关系；径/轴分量不静默迁移。
- 轴邻格：Omega1/4、cubic1/4，原N序列与V加权全域及局部norm，
  固定S*=P*/L*，局部也要求原>=1.8；初始RHS通过不等于演化通过。
  后续实际演化输入/终点/时间误差单列签收。
- 连续力：真实leaf full-ring fixed源与消费者点值映射；contact/inside独立积分；
  uncertainty<=原fixture tolerance/10，未有可靠界的点UNVERIFIED。
  RZ-CONTINUOUS-FORCE-REFERENCE-01仍OPEN。

## 2D性能诊断条件
Core仅批准2D CPU演化+续算各一次、CPU总<=2400s，通过后CUDA对应两条；
原publicCUDARelease路径，与privateJENS ELF分离。
目前未运行性能诊断：WSL暴露14 CORE/28 CPU但未暴露core_type，尚不能声称
已确认14700K的8个不同实际物理核与P/E类型；须落实/记录准确affinity，
以及12GiB进程树RAM、10GiBowned GPU allocation、所有临时输出/日志
10GiB和预估下一次写入的guard。JENSwhole-device GPU观察不是owned GPU证明。
无法落实guard时不启动。3D、warm-up/正式重复计时、long仍未授权。

所有原始场值/输出/ELF/full日志留studio/.local；本节点只有processed summary、
必要fixture源码和inert候选差异。完整四项finding/public能力仍各自review。
