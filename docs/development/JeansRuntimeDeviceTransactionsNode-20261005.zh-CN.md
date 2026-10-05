# JENS actual Runtime device 事务节点（2026-10-05）

## 状态与身份

SCOPED PASS，起始 HEAD 6cf5ba8d9c96b1f11ab41691bb94851cd2b0ea52，唯一 ARCH-compute-optim 工作区。
本节点接通实际 DriverRuntime/PrepareRegrid 的 device JENS 决策，不代表完整冻结短包。
公开 config refinement、output、API 与 Driver startup CUDA JENS gate 保持；RZ gate 保持。
CPU ARCH SHA256 仍为 7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。
对应冻结 CPU 9+9 receipt 复用；旧 Build Manifest 不代表当前已变化源码的新鲜度。

## 原 owner 与真实调用链

AmrTree 提供 CandidateParentGeometry：检查完整 logical sibling family、同层、下侧起点、
x1-fastest 子序，使用原 root geometry 构造同一候选父；无 pool 分配或 accepted 发布。
CPU/RZ CandidateParentResolved 复用此几何，再走原 restriction/可解析性/EOS。
PrepareRegrid 新增可选 parent consumer；默认 CPU 路径不变，device RZ 明确拒绝。

实际 Runtime 普通 regrid 仍走原曲率指标。JENS 通过整域 accepted Current lease consumer，
不读取 stale Host rho/X；repair-only 不运行普通曲率 consumer。所有 finest deficit 在改 flag 前拒绝，
JENS deficit 使用原严格比较，覆盖普通 keep/coarsen；关闭 JENS 的 ensure 仍直接返回。
合法性复用原 make_selection，不增加 Jeans 下限、默认值、EOS 或科学阈值。
父 hook 核对 committed topology、Current version、每个 child Device ledger/storage identity，
再调用上一节点原 backend private restriction→状态码→同一父 EOS/JENS。
nullopt 是粗化 veto，非法 summary 明确失败，parent minimum >= target 允许粗化。
仅移除内部 Runtime 旧拒绝，公开可用声明未改变。

## 4070 Ti 实机事务

复用原 cuda_regrid_transaction，无新 CTest/job。材料/几何来自 frozen uniform-lifecycle-1，
但这是内部 typed Runtime t=0 工程子组；没有注册模型 Setup、Poisson、Hydro 时间推进或 checkpoint。
1D/2D/3D root 4，目标160实际细化到8/16/32；父态 N0<160 保留fine；
实际 FP64 parent minimum 与 target 等号允许粗化；nextafter(+infinity) 必须再次细化。
原冻结目标64明确完成真实 coarse transaction。恢复的 uniform device minima 与 roots 完全相同。
每次重新 poison Host rho/X，Host tree EOS callback直接抛错，成功不能由 Host fallback 冒充。
已退役真实 full BackendStateAccess 不再被 contains 接受。

lrefinemax=0 的 deficit 在 topology 发布前明确拒绝，source/handles 不变。
容量8不能承载4 old+8 new=12的staged峰值：原精确 MemoryPool exhausted 错误，
旧handles/root count/device minima 保留，staged namespace为空；time/step始终0。
原4/41 species transaction、private parent status/fatal/恢复、lease反例均保持PASS。
首次与最终 CTest均1/1 PASS、无skip；最终新增target64断言后重编和复测。

## 相关 CPU 回归、构建与复现

jeans_diagnostics、shared_stage_scheduler、amr_operation_plans：3/3 PASS。
包括原 RZ ghost W、rigid rotation transfer、common theta、父 veto和角向restriction反例，
不把这些内部检查升级为完整RZ科学签收。
原 run_gravity_runtime_contract.py 实际 CPU Runtime fixture exit0：
4→8→4，7次真实gather，JENS非首version/Device-only拒绝，time/step0。
共享头文件触发四EOS Hydro路由重编，CUDA初次877.918s；最终仅test增量8.039s。
top parallel28/heavy pool1，peak RSS约1.89GiB，swap0，memory/pressure guard未触发。
保留原NVCC constexpr equality warnings；没有为消除警告修改无关实现。

复现入口：
- cmake --build build-cuda --target arch_cuda_regrid_transaction --parallel 28
- ctest --test-dir build-cuda -R '^cuda_regrid_transaction$' -V --output-on-failure
- cmake --build build-cpu --target arch_jeans_diagnostics arch_amr_operation_plans arch_shared_stage_scheduler --parallel 28
- ctest --test-dir build-cpu -R '^(jeans_diagnostics|amr_operation_plans|shared_stage_scheduler)$' -V
- python3 validation/gravity/run_gravity_runtime_contract.py --build build-cpu --output-root <new-local-directory>

处理后证据：validation/gravity/results/jeans-runtime-transactions-20261005/summary.json。
源码、fixture ELF、CPU ARCH、raw日志SHA分别记录，完整数据本机保管。
最终 architecture audit/diff check 与提交清单核对后提交review。
没有重跑未变CPU ELF的冻结9+9，没有Studio源码改动，不重复Studio全回归。

## 仍未完成

下一步完成真实应用初始/每个接受宏步/regrid/output/restart的CUDA三通道冻结短包，
配置/API/checkpoint身份同批协调；全部相应出口通过前不宣称public CUDA JENS已签收。
其他EOS科学域、RZ A→B→C→D剩余消费者及科学finding、统一长跑/同终点计时仍未完成。
本节点不调整科学定义/阈值、不启动Windows适配或新长轨迹。
