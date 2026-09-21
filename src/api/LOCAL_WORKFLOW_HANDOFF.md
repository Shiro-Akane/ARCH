# 本地工作流 API 扩展交接

交付到 `codex/studio-core-ui-contracts`，继续以 `origin/main` 的 `01cc4f723e674d47fe23850e7c0fef221e92e98c` 为冻结基线，在原 Core 接口提交 `5e96d4f004c9bd320fb232853d59b90006cdb0f2` 后增量实现。没有合入 Studio 分支历史。核对的前端 checkpoint 是 `studio/phase2f-plot-presentation` / `studio-phase2f-v0.13.0` / `e97e571ba98641384aee44425a29b83255401856`。

后续持续预览增量已整理为 [PREVIEW_SESSION_HANDOFF.md](PREVIEW_SESSION_HANDOFF.md)，接口见 [PREVIEW_SESSION_API.md](PREVIEW_SESSION_API.md)。在本文件记录的基础上接入会话、资源复用、阶段事件和图像版本管理；会话增量最终 CPU 验证 18/18 组通过。下方 15 组测试记录属于前一基础增量。

## 已可接入

- `--config-schema`：全部 90 个标准键新增 `presentation`，包含 displayName、description、subgroup；指定项提供 toggle、enabledBy。选项有 displayName，reflect/reflecting 合并为一个选项的 acceptedNames。
- `--inspect-config`：扩散模块和子项启用关系、系数来源范围、禁止显式出现的键、AMR 指标可用性。未执行 EOS 时标明 state-dependent，不假装已经计算实际导热率。
- 标准场、时间、长度和相关数值统一 CGS，包括 IdealGas。Sod `x_pos` 返回 cm，依据是现有明确坐标绑定，`automaticInference=false`。
- `--list-cases`：实际 binary 注册表、各项能力和编译模型源码指纹。
- `--inspect-case`：全部 11 个现有模型的真实 Setup/Get/组分读取、Init 少量采样、类型/默认值/来源、源码关联单位证据。统一入口供新增模型继续使用。
- `--preview`：场值追加 logDomain 统计，区分零值、负值、非有限值及正值范围；原始数据不变。
- `--amr-resources`：无需 Setup/EOS 的逐 Level 全域资源规模提示。
- `--preview-amr`：Linux/WSL CPU 上的真实 Sod 1D / CellularDet 2D 初始网格；包括叶块、2:1 层级、物理边界、单元尺寸、预算状态和资源信息。

准确调用、限制和返回字段见 [INITIAL_AMR_API.md](INITIAL_AMR_API.md)，展示字段见 [CONFIGURATION_API.md](CONFIGURATION_API.md)，模型检查见 [CASE_INSPECTION_API.md](CASE_INSPECTION_API.md)，实际响应见 [examples/local-workflow](examples/local-workflow/README.md)。

## Studio / Host 接续工作

1. 实现本地命令和独立桌面窗口；从 binary 查询模型，不把文件名当注册名。跟踪项目、源码和构建身份；处理未编译、源码变更、旧请求淘汰。
2. 消费 presentation 和适用状态整理表单：AMR 二级分组、所有标准项说明、枚举别名去重、关闭值开关。三个方向的 blocks 控件保持同级。
3. Diffusion 主开关控制整个子区；常量系数仅在对应通道启用且允许输入时出现。Helmholtz 启用扩散时，冲突常量键须通过可撤销编辑明确移除，不能只隐藏输入框。
4. 接入 AMR 叶块/单元线和资源表；limited 状态保留提示；预算不足不称为 OOM。沿用进程超时、取消、回收与响应身份检查。
5. 为全部注册模型接入 --inspect-case；根据能力区分参数检查、完整场图和 AMR 网格。读取单位证据，不维护前端模型单位表；模型检查时间预算独立读取（CPU 300 秒，Host wall 360 秒），允许用户取消。
6. 坐标轴和场值各自提供 Linear/Log、范围和截断。零值、负值、非有限值分别提示；Log 仅绘制正值，留空/遮罩无效值，曲线断开。Inspector 与源数组保留原值，切回 Linear 恢复完整显示。没有正值时显示无可绘制数据，不报成负数配置错误。

可直接转交的完整功能文档：[ARCH_STUDIO_LOCAL_LAUNCH_AMR_UX_REQUIREMENTS.zh-CN.md](../../docs/development/ARCH_STUDIO_LOCAL_LAUNCH_AMR_UX_REQUIREMENTS.zh-CN.md)。

## 单位与兼容性

配置扩展版本升至 2，外层 schemaVersion 保持 1.0。旧 configuration 示例已更新；Core A/B 历史目录保留当时快照，不用于判断本次单位行为。

IdealGas 无组分回退比热由旧 SI 数值 718 改为等价 CGS 数值 7.18e6 erg/(g K)。这会改变依赖该回退值的温度/能量关系；显式注册的 Cv 不自动缩放。Sod 仍显式使用 Cv=1，初始状态不变。独立 CPU 测试验证 300 K 对应比内能 2.154e9 erg/g 及反向转换。共享 CUDA 测试的参考常数同步，但本轮不编译或运行 CUDA。

本轮已在 `ProblemGenerator` 增加显式检查边界，并覆盖 11 个内置模型。单位集中在 `CaseUnitEvidence.cpp`，与实际编译 `.cpp` 的 SHA-256 匹配后发布；组分单位由 Core 读取器提供。新模型、源码变更、未执行分支和直接读取自定义 map 的路径有明确覆盖状态。数字类型检查只在观察器启用时执行，常规运行保留原读取行为。

这提供当前模型可用的单位结果和检查入口，并非任意 C++ 的自动单位求解。`automaticExpressionInference=false` 必须保留；回归测试验证仅凭相同参数读取和字段数值，不能区分 `rho=a` 与 `rho=a*b`（b=1）的单位要求。GUI 不必为此要求用户添加声明或逐模型适配。

CooperativeHotspots 的初始化会反复求等压状态。模型检查在同一请求中复用已验证的 EOS 来源，首次加载仍做前后内容校验，发布结果前再次校验所有来源；表内容改变则拒绝结果。避免每次迭代重复扫描整张表。正式模拟维持原有逐次内容核对，模型公式和 EOS 数值计算没有改写。新增来源校验测试同时检查请求结束后普通缓存失效行为仍有效。

API 按职责集中组织：ApplicationContract 管命令/版本/预算，StateSnapshot 共用状态响应，ParameterMetadata 共用读取记录与单位证据序列化，ValueDomain 共用 Log 统计，WorkerLimits 共用进程限制。默认值与科学规则仍归原 Core 模块管理。

## 验证与同步

CPU Debug，CUDA OFF、KLU OFF、OpenMP ON。2026-09-21 共 15 组 scoped CTest 最终通过：

- preview_initial_conversion、preview_api_contract、configuration_api_contract、preview_parameter_reads、preview_parameter_metadata、preview_sampling_limits、preview_cellular_2d。
- mainline_authority、refinement_indicator_math、amr_operation_plans、topology_transaction。
- ui_expansion_contract：9 项，含 Sod/CellularDet 与正式 CPU 初始化 t=0 的逐块对照、资源溢出和预算限制。
- initialization_probe：真实拦截点、观察器恢复、整数拦截、单位证据冲突/过期、非唯一反推、Log 统计和请求内来源校验。
- case_inspection_contract：6 项，覆盖全部 11 个编译模型、完整已观察数值参数单位、组分读取、1D/2D/3D、曲线坐标、错误来源与 Log 零值；最终约 48 秒。
- tabular_eos_ideal_gas：既有 3D/4D 数值回归、文件/组分缓存失效，新增检查请求中的来源变化拒绝及退出恢复。

二维完整回归约 454 秒通过；最后 EOS 请求作用域整理后又核对了两个方向的非方形初始场与直接 Init/EOS 参考。热点模型最初因重复扫描表超过工作进程预算，本轮加入请求内复用和末尾全文校验后，全模型检查通过。未运行 GPU 编译、CUDA 验证或 Studio UAT。

构建及测试记录保存在本机持久工作区的 build-ui-api 中：final-contract-tests.log、leaf-tests.log、api-tests.log、eos-tests.log、cellular-final-smoke.log、final-amr-tests.log。api-tests.log 含修复前热点模型的失败及二维完整回归；其修复结果以 final-contract-tests.log 为准。

已通过临时 Git 索引确认，本轮完整补丁可直接应用到上述 v0.13.0 checkpoint。

已有 `5e96d4f0` 的前端分支只需 cherry-pick 此后本轮新增提交，再重建 CPU binary。不要重复摘取 Core A/B，也不要合入整条前端历史。Host 的构建身份至少应覆盖新增 src/api、共享 src/driver/InitialMesh.h、src/amr/RefinementThermodynamics.h、注册模型、IdealGas/Species、EOSDispatcher/InspectionSources 及其实际依赖；建议以构建清单记录所有输入，而非手工固定短白名单。
