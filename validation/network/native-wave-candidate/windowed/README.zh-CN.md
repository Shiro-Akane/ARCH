# 有界工作窗口／因子 cohort 分离：合同通过，核反应未验收

2026-09-17：leaf-inline 完整结束和双端保全后，独立 18 项真实 GPU 合同全过并已双端核验。
见[原始证据](../../results/native-wave-20260917/window-contract-v1/README.zh-CN.md)。
这仍不是生产优化，没有 ODE／核反应／性能资格；下文准备检查不替代实际运行证据。

## 改动范围

`CuDssSparseWindowSolver` 是 Host 调度封装，最多 128 个逻辑槽，仅持有一个原有
`CuDssSparseWaveSolver`（最多 32 个 native 槽）。不增加 device 分配，不修改原 provider、
cuDSS、共享数学、精度、数值门槛或 256 MiB native 估计预算。
ODE 工作窗口的隔离接入正在重新编译，尚未实际运行，因此不能声称增加了实际核反应并发。

- `(逻辑槽, caller token, matrix address)` 对应独立、单调递增的内部 token；各单元可以有相同 caller token。
- 调用方修改系数必须换 token，不能以地址相同为由复用旧因子。
- 合法逻辑因子被其他页逐出后，显式重新 factorize；未知／过期／移址 token 仍报错。
- 整个窗口先检查，包括跨页的 output/input、output/output、output/CSR metadata 别名；
  Idle 但保留逻辑身份的原矩阵也不能被其他单元输出覆盖。
- 只有全部页成功才发布新逻辑身份；部分失败时清空 native 有效性，不回收已用内部 token。
- 底层物理工作统计原样返回，包括 padding 和额外 factor；上层逻辑请求、分页另计。
  因为被其他页逐出而恢复、因为显式／失败失效而恢复分别计数，不能混成一种 cache 成本。

风险是更大的窗口会失去部分因子复用、增加 native 调用和等待；不能假定它比原来更快。
详见 [源码与显存口径](../bounded-window-followup.zh-CN.md)。

另一个已核实的接入边界：生产 `execute_burn_batch` 的 cuDSS 路径仍逐 block 调用 owner，
首次用该 block 的 active-cell 数创建 pool；`execute_sparse_burn_cells` 也只处理一个 block。
因此仅放宽 owner 的窗口上限不会自动跨小 block 聚合。原小算例必须保留，不能换大 block
或增加物理单元来冒充同输入收益。后续跨 block gather/scatter 若确有必要，应独立实现并验证
每块网格映射、EOS 错误、summary/reduction、slot、AMR generation 及退休契约。

## 已完成的独立合同

两个制造解维度 151、201，各覆盖 `(window, cohort)`：
`(1,1), (2,1), (3,2), (8,8), (9,8), (32,32), (33,32), (64,32), (128,32)`，共 18 组。

复用 SHA 固定的原 GPU 制造解测试辅助函数：独立已知解 `1e-12`、原 CSR residual、
输入不修改、异步传输生命周期和严格负例分类。生成头文件只截取并关闭其原 namespace，
所有辅助函数字节保持不变，不另写一套物理／误差判断。

覆盖 cold/stale/relocated/zero token、错误操作、Host/null 指针、跨页别名、空页、
tail、Factorize-only／Solve／Idle 混合、逐出恢复、重复相同 caller token、显式失效，
以及最后一页 NaN 失败后不得发布前面页新身份。基础设施错误不得算作负例成功。

构建只新增两个 Host `.o`，链接已通过合同且冻结的 batch-kernel provider；
不重编 CUDA factory、不修改归档成员。继承原 g++-11 strict-FP 配方。
运行必须在 leaf-inline 结束、审查并完成服务器／本机双端归档后，使用既有资源 guard 单独执行。
`dispatch.sh` 本轮已执行且完成；没有自动并行后续任务。

冻结本机输入包为 48,128 bytes，SHA-256
`3f9e7f1fda8f906a9a9fb4b460c734bd801ba3d15c38e80ae084ce61b7330163`；
完整七文件身份见 `prepared-input-v1.json`；本轮上传和执行的就是该包，字节未改变。
收集器不在该输入包内，只在实验结束后另行上传，不改运行中的冻结配方。

本机的 6 项源码／配方检查及两项 shell 语法检查已通过（最近一轮 0.054 s），
日志见 `preparation-checks-v1.log`，只验证准备逻辑；实际 18 项合同结果见开头的原始证据。
即便合同通过，后续仍需独立 fresh factory 接入、真实三 ODE／容量／长轨迹／Helm 及配对性能，
不能用这里的制造解替代上述验收。

## 后续 factory 接入配方：新构建进行中

`prepare_factory.py` 只从已归档的两份 SHA 固定 execution/owner 头生成可逆的隔离 overlay，
保留所有 device 数学、launch shape、response/residual 检查、block gather/scatter 和生命周期代码。
Host 实验选择项 `ARCH_NATIVE_WINDOW_CELLS` 仅接受 32／64／128；有效容量仍受真实 caller 单元数限制。
ODE 全局 workspace 另加 32 MiB 显式上限，原 native cohort≤32 和 256 MiB 估计预算不变，
不设置或伪造硬件 warp，不改 stack/cache/runtime 限制。

本机先生成了 `build/window-factory-overlay-v1` 供审查；实际部署由下述完整源码副本配方生成同样两头，
不在上面的七文件合同包内，尚无新 factory 的运行结果。
合计 9 项准备测试通过，包含原 kernel 数学区域、allocation 后的执行/析构区域字节不变检查。
这些测试不说明两份 factory overlay 可编译；18 项合同只编译/验证窗口 wrapper。

未来需新编两个真实 factory，不得借用旧 CUDA `.o` 宣称新窗口已验证。
同一新二进制的 32／64／128 对照只隔离窗口大小的作用；wrapper 的 32 模式本身仍有额外 Host 工作，
还须保留原 native-wave 的独立对照，才能评价整个封装的净收益。

### 全新编译配方的接入约束

`factory_recipe.py` 和 `build_factory.py` 在合同通过前仅做本机准备；
本次后续部署不改变上述七文件合同包。
新 runner 必须先核验 18 项真实窗口合同、原输入／产物身份及双端完整备份，
之后也只能放在原资源护栏内串行运行。现已准备 `factory-worker.sh`／`factory-dispatch.sh`，
在完整窗口证据双端保全并发布后，已用新的五文件冻结包单独启动 factory worker 384860。
wrapper Host 编译及 150 核素依赖预扫描成功，正在新编 CUDA 对象；没有实际 ODE 结果。
包身份见 `prepared-factory-input-v1.json`，不修改旧七文件合同包。
另已准备独立 `collect_factory.py`，只在构建结束后上传；25 项本机配方/合成门槛检查通过，
见 `factory-collection-preparation-checks-v1.log`，不等于实际编译或科学资格。
收集器将重新核对完整副本、实际依赖、编译/链接命令、七个新产物及原资源护栏，
raw 保留全部私有源码/产物；外部原网络、SDK 和库以哈希依赖及父归档引用保留。
真实燃烧轨迹尚未运行。

不能只把两份改动头放到较早的 `-I`：原 `SparseBurnCells.cuh` 用同目录的
`#include "SparseOdeBatch.cuh"`，可能仍读到原调度器。配方改为复制原清单的全部 474 个文件，
只变两份已审阅执行头，再增加两份窗口文件；其余字节逐项保持。原源码、构建树和依赖库不动。
先做 NVCC 依赖预扫描，避免在错误头绑定上白编数小时；实际编译后的依赖文件也必须
包含新 executor／owner／window 头，且不能引用旧 source 下任何头。
链接明确使用两份新 CUDA factory 对象，不借用旧对象来证明新路径。

原网络（不叠加已完成独立诊断的 sink 内联）、共享物理、sm90、编译优化级别和 strict-FP 均保留。
这里只构建 factory，状态最多为 `factory-built-not-runtime-qualified`，不是三 ODE 运行通过。
尚未构建完整 ARCH 或证明跨 block 聚合；原小输入必须保留。

本机 19 项准备／门槛检查见 `fresh-build-preparation-checks-v1.log`；新增预扫描检查后
20 项于 4.903 s 通过，两个 factory shell 脚本语法检查通过，仍没有 factory 编译证据。
首次完整复制测试误用不含大表的 Git compact 投影而失败，改用已核验 raw 源库存后通过；
仍显式测试不完整 compact 必须被拒绝，没有取消原表或完整性门槛。
依赖文件检查使用合成文本，本机没有运行 C++／NVCC，不能当作实际编译证据。

## 原六项真实轨迹配方：本机准备，尚未部署

`run_focused.py`、`focused-worker.sh`、`focused-dispatch.sh` 只会在两份新 factory
完整编译、逐项归档并通过本机字节核验后，串行运行 audit150／audit200 的 BE_NR、BD、ROS4。
复用已修正默认 storage-controls 解析的原 validator，文件字节 SHA 固定；没有重写数值验收。
仍是原 2→3 单元、pool=2、四步、interval=1e-10、IdealGas、原组分和误差预算。
实际 `WINDOW_OWNER` 必须报告 selected=32、capacity/native=2、32 MiB workspace 上限和硬件 warp=32；
工作区字节必须与原 harness 的真实 lane-bytes 相符。没有 observer 或编译器改动。

该小输入只验证封装接入，没有超过一个 native page，因此即使通过也不能标为多页 ODE、
Helm／完整应用或性能通过。`collect_focused.py` 在结束后独立收集，保留失败与原始输出。
31 项本机配方／合成检查全过，包含 6 项新 focused 门槛检查；详细口径见
`focused-preparation-checks-v1.log`。它们不构成新 factory 的 CUDA／核反应运行证据。
