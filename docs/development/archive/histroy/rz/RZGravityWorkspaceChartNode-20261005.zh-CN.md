# 原 GravityWorkspace 的 RZ 物理几何消费节点（2026-10-05）

## 身份与范围

起始HEAD 5c1d3a3212b68a63e14a68d91667ad8d8146af69，唯一Linux/WSL工作区。
GEOMETRY PRODUCER PASS，不是完整RZ Runtime或科学签收。
上一节点修正tree→elliptic chart；本节点继续原workspace消费者，不重复tree实现。
公开SelfGravity RZ bind/regrid以及CUDA JENS gate全部保持，没有应用待授权candidate。

## 实际缺口与修正

旧GravityWorkspace即使收到显式RZ mesh，仍按Cylindrical dim2计算第二方向r*dphi，
并把face.center第二坐标送入cos/sin。对RZ该坐标是z，这会给出错误物理距离与观察点。
原owner现在使用两个不分配、不发布场的geometry producer，构造器直接调用同一函数：
- gravity_cell_geometry保持绑定block/offset；RZ用原GridMetrics::Rz dr/dz；
- gravity_boundary_point通过原显式GeometryView PhysicalPosition，RZ返回(r,0,z)。
Existing保留原PhysicalSpacing的精确center参数和原Position叶；不作center→lower→center回算。
观察点不是point-mass source，不改变finite-ring源定义、积分、G、数值下限或科学阈值。
内部header仅声明原workspace producer，caller继续拥有完整topology/storage资格检查；
producer本身不检查publication或授予runtime能力，不复制另一套重力数学。

## 验证与边界

原self_gravity_lifecycle在真实axis/offaxis uniform/mixed tree binding的3584cells上
逐个检查block/offset、dr/dz/inactive width，所有原生边界face检查(r,0,z)/face身份；
negative cell/invalid face明确拒绝。既有native measure、z Dirichlet及生产gate检查保留。
完整RZ构造器/solve仍因bind gate未运行；本证据不能称RZ field或runtime publication通过。

原2D/3D Cartesian真实SelfGravity force/lifecycle CTest 1/1 PASS。
当前共享gravity archive进入原actual Runtime fixture，4→8→4、7gather、time0/steps0 PASS；
记录fixtureELF、archive、源码SHA，不拿旧ARCH object证明新workspace。
CUDA-enabled build下同一Host CTest 1/1 PASS；明确不是Device RZ求解或科学签收。
CPU生产ARCH仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，
沿用匹配冻结receipt，不重复未变9+9；旧Build Manifest不能代表当前源码freshness。

首轮新fixture误写BoundaryPoint.point（实际成员position），完整编译错误留本机；
只修字段引用，不改数学/断言/误差门槛。最终CPU增量5.019s、CUDA-enabled增量5.020s，
parallel28、swap0、memory/pressure guard未触发。architecture/diff检查PASS。
没有simulation时间推进、H5/Plotfile/checkpoint生成、长跑或计时结论。

## 复现、下一步

cmake --build build-cpu --target arch_self_gravity --parallel 28
ctest --test-dir build-cpu -R '^self_gravity_lifecycle$' -V
python3 validation/gravity/run_gravity_runtime_contract.py --build build-cpu --output-root <new-local-folder>
cmake --build build-cuda --target arch_self_gravity --parallel 28
ctest --test-dir build-cuda -R '^self_gravity_lifecycle$' -V （Host）

处理后validation/gravity/results/rz-workspace-chart-20261005/summary.json；
raw/ELF保持studio/.local/integration。只交付处理后指标、身份与源码。
下一层仍要接authoritative finite-ring boundary/source/RHS/residual、真实stage/epoch失效，
以及原生force/work，RZ A→B→C→D科学finding与独立参考预算继续保持。
不因本接口检查就解除生产gate，不开展Windows适配或未批准长跑。
