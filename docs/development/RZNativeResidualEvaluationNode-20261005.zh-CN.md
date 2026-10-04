# RZ native A and actual residual evaluation node

**NATIVE A / EVALUATION / MANUFACTURED RESIDUAL BOX PASS；不是物理Poisson解或完整RZ签收。**

基线cb694c43a51da42aa9a54de6582090fd8d8e9535，唯一工作区/home/arch/projects/ARCH-compute-optim。科学清单§7.3 interior A和actual residual构造依赖；物理、production coefficients、求解策略和验收阈值保持。

## canonical A的真实约定

实际CompositePoisson::apply以boundary_value=0调用共享anchored gradient：
sum_k c_k*(phi_sample-phi_anchor) + c_B*(0-phi_anchor)，
再left减/right加 area/volume*gradient。因此homogeneous boundary项属于A，prescribed face values另经B进入RHS。不能以unanchored sum(c*phi)忽略存储常数约束缺陷，也不能漏掉-c_B*phi_anchor或在A/RHS重复计算prescribed data。

native_rz_operator_construction_error围住(A_ideal-A_stored)*phi。逐face、逐side消费同owner的ideal-root final stencil和native area/volume区间，计算coupling construction defect，再乘实际输入field的anchored difference上界。boundary homogeneous term使用B factor defect*abs(phi_anchor)。逐cell向外相加，并以ideal native weights取RMS。没有修改实际apply或生产解，只返回transient proof。

## actual evaluation与RHS分离

native_rz_residual_evaluation_error将A construction与现有stored-coefficient residual-evaluation arithmetic逐cell向外求和，围住actual computed residual相对A_ideal*phi-computed_rhs的偏差。实际apply/subtraction是否舍入、fake residual是否准确都不假设；返回construction/arithmetic单项及总native upper。

b_ideal与computed_rhs的差属于独立RHS ledger；此前construction+ideal-B potential propagation+actual RHS assembly已给出这个差。在本节点manufactured residual box中再加该RHS error，得到computed residual到A_ideal*phi-b_ideal的完整区间。actual residual evaluation使用已经给定的computed_rhs，所以RHS assembly没有再重复计入。

该分解只保证已声明理想root inputs上的离散数学表达式；不认证输入phi是连续解、不认证实际ring source/observer producer，不能提升production status或改变原rtol/atol。

## 实际CPU和独立Fraction检查

38个actual operator、3288 cells、7464 faces，沿前节点实际hierarchy包括10个derived coarse operator。phi取明确的有限制造array，实际op.apply再减actual effective_rhs产生computed residual。制造RHS使用source=0和独立root-scoped face uncertainty box；不作为真实引力source、时间演化或物理解。

独立Fraction按理想root geometry、exact Gram/final coefficients重建同一anchored A；另按所有actual stored FP64 coefficients/area/volume重建exact stored A。逐cell检查：
- abs(A_ideal*phi-A_stored*phi) <= construction；
- computed residual到exact stored A*phi-computed_rhs <= arithmetic；
- computed residual到ideal A*phi-computed_rhs <= combined evaluation；
- computed residual到ideal A*phi-b_ideal各face interval极值 <= full manufactured error。

ideal weights中的pi精确消去，各实际误差与返回cell bound的weighted norm²不超过returned upper²。所有38组PASS。未导入生产LU/interval helpers。fake finite zero residual留下正evaluation error；missing/NaN输入拒绝，exact-zero phi/rhs/residual误差精确零，无floor。

## Build及回归

复用原CPU Release tree、parallel28增量构建，guard未停止，swap growth0，11.169s，peak2739228 KiB。最终probe包含真实apply、computed residual及arithmetic单项输出；上述38组验证使用该最终probe，其准确SHA在summary中。

生产ARCH SHA256仍为7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，沿用相同ELF冻结JENS9短+9实际restart PASS receipt，不重复无相关变化的短包。source/probe/receipt SHA链见summary。

最后6项Core CTest（physical_constants、gravity_stage_contract、composite_poisson_analytic/contract、self_gravity_lifecycle/physics）全PASS，12.31s；boundary decomposition的38组回归、architecture audit及diff check PASS。

未configure/new build tree，未simulation/新长跑或CUDA，未merge main，未修改root STATUS，未Windows适配。

## 仍需交付

真实ideal source/observer potential error producer需绑定当前source/AMR/generation identity；再执行完整physical source/RHS原请求判据和独立Phi/force、RZ全消费者科学gate。自然recovery、axis/viscosity finding仍保留。对应CPU gate后才统一CUDA和获批计时/长跑。

复现：

    python3 validation/gravity/rz_native_residual_reference.py --probe build-cpu/arch_composite_poisson --output-root <new local output>

处理后summary：validation/gravity/results/rz-native-residual-evaluation-20261005/summary.json。
raw phi/apply/residual/coefficient/error arrays、全量日志及ELF留本机studio/.local/integration/rz-native-residual-evaluation-20261005；H5/plt/checkpoint不上传。production RZ gate保持。
