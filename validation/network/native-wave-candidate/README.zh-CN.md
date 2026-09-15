# 大网络原生批处理候选：开发中，未编译／未启用／未验收

2026-09-15。本目录为隔离执行层候选，不在生产 CMake、网络注册表或当前正式计时中启用。
服务器仍运行已冻结版本；本地没有 C++／CUDA 编译器，当前只有代码准备，不能称合同测试通过。
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
当前十二项生成器测试通过，见 `overlay-recipe-tests-v2.log`；十三文件准备记录见
`overlay-preparation-v2.json`。初版记录和十一项旧机械测试日志保留为历史证据。
生成器测试只验证 SHA、输入依赖清单、重写边界、输出记录和拒绝覆盖等机械合同，
不证明 C++ 可编译、ODE 行为正确或实际加速。

测试的上传显式完成后才调用候选，保证异常分支中的 Host 输入寿命；
这是正确性测试设置，不计作性能结果。测试 provider 的析构先于借用的设备数组，
其完成屏障也不能被 test fixture 的销毁顺序绕过。

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

只在当前四组耦合计时和原 BE 长轨迹补测结束后，确认服务器空闲，再执行。
必须外包现有 `tools/run_memory_guarded.py`（Host headroom 32768 MiB、swap growth 64 MiB、
pressure guard、GPU memory observation），以及 10800 秒总 wall guard；一次只运行一个 GPU 作业。
runner 参数为 `--build-dir`、`--recorded-link`、`--payload`、`--shared-manifest`、
`--helper-library` 和 `--output-dir`。它不自动修改当前队列，也不自动启用生产 SparseOdeBatch。
