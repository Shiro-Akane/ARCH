# 既有终点 runner：Linux CPU／CUDA Host 亲和性
基线6d6f4863a2e8a7c91c1eab4d503cf5b5c71dcf4d。
核对实际源码：paired_trials 已经完成每backend一次预热、>=3次交替成对、失败停止、
全部timing samples/负收益保留；旧 PhysicalEndpointRunnerProgress 的“仍需预热”是历史状态。
本次没有重复实现这些功能，仅补显式 Linux affinity。

## 行为
既有 run_cuda_matrix.py 增加 --cpu-affinity 与 --cuda-host-affinity。
只接受当前 sched_getaffinity(0) 内的整数CPU IDs/ranges，无重复、负值、倒序range、
命令片段或无界展开；线程预算不得超过所选逻辑CPU数量。
每backend在预热及全部测量中使用对应同一列表，通过固定/usr/bin/taskset argv先绑核再exec。
没有shell解释或绑定失败后的无绑定重试；错误仍停止下一backend/pair。
不提供列表保留历史 inherited 模式，不能将其认定为冻结affinity的正式benchmark。

显式绑定时固定 OMP_DYNAMIC=FALSE / OMP_PLACES=threads / OMP_PROC_BIND=spread，
记录requested threads/affinity、parent allowed CPUs、taskset SHA、指定OpenMP环境及启动链退出码。
execution.json与完整run.log在本机run目录，包括失败；不导出其他环境变量或秘密。
taskset和ARCH启动链错误不自动归为科学失败。elapsed包含taskset启动开销，CPU/CUDA口径相同。
actual_openmp_team_size=null，未执行实际ARCH并观测team；不能由env声称真实team count。
WSL CPU编号不提供可靠P/E对应，不宣称绑到了8P或12E。

用法（端点与输入必须由Core冻结，CPU列表须以当前可用集合为准）：
python3 validation/gravity/curved/run_cuda_matrix.py --arch /local/Release/ARCH \
  --endpoint-pair label:/local/approved.par:OWNER_T_END --output /local/new-run \
  --cpu-threads CPU_THREADS --cpu-affinity CPU_LIST \
  --cuda-host-threads HOST_THREADS --cuda-host-affinity HOST_CPU_LIST --repeats 3
占位符不是可运行冻结包；本轮不启动ARCH/CUDA，不提前进行性能验收。

## 验证
当前终点/编排/科学拒绝语义加6项affinity检查，共19/19 PASS，无skip。
真实/usr/bin/taskset在一个可用逻辑CPU上运行Python子进程，sched_getaffinity返回唯一指定CPU。
其余为合成输入/H5和stub，不启动ARCH：非法列表、线程超过预算在创建output前拒绝；
argv路径带空格保持单参数；8次预热/交替调用传递同一backend列表；
taskset失败不重试、execution/退出码/log保留；旧endpoint/parity/repair/残差拒绝仍通过。
--help与git diff --check PASS。准确源码/工具SHA、Python与可用CPU集合见Summary。
原始synthetic H5/logs留ignored .local，只提交脚本、测试、处理后结果。

保持qualified_benchmark=false。还需有限线程筛选、实际ARCH team/资源观测、
CPU-only对照与完整manifest，及Core冻结输入/物理终点/独立预算。
当前CPU科学gate、Jeans/RZ待决未清除；CPU通过后才实际执行CUDA和O9。
