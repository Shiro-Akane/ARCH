# 有限环体：canonical B 的面势误差传播节点

依据Core86bec324第7节，接续7cc33630。
本节点只完成已知可靠potential扰动界到canonical离散RHS的传播。
near/contact quadrature及完整residual ledger仍未认证，RZ production gate不变。

## 同一operator owner与单位

在原CompositePoisson增加propagate_boundary_error，不增加第二Poisson solver、
GravityBoundary owner、gravity constant、用户精度参数或browser command。
input覆盖原faces数组；每项absolute_error及明确quality：
CertifiedAbsolute / Estimate / Unknown。
有效physical face只允许CertifiedAbsolute；真实ring estimated_error
因error_is_certified=false被拒绝，不能用阶数差替代可靠bound。
状态为Bounded/InvalidInput/UncertifiedInput/Overflow，不带成功fallback。

使用原effective_rhs的同一stored area、boundary_coefficient、native volume：
e_b[i] = sum_faces abs(A_f*c_f/V_i)*e_phi[f]，
source端left/right符号分别相反，absolute传播不使用抵消。
phi/error_phi单位cm²/s²，mapped error单位s^-2；
禁止直接把potential error与Poisson residual比较。
norm上界消费operator同一stored native weights。

逐个正乘/除/加、sqrt用nextafter向上包住FP64误差；
缩放norm以避免不必要square overflow。
明确exact-zero运算保持0；
正值underflow的最小subnormal仅为向外舍入界，不是科学tolerance floor。
physical axis zero-area及非boundary coefficient0不参与。
overflow明确失败，不用inf当有效预算。

这个conditional bound针对stored离散coefficients的数学映射与stored权norm：
输入face界的数学证明不由本函数创造；
mesh/area/coefficient/normalized-weight构造相对物理离散式的误差，
source/RHS实际装配舍入、近场积分、moments/归约误差仍需独立ledger。
未将这些未认证量写进certified列。

## 检查与反例

Cartesian/RZ × uniform/mixed真实CompositePoisson，共4 fixtures；
每组16个有符号face perturbation patterns。
actual canonical effective_rhs扰动逐cell被正上界包含，
同一weighted RHS norm也被上界包含。
max bound ratio约1（输出精度），没有人为放大科学阈值。
physical zero budget保持精确0。

实际finite_ring_potential_estimate返回未认证误差；
转换成Estimate后ledger拒绝，不能silent upgrade quality。
负界拒绝、overflow失败；不是以estimate很小便接受。
RZ fixture保留CurvilinearIsolated chart/boundary验证，轴仍按既有owner处理。

相关composite_poisson_analytic/contract/self_gravity_lifecycle 3/3、
ring math/contact/dual-guard、architecture、diff check全部PASS。
初次fixture有mesh enum、boundary kind及常数namespace接线错误；
实际编译/runtime contract拒绝后修正，未绕过原验证。
完整日志本机保留，未改物理/阈值/输入以伪造科学通过。

## Build、身份、复现

唯一现有CPU Release tree standard incremental build parallel28，memory guard PASS，
min available21234272KiB、peak owned RSS2151228KiB、无swap增长。
没有configure、新worktree或build tree复制。
build source=7cc33630+本补丁，dirty=true；
ARCH ELF仍1917385417c8f366dc9834a9946744f55556405b6a4b5f48578efd14db803020。
新method尚未被生产caller使用，production行为/ELF不变；
不重复同binary冻结JENS9+9，也不重写Studio managed Manifest。

cmake --build build-cpu --target arch_composite_poisson --parallel 28
build-cpu/arch_composite_poisson boundary-ledger

摘要/source fingerprints：
validation/gravity/results/rz-boundary-rhs-ledger-20261004/summary.json。
raw：studio/.local/integration/rz-boundary-rhs-ledger-20261004，仅本机。
不上传H5/plt/checkpoint、ELF或完整日志。

## 未完成链

第7节仍需near/contact可靠求积界、全FP64 geometry/support/moment/reduction
ledger、tree budget descent/source-AMR epoch、RHS装配误差、
Tsafe与residual+Eb判据最终贯通，及独立Phi/face-force科学验证。
本节点不宣称end-to-end certified residual，也不解除values RZ gate。

RZ-AXIS-01与RZ-VISC-01独立保留，其他合法依赖继续推进。
CPU完整科学门槛后统一CUDA和冻结对应长跑；不做Windows。
指定review refs已fetch核对：codex/o8-boundaries仍11a321d56，
当前integration仅本方节点，无新的Core应力/轴区约定。
