# compute/optim 与 Studio 集成及 release 收束计划

本轮将 `studio/compute-optim-integration` 的 `b7cb8b69845d44d6cb0ec4d6b19f4aabb4c65b14` 合入 `compute/optim`。本地起点为 `a566310ed3327111356b4e4df10853f0f5faf8e1`，保留此前 O8 用户边界、RT/AMR 修正、Core RZ 契约和 Zenodo 文档。`main` 与已有 tag 不在本轮发布操作范围。

## 规则基准与完成标准

1. 优先采用最新已确认的 Core 契约及接口版本，而不是仅按提交日期选整份文件。新规则必须包含双方已验收功能；旧的正确约束不可因重构丢失。
2. 同一物理规则只由一个 Core 模块维护，配置解析、API、GUI 都从它取得结果。以最新配置完整性 v3 的显式必填、条件必填、退役参数拒绝和来源记录为基础；O8 已验收的用户边界规则迁入同一入口。
3. 两份规则冲突时，记录原版本、取舍、物理/接口理由及对应测试。无法证明合理性的变化保留待确认，不能借“更新版本”放宽门槛。
4. 合并提交仅代表集成节点。计划纳入 release 的功能必须完成实现、真实接线、错误/拒绝路径、演化与实际 checkpoint 续算，以及必要后端验收。私有 fixture 的通过不等于公共 simulation 可用。
5. 不完全的功能不能自动标为非阻断。若不能补齐，必须在正式 release 冻结前由项目明确调整范围，并保持代码拒绝、API 能力和文档一致；本轮不会自行把未完成项从 release 范围删去。
6. 本机以 CPU 编译与测试为主，不进行耗时 GPU 编译。历史 CUDA 证据原样归档；新集成代码的 CUDA 验证仍是后续独立签收项。

## 合并修正内容

| 模块 | 本轮处理 | 验收要求 |
| --- | --- | --- |
| 配置与 API | v3 统一校验、G 常数不可输入覆盖；保留 O8 用户 primitive 与势边界选项、case 源摘要及编译身份 | schema、stdin inspection、缺项/未知/退役输入拒绝一致；与真实启动一致 |
| 注册及 Setup | 同时绑定注册名称、源码身份与准备配置的 case 身份；Setup 使用检查后的不可变准备结果 | 错 case、修改受保护配置、错 species 来源明确失败 |
| 物理边界 | O8 独立 handler 继续作为唯一执行所有者；将来方已有、受公共门槛保护的 RZ chart/轴计划接到该 owner，消除旧内联 handler 重复 | 内置/O8 回归及候选 RZ seam 检查；公开 RZ/CUDA 门槛不解除 |
| 积分、AMR、扩散 | 保留 O8 通量观察、阶段修复与账本；结合新版 chart、reflux 和候选拒绝条件；显式区分 capture_budget 与 geometry_semantics 的参数位置 | CPU 编译、RK/RKL、AMR/reflux、边界收支；错误不发布完成状态 |
| 自引力 | 合并 O8 逐面政策与新版共享计算/候选作用域；保持周期背景处理与 Neumann 相容性独立；统一常数与真实阶段时间 | 原生 legacy/O8 残差和收支回归；候选不能进入正常物理输出消费者 |
| Studio | 保留真实 Linux/WSL CLI、项目/.par 生命周期、Controlled Build、初始生成、AMR、独立 Run/Restart 和 Plotfile 查询入口 | lint、typecheck、build、Studio/Host 测试、真实 CPU binary 接入 |
| 交付结构 | 生产 `src/host/desktop` 与 `tests/fixtures/validation` 分离；历史 Phase/QA 报告归档；入口 README 与索引链接当前指南 | 相对链接与脚本引用检查；发行目录不携带原始科学数据、测试缓存或开发历史报告 |

## RZ/Jeans 证据分类与 release 待闭环项

| 内容 | 收尾 checkpoint 状态 | 进一步必须完成的内容 | 负责人 |
| --- | --- | --- | --- |
| 私有 CPU 生产身份、重构/缓存、RK source 接线 | 报告提供 24 组短演化＋24 组真实 checkpoint 续算 PASS | 核对原始身份与已提交处理后摘要；完善 runner 对输入组合、binary/source 身份、失败原因的断言；集成后重新验证 | Core 集成，协作者补齐执行证据 |
| finite-ring 外源 | BLOCKED | 冻结且实现有证明的连续源/接触区误差界；生产绑定、消费者、演化/续算与预算不放宽 | Core 科学契约＋协作者实施 |
| 对称粘性应力/能量功 | NOT_RUN | 按已确认张量与能量功契约补齐真实路径；闭合/反例、AMR、完整演化与续算 | Core 科学契约＋协作者实施 |
| 轴邻格 | FAILED | 定位当前近轴阶数约 1 的原因，完成修正；原 1.8 阶门槛和多 Ω/网格/AMR/续算矩阵保持 | Core review＋协作者修正 |
| 连续面力参考 | BLOCKED | 独立连续参考及可证明误差上界，不能把 Gauss 16→32 的差作为证明 | Core 科学契约＋协作者实施 |
| 2D 性能诊断 | NOT_RUN | 真实物理核/type 绑定；RAM、swap、GPU 所有权、磁盘及下一次写入余量 guard；冻结预算后执行 | Core guard，协作者执行 |
| CUDA JENS | 私有 scoped 和 9＋9 的历史通过；公开门槛保持 | 公共接线、集成后的完整生命周期/续算与必要 CUDA 科学验收 | Core review＋协作者执行 |

上述事项不能统称为“RZ 已完成”。如果它们属于最终 release 的既定范围，每项都需要关闭后才可签收 release。

## 分阶段交付

| 阶段 | 输出 | 退出条件 |
| --- | --- | --- |
| I：合并与接口修正 | `compute/optim` 集成提交、冲突处理记录、保持不变的公开门槛 | 无冲突标记；CPU 正常构建；核心/API/Studio/Host 必需检查通过 |
| II：文档与交付目录 | 首页入口、当前功能矩阵、测试/历史归档索引、Linux 启动与复现指南 | 文档与当前代码一致；相对链接有效；生产包与验收材料分离 |
| III：功能闭环 | 上表未完成项逐项修正与科学证据；交互 UAT；正式 Plotfile 端到端验收 | 新功能可通过真实公共入口使用，拒绝路径与重启完整，无未批准回退 |
| IV：release 冻结 | 最终范围、版本、构建身份/依赖清单、完整测试报告、已知限制、迁移说明 | 所有纳入范围的功能完成；必要后端/平台验证完成；随后再审计合入 main 与打 tag |

## Plot / Preview 后续接口原则

完整 `plt_XXXX.h5` 是科学数据权威；Studio 只读。可选 XDMF 是第三方桥接，可重建 `archidx` 是查询索引，不复制一套默认 FP8/FP16 科学数据。viewport 查询使用有界分辨率/LOD与缓存，Inspector 回查 native leaf cell；完整文件关闭并发布后才读取。本地 worker 克制使用资源，远端保持 data-local。当前实现与此目标的差距必须由真实 writer/reader 对照测试和说明记录，不将旧 mock 或文件读取成功当成最终科研布局验收。

## 当前审批边界

2026-10-06，用户已明确授权“这两项兼容修正与 CPU 验证”：将已验收 O8 显式势边界迁入统一配置关系层；将来方已有的内部 RZ chart/轴计划接入 O8 唯一 handler。修正已应用，回归结果单独记录。公开 RZ 和 CUDA JENS 门槛保持，私有候选不自动成为公开功能；本轮不合入 main、不发布正式 release。

## Linux 桌面构建与完整交付要求

当前提供 `studio-cpu-release` 预设及 `arch-studio-assets` / `arch-studio-runtime` / `arch-studio` / `arch-studio-package` 组件。Node 24+ 用于源码构建；便携包包含 Host Node、h5wasm 和 Electron，Core 与系统图形/终端依赖仍单独提供。环境清单、Ubuntu 24.04 安装命令和本机 conda `work` 测试方式见[环境要求](../guides/StudioEnvironment.zh-CN.md)。本机已安装 xterm，使用真实 RunController 做短运行及真实 checkpoint 续算；不能把它扩称为整个 GUI 或 RZ 科学签收。

完整 GUI release 还须核对原生选择器、参数/绘图/AMR 交互与桌面关闭期间的真实作业行为。当前受控目录为 `build-studio-cpu`；任意现有编译树、缺少 binary 的完整工作台以及新未注册源码的编辑→编译流程仍须明确设计、真实接线和验收，不能把启动失败弹窗算作该工作流已完成。全部 Core 输入的界面支持要依据 v3 的真实 key、条件和能力响应验收；配置、初始化、场图、AMR、正式演化及 Plotfile 支持分别列明范围，不以模型注册数量替代完整签收。
