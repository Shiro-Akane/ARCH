# RZ ideal-B potential propagation and exact dyadic normalization node

**BOUNDARY DECOMPOSITION PASS FOR SCOPED MANUFACTURED INPUTS；完整physical RHS/Phi/force及production RZ gate仍待签收。**

基线621939e9a863533df6cf4daed6e8cc22f82b5e0c，唯一工作区/home/arch/projects/ARCH-compute-optim。科学清单§7.3的boundary误差组合依赖，不改变物理定义、production coefficients或验收阈值。

## scope及交叉项

新NativeRzFacePotentialError与原BoundaryPotentialError为不同输入类型，不支持将旧stored-coordinate ring errors隐式提升。默认scope Unknown；必须显式声明RootDyadicSourceAndObserver，且quality=CertifiedAbsolute。该声明本身不产生来源证明，真实producer仍必须提供理想source/observer误差与当前source/generation身份；未实现的producer不能靠标志取得scientific release。

CompositePoisson::native_rz_propagate_potential_error使用当前owner理想signed B区间的最大绝对系数传播e_f，再沿ideal native weights取RMS。由
B_ideal*f_exact - B_stored*f_hat
= (B_ideal-B_stored)*f_hat + B_ideal*(f_exact-f_hat)，
本传播与前节点construction误差及actual stored assembly组成完整boundary误差分解，避免旧stored-B*e_f遗漏的交叉项。返回类型单列native_norm_upper，未把stored-weight norm与ideal norm混用。

Unknown scope/Estimate拒绝为UncertifiedInput；缺失shape、负值、NaN拒绝InvalidInput，有限值乘积溢出返回Overflow。明确root-scoped certified zero保持exact zero，没有epsilon、abs修正或floor。非boundary faces不贡献B，无效producer不能因零误差自动取得root scope。

## 精确dyadic normalized basis

先前proof对相近root centers逐步向外乘/减，产生可避免的区间宽度。现在基于owner已验证level<=15、index<=INT_MAX，将二倍logical center/face坐标提升到共同dyadic level，在int64中精确相减，最后ldexp归一化。最大分子/差不超过48bits，binary64能精确保存；没有从世界坐标浮点相消倒推geometry。

root spacing共同比例或精确power-of-two比例按frexp/ldexp解析消去，并验证finite及逆缩放可恢复；其他比例仍逐项向外围住。该变化只收紧proof的basis/Gram enclosure，生产fit_interface的公式、实际coefficients、DenseLU pivot、fallback与threshold完全不变。

同一38-operator参考的最大coefficient interval width从4.283883470179717e-08降至4.727411351268529e-09 inverse-native-length；inverse defect upper最大从1.540823024726025e-11降至1.9185764088547346e-12。没有将改进比例作为验收阈值，也不保证任意真实用户请求可满足。

## actual canonical消费及独立验证

38个真实operator/3288 cells/7464面（1632boundary），包括现有10个真实derived coarse operator。实际effective_rhs使用source=0和此前manufactured signed face values；本节点另声明理想root-source/observer坐标下的manufactured face uncertainty box。它是数学输入，不是实际ring势生产者的科学证据。

独立Fraction以理想root geometry、exact Gram solve和exact signed B计算每个cell的全部face-value interval极值；检查actual FP64 RHS到两端最坏偏差不超过construction+ideal-potential-propagation+assembly总界。纯potential误差的abs(B)*e逐项检查，native weight中的pi精确消去，返回的传播及总native RMS上界平方比较通过。无生产LU/interval helper导入，raw vectors只留本机。

38组scope/Estimate/invalid/overflow/zero检查全通过。前节点stencil enclosure（38752 terms）与face/map（1632boundary）独立回归仍PASS。本组仍无自然recovery触发，不宣称其独立科学覆盖。

## Build与binary身份

复用原CPU Release tree、parallel28增量构建；guard未停止、swap growth0、11.144s、peak owned RSS2635632 KiB。无configure/new build tree。

生产ARCH SHA256仍为7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，匹配已有冻结JENS9短+9实际restart PASS receipt；无相关binary变化不重复短包，receipt SHA及source/probe identities见summary。

最终6项Core CTest（physical_constants、gravity_stage_contract、composite_poisson_analytic/contract、self_gravity_lifecycle/physics）PASS，11.49s；architecture audit、diff check PASS。未运行simulation/新长跑或CUDA，未merge main，未修改root STATUS，未开展Windows适配。

## 下一依赖和review

仍需真实ideal source/observer potential error producer，绑定当前source/AMR/generation身份；然后组合interior A与实际residual，取得原请求下完整physical RHS和独立Phi/force、RZ全消费者科学gate。axis/viscosity及自然recovery finding保持。对应CPU通过后才统一CUDA、获批计时及长跑。

复现：

    python3 validation/gravity/rz_ideal_potential_propagation_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local output>

标量summary：validation/gravity/results/rz-ideal-potential-propagation-20261005/summary.json。
raw geometry/coefficients/RHS/error arrays、完整日志与ELF留在本机studio/.local/integration/rz-ideal-potential-propagation-20261005。H5/plt/checkpoint不上传。
