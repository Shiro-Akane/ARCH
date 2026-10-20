# JENS device 候选父态 consumer（2026-10-05）

## 身份与边界

起始HEAD ec984ed963b566c9f8f071495d073f00ac5f790f，唯一ARCH-compute-optim工作区。
SCOPED PASS：私有受限父态→状态检查→authoritative EOS/JENS consumer。
不表示Driver/PrepareRegrid decision已接通，不解除public CUDA JENS或RZ gate。
CPU ARCH ELF仍7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510；
不重复对应未变ELF的冻结9+9，不将旧Build Manifest称为当前源码freshness。

## 实现和所有权

ComputeBackend新增evaluate_jeans_parent，缺失能力明确拒绝。
CudaBackendIndicators原owner使用accepted Current child accesses和原Grid几何；
完整handle/epoch/storage/slot、Cartesian/dimension/spacing/species/layout预检。
参数约定children以几何bit顺序给出；完整logical sibling closure由Host topology调用方负责，
不能把backend storage preflight等同完整publication或parent family资格。
后续Driver必须先检查整域accepted ledger，再核对logical family并调用此入口。

父state仅存在临时device scratch，无active/staged store entry，无Host场值上传。
原GridMetrics cache kernel构造父metric；
原launch_cuda_regrid_restriction调用共享RegridTransferMath。
将restriction layout/workspace检查提取为同一owner的无enqueue validate函数，
launch本身仍消费相同检查，未复制第二份transfer数学。
完成并读取4-byte restriction status后，才调用同一父state的原EOS/JENS。
共享CoarseFluid返回nullopt粗化veto；其他状态报原错误并安全清理。
EOS/result非法明确失败，不跳过、修复、降目标或使用叶块minimum代替parent。
正常仅下载status+FP64 minimum共12B，两次显式stream完成；
暂不把临时allocation或同步成本当作已优化performance结论。

## 正常实机检查

原cuda_regrid_transaction：真实private父state复用device accepted children，
与CPU真实AverageToCoarse后authoritative EOS/Jeans路由控制比较。
1D、2 children、4/41 species通过；沿用原静态16epsilon检查，
此CPU/GPU控制仅证明路由和同一父态消费，不提供新独立科学参考。
Host Current rho/composition已故意NaN，device结果仍正常。
每次4 kernels（metric/restriction/Jeans两kernel）、12B D2H、0B H2D、两次stream完成。
active/staged namespace规模不变，后项stale storage在enqueue前拒绝。
原Runtime lease、refine/restrict/rollback/survivor/source位级检查同时保持PASS。

## 失败和恢复

原始device child的energy或ENUC显式注入非法值，
只为共享状态码分支检查，不冒充可解析fine→不可解析nonconvex coarse的科学样本。
CoarseFluid veto和RestrictionEnuc fatal都只有metric/restriction两kernel与4B status；
不调用父EOS。显式下载对照证明source在失败期间位级未改；
重新上传原source后parent结果恢复。
veto一次fence；fatal另执行原quiesce_or_terminate安全清理，共两次。
第一版新断言漏计fatal cleanup fence，完整失败日志保留；
修正为原owner实际语义，未减少安全清理、未改变任何数学阈值。
最终原CTest 1/1 PASS，无skip；Host compute_backend 1/1 PASS。

原cuda_regrid_migration也1/1 PASS，共12个原dimension/species/chart标记。
cylindrical/spherical属于既有chart，不能把这些标记改称新RZ角动量通过。
四个EOS view的consumer控制调用均编译，但本节点数值只验证IdealGas上述状态。

## 证据和下一步

处理后summary位于validation/gravity/results/jeans-device-parent-20261005/summary.json。
raw/ELF/完整诊断保留studio/.local/integration/jeans-parent-cuda-20261005。
下一步将这个private consumer接入真实Host topology候选父hook与Runtime accepted lease，
检查target严格比较/等号保持、veto、publication/rollback，再完成初始、宏步、
regrid、restart、output三通道CUDA冻结短包；通过对应出口后才解除能力门槛。
RZ科学、其他EOS、正式benchmark与批准长跑仍不由此节点签收。
