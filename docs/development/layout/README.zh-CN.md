# P1 目录分类与头文件约定

2026-09-22，`physics/selfgravity`。目录整理基准为 `c95f606f5805b42f92611847e00d1fa282c82fbe`，
其 [P1 GPU 数值与设备验证](../../../validation/gravity/results/selfgravity-p1-gpu-20260921/README.md)
已经完成。本轮只处理文件位置、引用、构建接线、文档及因算例文本变化必须重核的源码摘要。
仍停在 P1；Poisson/MG、自引力耦合和性能优化均未实施。

## 分类规则与落点

超过 8 个直接文件触发职责检查，不作为硬性数量上限。相关内容共同维护，公开入口保持稳定。
共搬迁 294 个已有文件；完整的旧路径到新路径对应关系见 [moves.json](moves.json)。

| 原区域 | 当前职责目录 |
|---|---|
| `src/amr` | `topology` 拓扑/身份、`storage` 块/池、`exchange` ghost/边界计划、`transfer` 重网格传递、`flux` 通量修正、`refinement` 判据；AMRControl 留在入口层 |
| `src/driver` | `runtime` 生命周期/事务、`stages` 阶段绑定、`schedule` 调度契约、`io` 输出、`initialization` 根初始化；Driver/SolverDispatch/DriverUtils 保留共同入口 |
| `src/driver/dispatch` | `bindings` 积分器绑定、`capability` 解析与能力；PolicyDescriptor 保留入口 |
| `src/api` | `configuration` 参数元数据、`preview` 采样/快照、`session` 会话、`inspection` 模型检查、`protocol` 请求/响应、`resources` 工作进程限制；公开 API 声明保留原路径 |
| `src/core` | `config` 配置、`files` 文件身份、`problem` 注册/初始化；可移植性与补偿求和保留基础入口 |
| `src/cuda/hydro` | `kernels` 执行核、`boundary` 边界/交换、`policies` 数值策略绑定 |
| `src/cuda/microphysics` | `eos/owners` 表所有权、`network` 网络/组分资源、`burn` 稀疏批执行、`linalg` cuDSS/均衡化 |
| `src/cuda/runtime/burn` | `dense`、`sparse`、`dispatch`；routes 下按 `tabular3` / `tabular4` 归类绑定 |
| `src/numerics/burnsolver` | `ode` 积分/继续执行状态、`coupling` 热力学/网络导数；公开派发保留入口 |
| `src/physics/eos` | `tabular` 数学/视图、`sources` 加载/补全；基础 EOS、IdealGas、Helm 保留共同入口 |
| `tests` | Host/CUDA 按 amr、hydro、burn、eos、network、io 等职责分组；fixtures/math 对应归类；API 按 configuration/preview/session/inspection 分组 |
| `tests/tooling` | architecture、build_tools、microphysics、network、resources、validation；保留递归发现所需的包标记 |
| `cmake` | project、cuda、dependencies、templates、tests；CustomNetworks 保留共同模块入口 |

保留超过阈值的合理例外：

- `tools/` 与 `validation/network/` 是现有 CLI/import 入口；继续保持冻结验证配方、外部脚本和模块导入兼容。以后提取内部实现时再分类，不为数量阈值新增一层转发脚本。
- `diffusion`、`timmes_common` 为 8 个相关代码文件加说明；EOS 入口层也为 8 个代码文件。来源数学不拆分，只规范其项目头文件引用。
- API 根的公开声明/接口说明、历史响应示例、输入矩阵、results/archive 和已有文档入口保留。未跟踪 Studio 工程与第三方原始资料不在本轮改写范围。

## 引用约定

| 使用方 | 写法 | 搜索根 |
|---|---|---|
| 用户 `case.cpp`、内置 simulation 算例 | `<UserInterface.h>`、`<GlobalDefs.h>` | 仓库 `include/` 的稳定公开入口 |
| 项目内部 C++/CUDA | `"amr/topology/AmrTree.h"` 等 | `src/` |
| 测试共享辅助体 | `"fixtures/eos/HelmReference.h"`、`"math/geometry/CurvilinearMetricCases.h"` 等 | 仅测试目标的 `tests/` |
| 测试复用算例体 | `"Cellular/Cellular.cpp"` | 仅对应测试目标的 `simulation/` |
| 标准库/第三方库 | `<vector>`、`<cuda_runtime_api.h>` 等 | 编译器及依赖目标提供 |

根目录相对写法比 `../../../` 更不依赖调用文件层级；源码内禁止写死 `/home/...`、盘符或用户目录。
`<>` 本身不会自动定位项目：CMake 的 `arch_build_contract` 传播 `include/`、`src/` 和生成目录，
测试根仅附着于测试目标。用户换机器/移动检出目录后重新配置 CMake，不搬用旧 CMakeCache。

两个公开头只是转发到唯一实现，不复制声明/宏，也不是旧内部路径兼容层。
它们保持用户入口稳定，后续内部目录再变动只需更新转发目标。当前不是可独立安装的 SDK；
算例仍通过 ARCH 工程构建。生成网络包保留包内相对引用；CMake 按本机构建生成的注册头
可以含已解析的外部包绝对路径，这些构建产物必须重新生成，不提交为可搬运源码。

包含顺序、条件编译和实现主体保持不变。标准库、第三方、冻结证据以及测试中故意无效的
include 反例不机械改写。规则依据 [GCC 搜索路径](https://gcc.gnu.org/onlinedocs/cpp/Search-Path.html)
与 [CMake 目标 include 目录](https://cmake.org/cmake/help/latest/command/target_include_directories.html)。

## GUI、CGS 与接续

本分支已继承 GUI Core 的 `97a2b50c` / `d98f6f6e`：全系统 CGS，包含 IdealGas；
回退比热为 `7.18e6 erg/(g K)`。沿用已有物理常数和单位元数据，不增加另一套单位切换/转换层。
未知 custom 参数仍需其物理含义的单位依据；全局 CGS 不能推断任意字符串键的量纲。

11 个内置算例的旧源码摘要先与基准逐一核对，再确认非 include 文本完全一致后更新
CaseUnitEvidence 摘要；[对应记录](evidence/case-evidence-path-refresh.json)保留前后身份。
单位映射、Setup/Init 公式、API schema 和 session 契约不改。GUI 分支合并如涉及内部文件，
依据 moves.json 迁移其新增改动，不复制旧实现回原目录；公开 API 头路径仍可使用。

## 验收与边界

- 全部已配置 CPU 目标编译成功：Debug/GCC 13，OpenMP ON，CUDA/KLU OFF，4 路构建；
  最终构建 179 步，45.707 秒，内存保护未触发、采样未见 swap 增长。
  [构建日志](evidence/cpu-public-headers.log)。这不是运行性能比较。
- [实现核对](evidence/source-integrity.json)：393 个变动 C++/CUDA/模板文件、1,320 条项目 include；
  引用目标不变，实现主体除已审核的 11 条源码摘要外无变化。新增两个公开转发头单独登记。
- 现有工具检查 355/355、零跳过；[工具日志](evidence/tooling-complete.log)。没有修改数值断言或放宽预算。
- [CTest 清单核对](evidence/inventory-integrity.json)：CPU 53→53、CUDA 128→128，名称、属性和原已存在命令保持，只有必要的脚本路径迁移。CPU 初次清单中未编译的 32 个命令现已可见。
- CUDA 仅完成 [配置和源文件接线核对](evidence/cuda-configure-final.log)，本轮没有重新编译/执行 CUDA。
  按用户最新要求，目录和 include 整理以 CPU 编译验收；P1 GPU 运行证据仍绑定整理前提交。
- 未重跑 GUI 既有专项测试和 CPU/GPU 数值对照。远端自动 CI 仍按原规则执行完整工具和 CPU 清单。

[主实施计划](../SelfGravityImplementationPlan.zh-CN.md)与 [P1 交接](../SelfGravityP1Handoff.zh-CN.md)
共同约束后续阶段，当前没有进入 P2 的授权。
