# 物理终点 runner：预热与交替调度

基线2cc0b9ab。显式 endpoint mode 要求 CPU threads、CUDA Host threads 和 >=3 repeats。
每个后端独立 warmup 一次，随后 CPU→CUDA / CUDA→CPU 交替，全部测量样本保留；
warmup单列，不混入中位数。summary包括原始计时、min/max/median/count和负收益。
每次attempt记录phase/order/active backend/runs/status，失败立即停止并保存已完成证据。
固定步数legacy短检查仍可单对、无预热。

13项合成fixture/stub测试PASS，diff check PASS。未运行真实ARCH/CUDA/simulation。
线程筛选/亲和性、CPU-only对照、完整硬件/build/input manifest、资源采样和owner冻结预算
仍待完成，report qualified_benchmark=false，不伪称正式性能验收。
测试环境沿 PhysicalEndpointRunnerProgress-20261003 的独立Python3.12/numpy/h5py。
