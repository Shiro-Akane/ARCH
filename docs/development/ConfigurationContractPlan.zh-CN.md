# 配置完整性与缺失输入重构计划

2026-09-28。基于 v1.2.1，作为[计算优化后续计划](ComputeOptimizationPlan.zh-CN.md)
中O7.0的原始设计。当前已有受控`PreparedConfiguration`、配置扩展3、nullable诊断、G退役和用途响应的实现及分项验证；本文件保存参数条件、允许默认及原迁移要求，不能作为重新构建配置系统的清单。
2026-10-01 补充[Core／Studio 联合交付计划](StudioConfigurationHandoff.zh-CN.md)：
由一位实现负责人贯通配置与客户端，维护者保留科学规则和最终验收。
本文继续作为参数条件和允许默认的设计清单；运行权威是现有Core注册表和所选binary的正式响应，客户端不复制另一套必填判断。当前任务与状态见[O总表](ComputeOptimizationPlan.zh-CN.md#当前执行校准2026-10-08)，证据见[release记录](ComputeStudioReleasePlan-20261006.zh-CN.md)。

## 1. 选择：独立记录缺失，不用数值占位推断

本计划采用第一种方案，调整配置数据的构造和加载契约，独立保存参数是否已提供。
关键物理输入必须有明确来源；缺失、格式错误、未知选项与合法零值分别处理。
有依据的算法内部默认、真正的派生量和显式自动策略可保留，均应能追溯其来源。

| 方案 | 能解决什么 | 主要问题 | 结论 |
| --- | --- | --- | --- |
| 全部改为 0／false，遇到这些值报错 | 部分正数参数会较早失败 | 合法零加速度、原点、非活动轴、禁用开关也被误判；false 可能直接关闭待校验模块；其他层仍能回填；错误只显示“超出范围” | 不采用为缺失检测机制 |
| 在 GlobalDefs／加载器区分未提供与已解析配置 | 能指出缺少哪个键、何时需要、由谁提供 | 必须同步加载、Setup、API、直接 C++ 构造和测试 | 采用；围绕现有注册表和解析入口实施 |

存储可以安全初始化，但初始化值不能证明输入完整。也不使用 NaN、-1、空串或
`validated=true` 充当通用缺失标记；这些值有自身语义，单个可变标志还可能在修改后失效。
运行数学继续接收完全解析的普通数值，配置缺失检查不进入单元循环或 CUDA kernel。

## 2. v1.2.1设计基线的路径

以下为2026-09-28设计时的源码快照，用于解释迁移原因；不是当前行为或未完成任务清单。现有实现与最终科学验收分别核对。

| 位置 | 当前行为 | 重构要求 |
| --- | --- | --- |
| [GlobalDefs](../../src/data/GlobalDefs.h)、[标准参数表](../../src/core/config/StandardParameters.h)、[RuntimeParams](../../src/core/config/RuntimeParams.h) | 全部 95 个标准键都有 fallback；缺项先填值，随后只见一个有效数值 | 必填性与数值合法性分开，去掉关键字段的可运行隐式默认 |
| Grid／Physics 默认 | 默认 3D 单位立方域、outflow、IdealGas、gamma=1.4；燃烧／扩散默认关闭、gravity=none | 不能由遗漏输入决定几何、材料或是否包含物理模块 |
| IO 默认与 [DriverControl](../../src/driver/schedule/DriverControl.h) | tmax=0，校验接受非负终点，时间控制可立即判为到达终点 | 正式演化必须明确终点；初态检查与缺失终点分别处理 |
| [PolicyDescriptor](../../src/driver/dispatch/PolicyDescriptor.h) | 未知 solver／reconstruct／limiter／time_integrator 分别回退 HLLC／PCM／MinMod／RK2 | 所有显式未知方法报错；合法别名与显式 auto 单独保留 |
| [方法分发测试](../../tests/host/driver/test_resolved_execution_plan.cpp) | 现有断言要求上述未知方法回退成功 | 改成拒绝契约并保留已知别名／真实方法覆盖；属于语义变更，不是放宽科学门槛 |
| [ConfigParser](../../src/io/ConfigParser.h) | 已提供的数值／布尔格式严格；但无等号行被忽略、重复键由后值覆盖，缺少行号来源 | 保留严格数值解析；语法错误、重复和缺失分别诊断 |
| SimConfig::Get | 缺少 case 参数时使用调用处默认；标准键也保存在 custom 映射 | 必需 case 参数不能靠默认补全；同一标准键不能产生第二权威 |
| SimConfig::Get<int/bool> | 存在 parameter_reads 时检查精确整数／范围；普通数值读取路径直接 static_cast | 类型检查移入共同生产读取路径，不能仅在 preview 跟踪启用时有效 |
| [初始化网络组成](../../src/physics/network/timmes_common/TimmesNetworkSupport.h) | 全缺失／零和组成已有错误；指定核素以外的组分从零开始再按现行规则规范化 | 保留已有失败和组分语义；审查拼错核素名，不声称它目前会凭空补出纯物质 |
| [配置 API](../../src/api/configuration/Configuration.cpp) | 能报告 explicit/default；未知方法目前只给 POLICY_FALLBACK 警告 | 缺失输出 null 和结构化错误；与真正运行共用判定 |
| [main](../../src/main.cpp) | 加载后建立输出／日志，再调用 Setup；Setup 不在现有加载／分发异常处理块内 | 尽可能在建立资源前汇总错误；Setup 错误统一捕获，以明确非零状态退出 |

ConfigParser 在打不开文件时仍打印 “Using defaults”，但 RuntimeParams 随即抛错；
这里是误导诊断，不是已确认的文件缺失后继续运行。应同批修正文案。
其他算法中的数值降阶、拒步、AMR 正值保护和合法后端自动选择不因名称含 fallback
就一并删除，它们应按各自的数学契约审查。

## 3. 参数分级与清单

从设计基线的95个标准键出发，拟定 19 个常规必需项、50 个条件必需项、
25 个允许记录默认的辅助／算法项，另 1 个 gravity_G 退役。
“必需”表示必须由输入或受登记的明确模型定义提供；不是要求用户把同一模型公式抄到 .par。
模板可以提供推荐值，但推荐值不会在缺失时自动生效。

### 3.1 常规必需：19 项

| 职责 | 参数 | 提供要求 |
| --- | --- | --- |
| 几何与维度 | `geometry`、`nblockx1/nblockx2/nblockx3` | 三个轴计数显式给出，0 可用于非活动轴；dim 由它们推导 |
| 数值方法 | `solver/reconstruct/time_integrator/cfl` | 明确所用方法和稳定性控制；未知名字不可降为默认 |
| 后端 | `compute_backend` | 明确 cpu／cuda／auto；auto 的真实解析结果和原因记录 |
| 物理模块 | `eos_type/use_burn/use_diffusion/gravity_type` | false 和 none 是合法明确选择；缺项不是“关闭” |
| 网格层级 | `lrefinemin/lrefinemax` | 显式声明均匀层级或动态 AMR 范围 |
| 正值／状态边界 | `sml_rho/min_eint/max_eint` | 沿用已有参数和修复账本，值须适合该科学问题的 CGS 状态域 |
| 终点 | `tmax` | 正式演化明确目标物理时间；max_steps 只作上限，不能替代科学终点 |

仅检查配置的接口可返回缺项而不演化。明确请求初态预览或使用现有初始化检查入口
可以没有演化终点；不能让缺少 tmax 的普通运行被当作成功的零步模拟。
重启时区分 tmax 与检查点当前时刻，目标早于当前状态应给出明确诊断。

### 3.2 条件必需：50 项

| 条件 | 参数 | 边界 |
| --- | --- | --- |
| 活动坐标轴 | 六个 `x1/2/3_min/max`、六个 `x1/2/3l/r_boundary_type` | 只要求活动轴；显式提供的非活动字段仍检查类型与词法 |
| 选定方法确实消费该控制 | `limiter/EntropyFix/hll_wave_speed` | 分别按重构／Roe／HLL 系列能力判定，不能按任意方法名猜测 |
| 对应材料闭合 | `gamma/eos_table_path/eos_coulomb_mult` | IdealGas 的 gamma、Tabular 的表、Helm 的 Coulomb 选择；不替换成另一种 EOS |
| 需要网络或核素组成 | `network_name` | 不仅取决于 use_burn；非燃烧 Helm 初态也可能依赖网络组分，none 须为明确模型选择 |
| 燃烧启用 | `nuclearTempMin/nuclearDensMin/enucDtFactor/use_nse/ode_solver/ode_rtol/ode_atol/dt_init` | 物理激活、能量限步、NSE 选择及误差目标明确；保留两个 burn 半步 |
| 使用温度／组分下限 | `smallt/smallx` | 依实际 EOS／组成／burn 消费关系触发，不另添每个算法的小值参数 |
| NSE 启用或显式 auto 将其启用 | `nseTempThreshold/nseDensThreshold` | auto 先核对能力，再决定要求；显式 true 对不支持网络继续报错 |
| 扩散启用 | `diff_integrator/diff_cfl/use_thermal_diff/use_viscous_diff/use_species_diff` | 三个通道选择必须明确；不能默认全 false 后称已开启扩散 |
| 对应输运通道启用 | `nu_visc/alpha_therm/D_spec` | 有效常数材料需显式系数；已选 EOS 输运可由模型定义供给，0 委托只有在该通道真实支持时才有效 |
| self gravity | `gravity_boundary/gravity_rtol/gravity_atol` | 边界和精度目标必需；atol=0 是合法的显式值 |
| external gravity | `gravity_g_x/gravity_g_y/gravity_g_z` | 需要的向量分量完整声明，零分量合法；不猜缺失方向为无引力 |
| 动态 AMR | `regrid_interval/refine_var/refine_threshold/derefine_threshold` | 曲率阈值只在有曲率指标时要求；JENS 另用自己的格数条件 |
| restart=true | `restart_file` | 缺少、不可读或身份不相容分别报错，不改为新启动 |

实现时把条件放在现有能力／字段所有者中供运行和 API 共用。
启用子模块之前先确认其开关已提供；不能把 missing bool 解释成 false 后避开必填检查。
关闭模块不要求填写整套无效控制，但任何已提供的未知枚举、坏类型或矛盾请求都应被指出。

JENS 新增的 `jeans_cells` 也属于条件必需，替换前案“缺省 8”的加载方式。
8 仅可作为待验收模板推荐值；已启用 JENS 而没有分辨率目标时直接报缺项。
因此 G 退役后共 94 项，加入 jeans_cells 回到 95 项；这一重构不新增运行模式开关。

### 3.3 允许明确默认：25 项

| 所有者 | 可保留参数 | 限制 |
| --- | --- | --- |
| 资源与执行 | `max_blocks/cuda_device` | 容量不足／设备不可用仍明确失败；cuda_device=0 合法 |
| 时间步内部控制 | `dt_min/dt_max/tstep_change_factor` | 由已有算法默认所有者提供并记录；不改变现有步长数学或把它们都改零 |
| 数值方法内部控制 | `EntropyFixCoefficient/linear_solver` | 记录系数与 Auto 的解析来源，未知 token 不触发自动选择 |
| ODE 内部工作控制 | `ode_max_newton_iter/ode_max_substeps/ode_dt_safe_fac/ode_dt_fac_max/ode_dt_fac_min/ode_initial_dt_frac` | 复用已验收的内部默认；迭代不足仍报失败，不能以默认当作无限重试 |
| 椭圆／扩散工作上限 | `gravity_max_cycles/diff_max_stages` | 单一默认来源；不能截断后伪装收敛 |
| EOS 配套路径 | `eos_helm_table_path` | 可使用已登记的随库表；缺失或身份错误要失败，不更换热力学模型 |
| 非物理输出／执行上限 | `max_steps/out_dir/base_name/plt_dt/plt_dstep/chk_dt/chk_dstep/restart/plt_variables` | 默认可追溯；达到步数上限而未到目标要区分完成状态，重启不能自动开启 |

这些默认属于具名、受检查的允许集合；模板推荐与运行默认分开表达。
只允许对缺少的可选值应用已声明默认，用户显式填错时仍报错，不能退回该默认。
状态修复的物理下限已列为必需，机器舍入常数／内部防除零条件仍封装在算法里，
不把内部常数推广成用户必须填写的几十个新参数。

### 3.4 算例参数与数据资产

- 初态密度、温度／压力、材料、尺度、扰动及边界数据按算例声明关键性。
  核心参数不再在 custom map 中重读出另一份默认；显式输入不能被 Setup 静默覆盖。
- `case.cpp` 中明确固定的材料或解析模型常数属于模型定义，记录其来源与源码身份；
  可编辑的“缺少就用某个值”属于回填，应改为必需项或有说明的可选项。
  标准示例的推荐初值写入现有 .par，不能依靠隐含参数组成科学模型。
- 组分允许稀疏指定时，未指定核素为零须是已声明的输入契约；全缺失、非法核素、
  非有限／负值及不相容组成继续失败。保留现行 smallx 规范化与修复记录，不借此改燃烧数学。
- 任意新 case 参数按同一所需性与用途规则登记；用途为 verification 不豁免必填检查。
  现有 `log_dir`、组分键与用户扩展也要登记消费方，不能只审查 95 个 Core 键。
- 表文件、网络包和 checkpoint 检查缺失／内容／版本身份。已明示的随库资源定位
  可保留；无可用资源时不能静默改用 IdealGas、其他网络或从头运行。

## 4. 配置生命周期与文件归属

复用现有加载／Setup／分发链，形成下面的边界：

`原始输入与位置 → 条件需求解析 → case 准备与派生 → 完整校验 → 只读运行配置 → 现有后端视图`

- [ ] 在现有 `StandardParameters.h` 扩充 required／条件／允许默认元数据；
  必需项没有运行 fallback。推荐值另作模板元数据，不建立第二套参数注册表。
- [ ] `ConfigParser` 保存输入存在性、原始 token、文件／行位置；拒绝坏行、空键和重复键。
  不同时保存几份可相互覆盖的数字和字符串。未知键先等待所选 case／核素消费声明，
  确认未识别／未消费后报错并给出可能拼写；禁止自动纠正。
- [ ] `RuntimeParams` 复用该原始记录完成类型、必填、条件和适用性检查。
  有明确来源的零／false 与 missing 是不同状态；不依靠“值和默认相等”判断是否填写。
- [ ] `GlobalDefs.h` 的运行配置不再是能默认构造出完整科学问题的公共聚合。
  用受控构造／解析结果进入 Driver；直接 C++ 调用走同一完成检查。
  输入期可在现有映射使用存在位或 optional，运行期保留已解析的普通类型，
  `OdeConfigView/BurnConfigView` 等数值视图不携带字符串、optional 或缺项判断。
- [ ] Setup 所需的网格／材料字段在调用前完整校验。case 需求沿现有 ProblemGenerator
  注册契约声明，模板／GUI 可在执行物理初始化前列出缺项；不依靠 Setup 抛第一条错来枚举全部参数。
  可编辑的准备阶段与最终只读配置分开，Setup 的显式派生或赋值必须追踪，
  不能修改后还沿用旧“已验证”状态。
- [ ] 不用默认提供缺失初值去继续执行任意 Setup 代码；缺失时只返回诊断。
  Setup 的有效 EOS 查询继续允许，但必须先具备完整的相应材料／状态输入。
  配置确认后才建立 AMR／CUDA／输出资源，所有错误沿统一异常边界安全退出。
- [ ] 删除四类 `UseDefault` 的未知方法路径、陈旧 warning 和无人再用的回填辅助。
  合法别名、显式 compute_backend=auto／use_nse=auto 和线性求解自动分发按已有科学规则保留。
  backend=cuda 无设备、EOS 求值失败等仍不能自动降级为另一条物理路线。
- [ ] 程序化测试从具名的完整 fixture／受控构造创建配置；
  数学单元只构造其实际需要的小型视图，不要求每个 ODE 单测填写整个模拟的 95 项。
  不引入 legacy_defaults、strict_mode 或仅在 release 才严格的新兼容开关。

当前 `Setup(SimConfig&, ...)` 与字段可写性存在直接耦合，因此这不是修改一个头文件即可完成。
实施提交须同步公共接口、用户例子、初始化检查、重启及两后端入口；
`Init` 仍从同一 case 对象成员读取准备好的量，公开使用仍保持两个头文件。

## 5. 诊断与 API 契约

缺失检查汇总所有在当前信息下可判断的错误。若 eos_type 本身缺失，不猜一个 EOS
再列出它的下游必填项；用户补齐后继续判断真实依赖。建议响应沿现有 diagnostics 扩展：

`MISSING_REQUIRED_PARAMETER / INVALID_OPTION / INVALID_VALUE / DUPLICATE_PARAMETER / UNKNOWN_PARAMETER`

例如一份已选择 self gravity、IdealGas 的不完整输入应指出：

`缺少 gamma（eos_type=ideal）；缺少 gravity_boundary（gravity_type=self）；缺少 x1_max（x1 活动）`

错误包含所属物理模块、条件、期望类型／单位及输入位置；缺项附可定位的键名，
不伪造不存在的行号。提供枚举候选或指向说明，不能在报错同时替用户选择某种物理值。

- [ ] schema 发布必填条件、单位、允许默认、模板推荐与用途；缺失字段的值为 null，
  不以 0／false 或“已解析默认”冒充输入。
- [ ] 配置 inspection 可以展示部分输入和全部已知诊断，但 status／完整性明确不通过；
  preview 不依靠自动补值构造一个看似正常的初态。
- [ ] GUI 可以按契约提示并帮助填写，真正运行再次执行同一 Core 检查。
  不把验证逻辑仅放 GUI；不新增“GUI 高级”标记。输出读取与 CPU preview 不初始化 CUDA。
- [ ] 有效配置记录 input／case-defined／derived／documented-default 及生效值；
  关键值缺少来源不得进入运行配置。不会为每个运行单元附带这些字符串。
- [ ] 配置扩展从版本 2 升级为 3，外层封套未改时维持 schemaVersion=1.0；
  其他响应若发生破坏性变化，按各自扩展升级并同步会话内嵌响应。
  Core、Host 校验器、客户端类型、样例 JSON 和中英文 Reference／算例指南同批迁移。
  先冻结真实响应 fixture，再接表单，不能仅改界面提示以掩盖 null／missing 的不兼容。
  同一输入在命令行、inspection 和初始化预览中的缺失／类型判定一致。
  inspection 仍不承诺文件系统、设备和完整模拟已经可用。

## 6. 原实施顺序与验收要求

该工作排在 G 退役、JENS 和 RZ 之前，同属 O7.0；独立于外部软件，也不重开数学优化。
Studio、Host、Linux／WSL 与新平台执行的先后关系及验收出口见联合交付计划；
完整性整改可交给具备 C++ 能力的合作者，条件规则或科学参考疑点必须交维护者判定。

1. 以此 95 项清单及实际 case 读取为基线，登记必需条件、允许默认、数据来源和现有消费方。
   将原始解析和类型检查归一；先去掉未知方法自动回退与 preview 专有的整数严格性差异。
2. 引入输入存在性与受控配置构造，完成条件需求和聚合诊断；迁移 Setup／直接 C++ 入口。
   同步标准算例的显式输入，并保留现有解析／公式和数值预算。
3. 在新契约上完成 gravity_G 退役、常数引用、用途标签与 schema 示例刷新；
   小值继续使用已有参数，不增加每个模块的新 floor。再实现 JENS。
4. CPU 检查通过后统一编译受影响 CUDA 路径，确认加载规则不会使两后端构造不同数学；
   配置身份完整后才重建运行比较基线。
5. 扩展现有配置／分发／API／checkpoint 测试并收束重复覆盖。最后按主计划的 CI 预算交付；
   不添加 FLASH 依赖或另一套大型必跑矩阵。

验收清单：

- [ ] 缺项逐字段及条件组合检查：缺值时不创建 Driver／输出文件、不启动 CUDA；
  显式 0／false／none 正确通过，未知名称、坏 token、重复、错误核素与未消费键明确失败。
- [ ] 关闭模块不要求其专属参数；开关自身缺失不能当关闭。不同阶段有完整条件链诊断，
  一次给出所有可判定缺项，不只报告第一个正数校验失败。
- [ ] CLI／API／case Setup／直接构造一致；普通 Get<int/bool> 与 preview 同样严格，
  覆盖小数转整数、范围和显式布尔语义。
- [ ] 现有断言“未知方法回退成功”改为显式拒绝；模板省略改为完整显式输入。
  这些是经本计划定义的输入契约变化，独立物理真值、容差及方法收敛检查原样保留。
- [ ] 对有效配置，迁移前后有效物理／数值控制一致；选取现有 Hydro、燃烧、扩散、
  自引力及耦合锚点复验，解释因修复错误输入而改变的结果，不冒充纯行为等价。
- [ ] 重启的字段、配置身份和继续演化有效；缺少关键配置／数据或不相容 checkpoint 必须拒绝。
  历史不可用输入不保留静默兼容模式；版本变化与当前可用例子同步。
- [ ] 文档清楚区分“待填写”“模板推荐”“算法默认”“派生值”；使用有效最小示例演示
  缺项错误与正确输入。新手无需填写 ODE 的每个内部工作参数。
- [ ] 验证成本依主计划收束：复用现有入口和 fixture，缺项矩阵在解析层执行，
  不为每个错误参数启动一次完整物理模拟。

**完成标准：**缺少关键科学输入无法开始演化，错误能明确指导补齐；已明确的有效配置
保持同一物理方法，输入严格性在所有入口一致，运行热路径和共享数学不增加存在性检查。
