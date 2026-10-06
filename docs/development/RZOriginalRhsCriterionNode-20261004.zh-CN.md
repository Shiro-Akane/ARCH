# 原始 RHS 容差：保守 residual 判据节点

依据 Core 86bec324 第 7 节，接续 dd8c9489 的 canonical B 面势误差传播。
本节点只实现同一 CompositePoisson owner 的条件式误差验收，不改变 solver
的物理定义、停止容差、输入或冻结验收阈值，不接入生产 RZ 边界。

## 判据与单位

norm_interval 对 stored vector 与原 operator stored native weights 的数学范数
给出向外舍入区间，按 scale 缩放避免不必要的平方溢出。
正值下溢的向外舍入界不作为科学 tolerance floor；精确零仍为零。

assess_boundary_residual 强制调用方提供：
- canonical B 传播得到的 face error ledger；
- RHS assembly error 上界；
- residual evaluation error 上界；
- 明确 CertifiedAbsolute quality；
- 原始 rtol / atol。

后两项误差预算不提供默认为零的参数。
E_b = face RHS error upper + assembly error upper。
R_upper = stored computed residual norm upper + evaluation error upper。
b_lower = max(0, approximate RHS norm lower - E_b)。
T_safe = max(atol, downward(rtol * b_lower))。
只有 upward(R_upper + E_b) <= T_safe 才 Accepted。
mapped face error、assembly error 与 residual 均为 RHS 单位 s^-2；
不是将 cm²/s² 的 potential error 直接和 residual 比较。

负值、非有限值、extent 不符拒绝；未认证输入拒绝；溢出明确失败。
近场阶数差仍是 Estimate，不能因数值小而自动升级认证。
近似 RHS 抵消为零而 E_b > 0、atol = 0 时必须失败。

## 本次证据

真实 28-cell mixed RZ CompositePoisson：
16 个有符号 face perturbation patterns，接受的结果满足原始扰动 RHS 的请求。
同时检查 norm interval、精确零、抵消反例、未认证 arithmetic、
过大 residual evaluation error、负 rtol 和正 subnormal。
这些离散回归不是独立的物理坐标、近场求积或完整 FP64 证明。

boundary-acceptance、此前四组 Cartesian/RZ uniform/mixed boundary-ledger 均 PASS。
composite_poisson_analytic / composite_poisson_contract / self_gravity_lifecycle 3/3 PASS。
architecture audit 与 git diff --check PASS。
现有 CPU Release tree 受控增量编译 parallel 28；memory guard PASS、无 swap 增长。
没有 configure、复制 build tree、Windows 适配或科学长跑。

生产 ARCH SHA-256：
1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
与已有冻结 JENS 9+9 所用 ELF 相同，未重复该短包，未改写 Studio managed Manifest。
新增方法尚未被生产调用，不宣称生产 RZ 已获得完整可靠边界。

复现：
cmake --build build-cpu --target ARCH arch_composite_poisson arch_self_gravity --parallel 28
build-cpu/arch_composite_poisson boundary-acceptance
build-cpu/arch_composite_poisson boundary-ledger
ctest --test-dir build-cpu --output-on-failure -R 'composite_poisson_analytic|composite_poisson_contract|self_gravity_lifecycle'
python3 tools/audit_architecture.py

处理后身份/指标见 validation/gravity/results/rz-original-rhs-criterion-20261004/summary.json。
完整日志只留 studio/.local/integration/rz-original-rhs-criterion-20261004。
不提交原始 H5/plt/checkpoint、ELF 或全量日志。

## 保留门槛及下一步

这是一条 conditional sufficient criterion：可靠输入界来自调用方，
不是由 quality enum 或一次回归生成证明。
geometry/weights 构造、near/contact quadrature、moments/reduction、
source-AMR epoch、tree budget descent 以及 RHS/残差实际求值的误差界仍须贯通。
下一数学节点应先提供可靠 near/contact bound 或明确预算失败，
再接真实 owner 的装配与 residual ledger，最后独立验证 Phi / face force。
目前不得用 estimated_error 接通 production RZ。

RZ-AXIS-01 与 RZ-VISC-01 保留，等待 Core 明确各自科学约定；
其他已授权合法依赖继续推进。完整 CPU 科学门槛通过后才统一 CUDA，
并启动对应冻结长跑与计时。本节点不代表整阶段科学签收。
