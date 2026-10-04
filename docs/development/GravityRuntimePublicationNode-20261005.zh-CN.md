# Actual Runtime gravity publication node

基线23a4825baadf11faac10072809c137dd62a65d22；
唯一/home/arch/projects/ARCH-compute-optim。
统一计划第7项：补共享重力真实生产身份链证据；
不替代RZ连续科学gate，未宣称全阶段完成。

## 代码审计与实施边界

GravityStage::solve遍历runtime.handles，与active block对应；
逐块ledger.require_readable，读取请求slot的真实density、
version、storage lease，传给SelfGravity::prepare。
prepare逐块匹配bound mesh/view/layout/storage generation，
GatherDensity采用绑定的cell->block/offset收集全部活动叶；
每次solve先invalidate，失败不发布旧field。
Driver在宏步末invalidate，regrid后下次prepare按新epoch重建workspace。

旧gravity_stage_contract主要使用scheduler Probe，
self_gravity_lifecycle直接构造views；均不能单独证明真实GravityStage/Runtime接线。
新增test_gravity_runtime_contract.cpp直接创建实际Runtime、GravityStage、SelfGravity。
Capture仅委托既有Host执行器，并在其真实GatherDensity之后读取density和cell map，
没有替代求解器或手工构造请求views。
不修改scientific Core、生产能力、原物理定义或验收阈值。

## 实际执行证据

1. 初始化4个Cartesian周期root blocks，真实Current准备收集64活动cells；
   每个block均出现，density与实际slot/offset逐项完全相等。
2. 只修改最后一个block density及ledger interior version；
   first block version保持不变，实际新solve重新收集全部block。
3. 未发布Scratch，以及仅非首块Scratch未发布，两种实际请求均在gather前失败；
   旧gravity.potential也拒绝读取。
4. 真实发布Scratch/Next interior，按RK3已有descriptor分别调用实际prepare，
   收集对应slot的不同density；不是实际Hydro stage advance。
5. 显式invalidate（与Driver宏步末顺序相同），恢复公共Current版本、
   实际halo refresh、runtime.perform_regrid。
   4块变8块，epoch1变2；下次GravityStage准备实际rebind/gather128活动cells。

最终5次成功gather，诊断generation=1/2/5/6/7（两次失败lease仍退役），
stage=0/0/2/3/0，epoch=1/1/1/1/2，原Poisson residual<=target全部通过。
stage请求time=0/0.125/0.25/0.125/0.5是fixture输入身份，
Controller未推进，仍time=0/steps=0；不得把请求time当作演化终点。
仅生成本机gravity_solves.tsv，无H5/plt/checkpoint。

## 构建与检查

runner复用既有run_rz_runtime_boundary.py的可信compile/link方法：
读取当前CPU build compile_commands和ARCH link command，
重编fixture、GravityStage、DriverRuntime/DriverBoundary/DriverRegrid五个TU，
链接既有CPU archive；不configure、不改正式ARCH、不复制build tree。
准确fixture/重编source/ELF SHA在summary。
这是实际测试fixture ELF，不冒充新的production Build Manifest。

memory guard最终26.090s，peak owned RSS1249168KiB，
swap0，无guard stop；这不是冻结性能benchmark。
最终真实fixture PASS；
现有gravity_stage_contract/self_gravity_lifecycle两项PASS（0.27s）；
architecture/diff check PASS。
production ARCH SHA仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，
复用已有匹配冻结JENS9+9，不重复无变化ELF的包。

处理后summary：
validation/gravity/results/gravity-runtime-publication-20261005/summary.json。
raw编译/执行/TSV/ELF留：
studio/.local/integration/gravity-runtime-publication-20261005。

复现：
    python3 validation/gravity/run_gravity_runtime_contract.py --build build-cpu --output-root <new local folder>
    ctest --test-dir build-cpu -R '^(gravity_stage_contract|self_gravity_lifecycle)$' --output-on-failure

## 未关闭范围

这是当前支持的Cartesian共享身份路径；RZ Runtime regrid与production self-gravity
仍有能力门槛。不能把Cartesian结果换标签成RZ ring publication PASS。
RZ批准路径开放后仍需真实逐块density/stage/regrid环体producer消费与失效。
coarsen/restart/migration到device、实际Hydro stage演化、continuous Phi/force、
axis/viscosity及完整角动量消费科学预算均不由此节点签收。
CPU完整科学gate、统一CUDA、冻结benchmark和批准长跑仍依原计划。
