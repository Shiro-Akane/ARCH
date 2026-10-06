# compute/optim 与 Studio 集成验收记录

本次以 `compute/optim` 的 `a566310ed3327111356b4e4df10853f0f5faf8e1` 为本地起点，合入协作者 `studio/compute-optim-integration` 的 `b7cb8b69845d44d6cb0ec4d6b19f4aabb4c65b14`，并完成兼容修正、Linux 桌面依赖准备及 CPU 验证。集成工程检查通过；正式 release 仍须按[收束计划](ComputeStudioReleasePlan-20261006.zh-CN.md)完成剩余功能与科学验收。本次不合入 `main`，不创建 release tag，也没有进行 CUDA/GPU 编译。

[机器可读验收摘要](ComputeStudioIntegrationSummary-20261006.json)记录本轮实际数量与未完成状态；各测试组有交集，不计算相加后的“总验收数量”。

## 规则与兼容修正

采用最新已确认的配置完整性 v3，同时保留 `compute/optim` 已验收的 O8、RT/AMR 和边界规则。41 个内容冲突按模块处理，没有直接用整份旧文件或新文件覆盖另一侧功能。

| 内容 | 最终处理与验证 |
| --- | --- |
| 输入与 API | 单一标准键目录、显式/条件必填、未知/退役参数拒绝、case 输入与编译源码身份；活动目录为 95 个标准参数，注册模型为 16 个。数量来自实际 CPU 响应，客户端不得硬编码 |
| O8 用户边界/势 | 将 `user`、`dirichlet`、`neumann` 及适用几何的已验收规则迁入统一关系层；用户已授权本项兼容修正。`gravity_G` 等退役输入继续拒绝 |
| 注册与 Setup | 注册名称、源码摘要和准备配置的 case 身份一致；函数式 O8 注册增加配置声明，保留受保护配置及 species 来源检查 |
| 物理边界 | O8 handler 保持唯一执行所有者；已有且受公共门槛保护的内部 RZ chart/轴计划接入该入口，删除旧重复执行路径。用户已授权此修正；未解除公开 RZ 门槛 |
| 数值接线 | 合并 RK/RKL、通量观察、AMR/reflux 与共享重力路径；纠正 `capture_budget` 和 geometry 参数顺序，保持阶段修复和账本 |
| 初始化 Preview | O8 两种函数注册的已审阅域加入真实初始化/AMR入口；修复 RZ 边界解析早退遗漏共享条件填充的问题。初始化通过不等于完整 RZ 演化签收 |
| 旧测试输入 | 按 v3 补齐原有效值：HLL 波速、Sod 位置/零速度、平滑波模式、外源零横向分量、扩散示踪参数，以及原 `EntropyFix=true`、系数 `0.1`。仅迁移 fixture；物理参考、容差、尺度、算法与终点保持 |
| 私有 RZ runner | 加强 binary/source/输入身份、24 个唯一组合、有限误差、真实失败分类及拒绝后的状态恢复检查；离线反例测试通过。私有生产补丁仍未应用到公开源码 |

CPU API 示例及输入身份见[当前响应示例](../../src/api/examples/compute-studio-integration-20261006/README.md)。旧 v1/v2 交接材料保留原历史范围，当前实现以选中 binary 的 v3 响应为准。

## 本机环境与构建

验收环境为 Ubuntu WSL，Core、Host、Electron 均运行在 Linux 侧，图形显示使用 WSLg。

| 组件 | 本机实际情况 |
| --- | --- |
| 科学 Python | conda `work`，Python 3.12.14、h5py 3.16.0、NumPy 2.5.3 |
| 工具/资源保护 Python | `/usr/bin/python3`，具有 `pidfd_open`/`pidfd_send_signal`；已补齐系统 NumPy/h5py。`work` 缺少这组进程接口，完整 Tooling 不使用它 |
| Linux Node / npm | Node 24.21.0、npm 11.19.0；持久安装在用户目录 `.local/opt/node-studio`，官方 archive 校验及许可证已保留 |
| Electron | 锁定 44.4.3，实际安装；当前 binary 的共享库均能解析 |
| 独立终端/开发检查 | 已安装 xterm 390 与 shellcheck；flock、mold 可用 |
| CPU 构建 | `build-release-cpu` 用于测试；`studio-cpu-release` 生成 `build-studio-cpu`，与 Host 受控构建配置一致 |

新 `ARCH_BUILD_STUDIO` 默认为 OFF。启用后的 `arch-studio-assets`、`arch-studio-runtime`、`arch-studio`、`arch-studio-package` 分别管理锁定依赖/生产资源、Electron、Core 与 CLI、Linux 便携包。即使本机 npm 禁用安装钩子，Electron 仍由明确构建步骤准备。

便携包包含 Host Node、h5wasm、Electron 和生产资源，保留第三方许可证。Core、系统图形库与独立终端仍是环境依赖。包在仓库外使用自己的 Node 和 h5wasm 通过验证；开发版本为 `0.0.0`，尚未作为正式发行包签收。详细安装与排障见[环境要求](../guides/StudioEnvironment.zh-CN.md)。

## 独立验证结果

| 检查 | 结果 | 范围与限制 |
| --- | --- | --- |
| CPU Release 构建 | PASS | CUDA OFF；两并行编译任务；Core 与全部当前 CPU 测试目标实际编译 |
| Core CTest | 76/76 PASS | API、完整模型初始化、O8、边界/重力、扩散、AMR、续算等配置目标；81.29 秒。含 34 项私有 runner 离线检查，不额外重复累计 |
| Tooling | 482/482 PASS，零 skipped | 系统 Python；包含配置预设、资源保护及工程规则检查 |
| 冻结低密度物理矩阵 | 72/72 PASS | 真实 CPU 演化；平滑波阶数、稀疏波、外源、AMR、守恒/修复、续算、密度对比、多通量方案和 RKL 扩散。原 manifest 未放宽 |
| Studio / Host | 338/338 PASS，零 skipped | 完整 `npm test` 已包含 Host；不再把 Host 子集重复相加 |
| lint / typecheck / production build | PASS | Linux Node 24，生产资源及便携包实际构建 |
| 结构/CI 检查 | PASS | architecture audit；76 项 CTest inventory 锚点核对；CI 锁定 actionlint 1.7.12 含 shellcheck |
| 真实桌面启动 | 两条路径 PASS | CMake 生成的 CLI、仓库外便携包均出现 Electron 窗口并接入真实 CPU Core；Host health protocol 1.3 ready。关闭测试自有进程后 Host 正常退出 |
| 包外 HDF5 读取 | PASS | 仓库外包使用自身 Node/h5wasm，读取实际 Core 产生的 metadata 及一格 DENS。reader 仍如实返回 candidate/review 状态；不据此认证科研布局或所有 Plotfile 可渲染 |
| 真实 Run / Restart | PASS | 正式 RunController 使用真实 xterm 与 CPU Core；Sod 运行至 step 4、`t=1e-5`，从 step 2 checkpoint 续算；最终 21 个数据集逐位一致，终点属性一致 |

最终 CPU、构建和矩阵任务使用内存/进程保护；观测 swap 基线和峰值均为 0，保护器未触发。采样不能证明采样间隔内绝无瞬态压力，也不是容量或性能认证。

初次 Tooling 使用 `work` 时发现 13 项保护检查 skipped，补齐系统环境后完整重跑通过。低密度矩阵先后按缺项拒绝发现旧 fixture 的 Sod 零速度及 Roe 熵修正输入，失败目录和日志保留；只有最后全新目录完成 72 项并写出 PASS。这些旧输入迁移没有增加 Core 默认回填。

原生桌面测试证明窗口、真实 Host/Core 和自有进程退出；Run/Restart 证明正式 controller 与独立终端路径。它们没有替代完整 GUI 点击、原生文件选择器、缩放/色标/AMR 显示组合以及关闭桌面期间作业行为的人工 UAT。

## 生产内容与验收材料归类

| 位置 | 当前用途 |
| --- | --- |
| `studio/src`、`studio/host`、`studio/desktop` | 前端、Host、Linux 桌面及打包的生产实现 |
| `cmake/studio` | opt-in 桌面构建与 CLI 模板；普通 Core 编译不依赖 npm |
| `src/api` | Core 配置、能力、初始化、AMR 和 session 接口；示例为小型 metadata/输入 |
| `studio/tests`、`tests` | 必须保留的自动验证；不是发行运行时资源 |
| `studio/tests/fixtures/sod-1d.h5` | 保留的 14 KiB 历史真实 CPU Sod reader 回归 fixture，来源记录随文件保留；不进入生产包，不代表新增科学验收 |
| `studio/tests/tools` | checkpoint 比较与历史样本导入等验证工具，已从生产 scripts 入口分出 |
| `validation` | 科学参考、冻结输入、runner 和经处理的证据；大型 HDF5、完整数组及原始日志留在执行机器 |
| `studio/docs/contracts` | 当前 Host Plotfile 约束 |
| `studio/docs/archive` | 58 份阶段/QA/UAT 历史报告及旧入口记录，保留范围和版本；不作为当前启动说明 |
| `docs/guides/Studio*` | 当前操作、安装和排障要求；首页 README 与文档索引已添加入口 |

报告移动后相对链接和验证脚本路径同步处理；发行 staging 仅复制生产资源和必要 Host 源码，排除测试树与历史文档。既有 Zenodo 内容、专用 GNN 分支及其他工作区未在本次清理中删除。

提交前既有密钥扫描识别了六个非认证 session UUID、一个历史 Git ref 和一个文件摘要。公开摘要中的 UUID 改为跨文件一致的 `session-id` 标签，保留相等/不同进程关系；文件身份改用 `path`/`sha256` 记录，原摘要值保持。历史 Git ref 原值保持。扫描器、独立策略与 hook 未改动，重新扫描通过。

## 历史/私有科学证据与 release 必须补齐的内容

协作者报告的私有 CPU 24 次演化＋24 次真实续算及私有 CUDA JENS scoped / 9＋9 通过，保留原 artifact 身份与证据归属。这些数据在对方机器上，不写成本机公共路径已重新通过，也不能解除公开门槛。

| 项目 | 当前状态 | release 退出要求 |
| --- | --- | --- |
| finite-ring | BLOCKED | 独立连续源/接触区参考与可证明误差界，完成真实生产消费者和演化/续算 |
| 对称粘性 | NOT_RUN | 真正应力及能量功路径、反例、AMR、演化/续算全部验收 |
| 轴邻格 | FAILED | 纠正约一阶结果，原 1.8 阶门槛保持，完成多网格/Ω/AMR/续算 |
| 连续面力 | BLOCKED | Gauss 16→32 的差不是误差证明；必须有独立参考及误差上界 |
| 2D 性能诊断 | NOT_RUN | 真实核/type 所有权、完整资源 guard 和冻结预算先完成 |
| 公共 CUDA JENS / 新集成 CUDA | 未签收 | 保持公开拒绝，完成公共生命周期、续算及必要 GPU 科学验证；本机未构建 CUDA |
| GUI 全流程 | 部分验收 | 原生交互 UAT；任意现有编译树、缺 binary 的完整工作台、新未注册源码编辑→编译→预览流程仍需真实闭环 |
| Plotfile 下一阶段 | 局部只读候选 | 完整 plt 为权威；metadata/XDMF、可重建索引、viewport LOD、有界缓存、native cell Inspector、完整发布和真实 writer/reader 对照验收 |

所有 Core 功能的界面支持按输入、配置、初始化、场图、AMR、正式演化和 Plotfile 分别验收；注册数量不能替代功能覆盖。上述未完成项如果在 release 范围内，必须完成，不能默认当作非阻断或自行删去。

## 合作者同步与接下来分工

协作者先追 `origin/compute/optim` 的本次两父提交集成节点，阅读[当前指南](../guides/Studio.zh-CN.md)、[环境清单](../guides/StudioEnvironment.zh-CN.md)、[API](../../src/api/README.md)、[实际响应示例](../../src/api/examples/compute-studio-integration-20261006/README.md)及[release 计划](ComputeStudioReleasePlan-20261006.zh-CN.md)。重新配置/编译 CPU，不能沿用旧 binary 的 schema、源码身份或旧测试数量。

Core 负责冻结科学参考/误差界、修复与验收物理接线、公共能力及资源 guard；Studio/Host 负责原生交互、构建/作业生命周期、能力驱动的全部参数呈现和受支持 Plotfile 客户端。执行侧保留完整原始数据，提交准确身份及经处理证据。各项依赖满足后并行推进，最终逐项签收，再审计合入 `main` 和冻结 release 版本。
