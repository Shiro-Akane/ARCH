# RZ 实际 Driver timestep candidate 接线

基线25210f04e5faed1460e69f79c193dceefb700e64。
calculate_timestep_candidates原先丢弃Runtime固定chart，Host hydro/diffusion
均调用Existing默认helper。现在两个调用显式传runtime.geometry_semantics()。
CFL定义、EOS、共享reduction及RKL系数/stage cap不改。

## 真实检查

扩展真实Runtime fixture，四组五叶块mixed-AMR，包含轴/非轴、径向/轴向
粗细接口。实际Driver聚合Hydro候选，对照独立常态两面声学dr/dz公式，
最大差1.0842021724855044e-19，沿用既有2e-12工程算术门槛。
测试使用rho2、mom(2,6,4)、energy100，pressure=(100-14)*0.4；
phi只通过完整动能影响EOS，不作为第三空间传输轴。

两种RKL resolved route各计算4组，diffusion FE候选与显式RZ共享leaf聚合
bit相等；STS候选打印原值，不将极大stage-cap稳定上限冒称实际选定宏步、
accuracy或演化验收。此项是接线一致性，不是独立diffusion物理oracle。
坏active rho NaN经实际并行Driver聚合明确报错，未被IgnoreNaN吞掉。

Runtime fixture采用OMP_NUM_THREADS=2，四个translation units重新编译，
链接现有trusted CPU依赖archive；不是production ARCH rebuild。
time/step保持0，无H5/plt/checkpoint。最终curvilinear_metrics/
shared_stage_scheduler/reduction_contract scoped3/3 PASS。

原始本地日志与fixture ELF位于ignored
studio/.local/integration/rz-runtime-timestep-20261004；处理后的指标和
精确源码/头文件/fixture身份见同名Summary.json。
可复现runner仍为validation/amr/run_rz_runtime_boundary.py。

## 剩余

advance_diffusion的真实RKL阶段、operator/reflux/ghost callbacks仍需统一chart；
当前仅完成candidate计算，不宣称其演化已可用。公共runtime几何配置、
checkpoint语义、重网格角动量finding、finite-ring gravity、CUDA与冻结
演化验收仍未完成。下一步继续真实RKL1/RKL2接线与制造解检查。
