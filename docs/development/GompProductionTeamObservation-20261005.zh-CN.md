# 实际 production OpenMP team 外部诊断

## 缺口与工具

standalone OpenMP helper 只能证明平台能创建对应team，
不能替代 production ARCH 的实际parallel-region观察。
当前CPU ELF动态符号只引用GOMP_parallel作为parallel入口，
运行库为GNU libgomp。ABI核对使用
[GCC 13.3 parallel.c](https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-13.3.0/libgomp/parallel.c)
的GOMP_4.0入口；本工具不复制该实现或任何科学kernel。

tools/gomp_team_trace.cpp 是Linux/GNU限定的外部诊断library，
通过LD_PRELOAD及可信CLI传入的继承文件描述符使用。
num_threads/flags与原始body/data保持原含义，真实region内查询team。
不添加barrier、不改变调度选择、不修改Core或ARCH ELF。
先保存128个region全部成员的CPU/place/affinity；
所有拦截region继续统计team-size histogram，超过1024的team明确单列。
超出128后不声称成员明细完整。

该library不能由浏览器提交或进入Host通用命令接口。
无ARCH_GOMP_TRACE_FD时只转发。失效descriptor/无法解析ABI明确失败，
不静默填写观测。输出是诊断元数据，不是科学输出。

构建方式：

    c++ -std=c++20 -O2 -fPIC -shared -fopenmp -Wall -Wextra -pedantic tools/gomp_team_trace.cpp -ldl -o <local trace.so>

可信本地Python harness以pass_fds传递已新建日志的fd；
环境LD_PRELOAD=<trace.so>、ARCH_GOMP_TRACE_FD=<fd>。
普通应用和正式计时不要加载该library。

## 验证和实际发现

三个真实GNU工具测试PASS：
- 8成员正常region及1成员serialized region，body调用数保持9；
- 130个2成员region加1个serialized，共131；
  明细只128，histogram仍完整统计130+1；
- 未设置descriptor时无记录，原body正常执行。
architecture audit/diff check PASS。初次compile的member初始化warning已修正；
最终compile无warning，日志保留。

当前ARCH执行已获准的原Gaussian cloud-1 t=0样本，仅out_dir改到新的本机目录。
OMP_NUM_THREADS=8、DYNAMIC=FALSE、PLACES=threads、PROC_BIND=spread。
baseline与观测执行均exit0：
- GOMP_parallel实际调用1058；
- team=1：1053次；
- team=8：5次；
- 前128个region所有成员CPU落在实际mask中，mask属于当前允许logical CPU；
- 22个输出dataset对照一致，numeric数组逐字节保持、string按值保持；
- 所有比较的plot time=0，没有simulation timestep演化。

最初比较器对HDF5 object dtype使用tobytes，误比较字符串对象的指针。
保留初次记录；只修正string按值比较并重新分析既有文件，
首轮22项一致；最终histogram版本也与同一baseline22项一致。
这不是靠修数据通过或丢弃numeric差异。

处理后 [summary](../../validation/gravity/results/gomp-production-team-20261005/summary.json)
记录准确库/代码/ELF、输出身份、全部histogram和覆盖范围。
原始H5/日志/成员明细留本机studio/.local/integration/gomp-production-team-20261005。
不上传trace library或原生数组。

## 解释与后续边界

请求8线程不代表每个region实际8线程；serialized regions不等于全局OpenMP关闭。
这不是最佳线程筛选、性能收益或CUDA证明，也不能外推其他模型/阶段。
LD_PRELOAD和成员查询有额外开销，观测进程的计时禁止进入正式benchmark。
run_cuda_matrix的actual_openmp_team_size仍不被自动填入；
正式执行需要自己的身份与证据，不能借用本样本伪造。

production CPU SHA仍
7d3bd3d396dfcbcd8126aa82d37b293c963ceb8a23428b184cb5a15ceab0b510。
不重建ARCH、修改科学阈值或重复JENS全包；
RZ科学门槛、统一CUDA、冻结benchmark/批准长跑仍未完成。
平台继续Linux/WSL，不做Windows适配。
