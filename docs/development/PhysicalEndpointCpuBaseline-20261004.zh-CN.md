# 同物理终点工具：独立 CPU-only 基线

联合计划 StudioConfigurationHandoff 第7节要求同一个 CUDA Release binary 的 CPU/CUDA 主比较，同时保留 CPU-only Release 生产基线，不能只选较慢 CPU 路径。此次只补齐工具，不启动实际计时或科学演化。

run_cuda_matrix.py 新增可选 --cpu-only-arch，记录独立 binary path/SHA，并拒绝与主 binary 同 SHA。CPU-only 运行与主 CPU 路径使用同一线程预算、taskset affinity 和 frozen-source 输入。暖机三路各一次；测量保留全部三路样本，交替 CPU/CUDA 首跑次序，同时交替独立 CPU-only 的前后位置。CPU-only 执行仍显式请求 CPU backend；任何运行或比较失败立即停止，保留 attempt 和失败阶段，不挑选成功样本。

compare_cpu_baselines 对两路均要求真实 backend plan resolved=cpu，复用现有终点、场值和科学检查，误差预算不变。原 compare_pair 仍要求 CUDA 运行真实 resolved=cuda，不能以 CPU 基线替代。summary 保留原 CPU-in-CUDA/CUDA 两个比值，另列 CPU-only/CUDA 两个比值以及三路全部原始计时样本与统计。测量中任一路缺失拒绝汇总。

28/28 工具测试 PASS，0 skip，新增4项覆盖三路次序/预算/独立 executable、失败停止、CPU plan/原预算和同 SHA 拒绝。测试的计时与 H5 为合成 fixtures，仅继承既有 taskset 小型 Python 进程探针；不是实际 ARCH 性能或物理证据。CLI --help 和 git diff --check PASS。

文件名、不同 SHA 和 resolved=cpu 均不能证明 CPU-only Release 构建；build_kind_verification=pending 保留，qualified_benchmark=false。需要受控构建记录证明 CUDA OFF、Release、toolchain/flags，并冻结硬件、有效输入与外部 EOS/库身份。线程/affinity 筛选、资源采样和已批准终点/参考/预算也仍未闭合。此工具不绕过 CPU 科学 gate、不启动 CUDA、不开长跑；既有科学失败和 owner 待决项不变。

完整测试日志和临时数据留本机 ignored studio/.local；仅提交代码、测试和处理后的摘要。新增接口可选，旧两路短检查不自动变成三路实际运行。不 push/tag。
