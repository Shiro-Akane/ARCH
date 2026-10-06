# RZ face geometry and canonical boundary-map construction node

**FACE/B CONSTRUCTION PASS；完整physical RHS/Phi/force及production RZ gate仍待签收。**

基线e02d8342e91b5ea6ba868b506c194731eb35d4af；唯一工作区/home/arch/projects/ARCH-compute-optim。属于科学清单§7.3的native geometry/coefficients/RHS组合依赖；不改变物理或验收阈值。

## 真实面所有者与原生几何

CompositePoisson::native_rz_face_enclosure(face_index)消费当前owner的真实final face及left/right leaf identity，返回root-dyadic理想face center、面积、area/volume和signed boundary-map B的区间与stored construction error。只接受有效当前RZ 2D index，非RZ/missing index拒绝。

radial face A=2*pi*r_face*dz_fragment，axial face A=pi*(r_hi²-r_lo²)；体积V=pi*(r_hi²-r_lo²)*dz。root integer leaf identity定义理想fragment中心/宽度，细侧切向fragment保持实际face布局。径向平方差使用dr*(2*r_lo+dr)，避免薄环相消。面积数学pi使用向外区间；area/volume共同pi解析消去，再围住原生几何比值。

boundary map按left正/right负的实际effective_rhs方向，组合前节点已认证的final boundary coefficient；实际stored A*c/V的整个数学表达式另向外围住并比较。nonboundary map与absent side保持精确零；不把几何系数单位当成势单位：A/V为inverse-native-length，B为inverse-native-length²，乘势后才是RHS单位。

## 当前canonical RHS消费

native_rz_boundary_construction_error(face_values)给出abs((B_ideal-B_stored)*f_hat)的逐cell保守界，并沿ideal native weights取RMS。source、真实f_hat积分或source/observer误差、interior A、RHS assembly roundoff不混入这项。类型显式native_norm_upper，与原stored-weight ledger分离。

真实probe用当前op.effective_rhs(source=0, manufactured signed face_values)生成actual FP64 RHS；通过现有bound_rhs_assembly_roundoff单列实际stored-coefficient运算偏差，再与上述construction逐cell向外求和。没有simulation、物理初始条件/求解/力或正式输出。输入中的面值是数学manufactured数据，不是已认证的真实ring potential。

缺失/NaN face values拒绝，精确零输入的construction cells/norm保持零；没有epsilon或floor。source=0只限定此B消费fixture，不作为任意真实source或Poisson solve的签收。

## 独立检查

38个实际operator、3288 cells、7464面（1632 boundary faces）；包含10个真实hierarchy-derived coarse operator。独立Fraction从root/leaf坐标计算理想centers、area_without_pi、volume_without_pi和原生A/V，并复用独立exact Gram reference的理想final coefficient。

逐center interval/error、逐face area（100/140位pi）、exact A/V及signed B interval/error全部通过。exact stored-B与ideal-B各自累加manufactured face values，验证construction误差；actual FP64 RHS与ideal RHS再验证construction+assembly bound。物理weights中pi精确消去，Fraction weighted norm²与returned native upper²对照通过。

此前stencil enclosure的38 operator/7464 face/38752 coefficient terms回归仍PASS。未将observed difference作为新阈值或除以最小量掩盖误差；原理想stencil区间宽度/保守性不会自动授权降低原residual要求。

## 组合时必须保留的下一项

对于真实potential误差，完整差为：
B_ideal*f_exact - B_stored*f_hat
= (B_ideal-B_stored)*f_hat + B_ideal*(f_exact-f_hat)。

因此不能只把本construction加到旧stored-B*e_f传播上而遗漏交叉项。需以ideal B传播e_f，或显式增加abs(B_ideal-B_stored)*e_f。同时还需真实source/observer坐标差异、interior A构造和实际residual评价；完整physical status仍unknown，RZ gate保持。自然recovery、本来开放的axis/viscosity/全旋转消费者及连续Phi/force也未关闭。

## Build、binary与回归身份

原CPU Release tree增量编译parallel28；face/ledger两次guard均未停止、swap growth0，分别10.152s/peak2582156 KiB、11.157s/peak2572856 KiB。最终零和处理仅重新编译测试fixture。未主动configure、新建/复制build tree。

生产ARCH SHA256仍为7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。
沿用相同ELF已通过的冻结JENS9短+9真实restart receipt，不重复无相关变化的短包；准确receipt SHA和输入身份链见summary。

最终6项Core CTest（physical_constants、gravity_stage_contract、composite_poisson_analytic/contract、self_gravity_lifecycle/physics）全PASS，10.19s；architecture audit、diff check PASS。未CUDA、未未获批长跑、未Windows适配、未merge main，root STATUS不变。

## Review与本地证据

复现：

    python3 validation/gravity/rz_face_map_enclosure_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local output>

处理后摘要：validation/gravity/results/rz-face-map-enclosure-20261005/summary.json。
raw face/coefficients/RHS/geometry arrays、完整日志和ELF仅留本机studio/.local/integration/rz-face-map-enclosure-20261005；H5/plt/checkpoint不上传。对应CPU科学gate通过后才统一CUDA及获批计时/长跑。
