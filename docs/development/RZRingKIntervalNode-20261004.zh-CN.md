# 环体近场基础：K 的 AGM 可靠区间

依据 Core 86bec324 科学清单第7.2–7.4节；接续 6c10ff3f。
共享数学继续位于 GravityBoundary 原有 FiniteRingBoundaryMath，不新增 solver、
常数、softening、用户精度配置或外部库。生产 near/contact 及 RZ 能力仍保留门槛。

## 实际算法与证明范围

新增 ring_elliptic_k_interval，输入是精确 stored FP64 complementary root q，
对应 K(m)，q=sqrt(1-m)。NIST DLMF 19.8.1/19.8.5 采用模数 k：
https://dlmf.nist.gov/19.8
这里 K(m) 的 m=k²，明确转换，不把 q 当作 m。

数学 AGM 的精确序列满足 b_n <= M(1,q) <= a_n，
K=pi/(2*M)。分别以向外舍入区间包住 a_n 和 b_n；
用 pi_lower/(2*a_upper) 与 pi_upper/(2*b_lower) 夹住 K。
共享 std::numbers::pi_v<double> 前后相邻数包住数学 pi。
sqrt(a*b) 改为区间 sqrt(a)*sqrt(b)，避免正 subnormal 的乘积先下溢为零。
这是数学恒等变形及 FP64 包络，不改变物理源。

当前 project 的 host/device flags 明确 no-fast-math、no implicit contraction；
CUDA 声明 prec-div / prec-sqrt / no-ftz，但本节点只验证 CPU。
该区间要求 IEEE binary64 与 gradual underflow、基本运算/sqrt 的正确舍入约束；
不是任意编译器/设备环境的无条件认证。

最多32次迭代，明确实际 iterations。
64 epsilon 的区间宽度条件是内部叶算法工作目标，不是科学 residual、
空间误差或物理阈值。无法满足时返回 PrecisionLimit/WorkLimit，
不静默升级 convergence。q=0 返回 SingularSample；非有限或范围外输入拒绝。

该函数只包住“精确 stored q”的 K。
距离 d/s、Duffy 坐标/权重、轴解析式、source 求积与求和的误差不因此消失。
原 finite_ring_potential_estimate 未改，error_is_certified 仍 false；
禁止把本 K 区间直接当成整个 ring potential 的 certified bound。

## 验证证据

C++专项覆盖 q=1、0.5、0.01、1e-12、1e-100、1e-300、最小 subnormal；
零及 invalid 拒绝。数学 probe 仅供本 scoped test，不新增 ARCH 用户 CLI。

独立 Python Decimal 工具不导入生产 kernel：
用 exact binary64 输入、Machin atan 级数独立计算 pi，
160/240 位各自计算 AGM 参考并核对一致性，再验证 FP64 区间包含参考。
包含近1、正常/次正规边界、跨指数域及固定随机种子共85个输入。
全部 PASS，最大相对区间宽度1.321422608996641e-14，最大13次迭代。
高精度参考是复核证据；可靠区间的依据是上述数学不等式及算术包络，
不是按观测误差放大得到经验界。

现有 ring moments/kernel/contact/双身份 guard、boundary acceptance PASS。
composite_poisson_analytic / composite_poisson_contract / self_gravity_lifecycle 3/3 PASS。
architecture audit、git diff --check PASS。
受控现有 CPU Release 增量 build parallel28、无 swap 增长。
ARCH SHA256 仍：
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
生产方法尚未调用新增区间，未重复同 ELF 的已通过 JENS9+9。

复现：
cmake --build build-cpu --target arch_composite_poisson --parallel 28
build-cpu/arch_composite_poisson ring-k-interval
python3 validation/gravity/rz_ring_k_interval_reference.py --probe build-cpu/arch_composite_poisson --output /tmp/rz-ring-k-reference.json

处理后指标与 source identity：
validation/gravity/results/rz-ring-k-interval-20261004/summary.json。
raw/full logs 仅在 studio/.local/integration/rz-ring-k-interval-20261004。
不上传 H5/plt/checkpoint、ELF 或全量日志；不做 Windows。

## 未关闭出口

下一步需要把距离/补参数与 Duffy 子域上的 kernel 范围包络贯通，
处理对数奇点子域的可靠积分界和工作预算失败，再接同一树/epoch 与 canonical B。
只有完整误差 ledger、原始 RHS residual 判据和独立 Phi/face force gate 满足后
才能解除 RZ production gate。本次并不关闭 near-contact 科学签收、
RZ-AXIS-01 或 RZ-VISC-01，也不启动尚未获准的新 RZ 长跑。
