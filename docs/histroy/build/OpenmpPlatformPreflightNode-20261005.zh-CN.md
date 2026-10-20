# Second-platform standalone OpenMP team / place preflight

基线a159dae30407c4a46e1ec578b8c9e2db10d4ea42。
统一计划第8项所需平台执行前检；不是科学/性能出口。
本节点复用现有platform_preflight，不新增benchmark或科学runner。

## 已有缺口与最小扩展

现有nvidia-smi/CPU/memory snapshot保留。
run_cuda_matrix已有taskset-before-exec及OMP环境，但
actual_openmp_team_size仍null；不能将环境变量解释成生产实测。
本节点不替它填写推测值。

新增tools/openmp_affinity_probe.cpp：独立OpenMP region记录实际team size、
每thread CPU、place、sched_getaffinity和binding。
现有platform_preflight新增可选--openmp-probe/--probe-threads；
二者成对、thread预算不超当前allowed CPUs。
按runner既有显式设置OMP_DYNAMIC=FALSE、OMP_PLACES=threads、OMP_PROC_BIND=spread。
校验完整唯一thread、actual size、可读且预算内mask、真实CPU属于mask、
可用且唯一place与binding；坏响应为unavailable并exit2，保留原因。
默认仅snapshot行为保持；helper无需ARCH、EOS、CUDA或任何科学初始化。
trusted executable的SHA是观测身份，不是签名或source真实性的自动认证。

## 真实本机观测

使用既有CPU build所指定compiler构建helper，准确command、compiler version/SHA、
helper ELF和libgomp SHA在summary。
WSL允许逻辑CPU0..27，共28；CPU名称i7-14700K。
GPU exact name NVIDIA GeForce RTX 4070 Ti匹配；未推断物理/P-E核。
cpufreq policy未由WSL暴露，仍明确unavailable。

| requested threads | observed standalone team | places | observed CPUs |
| --- | --- | --- | --- |
| 1 | 1 | 28 | 0 |
| 8 | 8 | 28 | 0,4,8,12,16,19,22,25 |
| 28 | 28 | 28 | 0..27 |

三次均observed proc_bind=4，每thread mask为一个逻辑CPU、place唯一。
这证明本次独立helper的spread/threads绑定；不是CPU性能筛选，
不能把28逻辑CPU叫28物理核，不能将任一列表命名为P-only或E-only。

6项工具tests PASS：原GPU解析/unknown语义、实际team校验、
错误team/thread/mask/cpu/place/bind拒绝、oversubscription执行前拒绝。
真实1/8/28 team三次PASS；helper编译无告警；
architecture audit/diff check PASS。
CPU production ELF保持7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510，
无Core build或科学变更，沿用匹配JENS短包，不重复。

## 复现和输出

当前CPU compiler:
    /usr/bin/c++ -std=c++20 -O2 -fopenmp -Wall -Wextra -pedantic tools/openmp_affinity_probe.cpp -o <local helper>
    python3 validation/gravity/curved/platform_preflight.py --expected-gpu 'NVIDIA GeForce RTX 4070 Ti' --openmp-probe <local helper> --probe-threads 8

处理后summary：
validation/gravity/results/openmp-platform-preflight-20261005/summary.json。
完整snapshot、GPU瞬时metrics、stderr、compile command、ELF、ldd和测试日志留：
studio/.local/integration/openmp-platform-preflight-20261005。

## 尚未取得的出口

独立helper不证明ARCH每个production parallel region的actual team。
正式runner仍须使用生产观测/诊断，并在代表冻结场景筛选实际线程/affinity。
没选最佳thread数、没启动CUDA构建/benchmark/长跑；
snapshot不能证明idle、peak或ARCH资源归因，不据此qualified_benchmark=true。

当前endpoint freeze报告描述工具的snapshot保护，不是Core冻结的科学场景；
未批准RZ子集、完整CPU科学gate、实际binary/build/data manifest、
连续资源采样、三路同终点计时和批准长时预算均按原计划继续。
不扩大Windows或新硬件适配，不修改数学或阈值。
