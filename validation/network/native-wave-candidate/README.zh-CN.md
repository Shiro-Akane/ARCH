# 大网络原生批处理候选：独立 GPU 合同通过，尚未生产验收

2026-09-17 容量补充：[首轮容量记录](../results/native-wave-20260917/capacity-v1/README.zh-CN.md)
在 150/BE/pool8 的原 1,800 秒墙钟限制处超时，没有完整 harness 通过，非 OOM。
失败已双端保全；仅增加墙钟等待的新一轮正在运行，科学时长／步数／预算未改。
另已准备[合并显式 kernel 启动的独立候选](batched-kernels/README.zh-CN.md)，尚未编译或运行；
不因源码准备完成就将其计入 v4 的数值或性能资格。

2026-09-15；2026-09-16 收紧负向测试判据。本目录为隔离执行层候选，不在生产 CMake、网络注册表或当前正式计时中启用。
2026-09-17：在冻结正式矩阵和原 BE 长程补测结束且双端归档后，v4 已在服务器隔离目录重新编译、链接并通过 151/201 阶 × 容量 1/2/8/32 八组真实 GPU 合同。
详见 [原始证据及限定范围](../results/native-wave-20260917/standalone-v4/README.zh-CN.md)。随后已在全新 source/build 完整编译、链接两个实际核反应 factory，并通过 150/200 × 三 ODE × 2→3 单元、原四步轨迹，见 [factory 与 focused 证据](../results/native-wave-20260917/factory-focused-v1/README.zh-CN.md)。尚未通过本候选的大容量／长轨迹、真实 Helm 应用或性能验收。下文“未编译／未运行”表述保留为运行前准备历史，不覆盖本次结果。
仓库原有 Python 架构审计 100 项通过，日志见 `existing-architecture-tests.log`；
它只说明现有架构检查未被改变／破坏，不验证本目录 C++ 是否可编译或 CUDA 是否正确。

## 本次尝试

现有生产调度逐 lane 完成 cuDSS factorize/solve；这个候选使用一个固定、有界的
1–32 lane cohort 提交原生 non-uniform batch。矩阵、RHS、解仍在 GPU，Host 只处理请求／状态。
equilibration、原系统残差、最多两轮原系统修正均调用现有共享接口；不复制 ODE／网络／EOS 数学。
保留 BTF_COLAMD、GPU-only、禁用 host factor spill、原编译 IR 次数及残差预算。

为不在每个自适应分歧后重新创建 descriptor，cohort 的成员和 CSR pattern 固定。
任一 lane 需要新因子时，会重新分解整个 cohort；纯复用波不重新分解。
未用 lane 的私有占位矩阵为单位阵，inactive RHS 为零，绝不写回物理矩阵／状态或 inactive 输出。
这可能增加无用分解，故统计显式包含整个 cohort 的 native factor/solve system 数，
不能只凭 API 调用减少宣传性能成功。成员分歧严重时可能需要否决该候选。

原始地址和非零 token 共同标识每个 lane 的因子；过期／换址请求拒绝。
外部残差拒绝使全部 native factors 失效；下一合法请求重建分析并恢复对应原矩阵。
任何 opaque native error 使整个调用失败，不解释成物理刚性，也不发布部分“成功”。
原系统 residual 的数值拒绝仍须由现有 `validate_corrections` 决定。

候选私有数组和 native peak 估算合计上限为 256 MiB，预分配维度与分析后估算分别检查；
该估算不是实际进程／显存硬上限，GPU 运行时仍必须开原资源 guard 并记录实际高水位。
不启用 CPU fallback、混合精度、改库、无界 cache 或修改科学终点。

## 文档核对与已知依据

已有独立 API probe 在原 cuDSS 0.8.0.10 上通过 batch 1/2/8/32 的制造解；不是核反应验证。
本次按 agent-reach 网页流程尝试 Jina 连接超时，随后用备用浏览工具读取 NVIDIA 官方文档：

- 非均匀 batch 使用设备指针数组，维度在 Host；聚合行数和非零数受 INT_MAX 约束。
  [cuDSS functions](https://docs.nvidia.com/cuda/cudss/functions.html#cudssmatrixcreatebatchcsr)。
- Host 库状态和 DATA_INFO 需分别检查；内存估算在 analysis 后查询，可能不支持，候选对此直接拒绝。
  [cuDSS data types](https://docs.nvidia.com/cuda/cudss/types.html#cudssdataparam-t)。

这里只使用既有 probe 已调用过的 0.8 接口，仍需用服务器固定头文件编译和实测确认；
不因在线文档描述而更换安装库或把未运行结果写成通过。

## 后续验证顺序（尚未运行）

1. 在当前全部耦合正式矩阵和原 BE 长轨迹补测结束后，空闲窗口单独编译本候选和测试。
   保留首条编译错误、完整命令、产物 SHA、原库和共享 kernel 对象身份。
2. `test_sparse_wave`：151／201 阶 × capacity 1／2／8／32；制造解、原残差、复用／换 token、
   factor-only、首次未使用 lane 随后激活、不活跃尾部、不写原矩阵和 RHS、非法指针／别名、
   NaN 失败、奇异不相容方程拒绝、失效重建和容量门槛。
   测试预先确定，不能删掉失败分支使它“通过”。
3. 通过后才在独立 SparseOdeBatch overlay 接入同一 continuation：三 ODE、150/200、
   原四步、32→33 容量更换、原长轨迹、EOS 错误和资源退休；科学预算不变。
4. 再做完整 ARCH＋真实 Helm 应用与旧版本配对、至少五次交替正式样本；
   同时报请求工作与包含 inactive 额外工作的 native counters，以及端到端和稳态指标。
5. 只有数值／失败合同通过且实测收益成立，才考虑生产注册与完整回归；无收益明确保留并否决。

当前正式计时 worker、旧 provider、ODE headers、生产构建文件与待执行 BE 补测均未修改。

## 隔离调度生成配方（仅本机机械检查）

`make_overlay.py --root <本仓库> --output <全新目录>` 只生成供后续实验使用的文件，
不连接服务器、不编译、不运行，也不改生产源文件。配方锁定原 `SparseOdeBatch.cuh` 的完整 SHA，
仅替换 Host 请求调度；设备 continuation／残差验收区域与原 response upload 原样保留。
共享输入清单锁定九个 canonical 文件，包含实际归一化数学 `LinearEquilibration.h`。
任何输入、清单或锚点变化都拒绝模糊套补丁；已有输出目录和失败现场不会覆盖。

初版十二文件准备包遗漏该归一化头文件的显式清单锁定，尚未上传或编译；旧准备记录保留，
由包含九个共享输入、共十三文件的新准备包取代。生产公式没有变化。
此前十二项生成器测试通过，见 `overlay-recipe-tests-v2.log`；对应十三文件准备记录见
`overlay-preparation-v2.json`。初版记录和十一项旧机械测试日志保留为历史证据。
生成器测试只验证 SHA、输入依赖清单、重写边界、输出记录和拒绝覆盖等机械合同，
不证明 C++ 可编译、ODE 行为正确或实际加速。

测试的上传显式完成后才调用候选，保证异常分支中的 Host 输入寿命；
这是正确性测试设置，不计作性能结果。测试 provider 的析构先于借用的设备数组，
其完成屏障也不能被 test fixture 的销毁顺序绕过。
随后代码审阅补充了 test-only 传输完成 guard，覆盖 metadata 上传／结果下载途中异常的
Host 和设备缓冲寿命；该次 payload 记录为 `overlay-preparation-v3.json`，v1/v2 保留。
这个改动仍未经过真实 C++／CUDA 编译或故障注入，不当成安全资格通过。

构建时需携带当前提交的 canonical `SparseEquilibration.cu/.h`、设备分配和共享残差头，
不要直接拿旧 factory 构建树的同名源码假定它已包含本轮残差修正。
沿用冻结的真实编译／链接命令及依赖，改写输出到新目录；用新的共享 kernel 对象及候选对象链接测试。
若借用旧 factor-cache v2 provider 库解析 `CuDssResult::require_success`，须记录原库 SHA；
这不代表生产 SparseOdeBatch 已切换到新候选。首个编译或链接失败也必须保存，不跳过。

## 独立构建与合同 runner（未在服务器执行）

`run_standalone_contracts.py` 根据已成功的原始 compile database／link record，
分别重新编译候选、当前 canonical `SparseEquilibration.cu` 和真实 CUDA 测试。
三个新对象在已锁定 helper archive 之前链接；保留原 strict-FP、IR=2 和库设置。
输出必须是冻结源码、构建树和 payload 之外的新目录，首次失败即保留命令、stdout/stderr 和状态。
逐命令 `/usr/bin/time -v` 记录峰值 RSS，输入、链接库、产物在前后校验身份。

配方的十二项本地单元检查通过，见 `standalone-recipe-tests-v1.log`。
这些测试没有启动编译器、SSH 或 GPU，不是下面八组 CUDA 合同已通过。
真实运行矩阵固定为 151／201 阶 × capacity 1／2／8／32，单次合同 300 秒、单次构建 1800 秒；
不接受无参数退出或只有 return code 0，必须出现匹配维度和容量的唯一完成标记。
即使全部通过，状态也仅为 `standalone-provider-contracts-passed`，`release_qualified` 始终为 false。

只在全部冻结耦合计时和原 BE 长轨迹补测结束后，确认服务器空闲，再执行。
必须外包现有 `tools/run_memory_guarded.py`（Host headroom 32768 MiB、swap growth 64 MiB、
pressure guard、GPU memory observation），以及 10800 秒总 wall guard；一次只运行一个 GPU 作业。
runner 参数为 `--build-dir`、`--recorded-link`、`--payload`、`--shared-manifest`、
`--helper-library` 和 `--output-dir`。它不自动修改当前队列，也不自动启用生产 SparseOdeBatch。

## Factory 接入前置门槛（仅本机配方测试）

`prepare_factory_overlay.py` 只有在同一十三文件 payload 的真实独立合同记录通过后，
才允许生成供独立源码树使用的十四文件 overlay。它检查八组完成标记、逐命令成功状态、
四个新对象／可执行文件及输入 SHA，不接受仅有 exit code 0、失败记录或旧候选记录。
增加的唯一 CMake 注册是在原 strict-FP／IR=2 provider target 中加入新执行层源文件；
旧 provider 保留供原合同和结果处理使用。该步骤不修改本仓库的生产 CMake／源文件。

后续必须使用新源码树和新构建目录，重新编译实际包含新调度器的 factory／网络实例；
旧对象不能证明新路线通过。这个准备脚本不执行构建，也不能证明这些后续要求已完成。
现有 canonical 源码、全部 EOS／network 注册、数学、库和原测试不删减。

十四项本机配方测试使用明确标记的合成记录，只测试拒绝旧输入、遗漏合同、改动产物、
覆盖生产文件／失败现场等机械行为。它们不编译或调用 CUDA，不能算 GPU／ODE 验收；
当前还没有用真实通过记录生成 factory overlay，服务器原采样队列未变化。

## 2026-09-16：负向测试不能靠任意异常通过

代码复核发现，旧测试的通用拒绝 helper 捕获任意 `std::exception`，奇异系统分支还捕获
任意 `std::runtime_error`。虽然显式排除了 Host `bad_alloc`，CUDA／cuDSS 的资源或 API
错误仍可能被误认成预期拒绝。旧 v1/v2/v3 尚未编译或运行，不存在可保留的 GPU 通过资格。

当前仅收紧测试：输入合同必须是匹配原因的 `logic_error`；NaN／奇异系统检查返回的
CUDA／库状态，不再吞掉任意运行异常。明确的内存分配、启动资源、非法地址、未初始化、
不支持、API 参数及内部错误不计作负向通过。允许的 native execution／IR 失败及非零
INFO 仍是 opaque 失败，不被解释为某种刚性或奇异性代码；后续正常矩阵恢复检查仍保留。
枚举核对来自服务器原 cuDSS 0.8.0.10 的 `cudss_data_types.h`，没有升级或修改库。
所读头文件 SHA-256 和枚举摘录保存在 `cudss-status-enum-evidence-v4.log`。

测试内另加纯 Host 的合成分类检查，供之后编译后的同一测试程序实际执行；
它们目前未运行，也不是 CUDA 内存耗尽／非法访问的真实故障注入。
后续使用 `overlay-preparation-v4.json` 对应的十三文件 payload，旧包保留。
v4 只改变 `test_sparse_wave.cpp`；provider、调度器、九个共享输入及科学容差不变。
十三文件逐项回读及 v3/v4 差异核对见 `payload-identity-v4.log`。
现有 38 项本机生成／构建配方回归通过（1.814 秒），见 `recipe-tests-v4.log`；
这不执行新 C++ 分类检查、编译器或 GPU，不能算负向 CUDA 合同已通过。
