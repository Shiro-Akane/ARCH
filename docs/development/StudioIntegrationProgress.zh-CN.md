# 联合交付执行清单

本轮依据 StudioConfigurationHandoff.zh-CN.md，完整范围保留：3B 收尾、O7.0 配置 v3、
Linux/WSL 3C、模型初态/AMR、O7.1–O7.5、CPU 后 CUDA、冻结方案短测和批准的长时子集。
plt 按独立出口交付。Windows 适配/安装包、O8/O10、main 合并均不在本轮实施范围。

## 取得的准确基线

- compute/optim：8fc0dd25eefd2243e8c36f85440bac46994e2e73。
- Studio：c96e9da0a6d114fd0323102b73dba4a3cd8da9ba / studio-phase3b-v0.21.0。
- 独立分支：studio/compute-optim-integration。
- 仅按 Git tree 引入封箱 studio/ 子树，导入前后子树对象一致。
- 非 Studio 来源保留 compute/optim；相对 main 的上游增量为文档/既有审计，没有覆盖 Core。
- 原 3B 工作区 clean 且未修改；原 tag 不移动。
- 旧 3B 测试结果只是历史证据，本轮复验独立记录。
- npm test 包含 Host，不能把总数与 Host 子集相加。

## 顺序清单

| 阶段 | 实现 | 本轮工程验证 | 科学 review / 性能 |
| --- | --- | --- | --- |
| 1 3B 源码接收与复验 | 封箱源码已引入；native Open/Save As 与 Host 关联已有证据 | 当前235 Studio/Host及lint/typecheck/production PASS；原生文件选择、Dirty保护、空格/中文SaveAs/Reopen、外部冲突拒绝及显式overwrite已有直接证据；用户截图确认独立窗口可见；历史177项不替代本轮 | 不适用 |
| 2 O7.0 + 配置 v3/Host/Studio | v3实际API/Host/表单、注册模型声明、shared CGS G、只读Driver边界已实现；准备/来源边界持续核查 | CPU定向配置/组件、当前v3样例/Host/表单已有验证；整体科学/原生UAT未签收 | 历史特殊G输入换算待维护者批准，不沿旧物理预算宣称通过 |
| 3 Linux/WSL 3C 启动/Configure/Build | 已有本地 CPU profile、独立 Host 与 Linux 启动实现 | 原生Configure/Build通过；真实CMake微型工程编译失败/恢复通过；完整依赖freshness仍unknown，dependenciesComplete=false；边界见Studio3CExitAudit | 不适用 |
| 4 3C Run/Restart/进程隔离 | 已有独立终端、持久历史、身份核验/Stop及canonical输出目录锁 | 原生Sod Run/Restart/Stop、关闭后计算继续及历史恢复通过；235项Studio/Host通过；同场景Preview cancel/不同项目Host重开隔离已PASS，Run同PID/start持续推进并自然完成；保留Host/native覆盖区分 | 不推广为其他模型/后端演化验收 |
| 5 全模型初态/AMR | Core维度驱动真实生成/CLI/session已接通；Host动态Profiles/三轴协议、UI配置维度选择/三维切片/单区状态已实现；AMR native/三维切面显示已实现；14模型真实HTTP已验证；Gaussian Cartesian三维桌面代表已通过切片/Inspector/根AMR/关闭清理；完整桌面矩阵待完成 | CPU14维护模型字段+根AMR、三维代表/曲线参考、非立方体与warm session通过；不是混合细化/科学认证 | Setup域/预算与科学验收分开 |
| 6 O7.1 JENS | 待实施 | 先 CPU | 独立参考/预算由维护者确认 |
| 7 O7.2–O7.5 RZ | 待实施 | 分层 CPU | O7.4 科学方案须 review |
| 8 CUDA/第二平台短测 | 待 CPU 完成 | 未编译/未计时 | 冻结同物理终点；保留负收益 |
| 9 批准的 O9 长时子集 | 待冻结输入/预算 | 未执行 | 未批准项不能称完成 |
| 独立 plt 只读出口 | 已只读核对writer及既有H5 shape/身份缺失；读取接口未实现 | 元数据审计完成，未读完整场/未执行结果UAT | 原生单元与数据身份 |

## 不变量与交付

不更改独立参考、物理定义或误差阈值以求通过。不以 CPU/GPU 一致替代科学验证。
新运行 H5/plt/checkpoint/完整数组/trace 留本机持久目录；只提交经过检查的
汇总指标、逐次计时表、必要图表、诊断摘要与本地数据索引。
每阶段更新本清单并提交，维护者 review 决定合并。未知/失败/未验证状态如实保留。

导入检查：旧 target、第三方许可证和 round-trip fixture 自带尾随空白；整棵新增子树的 diff-check 报出这些历史字节。为保持封箱子树及测试原文，未格式化它们。相对 c96e9da0 的 Studio diff-check 与本轮新增清单的 diff-check 分别通过。

本轮详细证据及未完成项见 [3B Linux 复验](../../studio/STUDIO_3B_LINUX_REVALIDATION.md)。

## 配置 v3 候选契约（非运行实现）

入口为 src/api/CONFIGURATION_V3_CANDIDATE.md；共享样例位于
src/api/examples/configuration-v3-candidate/。94-key 目标目录继承现有 Core 的
单位/选项/展示元数据，按唯一计划限制默认集合；当前生产 binary 仍发布 v2 和 95 keys。
四组完整 JSON 封套覆盖有效 Sod、空输入、缺少 burn 开关、语法/重复错误。
Sod 成功样例覆盖 94 标准项、7 case 项及 log_dir；完整性限于静态声明检查。
缺少开关不解析为 false；解析值、默认解析结果、派生值分开记录。

检查命令：python3 src/api/examples/configuration-v3-candidate/verify_candidates.py。
此检查只证明候选数据内部一致，不证明 ARCH 已执行 v3。接下来迁移 Core 输入记录、
共同条件解析及受控构造，再将真实输出接入 Host/Studio；不重复已通过的未变更 3B 检查。
其余模型声明、case-defined 例子、错误/预算/身份矩阵随实现补齐并运行真实契约回归。

### O7.0 输入记录基础

ConfigParser 现在共用 Read 收集与 Load 拒绝路径：原文/字节位置/多次赋值均保留，
坏行、空键、重复键汇总。重复键仍是 present，不读取第一/最后值，也不变成缺项后回填。
静态调用可保留有效记录；运行 Load 在 Resolve/Setup 前拒绝语法错误。文件打不开时
移除误导的 Using defaults 文案。数值解析和科学公式未改。

轻量验证：g++ -std=c++20 -O2 -Wall -Wextra -pedantic -Isrc 编译
 tests/host/io/test_config_input_records.cpp（输出留在 studio/.local/integration）；
使用共享候选目录运行 PASS，无编译告警。覆盖零/false、坏数值、重复2/3次、聚合错误、
UTF-8列号、CRLF、无末尾换行、空字符串及重复加载清理。已登记 config_input_records
到现有 Host CTest；本次直接编译执行，尚未运行完整 CMake/Core/Host 迁移回归。
解析器改动不等于完成 v3：必填/条件解析、受控构造、API 聚合序列化及客户端仍待迁移。

### O7.0 显式未知方法拒绝

方法注册表移除 UseDefault 分支与 defaulted 标记。未知 solver/reconstruct/limiter/
time_integrator 在共同解析及运行工厂均失败；目录不再发布 core-fallback，配置 API
删除 POLICY_FALLBACK 警告路径。合法别名、backend auto、linear-solver auto 保留。

独立 build-cpu 由 cpu-release preset + BUILD_TESTING=ON 配置，CUDA=OFF；
HighFive 使用已核对 clean v2.9.0 源码覆盖，KLU 来自系统库，不复用原项目缓存。
受影响目标采用内存保护器（2 GiB 余量、swap 增长上限256 MiB、PSI保护）及
28任务上限编译。两项 CTest：config_input_records / resolved_execution_plan PASS；
API Configuration.cpp 对象定向编译 PASS。构建最低可用内存约20.9 GiB，swap无增长。
完整 ARCH 链接及 configuration_api_contract 尚未执行，留待配置生命周期接线后运行；
不将对象编译或两项测试称为 v3 全回归。原3B基线与 tag 未改，未启动 CUDA或模拟。

### O7.0 共同需求解析基础

现有 StandardParameters 唯一目录增加 RequirementKind/InputCondition，不建立第二份
生产键表。AllowedDefault 仅开放计划允许的25项；旧fallback字段暂供未迁移的v2加载器，
不是v3许可，必须在生产接线阶段移除关键参数回填。InputResolution 保留nullable parsed/
resolved/source、显式缺失/无效/重复状态和三态需求；空输入只报告可确定的19项必填，
缺少开关不会当成false。初态用途可省tmax，正式演化不能省。模型/材料消费声明未知时
保持unknown，不能把按标准项检查通过等同于全部case或模拟就绪。

实现核对：只有MUSCL消费所选limiter，PCM/PPM不要求；Tabular输运不能按EOS名称
猜测，须由真实材料声明判定。Helm显式扩散常量即使0仍报错。新解析层识别gravity_G
退役，但旧运行路径/G_const尚未迁移；没有改变共享物理常数或科学数学。

CTest input_resolution PASS：19项逐项删除、条件需求各组、合法0/false、未知依赖、
关闭模块坏token、未知方法、重复项、NSE auto capability、外部引力完整向量、
Helm禁止系数、旧alias和G退役。API对象对新增目录字段编译PASS；diff-check PASS。
证据日志在studio/.local/integration/input-resolution-check.log，不提交构建产物。

此提交是解析基础，不是v3发布：尚未接入RuntimeParams/inspection/Host，未构造只读
完整运行配置，尚缺case声明、完整范围/组合检查与post-Setup重新验证；不生成checkpoint。

### O7.0 普通 case 读取与 Preview 严格性统一

ConfigParser 提供共同 Boolean/数值可表示性检查；SimConfig::Get 无论有无观察器均
检查整数词法/范围、布尔 true/false、浮点有限性。既有 custom_string_params 保留全部
原 token（包括数值），普通运行不再依赖只有 Preview 才持有的词法证据。
旧程序化数值覆盖仍返回覆盖值并在观察记录标为 unknown；不冒充原输入，后续受控
构造/来源迁移仍须收束该可变路径。没有新增物理默认，也未完成 case 声明。

三项受影响 CTest PASS：preview_parameter_reads、config_input_records、input_resolution。
参数读取测试对 observer on/off 分别覆盖小数/科学计数整数、溢出、非法布尔、合法
零值/符号整数/大小写布尔及 programmatic 非有限/截断拒绝，并保留旧歧义/来源回归。
使用相同CPU构建与内存保护器，swap无增长；日志留在studio/.local/integration。
完整二进制/CLI/Host回归仍待生命周期和v3接口接线后执行；此项不代表整体v3完成。

### O7.0 注册模型的静态声明入口

ProblemRegistry 的 registration 增加静态 configuration_declaration callback；
DescribeConfiguration 不调用 creator，也不构造模型或执行 Setup。typed注册宏只转发
模型自己的 DescribeConfiguration；未实现该方法的模型保持 complete=false。
Sod 首先声明现有七个可编辑 primitive 输入为 required，并声明自己的材料/组分消费。
CaseInputResolution 汇总缺项/坏类型/重复源位置，不依赖通过默认值执行 Setup 来探测。

case_configuration CTest PASS（工厂/构造函数设计为一旦调用就抛错）；真实 Sod 对象
编译PASS。Sod改动经diff核对仅插入静态声明，移除该块后与父提交文件逐字节一致，
Setup/Init/单位表达式未改。按此审阅更新该模型的unit evidence与候选schema源码SHA，
没有批量自动刷新其他模型的单位审计身份。

当前仅Sod已声明，其余模型未覆盖；声明callback尚未接进正式Load/API入口。
case范围/派生值/未知键与species归属检查、完整受控构造、v3序列化和Host仍未完成。
不以此测试证明可运行性，不建立阶段完成tag；其余注册模型继续沿同一接口迁移。

### O7.0 ExternalGravity / JeansWave 静态输入声明

核对真实 Setup 后，ExternalGravity 声明 rho0/pressure0/velocity_x0；
JeansWave 声明 rho0/pressure0/amplitude/phase/mode/standing_wave，均无隐式默认。
后者的 standing_wave 当前由模型按字符串读取，且只接受小写 true/false；
CaseParameter 因而增加精确 options，拒绝未知 token，不自行引入大小写别名。
这两个模型均不初始化网络或消费网络 floors；共同 burn 需求仍由标准解析层处理。

case_configuration CTest PASS：选项合法值、大小写差异、数值布尔、未知 token、
缺项不补默认及错误源位置。两个真实模型对象编译 PASS；28任务上限/内存保护下
最低可用内存约21.2 GiB，无swap增长。git diff --check PASS。
模型变更仅插入静态声明，移除插入块后文件与父提交逐字节相同，
Setup/Init/科学公式未改；只更新这两个已审阅模型的单位证据SHA。

目前共3个模型具有静态声明，另外11个尚未迁移。静态完整性只表示该模型声明
覆盖其读取项，不能替代范围、组合、稳定性检查或模拟就绪判定。
普通加载/API仍未接线，生产v3和跨入口一致性尚待实现，本次不建立完成tag。

### O7.0 全部内置模型声明与稀疏组分输入

剩余11个模型补入静态声明，现有14个内置模型均有入口（数量仅为本次源码证据）。
声明按真实Setup读取项登记；Gaussian/GravityBox的gas_cv按所选网络物种数量判定，
GravityBox非活动中心坐标不要求；Sedov能量单位按已有维度语义解析。
CooperativeHotspots选项保留原大小写不敏感语义；没有为其他模型推断别名。
动态组分声明未解析时，整体coverage保持不完整，不把未知网络当none。

新增CompositionInput从实际CPU网络SPECIES_NAMES取得核素键，不构造模型、不执行Setup
或EOS。稀疏输入保留原值不归一化；未提供物种记录missing/parsed=null、case-defined零
及network来源依赖。拒绝坏类型、负输入、大小写重复和非正/非有限总和。
消费归属检查共用标准目录、retired目录、case声明及实际核素名单；辅助键须调用方
明确登记。拼错核素和未登记辅助键报UNKNOWN_PARAMETER，声明未知时不猜测。
log_dir派生来源/辅助解析、未知键建议及生产接线仍待后续完成。

实际检查：config_input_records/input_resolution/case_configuration三项CTest PASS；
四个内置网络的元数据、稀疏原值、来源、坏值、大小写重复、缺失network、未知核素
和不完整coverage均有针对性回归。全部14个模型对象与新增CompositionInput对象编译
PASS；最终对象编译最低可用内存约17.7 GiB，无swap增长。diff-check PASS。
这不是全部模型运行/CLI/API回归：本步未调用模型Setup或运行simulation。

逐个核对11个模型diff：去掉新增静态方法后与HEAD原文件逐字节一致，
仅按该审阅更新相应unit evidence SHA，没有改变初始化公式或物理阈值。
内置Timmes与生成网络当前归一化实现不同；本次未修改任何归一化/网络数学。
新原始输入拒绝层尚未接入旧RuntimeParams；后续必须统一入口才能称配置整改生效。
下一步迁移正式加载/资源边界、受控运行配置和v3 API，再接Host/Studio。

### O7.0 真实 CLI / 初态入口前置检查

ConfigurationInput 汇总标准、静态case、组分、辅助和消费归属检查；
log_dir缺失时保留parsed=null并记录从out_dir派生的来源。重复诊断合并但保留
源位置；CLI聚合错误逐项显示键名。声明查询不调用模型工厂或Setup。

CLI普通运行、Preview和inspect-case已接入case-aware检查：正式演化要求tmax，
初态用途不要求演化终点。CLI将Setup和其后控制检查置于统一异常边界，
在输出/日志目录创建之前拒绝失败；没有改变积分器或初始化公式。
参数文件一次读取后在同一parser上检查和构造，避免检查后重新读取引入身份竞态。

验证：四项定向CTest（configuration_input / case_configuration / input_resolution /
config_input_records）PASS，覆盖标准+case缺项聚合、语法/重复/未知组合、
derived log_dir、未知case和加载无资源副作用。CPU ARCH实际链接PASS，28任务上限
内存保护下最低可用内存约14.7 GiB，无swap增长，未编译CUDA。

tests/api/configuration/test_configuration_entry.py真实二进制4/4 PASS：
CLI缺tmax/cfl/rho_left聚合；语法/未知case/Setup失败不创建输出或日志；
Preview和inspect-case共享缺项拒绝；省tmax的有效Sod初态保留DENS/PRES数值。
只做4点初态/初始化检查，没有时间演化，没有生成科学H5/plt/checkpoint。
测试授权按当前联合交付文件6.1节核对；旧3B范围导致的一次自动审批拦截已解除。
本轮验证binary SHA-256：c1328e1beee862e7eaae9e0c6227ac1e23a33f1b0a7c269046876310c58cfa15。

过渡状态仍明确未完成：配置inspection/resource-estimate及直接测试加载仍有旧v2入口；
新case-aware路径后的字段构造也仍用旧Resolve，尚需删除关键fallback和第二权威映射，
收束直接C++构造、追踪Setup更改并建立最终只读配置。v3序列化、活动.par/旧测试迁移、
Host/Studio尚未完成，不能把本次真实入口检查称为整体配置验收通过。
下一步接入部分输入inspection并迁移共同字段构造；不发布阶段tag、不push。

### O7.0 部分输入范围检查的共同所有者

为移除inspection对带默认SimConfig的依赖，将既有单参数范围规则提取为
ScalarControlValidation，并由原ValidateControls和StandardInputResolution共用。
不改变上下限、允许零/禁用值或科学误差预算；跨字段、材料、拓扑与资源规则
仍保留在原所有者，尚未完成部分输入版的所有跨字段检查。

部分输入现在聚合所有已提供单参数范围错误，包括inactive模块中的显式坏值；
parsedValue保留类型正确的原数值，resolved/source清空且保留源位置，不回退默认。
没有提供的关键参数仍保持missing，不为执行校验构造虚拟数值。

验证：本地提取前/后390组runtime范围判断一致（包括边界、NaN和Inf），对照程序
和输出留studio/.local/integration/scalar-parity，不作为另一套生产规则提交。
5项相关CTest PASS（configuration_entry_contract内含5项真实入口检查，不重复相加），
新增CLI同时报告cfl/gamma/tmax/dt_min/ode_rtol错误并确认无科学输出目录。
CPU ARCH重链PASS；28任务内存保护最低可用内存约14.1 GiB，无swap增长；diff-check PASS。
本次未修改运行公式、未执行时间演化、未编译CUDA。

这是v3部分输入inspection迁移的前置收束。当前--inspect-config仍为旧v2，
尚需迁移跨字段判断、序列化、schema和客户端；不将单参数规则完备称为
全部配置检查完备，也不提前宣告阶段完成。

### O7.0 跨字段与自引力拓扑共同检查

ControlRelations提取原有步长/能量上下界、HLL信号速度、Coulomb选项、
活动轴域、轴拓扑、AMR层级/曲率阈值及自引力边界契约，供运行与部分输入共用。
自引力保持当前Cartesian/cylindrical/spherical定义、完整方位角、极点和面规则；
没有提前实现RZ、改动64*epsilon方位角容差或1e-12极点容差。

部分输入仅在依赖值真实有效时判断；检查同一输入快照后才统一清空失败值的
resolved/source，避免前一错误遮蔽后续独立错误。保留parsed/raw、位置和related_keys。
缺少几何/边界/活动轴信息不猜测；未知维度不会被填为可运行维度。
汇总层保留同键不同关系错误，并合并重复诊断的相关键证据。

本地对照原/新完整运行判定657组一致，其中156组接受，排除了全失败的空验证；
临时对照程序和结果留studio/.local/integration/relation-parity。
相关5项CTest全部PASS（真实入口组含6个测试，不重复加总）；
真实CLI能同时返回网格、能量和步长关系错误且不创建输出。
CPU ARCH重链PASS，28任务内存保护最低可用内存约14.5 GiB，无swap增长；
git diff --check PASS。没有时间演化、CUDA或原始科学输出提交。

仍未宣告完整性整改完成：case的具体物理域检查、Setup赋值来源/重新验证、
受控只读构造与旧fallback清理仍待收束。下一步直接迁移v3 schema/inspection序列化，
以当前共同解析结果返回nullable值及诊断，随后迁移Host/Studio。

### O7.0 v3 schema / inspection 实际响应迁移

配置扩展改为3。inspection删除默认SimConfig构造与旧input_value回填，
直接消费AnalyzeConfigurationInput，返回nullable parsed/resolved、原文位置、
input/case-defined/derived/documented-default来源、条件三态和聚合诊断。
schema发布94活动标准项、25允许默认、14模型声明及辅助log_dir；这些是当前
binary证据，不是前端固定数量。gravity_G归入retiredKeys。
响应超限保留identity，明确降低coverage与completeness。

13项configuration_v3_contract真实binary测试PASS，覆盖声明/来源、空输入、
缺失条件、重复/非法/退役/未知、原文字节身份、路径不访问、稀疏组分不归一化、
Setup物理域不冒充已检查、8MiB预算。最终6项相关CTest全部PASS，v3组含13项实际响应检查，不重复加总。
最终审查补齐超限错误诊断schema，重新编译后完成该最终测试，四份配置样例与最终binary响应逐份一致。
configuration_entry_contract仍含6项实际入口检查，不与CTest条目相加。
CPU重链PASS；首次漏传InputContext导致编译失败，修正后构建及测试通过。
最后构建最低可用内存约20.8GiB，无swap增长；git diff --check PASS。

实际配置JSON证据在src/api/examples/configuration-v3，capture.json记录binary
SHA、base commit和修改实现指纹；没有科学数组或H5/plt/checkpoint提交。
CONFIGURATION_API及API入口说明同步为当前v3，旧v2样例标明历史用途。

边界仍明确：Host/Studio未迁移，旧全套v2用例未宣称通过；动态coordinates/
diffusion/amr摘要需从partial inputs安全接线，不能恢复默认SimConfig。
case物理域与Setup后重新验证、受控只读构造、旧fallback/G_const清理及
后续3C/全模型/Jeans/RZ/CPU-CUDA验证尚未完成。本提交是集成中的可审阅步骤，
不建立完成tag、不push、不进入物理性能测试。

### O7.0 Host / 参数工作台 v3 接入

Host与React共享类型/运行时校验迁移为配置扩展3，旧1/2版本明确不兼容。
直接消费Core同一目录下的实际v3 fixture，没有复制前端默认目录。
校验nullable标量类型、missing/invalid/duplicate、来源证据、条件三态、
位置、诊断与coverage；成功不得掩盖不完整或错误。exit7受限错误保留身份。
未知旧derived metadata不能未经验证流入UI。

Parameter Catalog缺失文本保持空白；允许默认单列展示，不成为Working Copy。
Inspector分开parsed、resolved和Preview model-read。标准、辅助和当前case声明
可在首次Preview之前编辑；未知坐标布局仍保留轴参数可达。外部加速度三个分量
保持可填写，不再凭维度隐藏Core要求的显式零。gravity_G加入退役显式删除路径。
Path preflight仅查Core标记的standard/auxiliary路径，用resolved值并保持真实cwd。

显式草稿Save/Save As/Download不再被科学检查错误阻止；仍经无损序列化、
路径所有权/覆盖确认/外部修改检查，保存不授予Preview或Run资格。
空配置和非法原文可以重新打开；结构性注入仍被serializer拒绝。
去除本地AMR阈值/轴拓扑/能量比较中的缺项默认补值。

验证：npm test 180/180 PASS（Host是其中子集，不相加）、lint PASS、
typecheck PASS、production build PASS、git diff --check PASS。
首次回归4个失败：三个旧v2/G编辑期望已按v3迁移；一个大型schema测试脚本
process.exit导致stdout未刷完，改为write完成后退出，未放宽生产校验。
原有Vite大chunk提示仍存在，不因本阶段顺手调整打包。

真实CPU binary经ConfigurationAdapter的独立集成测试PASS：
valid/empty/duplicate+syntax/bounded-overflow四类；精确stdin/configRevision、
binary身份、错误保留、原磁盘文件不变。binary SHA-256：
870594c98e894823a07711e0a141123aec4a9d3a96a578cde02b7520a5c28ee3。
测试使用隔离fixture Build Profile，仅用于验证真实命令传输，不声称正式工程
Build provenance已验收。复现：在studio下运行node
tests/configuration-core-v3.integration.ts ../build-cpu/bin/ARCH。
没有执行Setup/Preview/timestep、CUDA或科学输出。

仍在O7.0：partial-input的动态坐标/Diffusion/AMR摘要尚待Core接线，
完整production UAT未执行；不可把本次render/协议测试写作真实桌面验收。
受控只读运行构造、Setup后校验/来源、G_const清理、旧Core输入与全套检查迁移
仍待完成。3C及后续模型/Jeans/RZ/CPU-CUDA与冻结benchmark目标保持不变。

### O7.0 资源估算入口移除旧默认 loader

审查真实调用链发现--amr-resources仍调用无case的RuntimeParams::LoadText。
现改为ConfigParser + ResolveStandardInput，直接读取估算所需的三轴blocks、
lrefinemin/lrefinemax及登记max_blocks；不构造SimConfig，不猜EOS、case初态或tmax。
缺少五项mesh输入时逐键拒绝，零轴计数保留。显式语法/类型/范围错误仍聚合拒绝。
caseId继续是资源估算上下文，未知case参数覆盖不在该入口声明范围内。

原资源公式抽为同文件函数，真实运行包装仍使用原有效grid.dim和相同blocks、
层级/容量/组分数量；没有改动存储估算或OOM语义。API新增明确
configurationScope=resource-count-inputs与simulationReadiness=not_checked。
这不是完整科学配置或运行就绪检查，INITIAL_AMR_API文档同步。

CPU重链PASS，28并发内存保护最低可用约20.7GiB，无swap增长。
6项相关CTest全PASS，configuration_v3_contract现含16项：
新增mesh缺项/1D-2D-3D计数/非法显式输入/overflow检查，并逐项对比既有
sod-resources.json的计数/内存字段；无需生成真实AMR或科学输出。
git diff --check PASS。API源码已无旧无case loader调用。
无源文件变更涉及Studio，不重复其已通过的180项回归。

完整RuntimeParams仍有旧字段fallback、测试用无case入口和公共SimConfig构造；
本次只关闭真实资源API的默认构造绕路，不能宣告受控只读配置完成。
后续继续完成共同配置构造/Setup来源、partial metadata、输入迁移及全目标，
没有推进CUDA、Jeans/RZ或科学benchmark，没有push或新增完成tag。

### O7.0 标准参数单一读取所有权

标准目录提取到同一 StandardParameterEntries.inc；schema/解析定义与轻量键分类
共同展开，95项原定义逐项保持不变，不建立第二套前端或Core键表。
RuntimeParams不再向custom数值/字符串map复制标准输入；原文仍由parser/trace保留。
SimConfig::Get拒绝标准及历史退役键，即使C++调用方手动注入同名custom项也不能绕过。
GaussianPulse/GravityBox的Setup移除network_name二次Get，沿用已解析的标准字段；
没有改写物理计算或给network追加默认。两份模型来源SHA证据随源码更新。

扫描src/simulation/include未发现剩余以标准键字面量调用Get的生产读取。
新增configuration_input回归核对全部标准键无custom副本、x_pos正常读取、
缺失辅助log_dir不伪造显式输入，以及手动注入cfl/network_name/退役键仍拒绝。
CPU ARCH及受影响测试构建PASS，28并发内存保护最低可用约14.5GiB，无swap增长；
6项相关CTest PASS：configuration_entry_contract、configuration_v3_contract、
preview_parameter_reads、input_resolution、case_configuration、configuration_input。
实际API组分别包含6/16项Python检查，不与CTest条目重复加总。
git diff --check PASS。没有时间演化、CUDA、科学输出上传、push或完成tag。

这只消除标准参数的第二份可变读取来源；SimConfig仍可变、RuntimeParams仍有
transitional fallback、Setup后来源/冻结和动态partial metadata尚未完成。
不以本次窄回归宣称完整O7.0或全Core测试完成。后续继续既定联合交付目标。

### O7.0 清理测试对无case默认loader的依赖

CaseParameterValues提取原RuntimeParams的custom词法/数值捕获逻辑，生产加载器和
Get/observer单元测试共用，不增加另一份科学默认或完整配置校验器。
该辅助函数明确只填case值存储，不授予Setup/Preview/evolution资格。
初始化观察单元显式给定a/b，状态转换测试只构造实际数学视图；
网格加载和exp表达式检查转入完整声明的configuration_input fixture。
旧gravity_G覆盖表达式断言改为合法external acceleration字段；共享G删除仍待后续。

真实Cellular参考程序改用CellularDet InitialState声明入口并链接共同组分声明。
CPU ARCH、相关单元及参考程序构建PASS；最低可用内存约14.8GiB，无swap增长。
最终6项CTest PASS：preview_initial_conversion、configuration_entry_contract、
configuration_v3_contract、preview_parameter_reads、configuration_input、
initialization_probe。补充表达式断言后仅重编受影响目标，再执行最终检查。
git diff --check PASS。未改科学公式、转换误差预算或执行时间演化。

实际运行参考程序验证旧CellularPreview2D.par被聚合MISSING_PARAMETER拒绝，
退出1且无stdout科学数组；该旧fixture需要迁移显式控制项及避免重复赋值。
本次未把二维全组或该参考正向场比较声称为PASS。其他checkpoint/CUDA测试
仍有旧无case loader调用，RuntimeParams的fallback和公开构造尚待完成迁移；
不新增legacy mode，不宣告O7.0完成。未push或建立完成tag。

### O7.0 Cellular 二维参考输入/回归迁移

CellularPreview2D.par显式写入原loader提供的25个控制值，保留原Helmholtz、
aprox19、burn、初态密度/温度/速度/扰动及网格。数值来源为原标准参数注册表
及GlobalDefs所有者；GridConfig至SimConfig前的存储定义与8fc0dd25逐字一致。
没有新增模板物理值、修改容差或给该init-only样例补演化终点。

二维测试变体改为替换原赋值，不再依靠追加重复键覆盖。
3D不支持测试补齐第三轴显式边界后测试真正的维度限制；
IdealGas失败样例显式给出原gamma=1.4后仍验证采样失败。
NaN和未知network按共同配置检查在Setup之前失败，错误预期同步为
INVALID_CONFIGURATION；不保留旧绕到Setup才失败的路径。

真实CPU preview_cellular_2d全部10项PASS（15.89s），含shock_dir=0/1、
5x3与真实独立Init参考逐字段比较、扰动内外区、128x128默认及256x256上限、
参数/采样失败、EOS/采样错误状态、8MiB响应预算、CPU-only和终止无输出。
原比较rel_tol=2e-12、abs_tol=1e-12保持不变；这证明采样/转换链一致，
不是独立物理真值验收。无timestep、Plotfile或checkpoint生成。
此次只改输入、Python测试与文档，复用上一提交已构建的真实CPU程序；
未重复无关Studio测试，git diff --check PASS。
该结果替代上一节中旧Cellular参考输入的未迁移状态；其他旧Core输入和
session/full suite尚未全部迁移，不据此宣告O7.0完成。未push或新建tag。

### O7.0 后处理与NSE测试移除无case loader

checkpoint后处理不再调用RuntimeParams或默认构造完整SimConfig。
局部Inputs复用Core的原文/类型/范围/重复键检查，只要求所选指标消费的
显式几何/轴域或外力/初态字段；缺少无关演化参数不冒充完整科学配置。
物理体积积分要求全部活动轴域及三个blocks计数，几何大小写保持兼容；
Cartesian无参数的旧归一化报告模式保持原有标记和数学。
external gravity解析参考不再为rho0/pressure0/velocity_x0填隐含值。
未改动GridMetrics、体积积分、参考公式或误差预算。

九种几何/维度测试现覆盖实际解析到积分路径；补充缺失轴域/计数、
重复键、坏行、非法显式值、合法零值和缺失参考密度拒绝检查。
checkpoint_conservation_metrics与checkpoint_temporal_comparison PASS。

NSE参数测试从checkpoint序列化组移到完整声明configuration_input入口；
原true/false/auto大小写、显式阈值及非法数值覆盖保留。
删除旧空文件默认成功断言，新增缺use_nse及两项NSE阈值必须报缺项。
configuration_input及checkpoint_compatibility PASS，原checkpoint指纹/
兼容身份/HDF5往返/原生组分恢复检查保留。
两次受影响CPU构建PASS，内存保护均无swap增长；最终diff-check PASS。
测试临时HDF5仅在本机，不提交/上传原始科学数据。

仍有CUDA测试的旧无case loader调用，尚未统一CUDA验证；运行构造、Setup
来源/只读边界及其他旧输入/完整回归仍未完成。本步不宣告O7.0完成，
不重复已通过无关Studio检查，不push、不建立tag。

### O7.0 删除无case RuntimeParams入口

删除Load(filename)及LoadText(text, reads)重载；活动生产/测试加载均必须
提供case ID，并经过AnalyzeConfigurationInput/RequireDeclaredInputs。
不增加strict/legacy模式。历史results中的源码快照保留原貌，不能当成
新接口可直接编译的活动调用方；文档加载链签名同步。

CPU mainline_authority现在以具名完整runtime-authority.par构造测试变体，
不由loader补齐科学输入；保留合法大小写/别名/活动边界要求与数值helper预算。
非法选项在加载时拒绝，非活动轴的非法显式边界同样拒绝；
另外保留程序化坏配置在dispatch被拒绝的测试。合法非活动边界仍不进入
活动执行要求。log_dir派生/显式覆盖检查仍使用实际加载后的配置。

BurnOneZone声明抽到同目录Configuration.h，生产模型和controller fixture
复用同一声明/组分来源，无模型构造或Setup执行。更新该case源码SHA证据。
最后一个CUDA witness源码调用改为case-aware完整配置，并通过typed
numerics读取tstep_change_factor，不再用标准键Get。CUDA目标的声明链接/
include同步，但本轮未编译或运行CUDA，不将其标为GPU通过。

冻结bd.par保持逐字不变。新增bd-config-v3.par，显式记录原有效
min_eint=1e-10、hll_wave_speed=roe、eos_coulomb_mult=1.0；
CPU configuration_input从该真实文件核对BD/rtol/atol/步长增长/密度/温度。
验证README中英文说明迁移状态，不改写历史科学结果或预算。

最终CPU ARCH及相关目标构建PASS；28并发最低可用约15GiB，无swap增长。
4项CTest PASS：mainline_authority、configuration_input、
configuration_entry_contract、configuration_v3_contract。diff-check PASS，
冻结bd.par diff为空。无科学时间演化、原始输出上传、push或完成tag。
RuntimeParams内部仍有transitional fallback，SimConfig/Setup只读生命周期
与来源追踪仍未完成；后续继续受控构造，不能据本提交宣布O7.0完成。

### O7.0 Runtime直接消费resolved records，删除关键字段fallback

RuntimeParams现在将AnalyzeConfigurationInput的StandardInputResolution
直接映射到typed字段，不再重新调用parser.GetInt/Double/Bool/String或表达式
解析并使用Default*回填。映射遇到未解析且仍必需/未知的字段明确失败；
不适用且缺失的字段只保留内部存储初始化，不产生input/source记录。
表达式仅由共同解析器求值；NSE/entropy/HLL选择从已检查的typed record转换。
Helm禁止显式输运系数规则继续由共同聚合检查负责，不重复另一份入口判定。

注册表required/conditional/retired条目改为无declared_default；只保留批准的
25项optional默认，逐项与提交前定义比较完全一致。删除DefaultInt/Double/
Bool/String、ValidateStandardTokens和loader中gravity_G写入路径。
GlobalDefs仍有可变存储及G_const，完整受控只读构造/常数迁移尚未完成；
不能将这次数据映射改动宣称为最终运行生命周期完成。

CPU ARCH及受影响目标构建PASS，28并发内存保护最低可用约13.8GiB，
无swap增长。9项CTest全部PASS：配置入口/v3、input_resolution、
case_configuration、configuration_input、mainline_authority、checkpoint
指标/时间比较和真实preview_cellular_2d（该组10项）。NSE/表达式/显式零/
默认来源/BD实际控制值与Cellular直接Init对比保持通过，科学容差未改。
diff-check PASS；没有时间演化、CUDA、原始数据上传、push或完成tag。

中英文Reference的90行分组参数表同步必填/条件/允许默认/退役，覆盖原95键；
删除关键字段旧加载默认的误导说明，负轴计数不再写成关闭轴。
尚待Setup派生来源和修改后校验、只读运行边界、partial动态metadata、
其余有效输入/全回归及后续Studio/3C/模型/Jeans/RZ工作，整体目标继续。

### O7.0 统一Setup前后控制校验

审查发现main在Setup后调用ValidateControls，而Preview和inspect-case没有
同一保证。ProblemGenerator增加SetupChecked作为应用准备边界，main与
Preview直接调用，InspectSetup在观察器保护内调用；Setup前检查已有controls，
Setup后以实际species.count重新检查，再允许上层发布ready/继续初始化。
不改模型Setup/Init的物理公式或虚接口，不在单元循环中增加存在性检查。

新增单元覆盖：Setup改出非法cfl时普通/observed路径均拒绝、失败后恢复原观察器；
初始非法controls不进入模型；Setup新增两种组分后smallx总量约束重新检查。
原Setup主动抛错、观察器恢复及初态观察测试保留。
CPU ARCH及受影响目标构建PASS，28并发最低可用约13.6GiB，无swap增长。
initialization_probe、configuration_entry_contract、preview_cellular_2d三项CTest
PASS（二维组内10项真实CPU init-only检查），diff-check PASS。
无演化、CUDA或科学原始输出上传；API说明同步普通读取严格性与检查边界。

这只是共同数值检查入口，不是最终只读配置，也未证明任意Setup修改的来源；
数值合法但未声明的改写、Setup派生值、最终运行身份仍需下一步收束。
现有内置simulation cpp未发现直接typed配置赋值，不能据此保证任意用户模型不修改。
整体配置/Studio/Jeans/RZ目标继续，未push或建立完成tag。

### O7.0 保留加载边界来源证据

RuntimeParams 的文件和内存入口在完整聚合检查成功后，保留不可变的
ConfigurationInput：case、purpose、原 token、标准/模型/组分/辅助参数记录，
包含来源、位置、允许默认和派生证据。SimConfig 私有持有该快照；
调用方只能取得 const 记录，复制配置共享其生命周期。默认构造不产生快照。
此快照仅代表加载时的输入；后续可变字段并不因此获得有效/就绪认证。

新增回归核对 explicit/default/absent/derived 的区别，表达式原 token、
文件组分位置、parser/临时配置销毁后的生命周期，以及修改 cfl/x_pos
不改写原加载证据。首次测试发现 GetAllParams 对坏语法提前抛错；
已将快照采集移到 RequireDeclaredInputs 之后，保留聚合诊断语义。
修复后 configuration_input、configuration_entry_contract、
configuration_v3_contract、mainline_authority、initialization_probe、
preview_cellular_2d 六项 CTest 全部 PASS（18.37 秒）。
CPU 增量构建 28 并发 PASS，最低可用约14.4 GiB，无 swap 增长，
git diff --check PASS。未修改科学公式/容差，未执行演化/CUDA或上传 raw data。

Setup 合法数值改写的来源核对、受控最终只读构造及直接 C++ 运行边界
仍未完成；后续必须使用本记录核对，而不是将非空快照当作 validated 标记。
整体目标继续，未建立完成 tag 或 push。

### O7.0 准备快照与CLI只读分发边界

加载成功后保留 typed 值快照。SetupChecked 在调用前要求来自完整 case-aware
加载且值未被改写，调用后重新检查数值和所有配置分组/自定义映射。
合法数值的未登记改写返回 UNDECLARED_CONFIGURATION_CHANGE；
比较包含 dim、AMR/plot 派生选择和可写 G_const，不采用内存字节比较。
观察器不属于科学输入比较，原失败恢复逻辑保持。

成功准备产生私有构造 PreparedConfiguration，拥有 const 配置/组分副本。
CLI 输出与日志使用该副本，DispatchSolver 不再接受任意 SimConfig 或独立 species，
同时检查 model 实例身份和 Evolution purpose。现有 auto backend/NSE 内部分发
仍按原实现处理；本次没有改变科学公式或策略，不能据此宣布最终所有层均只读。

初始化探针改用具名完整 Sod 输入加声明的 a/b，不再默认构造科学问题。
测试覆盖缺少加载证据、Setup前改值、Setup后数值合法但无来源的 cfl/case/out_dir/
允许默认/AMR派生/G/整体替换，以及只读副本不受随后可变配置或组分修改影响。
原非法值/species-count/观察器恢复/真实Init观察检查保留。
CPU ARCH 和受影响目标编译 PASS，28并发最低可用约14.8GiB，无swap增长。
六项CTest PASS（configuration_input、configuration_entry_contract、
configuration_v3_contract、mainline_authority、initialization_probe、
preview_cellular_2d，18.47秒），diff-check PASS。

尚待：模型提供值/派生值的正式登记与来源传播、实际材料来源、
更下层 Driver/Preview 数值消费迁移、所有直接C++入口以及完整输入/回归迁移。
目前拒绝未登记改写是安全的中间状态，不替代计划要求的合法派生支持。
未运行演化/CUDA、未改科学门槛、未上传raw data、未push/建立完成tag。

### O7.0 注册模型的标准输入提供值

CaseConfiguration.standard_values 可在 Setup 前提供具名 CaseDefined/Derived
标准输入，复用原类型/选项/标量/关联/必填检查。显式输入优先且非法输入不回退；
原始缺失保持 Missing/null parsed/无伪造行号，允许默认仍是独立来源。
派生要求依赖和 owner；拒绝循环/缺失/无效依赖、错误类型/键及伪装为
DocumentedDefault。加载快照保留注册源码文件和SHA，缺少来源身份时拒绝。

模型提供开关后再次解析消费者条件，提供值本身必须保持稳定；
不执行任意Setup以探测需求。没有给当前生产case添加物理默认或改公式。
合成声明测试验证typed映射、source/raw区别、显式值不被覆盖、错误值不回退、
完整来源、EOS控制消费者重算和不稳定声明拒绝。API文档同步范围。

最终CPU构建 PASS，28并发最低可用约14.0GiB，无swap增长；
configuration_input、configuration_entry_contract、configuration_v3_contract、
mainline_authority、initialization_probe、preview_cellular_2d 六项CTest PASS
（18.39秒），diff-check PASS。没有演化、CUDA、raw上传、push或完成tag。
自定义case输入提供值、材料来源及剩余只读/入口/样例迁移仍待完成；
整体配置/Studio/Jeans/RZ目标继续。

### O7.0 模型Get消费已解析记录

检查发现加载来源记录尚未决定实际Get结果，模型仍读取可变custom数值/字符串
并接受调用处fallback。现已将正式加载配置的Get接到loader私有记录：
模型/组分/辅助输入的resolved值和加载token；未声明读取拒绝，
声明但无resolved值拒绝，类型不一致拒绝。调用处fallback不再提供物理初值。
derived log_dir从其resolved来源读取，不写入原始custom map。

回归覆盖普通/observed读取同样严格，条件不适用却被消费时不得使用fallback，
可变数值和token被改写后Get仍返回原解析值。Setup前快照仍拒绝这些适配表改写。
初始化探针中严格整数测试继续使用单独的狭义词法视图，避免测试仅因
未声明读取而通过；该视图不能进入SetupChecked/PreparedConfiguration。
API文档说明metadata中的caller fallback不是批准默认。

最终CPU ARCH及受影响目标构建 PASS，28并发最低可用约14.0GiB、无swap增长；
configuration_input、configuration_entry_contract、configuration_v3_contract、
mainline_authority、initialization_probe、preview_cellular_2d六项CTest PASS
（18.36秒），diff-check PASS。未执行演化/CUDA、raw上传、push或完成tag。
网络/inspection直接遍历custom adapter的剩余路径、材料来源及其余配置迁移
仍待完成；当前不宣称已删除所有重复适配存储。整体目标继续。

### O7.0 网络组分读取已解析输入

内置Timmes与新生成网络统一调用InitialComposition原始输入读取器，
要求完整且未改写的加载状态，读取声明的组分值；不再遍历可变custom数值表。
大小写核素名仍按Core声明匹配，读取/单位观察保留原始输入拼写及token。
缺失稀疏成员仍是声明零值。共享步骤不进行floor或归一化。

审计发现生成网络原算法与Timmes不同：本次分别原样保留smallx加法/除以sum
和normalize_composition，不借配置迁移统一数值算法。已有外部生成包需再生成，
本次不改写历史包或科学数据。

配置测试新增真实BD aprox13的原始比例、稀疏零值、原归一化逐值一致、
大写XC12观察身份、默认构造与改写拒绝。首次测试误配aprox19，
被xh1未声明拒绝；已按fixture原network_name修正为aprox13，同时保留
错误网络含额外核素时不得静默补零的拒绝用例。原物理输入未改。

CPU ARCH及相关目标构建PASS，28并发最低可用约14.5GiB，无swap增长。
七项所选CTest中入口/v3/运行策略/初始化/真实Cellular/网络生成器六项先通过，
修正测试网络后仅重编译并复测configuration_input，PASS；diff-check PASS。
没有重复已通过未受修改的检查。没有演化、CUDA、raw上传、push或完成tag。
材料登记来源、剩余inspection适配表及配置/Studio/Jeans/RZ整体任务仍继续。

### O7.0 删除旧custom双表及未加载Get回填

生产源码盘点后删除SimConfig公开custom_params/custom_string_params及
CaseParameterValues.h。模型参数只有loader写入的private resolved记录；
未加载Get返回INCOMPLETE_CONFIGURATION，无法再由调用处fallback组成模型值。
RuntimeParams从不可变原始输入和resolved值为观察器临时生成快照，
不是另一份可写运行配置。Preview/inspection改为加载身份检查，
shock_dir使用已严格解析的声明int，不再从double适配表重复推断。

参数观察测试迁移到具名完整fixture和注册case，保留普通/observed严格数值、
整数/布尔、来源歧义、未知约束和不可绑定路径。未归属的观察值直接测试
observer，不再把任意config override当成受支持行为。数值转换单测直接检查
原Parse/ValidateNumeric函数；初始化/加载测试同步删除双表修改假设。
源码搜索确认仅测试中的禁止声明/生成字符串检查仍提到旧字段。

CPU ARCH及六个相关可执行目标构建PASS，28并发最低可用约14.1GiB，
无swap增长。七项CTest（入口、v3、parameter_reads、configuration_input、
mainline_authority、initialization_probe、preview_cellular_2d）全部PASS，
18.07秒；diff-check PASS。未运行演化/CUDA，未push/tag或上传raw。
材料来源、case提供值/其余输入迁移、完整Core/Studio及后续3C/Jeans/RZ
仍未完成；整体目标继续。

### O7.0 材料来源实际路径审计

新增MaterialInputProvenance.zh-CN.md：以b523e0ee源码与当前CPU binary核对
14个注册模型的编译cpp SHA，全数一致；逐字段记录直接add_species常数和
gamma/gas_cv输入，以及内置/生成网络各自属性所有者和共享源码SHA。
确认三个模型当前不登记species，不能添加统一非空门槛或伪材料；
内置1.6667与生成器5.0/3.0的既有差异不得顺手统一。
本轮只读审计加来源记录，不是材料运行时provenance实现或科学验收。
下一步按所有者提供逐属性来源并接准备边界，保持现有实际材料数值。
未重复编译/已通过回归，无Setup/EOS/演化/CUDA、raw上传、push或完成tag。

### O7.0 材料来源接入准备结果

新增Host侧MaterialValue，区分ModelDefinition、ResolvedInput、NetworkTable
与NetworkDefinition。六个直接材料模型保留原常数与gamma/gas_cv数值，
经MaterialConstant/MaterialInput附带注册源码身份或解析键；
内置/生成网络的AION/ZION和适配器常数分别标记。未改任何原材料数值、
归一化或设备数值视图。网络owner仍不是已验证网络包hash。

SetupChecked在发布PreparedConfiguration前核对登记来源与数值；
缺少来源/登记后改值拒绝，零species的原模型保留。数学单元的raw double
重载不再被当成有应用provenance。测试覆盖数值改写、未归属登记、输入键/
模型identity、网络table/constant区分；原失败与实际Cellular初始化继续通过。
公开中英文Reference及材料示例更新，API/UI完整来源显示仍未实现。

CPU构建PASS，28并发最低可用约14.1GiB，无swap增长；
八项CTest（入口/v3/parameter_reads/生成器/configuration_input/
mainline_authority/initialization_probe/真实Cellular）全PASS，18.78秒。
逐项review仅包装材料实参后更新六个cpp单位证据SHA；
真实--list-cases确认14个compiledSourceSha一致且单位证据均current。
diff-check PASS。未演化/CUDA/raw上传/push/tag；整体目标继续。

### O7.0 Sod 正式输入与 Preview / metadata 回归迁移

Sod.par 采用已验证的 configuration-v3/sod-valid.par 所登记的六个显式
旧有效控制值；原 Riemann 输入、EOS 数值、离散与科学阈值不变。
Preview 测试 override 改为替换唯一赋值，重复键仍由专门失败测试覆盖，
不再依赖 last-wins。缺失 x_pos 不使用 Get 字面量作为物理默认；
非法数值、未知 EOS、未知 case 在配置阶段失败，不伪造 Setup read/binding。
Helm 测试显式提供原 eos_coulomb_mult=1，保留原 EOS 源身份、范围拒绝
及场值判据。

首次回归仅未知 case 的旧 support-stage 断言失败；核对
ConfigurationInput 的 UNKNOWN_CASE 及已有 v3 覆盖后迁移为 configuration。
最终真实 CPU Preview 11 PASS / 1 明确跳过，metadata 8 PASS，diff-check PASS。
跳过项为原 opt-in production simulation oracle，本轮未运行演化；
其他成功/失败 Preview 均核对不创建文件或目录。无 Core 实现变更、无需重编译。
尚未完成其他 canonical inputs/API tests、完整回归及后续整体目标；
没有运行 CUDA、上传 raw、push 或创建完成 tag。

### O7.0 持久 Preview Session 显式输入回归

CooperativeHotspots 正式输入补齐六个旧有效控制值；逐项核对集成基线
8fc0dd25 的 StandardParameters/GlobalDefs，保留 cpu、Coulomb=1、
roe、min_eint=1e-10、smallt=1e5、smallx=1e-20，不改热点定义或收敛预算。
会话测试读取完整 Sod/Cellular/Hotspots 输入，编辑替换唯一赋值。
取消测试若请求在 Setup 前失败立即给出失败响应，避免等待不存在的事件。

迁移 helper 首次误写 literal backslash-n 导致配置缺项；修正换行后原8项PASS。
新增真实 warm-session 配置失败/恢复：缺失、重复及非法 x_pos 均配置阶段
退出3，无旧 field/metadata/binding回放，输入hash正确，资源清理后下一有效请求
与首个成功响应一致。最终9/9 PASS，5.444秒，diff-check PASS。
保留原 cold/warm 完整结果比较、EOS同size/mtime内容变化、热点重新Setup、
真实初始AMR、transport边界、request-limit和process termination/restart检查。
无时间演化、CUDA、Core重编译、raw上传、push/tag；完整其他模型回归仍待迁移。

### O7.0 正式模型输入静态审计及 Sedov/Jeans 迁移

审计7个正式输入（Sedov/Gaussian/RT/GravityBox/JeansWave/Cellular/SNIa）。
全部仍有缺项；Gaussian还存在INVALID_COMPOSITION，RT limiter存在
INVALID_OPTION，保留失败待追溯原行为，不替换物理值使其通过。
其余包括条件依赖未知、旧case Get隐含值，不把静态缺项当作物理失败。

Sedov/Jeans补入基线StandardParameters/GlobalDefs的cpu/roe及原数值floor。
Sedov center_z显式为原域中点0.5；Jeans phase=0/mode=1/standing_wave=false
保留既有Setup有效值，引力periodic/rtol=1e-10/atol=0来自基线登记默认。
没有修改Jeans公式、G、波态或误差门槛，不代表O7 Jeans新功能完成。

配置v3新增正式Sod/Sedov/Jeans/Hotspots输入完整性与input来源检查，
全套17/17 PASS（1.863秒）。实际CPU inspect-case：Sedov 9个采样、
Jeans 3个采样均ok/Setup ready，输入SHA匹配；临时cwd无文件，
无timeStepping、CUDA或Driver输出。该检查不等于完整场预览/演化验收。
无需重编译；未push/tag或上传raw，其他模型迁移及整体目标仍未完成。

### O7.0 RT 原策略迁移与 Gaussian 声明问题定位

追溯8fc0dd25 PolicyDescriptor：LimiterPolicies为UseDefault，MinModPolicy
标记default=true；RT旧limiter=none不是NoLimiter，而是静默回退MinMod。
现将正式输入显式化minmod，保留PPM及原科学参数；补齐cpu/roe/floor、
external x/z零分量、burn/diffusion=false，均来自原登记默认。
没有扩充策略或恢复未知值fallback。配置v3全17项PASS（1.821秒），
其中正式样例矩阵新增RT；真实RT inspect-case成功、9采样，无文件/演化/CUDA。
diff-check PASS，无编译/push/tag/raw上传。

Gaussian未通过的问题已定位：Setup调用SetupNetworkAndFractions，
但随后default_X.assign(...,0)，Init仅用前两个species及高斯分布构造组分。
本轮引入的统一正组分sum要求因此把被丢弃的中间读错误当成物理输入必需。
后续应分离network species登记与外部初始组分读取，声明模型自产组分来源；
不能给Gaussian.par随意补xhe4/xc12来绕过，也不能全局放宽其他模型正sum。
Gaussian实际修复、完整模型检查/后续整体目标尚未完成。

### O7.0 Gaussian 模型自产空间组分契约修复

CompositionDeclaration新增consumes_input（默认true），Gaussian显式false。
其网络键仍有所有权和严格解析/重复/负值检查；缺失不生成虚假零值来源，
API applicability=not-applicable。其他模型的正有限组分sum规则不变。
共享helper拆出SetupNetworkSpecies，复用同一注册分派；Gaussian不再读取
随后被丢弃的外部组分。Init高斯表达式、物种顺序和材料数值均未改变。
正式Gaussian输入仅补原cpu/EOS/floor/模块控制值，无人为补造质量分数。
Gaussian cpp单位证据在逐行检查后更新，真实capability报告current。

CPU增量构建28并发通过，最低可用14097252KiB、无swap增长，
主构建29.421秒；单位证据单文件后续增量链接通过。
配置v3最终19/19 PASS（1.865秒）：含静态缺失null/显式不适用、
非法值与重复拒绝、Cellular缺组分仍拒绝；真实Gaussian 9点Init精确匹配
原高斯组分公式，外部组分变化不改data并明确unobserved，无文件/时间步。
configuration_input、parameter metadata、Preview session、Cellular2D四项
CTest全部PASS（21.60秒）。不据此宣称完整演化/EOS科学验收。
API文档同步；diff-check PASS。完整模型/Studio回归仍待完成，
未运行CUDA、push/tag或上传raw，整体目标继续。

### O7.0 GravityBox 正式输入显式迁移

对照8fc0dd25 Setup确认burn=false时network_name由case选none，
不能误用旧全局aprox19。正式输入补显式none与原gas_cv=1.2471693927e8，
原temperature_amplitude/velocity0=0、hydrostatic_radial=false；
center_x=5e7和width=8e6来自此固定域中点/0.08域宽。
cpu/roe/floors/引力rtol/atol均保留已追溯旧值，无新增G覆盖或物理变更。

配置v3正式样例矩阵加入GravityBox，全19项PASS（1.881秒）。
真实CPU inspect-case成功、3采样；逐点密度精确匹配原周期公式，
温度1e7、速度0和单气体fraction=[1]一致，输入hash匹配、无文件输出。
此检查不执行Poisson或演化，不代表自引力科学验收。
diff-check PASS；无重编译/CUDA/push/tag/raw上传。其余输入与整体目标继续。

### O7.0 Cellular/SNIa 正式输入迁移

Cellular.par显式补原左边界outflow，NSE true/4.5e9/1e6及CPU、
EOS、ODE、floor、关闭diffusion等登记值；修正“未填使用默认”的旧说明。
SNIa二维Cartesian保留原热扩散，显式关闭原未开的species/viscous通道，
补原diff_cfl、ODE、Coulomb、floor、引力容差、enucDtFactor=1e30；
center_z=0.5来自Setup成员原值。未改各文件已有物理数值/终点/步数。

正式输入矩阵新增上述两项，配置v3 19/19 PASS（1.911秒）。
真实CPU inspect-case：Cellular 3点、SNIa 9点均成功，输入hash匹配、
无文件/时间步。为临时cwd显式映射本地EOS路径，未改正式相对路径。
此证据只覆盖声明和初始化，不替代四模块演化或独立科学参考。
其他SNIa几何/AMR变体、全模型检查和完整后续目标仍待迁移/验证。
未重编译、CUDA、push/tag或上传raw。

### O7.0 SNIa 几何/AMR 变体显式输入迁移

五个AMR变体仅补静态检查确认缺失且前次已核实的旧控制值；
保留各自geometry、domain、blocks、refinement、gravity_rtol、输入场与终点。
二维center_z补原Setup 0.5，三维已有center_z不覆盖。
未更改物理函数、独立参考、预算或AMR参数。

配置v3新增SNIa六份正式输入矩阵及显式来源/热扩散通道检查，
全20/20 PASS（1.950秒）。五个真实CPU inspect-case均ok：
二维Cartesian/polar各9点，三维Cartesian/cylindrical/spherical各27点，
请求hash一致、临时cwd无文件、timeStepping未执行。
未运行实际AMR层次、Poisson、演化或CUDA，不能据此宣称几何科学验收。
diff-check PASS；未编译/push/tag/raw上传。剩余输入/API和整体目标继续。

### O7.0 14模型 inspect-case 回归迁移

检查测试改用已迁移正式输入；没有正式simulation par的模型使用完整公共
控制及原Setup有效case值。BurnOneZone复用bd-config-v3，BurnGradient沿
原初始化温度形状显式输入。只在测试副本映射EOS绝对路径/CPU检查CUDA请求。
unique edit替代重复追加；unknown future_knob改为配置失败覆盖，
非法integer在配置阶段拒绝，命令错误封套配置版本更新为3。
旧network_name Setup read已移除，string metadata检查改为真实读取hotspot_mode。

初次仅UNKNOWN_CASE旧support退出4断言失败；迁移为统一配置退出3后，
完整6个测试方法PASS（3.908秒），其中注册矩阵实际遍历14模型：
逐模型compiledSourceSHA/单位evidence current、3^dimension Init样本、
DENS正值/字段单位、无CUDA/科学输出均验证；Sedov1/2/3维单位、
Gaussian曲线坐标、错误metadata、整数拒绝和log零值检查保留。
这是inspect-case初始化覆盖，不是全模型full field Preview或演化认证。
diff-check PASS。未重编译/演化/CUDA/raw上传/push/tag，其他完整回归继续。

### O7.0 部分输入坐标摘要恢复及 Host 接线

旧配置回归审计发现inspection缺少当前坐标摘要，不只是测试默认值过时。
现在仅以resolved geometry/nblockx1/2/3调用共用CoordinateMetadata；
缺失/非法/拓扑不合法返回null，不构造默认SimConfig；
其他物理缺项不抹去已知拓扑。保留现有九种几何/维度语义，不提前改RZ。

真实Core/Host检查首次暴露Host校验后丢弃返回对象，导致null仍穿透
optional类型；改为路径检查和响应复用同一已校验对象，null映射缺省，
没有任何默认坐标/维度回填。API说明和真实集成断言同步。
单文件CPU增量编译PASS；配置v3 21/21 PASS（2.064秒），
覆盖九映射/单位/缺项/非法/非拓扑缺项。实际ConfigurationAdapter四类
请求PASS，typecheck/lint及完整Studio180/180 PASS，diff-check PASS。
此Host证据使用隔离测试Build Profile，不冒充真实工程Build provenance。
剩余旧configuration_api/ui_expansion回归和动态diffusion/AMR摘要仍待收敛；
无演化/CUDA/push/tag/raw上传，整体目标未完成。

### O7.0 部分输入 diffusion 摘要恢复

共用DiffusionMetadata增加EOS/enable显式重载，Preview仍走原字段/规则，
inspection仅在两者resolved时提供摘要，否则null。没有默认SimConfig
或默认开关。Host接受并校验version/source/channel/conflict keys，null
转缺省；Studio既有冲突Remove路径重新取得真实Core摘要。
测试覆盖未知、关闭、Ideal常系数、Helm状态相关来源、显式零系数冲突
仍保留parsedValue及diagnostic，不以隐藏/删除消除错误。

CPU增量PASS（14.169秒，无swap增长）。配置v3 22/22 PASS，
真实Core/ConfigurationAdapter四类请求PASS；Studio180/180、
lint/typecheck、diff-check PASS。受共享helper影响的普通Preview与
PreviewSession两个CTest PASS（7.31秒，原演化oracle保持opt-in跳过）。
未运行演化/CUDA/push/tag/raw上传。动态AMR及旧API套件仍待收敛，
这些局部成功不代表配置阶段或整体目标已完成。

### O7.0 部分输入 AMR 指标摘要与运行解析统一

将RuntimeParams原指标token解析/过滤原样抽到RefinementSelection.h；
运行和inspection共享，Preview/inspection共用RefinementMetadata重载。
缺拓扑/use_burn/refine_var依赖返回null，不创建默认SimConfig或网格。
alias/无可用指标现返回INVALID_REFINEMENT_SELECTION，保留原输入；
只选JENS不回退DENS。namedSpecies仍明确需要case Setup，不冒充已验证。
Host校验choices唯一键/bool/reason并规范化null，既有UI摘要重新接通。

首编因项目Json无is_null接口失败；改为直接检查typed依赖后增量通过。
最终配置v3 23/23 PASS（2.246秒），含过滤、缺依赖、JENS-only明确失败。
抽取后configuration_input、普通Preview、PreviewSession三CTest PASS
（7.88秒；随后仅inspection错误报告修改），真实Core/Host四类请求PASS。
最终Studio180/180、lint/typecheck及diff-check PASS。受控构建无swap增长。
未演化/CUDA/push/tag/raw上传。旧API测试迁移、完整配置目标与后续工作未完成。

### O7.0 原 configuration_api_contract 整套迁移

旧套件改为明确overlay完整Sod输入，empty单独以partial输入检查；
无运行默认回填、无重复覆盖。schema目录从StandardParameterEntries核对
94 active并排除退役G，allowedDefault/input/source/condition字段按v3验证。
保留数值全token/整数/浮点/expression边界、范围/跨参数、九种几何单位、
未知策略、六退役键、无EOS/设备/文件访问和90k键预算拒绝覆盖。
Preview非法输入使用唯一赋值，避免被duplicate错误提前掩盖数值错误。

新增非活动输入保留测试首次误选Core明令禁止的Coulomb/HLL组合而失败；
恢复原两项拒绝断言，用burn=false的合法ode_rtol和diffusion=false的
合法D_spec验证保留/不适用。不改Core约束或为通过而删除原覆盖。
最终13/13 PASS（10.671秒），diff-check PASS。无Core修改/重编译、
演化/CUDA/push/tag/raw上传。ui_expansion及更广泛配置/运行边界仍待完成。

### O7.0 validation runtime EOS identity fail-closed

核对 StandardParameterEntries 的 eos_type required/no-default 与 EOS API 枚举后，
移除 validation_provenance.runtime_inputs 对缺失 eos_type 的 implicit ideal。
缺失/空/未知值拒绝生成 runtime identity；显式 scientific override 可提供 EOS，
且原参数 SHA 与 override 均保留。未修改物理定义、阈值或 Core。

runtime_validation_inputs 23/23、validation_provenance 40/40 和 diff-check PASS。
这些是 Python mock/临时文本 fixture 测试，不是 CPU/CUDA 科学验证。
Tabular 可能按源表组分条件加载辅助 eos_helm_table_path，而验证工具当前
仅记录主 eos_table_path；该依赖覆盖缺口尚未修复，不能宣称完整 provenance。
AMR 生产 t=0 checkpoint 对照仍因自动审批边界拒绝而未重试。
无 ARCH 运行/重编译、科学输出、push/tag。

### O7.0 validation input ambiguity rejection

对照 ConfigParser 的首个 # 注释、首个 = 分隔及 trim 规则，验证工具
read_parameter_map 不再静默 last-value-wins 或忽略 malformed line。
重复键（含相同值）、空键及无等号行拒绝，错误带文件/行号；
空值、表达式和含额外 = 的值保留为 raw，不在 Python 重做 Core 语义解析。
参数 renderer 写目录/文件前检查 canonical source，不能用执行 override
掩盖重复输入。runtime identity 的 scientific override 也不能掩盖该问题。

runtime_validation_inputs 26/26、validation_provenance 40/40、
validate_backend_results 30/30、diff-check PASS；均为工具单元测试。
另已核实 tabular_source_fingerprint 由 Core inspect_tabular_source 决定
是否绑定电子表 hash，涵盖源格式和 interpretation。Python 依赖记录仍未
接入此权威信息，不能仅凭存在 eos_helm_table_path 认定实际消费；该项未完成。
无生产运行/科学输出/重编译/push/tag，AMR checkpoint 对照未重试。

### O7.0 shared AMR selection diagnostics before Setup

将既有 ResolveRefinementSelection 放入 AnalyzeConfigurationInput 的已知输入分析，
结果以 optional selection 保留供 inspection 消费；无可用 indicator 或旧别名
的错误现在进入共用诊断，可与其他缺项一次汇总。inspection 不再独立追加相同规则。
维度/geometry/burn 未解析时不猜值；named species 的运行期登记检查仍不冒充完成。
不修改 indicator 过滤规则、AMR 数学或 JENS 可用性，不构造网格/模型。

新增 C++ 无资源断言覆盖 JENS 与缺失 cfl 聚合、parsed token 保留、
RequireDeclaredInputs 拒绝和未知维度不推断。编译前发现断言误写旧错误码名称，
修正为实际 MISSING_PARAMETER 后测试通过，没有更改 Core 错误码。
受控增量构建 23.233 秒，最低 available 16346100 KiB，swap 峰值 0。
configuration_input、configuration_api_contract、configuration_v3_contract
3/3 CTest PASS（13.75 秒）；Studio 180/180 PASS。
真实 Core/Host v3 四类请求 PASS，CPU binary SHA256
e6c60aa3ae9dcc7bf1c1bd7b7e2ba745bb815739b92f0b02a05117d983809719。
Host 集成使用隔离 test Build Profile，不代表真实项目构建 provenance/UAT 已完成。
未执行生产 AMR checkpoint 对照、演化、CUDA、push/tag；完整 O7.0 仍未完成。

### O7.0 rejected AMR selection record consistency

共用选择诊断发现无可用 indicator 时，refine_var 现标为 invalid，
保留 raw/parsed/location，清除 resolvedValue/valueSource/sourceEvidence。
避免 rejected selection 仍以可用配置值形式呈现；不改变选择规则。
inspection 回归覆盖 JENS、旧别名 RHO、1D VELY、burn-off ENUC 和空选择。
受控 CPU 增量构建 25.262 秒，无 swap；configuration_v3_contract 和
configuration_input 2/2 PASS（2.33 秒），diff-check PASS。
未执行生产 AMR、simulation、CUDA 或上传原始结果；完整目标未完成。

### 2026-10-02 授权恢复联合交付

用户明确以 compute/optim 当前联合计划取代旧 Phase 3B 对以下操作的限制：
允许 O7.0 科学 Core G 接线、可写 gravity_G 退役、调用方/配置/checkpoint
身份/测试迁移；允许现有明确 t=0 AMR checkpoint 对照，原始 H5/checkpoint
保存在本机持久目录，仅提交处理后指标、摘要、输入/构建身份和必要诊断。
冻结物理定义/阈值不变；历史非物理 G 等效换算提交维护者确认。
t=0 覆盖独立标注，不代替后续演化验收。继续 Linux/WSL，不做 Windows 适配。

恢复前 HEAD 06e67f30，工作树 clean；fetch 后 origin/compute/optim
仍为 8fc0dd25eefd2243e8c36f85440bac46994e2e73，无自动 merge/reset。
此前审批拒绝记录属于历史状态，现按这次直接授权重新审批推进。

### O7.0 restored authorization: G wiring and persistent t=0 evidence

UniformGravity no longer accepts caller-supplied G; GravityConfig.G_const removed.
SelfGravity RHS/boundary/timestep, GravityStage/checkpoint identity, GravityBox and
JeansWave now use shared CGS constant directly. Existing identity fields retained;
no old checkpoint value is used to overwrite the constant. Removed inactive API
presentation/scalar validation of retired G; registry rejection preserved.
Model source hashes updated. Setup mutation test now targets max_cycles because
G is no longer writable; compile-time tests prohibit the removed public controls.
Historical 1e-20 and FLASH 6.67408e-8 inputs untouched, pending maintainer-approved
scientific migration; full G retirement of all active validation inputs remains open.

Poisson contract/analytic 2/2 PASS. CPU rebuild 29.402s, zero swap.
Configuration v3/input, initialization probe, registered-case inspection and
self-gravity lifecycle 5/5 PASS. UI expansion migrated to explicit nonduplicate
inputs and v3 schema. First run 8/9 methods passed, including Sod/Cellular t=0
production checkpoint comparisons. One applicability enum expectation was wrong;
fixed to satisfied and reran that method only: PASS. Original mesh thresholds intact.
Raw input/preview/log/checkpoint kept in studio/.local/integration/amr-production-oracle.
Processed identity/count/equality summary: O7ConfigurationT0AmrSummary.json.
t=0 checks only initial topology and zero checkpoint time; NOT evolution acceptance.
No CUDA or Windows work, push or release tag. Remaining joint goal unchanged.

### O7.0 shared G checkpoint compatibility regression

补充 checkpoint identity 专项断言：self identity 精确记录共享 CGS G，
相同身份可接受；保存值 1e-20、6.67408e-8、0 和 NaN 明确拒绝，
expected identity 不会被保存值覆盖。不改变兼容字段布局或科学阈值。
arch_checkpoint_compatibility 完整现有单元套件 PASS，diff-check PASS。
原始合成 HDF5 fixture 保存在 studio/.local/integration/gravity-checkpoint-compatibility。
套件包含故意删除 dataset/attribute 的拒绝测试，产生 HDF5 诊断但最终退出0。
这些 fixture 的 step=7 是测试构造数据，不是新运行的演化证据。
未执行科学演化、CUDA、Windows适配或push。历史特殊G输入换算仍待维护者确认。

### O7.0 active gravity validation entry audit

run_self_gravity 的旧 gravity_G restart-controls 检查已改为独立
RETIRED_PARAMETER 配置拒绝；保留其他三项 checkpoint controls 拒绝。
原因：退役键在读取 checkpoint 前失败，不能继续声称验证了 saved-G 兼容性。
实际 saved-G 拒绝已由上一提交 checkpoint 单元测试验证。
Python compile 语法检查 PASS；未运行演化 campaign，不宣称该 campaign 通过。
GravityConstantInputMigrationPending.zh-CN.md 列出径向非物理 G 与 FLASH ARCH
输入待确认项；未改输入、独立物理参考或阈值。

### O7.0 complete configured CPU target compilation

After f491bb99, built all configured CPU targets: 125 Ninja steps PASS.
Memory guard: 25.521 seconds, minimum available 17612008 KiB, peak owned RSS
6573600 KiB, zero swap growth, no guard stop. Local build log:
studio/.local/integration/o7-cpu-all-targets-build.log.
CTest inventory has 67 tests; this is an inventory, NOT 67 passing tests.

Additional scoped configuration/constants/stage/backend checks 15/15 PASS
(0.22s): config_input_records, input_resolution, case_configuration,
physical_constants, state_residency, shared_stage_scheduler, gravity_stage_contract,
boundary_plan, same_level_exchange_plan, amr_operation_plans, amr_flux_surface_plan,
topology_transaction, resolved_execution_plan, runtime_probe_and_capabilities,
compute_backend. Previously passed production t=0 checks were not repeated.
No evolution campaign/CUDA/Windows work; historical physics migration and controlled
final runtime configuration remain open. Full joint delivery not complete.

### O7.0 frozen configuration through actual Driver boundary

Added RuntimeConfiguration with private constructor, owned const config/species;
only checked DispatchSolver is a friend. Existing use_nse=auto resolves first,
then config freezes before resource construction. Euler/RK2/RK3 bindings and
launch helpers through run_simulation now accept this snapshot, not caller-supplied
SimConfig/species pairs. Existing plan/backend resolution and numerical code unchanged.
Compile-time checks prohibit default/external construction and require const access.

First build failed on unqualified PreparedConfiguration in global friend declaration;
qualified the type, preserving private access. Retry build PASS (19.141s, zero swap).
configuration_entry_contract/configuration_input/resolved_execution_plan 3/3 PASS.
Because real Driver interfaces changed, reran existing authorized t=0 Sod/Cellular
topology comparison: PASS; raw evidence in fresh persistent .local directories,
processed summary O7RuntimeConfigurationT0Summary.json. No evolution/CUDA claim.
Public preparation SimConfig and further provenance/metadata migration still remain;
this boundary closure alone does not establish complete O7.0 or joint delivery.

### O7.0 extended CPU component regression after frozen Driver migration

At f2b1f012, additional Preview/parameter/cache checks 7/7 PASS (16.36s):
preview_initial_conversion, preview_parameter_reads, preview_parameter_metadata,
preview_sampling_limits, preview_verified_resources, preview_exact_sample_cache,
preview_cellular_2d.

Additional CPU component checks 21/21 PASS (36.57s): refinement_indicator_math,
curvilinear_metrics, compensated_sum, sparse_ode_continuation, sparse_residual,
checkpoint_conservation_metrics, burn_mainline_reference, generated_nse,
tabular_strict_math, baryon_source_format, helm_components, mainline_authority,
block_handle, reduction_contract, device_block_store_lifecycle, low_density_math,
composite_poisson_contract, tabular_eos_ideal_gas, native_tabular_eos,
tabular_component_completion, sparse_klu_161_equations.

These are existing component/reference tests with unchanged budgets, not full
scientific evolution campaigns or CUDA qualification. No claim of all 67 CTests
passing; no change to historical pending G inputs. No code changes in this batch.

### O7.0 active Sod beginner and comparison inputs

Migrated Sod_beginner.par and SodFlash1D.par to explicit former controls:
sml_rho=1e-12/min_eint=1e-10/max_eint=1e21/hll_wave_speed=roe,
CPU when previously omitted, network_name=none as the checked nonburn Sod input.
Numerical defaults verified against 8fc0dd25 GlobalDefs; physical initial states,
domain, grid, terminal time and output schedule unchanged.
Real ARCH --inspect-config returned status ok, declared completeness complete,
no diagnostics for both. Setup/EOS/filesystem/CUDA not executed; simulation readiness
not checked. This is input migration evidence, not a rerun of FLASH or evolution.

### O7.0 first-run documentation duplicate-key correction

Updated both root READMEs: GPU selection replaces the explicit CPU assignment
in a copied Sod input, rather than appending a duplicate. Added existing stdin
--inspect-config usage and declared-only/no-Setup/no-EOS/no-path/readiness limits.
Command/fixture verified in preceding input migration; no redundant execution.
Case authoring guide still needs declaration migration; public docs not complete.

### O7.0 public case guide declaration migration

Both SimulationCase guides now include static DescribeConfiguration in the public
contract and identical GaussianDensity example, with owned float/unit declarations
and actual material consumer flags. Replaced legacy unknown-as-custom and implicit
Get fallback wording. Explained complete declaration versus resource readiness,
explicit teaching input creation from migrated Sod, and no standard-control mutation.
Extracted identical C++ example to local documented-case fixture; real public-header
C++20 syntax compilation PASS, diff-check PASS. This is compile verification only;
example was not added to production registry or executed as a scientific case.
Further reference/API example refresh remains open; no all-docs completion claim.

### O7.0 parsing reference corrections

Both references now match tested v3 behavior: duplicate/no-equals/empty-key rejection,
declared ownership instead of removed mutable custom maps, no Get missing fallback,
no unknown hydro-method fallback, and nine expression fields excluding retired G.
Legal aliases/explicit auto distinguished from unknown-method fallback.
Documentation-only change; relied on existing parser/contract tests and source audit,
diff-check PASS; no redundant simulation. Full reference migration not yet claimed.


### 2026-10-02 — O7.0 case-scoped usage metadata

- 按 ComputeOptimizationPlan 1.3 在原 case 声明中标注 JeansWave、SmoothAdvection、DiffusionMode、ExternalGravity、BurnOneZone 的 verification 用途；GravityBox 仅 hydrostatic_radial 为 verification。标准数值控制及其余初态参数仍为 simulation。
- Inspector 展示当前 inspection 返回的用途；缺少响应时显示 Unavailable，不按键名推断。用途不改变必填、值解析或可用性。
- 五个修改的 case.cpp 自 Setup 至文件末尾与前一提交逐字相同；单位证据 SHA 仅在确认物理表达式未变后更新。BurnOneZone 只改声明头。
- CPU ARCH 增量构建 PASS（15.144s，无 swap）；configuration_v3_contract、case_inspection_contract 2/2 PASS（6.21s）；Studio ui-render 6/6、typecheck、lint PASS；git diff --check PASS。
- 新断言覆盖 case-scoped 同名 rho0、标准控制不误标、缺失 verification 输入保持 missing/null。此次无 simulation、无新增 t=0 输出；后续演化与完整交付仍未完成。


### 2026-10-02 — Refresh actual v3 API evidence

- 从 clean e4c87d98 的已编译 CPU ARCH 重新采集 schema、有效 Sod、empty、invalid 四份静态响应；记录 binary SHA、精确输入、退出码及实现身份。94 个可写标准参数，gravity_G 仍为 retired，case 用途标签与实现同步。
- 每次在独立空目录调用，确认无文件副作用；不执行 Setup、Preview 或 simulation。输入未改变，没有科学数组或原始 H5。
- 直接依赖这些响应的 configuration/Host/path/catalog/workspace/Inspector 回归 26/26 PASS；git diff --check PASS。历史 candidate 保留候选身份，不冒充当前 binary。


### 2026-10-02 — Current v3 Studio regression and 3C boundary

- 在 edd73d32 执行 npm test：181/181 PASS（包含 Host 测试，不重复相加）；npm run build PASS（TypeScript + Vite，674 modules）。保留 bundle >500kB 提示，未做无关重构。dist 为本地忽略产物。
- 3B 参数布局已按 Core 契约展示 external 三分量，包括非活动轴显式零；本轮只读核对，无重复实现。此前 typecheck/lint 在 e4c87d98 通过，随后只有响应样例与文档变化。
- 3C 尚未完成：WorkflowBar Configure disabled，Run/Restart disabled；BuildRunner 仅支持现有 tree；旧 Profile 为部分硬编码依赖且 dependenciesComplete=false。必须实现受控 Configure、完整依赖身份及独立终端 Run/Restart，不得用现有按钮或 no-work 构建冒充完成。
- 本次为自动化基线，不替代 Linux/WSL production UAT、文件选择器、终端/Host 生命周期验收。未运行演化、未进入 CUDA，完整联合目标仍在进行。


### 2026-10-02 — CMake configuration evidence foundation

- 当前 ARCH build tree 无 File API reply；只读 Ninja deps 确认包含仓库外 HighFive 与系统头。固定少量路径不可升级为完整依赖证明。
- 新增 Host-only CMake cmakeFiles-v1 读取器：绑定真实 source/build/reply 路径，检查版本/大小/读取稳定性，哈希实际配置输入（含外部模块），明确 dependenciesComplete=false，缺编译 include、link 输入及工具链身份。
- 负向边界及本机 CMake 微型无语言工程 2/2 PASS（带空格 source/build 路径、外部 CMake 模块）；typecheck/lint PASS。未 configure ARCH，未 Build/Run 科学任务。
- 此模块尚待接入受控 Configure 和 Build Manifest，不宣称 Configure 可用或构建身份完整；下一步需生成受控 File API query 并补齐实际编译/link/toolchain 证据。


### 2026-10-02 — Controlled Configure execution foundation

- 新增 Host-owned ConfigureProfile/ConfigureRunner：固定 CMake 路径与 argv、受控 env、有界 BuildLog；绑定 source/build/generator，拒绝不同缓存绑定和非空未配置目录，不迁移既有树。生成 File API query，成功后读取实际配置证据，不创建 Build Manifest。
- 本机 CMake 无语言微型工程验证：带空格路径、重复配置、未知 profile、错误绑定、非空目录和非法 CMake 输入；连同 evidence suite 3/3 PASS，typecheck PASS；修复 close 回调 lint 后 lint PASS。
- 未配置正式 ARCH tree。仍待 Host/HTTP/UI 接线、跨 Build/Preview 互斥、取消/退出生命周期及完整依赖采集；此提交不代表 3C Configure 功能验收。Run/Restart 与科学后续目标保持未完成。


### 2026-10-02 — Configure cancellation boundary

- Configure 使用独立 Linux 进程组；取消只向其 PID 对应进程组发送 TERM，仍存活时限时 KILL；等待 child close 才结束 active 状态。准备/证据阶段的取消也不能产生成功结果。
- 真实 CMake 延迟工程验证取消与并发拒绝，取消后 PID 不存在、无成功 evidence；连同 Configure 正/负路径 2/2 PASS，typecheck/lint PASS。
- 尚待接入 Host shutdown、跨 Build/Preview 互斥和 UI；测试不声称覆盖任意自行脱离进程组的外部工具。未运行 ARCH 或改动科学输入。


### 2026-10-02 — Configure first-failure recovery

- 修复首次配置失败/取消留下 query 而无 cache 后无法重试的问题：Host 记录 source/build/profile 身份，仅匹配的已认领目录可重试；不清理目录，不接受其他非空目录。失败或取消响应不保留成功 evidence。
- 真实 CMake 失败→去除测试 cache→修正输入→重试成功；取消和其他绑定检查保留。2/2 PASS，typecheck/lint PASS。初次测试因测试文本换行转义错误失败，修正后通过；未修改验收条件。
- Configure 仍待 Host/UI 接线和完整依赖证据，未执行正式 ARCH 配置/演化。


### 2026-10-02 — Configure HTTP boundary

- 可选 ProjectReader Configure 接入 POST /api/configure 与 GET /api/configure/status；继承精确 origin/host/protocol，POST 仅允许 projectId/profileId，拒绝过期项目/未知 profile/命令参数。配置期间拒绝冲突项目操作，状态保存真实终态。
- 真实 HTTP→CMake 成功与注入拒绝、旧 Build 安全回归合计 5/5 PASS；typecheck/lint PASS。未默认启用生产 profile，项目装配/UI/退出接线仍待完成；不宣称 3C 已完成。


### 2026-10-02 — Configure operation-scoped controls

- Configure 状态公开 operationId；events/cancel 仅作用于匹配任务，旧 ID 返回 404，取消仅 POST 且无请求体。日志复用 BuildLog 有界缓冲。
- Configure 实际 CMake/取消/HTTP 回归 3/3 PASS，typecheck/lint PASS；新增日志可读、过期取消 ID、错误 HTTP 方法和请求体拒绝断言。尚待生产 profile/项目装配/UI，不宣称端到端完成。


### 2026-10-02 — Configure project lifecycle

- openProject 可接收 Host-only ConfigureProfile，校验 source root 与项目一致；Build/Preview busy 条件计入 Configure。CLI/Desktop Linux Host 退出等待 Configure shutdown；shutdown 拒绝新配置并等待实际 close。
- 真实 CMake shutdown/PID 消失/关闭后拒绝重启及已有 Configure/HTTP 检查 3/3 PASS，typecheck/lint PASS。尚未选择生产 ConfigureProfile 或接通 UI，不代表真实 ARCH Configure/Build/Run 已完成。


### 2026-10-02 — Configure/Build association

- 项目装配提前拒绝 Configure 与 Build 的 source/build directory 不一致；单独 Configure 仍严格绑定 managed source。
- 新增 openProject→Configure 真实 CMake 测试及错误关联拒绝；Configure suite 4/4 PASS、typecheck PASS。生产 profile/UI 尚待启用，完整交付未完成。


### 2026-10-02 — Selectable local CPU workflow profile

- 现有 CLI --build-profile studio-cpu-release 现在选择 Host-owned Configure+Build 配对；source root 来自已验证项目，独立 build-studio-cpu，不修改 build-cpu/CUDA 验证树。Release/CPU/OpenMP、输出位置和 CMake 参数由 Host 决定。
- 首次无 build tree 时 Configure 对象仍可用；Build 诚实保持未配置，dependenciesComplete=false。默认暂用 4 jobs，后续资源策略/完整依赖证据仍需接入。
- Configure/profile/project/HTTP suite 5/5 PASS，typecheck/lint PASS。未执行正式 ARCH Configure/Build，Preview 关联、UI 和真实端到端验证仍待完成。


### 2026-10-02 — Configure client response identity

- Configure status/start/cancel 响应补齐协议与当前项目身份；前端运行时校验拒绝旧项目、旧协议、无任务 ID 的 active、错误 operation/result 配对和非零 exit 的虚假成功。
- 新客户端负向测试及真实 Host Configure 回归 6/6 PASS，typecheck PASS。UI polling/button 仍待接入，不以 adapter 通过代替用户操作验收。


### 2026-10-02 — Configure workflow UI

- 工作流 Configure 按 Host profile 可用性启用，显示实际任务状态，支持取消；终端抽屉显示有界日志，关闭抽屉不取消。轮询校验项目/operation 身份，旧项目响应不覆盖当前状态。
- Build 状态读取在无活动任务时重新验证配置和 freshness，使首次 Configure 后可识别实际 tree；仍不将配置成功等同 binary current。
- typecheck/lint PASS，Configure adapter+UI render 7/7 PASS。尚待 production build/真实 Linux UI UAT；Run/Restart 仍禁用，未声称 3C 完成。


### 2026-10-02 — First Configure enables Build

- 修复工作流 Build 按钮同时依赖连接时旧 capabilities.build 的问题；改用当前已校验 Build status.configured 和任务 busy 状态。
- 真实微型 CMake 验证无 tree→Configure→Build configured，binary 仍 missing；未编译时不作成功声明。Configure+UI 回归 11/11 PASS，typecheck PASS。


### 2026-10-02 — Actual local CPU Configure/Build

- 在 clean 8987eb69 经 openProject/ConfigureRunner 实际配置 build-studio-cpu，随后经 BuildRunner 编译 ARCH 成功。旧 build-cpu/build-cuda 保留。
- 内存 guard 41.162s、peak owned RSS 3723316 KiB、swap 0；未停止。manifest 输入稳定，binary 身份见 StudioLocalCpuBuildSummary.json；完整日志留 studio/.local/integration。
- binaryState 诚实保持 freshness-unknown（完整编译依赖仍未覆盖），不冒充 current。此次未执行 simulation/Preview，也不代替 Linux UI UAT 或 Run/Restart 验收。


### 2026-10-02 — Compiler-recorded include dependency capture

- 加入固定 Ninja -t deps 读取器，保留项目外/带空格路径，拒绝 stale、缺失、截断记录；不从文件名扩展猜依赖。
- 实际 build-studio-cpu 读取 66 个 object、735 个独立 input；只读，无编译或演化。解析负向测试 PASS、typecheck PASS。
- codemodel 同时确认 ARCH 关联三个内部库，并含系统动态库与 -lm；完整 target/link/toolchain 哈希和 manifest 接入仍待完成，dependenciesComplete 保持 false。


### 2026-10-02 — Compiler input fingerprints in Build Manifest

- 本地 CPU profile 在成功 Build 时采集实际 Ninja compiler 输入 SHA/size，记录到 manifest；失败以 compilerInputError 明示。文件数/逐文件/总字节限制和读取稳定性检查保持有界。
- 实际只读采集 66 objects、735 files、10089196 bytes 成功。Build/解析回归 8/8、typecheck/lint PASS。
- 此次未重新 Build 更新旧 manifest；尚缺 pre-build 完整输入稳定性、link/toolchain/新增依赖比较，因此 dependenciesComplete=false 保持不变。


### 2026-10-02 — Compiler input stability across Build

- CPU Host Build 记录构建前/后的 compiler input snapshot，比较 object 数、路径集合、内容 SHA 与 size。首次无 deps 或采集失败不伪造稳定；manifest 标记 compilerInputsStableDuringBuild=false，freshness unknown（不谎报必然源码变化）。
- Build/依赖稳定性回归 9/9 PASS、typecheck PASS。完整 link/toolchain 和后续 freshness 动态比对仍待接入；未升级 dependenciesComplete。


### 2026-10-02 — Actual compiler-aware manifest validation

- 在 clean 64a04d2e 经真实 Host Build 路径刷新 manifest：66 objects / 735 compiler inputs，前后内容/集合稳定，binary SHA 与首次构建相同；freshness-unknown 正确保留。guard 1.015s、swap 0，无增量编译、无 simulation。
- 精简证据见 StudioCompilerInputsBuildSummary.json；当前完整记录按 build ID 留本地。发现本地 smoke 脚本旧固定结果文件被本次覆盖，首次摘要身份仍在 Git，但该路径不能再作为首次完整日志；已改为每 build ID 独立文件，避免后续覆盖。旧 guard 日志保留。
- 链接/工具链及持续 freshness 检查仍未完成，不据此宣布完整构建证明。


### 2026-10-02 — Freshness checks compiler inputs

- 本地 CPU freshness 读取实际 compiler dependency graph，与成功 manifest 内容哈希/路径集合比较；固定 tracked list 之外的 header 变化可标 needs-build。缺失/stale/读取失败保持 freshness-unknown。
- 不提升 dependenciesComplete，link/toolchain 完整性仍待接入。此路径读取实际图，不信任持久 manifest 中任意新增路径去扫描文件。


### 2026-10-02 — Configure integration full regression

- clean 01185a62：npm test 192/192 PASS（含 Host，不重复累加），lint PASS，npm run build（typecheck+Vite）PASS，676 modules。现有 bundle size 提示保留，未顺手重构。
- 结果/本地日志哈希见 StudioConfigureRegressionSummary.json。覆盖当前参数、Preview、保存和 Configure 自动回归，不等同 Linux desktop UAT 或 Run/Restart/科学验收。


### 2026-10-02 — Actual compiler driver identity

- 新增 CMake toolchains-v1 读取器，校验 reply 位于选定 build tree，记录 compiler path/realpath/id/version/SHA/size；明确缺 compiler 子程序、linker、隐式库，不宣称完整。
- 实际读取 GNU C/C++ 13.3.0 两个 driver，完整身份留 .local/integration/compiler-driver-evidence.json；typecheck PASS。尚待 manifest 接入与其他工具链组件采集，未运行编译或演化。


### 2026-10-02 — Compiler driver manifest integration

- 新增工具链边界测试：内容变化、错误版本、跨 tree reply 与缺文件均验证；CMake evidence suite 3/3 PASS。
- 本地 CPU manifest 现在沿当前 File API index 采集 compiler driver 身份，失败独立记录 compilerDriverError；不将缺 compiler 子程序/linker/隐式库的部分证据标为完整。


### 2026-10-02 — Persisted compiler evidence validation

- 读取 manifest 时验证 compiler 输入/driver 的结构、SHA、size、绝对路径、重复项与稳定性布尔；损坏记录不恢复为有效 provenance。
- 实际保存/重新读取/篡改字段拒绝及 Build suite 9/9 PASS，typecheck PASS。完整链接与工具链依赖仍未完成。


### 2026-10-02 — Actual target link-input audit

- Ninja 1.11.1 -t inputs ARCH 包含 explicit/implicit/order-only，但明确排除 validation inputs；且含 phony，不可直接作为全文件清单。
- 处理后审计 StudioLinkInputAudit.json 记录实际外部链接文件 SHA/realpath/格式，覆盖 HDF5/KLU/OpenMP 等。libm 等可能为 linker script，不能只哈希脚本就假定实际 ELF 已覆盖。
- 下一步需按 rule 区分 phony、追踪 linker script/隐式库与 compiler 子程序；当前不提升完整覆盖。只读审计，无编译或演化。


### 2026-10-02 — Linker-native dependency evidence

- 本机 mold 2.30.0 支持 --dependency-file。新增默认 OFF 的 ARCH_EMIT_LINK_DEPENDENCIES，仅 Studio CPU profile ON；ARCH.link.d 由真实链接器输出，不改数值 flags/科学逻辑，其他 linker 不支持时明确失败。
- 实际 Host Configure/Build PASS；guard 7.101s、peak owned RSS 2039732 KiB、swap 0。输出包含解析后的 libm.so.6、libstdc++、crt 启动对象，以及 LTO 临时产物；后者链接后已消失，需明确分类，不能静默忽略当完整。
- 未更新旧 CPU/CUDA 验证树，未运行模拟；依赖解析/manifest 接入仍待完成，完整覆盖保持 false。

### 2026-10-02 — Linker input manifest and freshness

- 3C 构建身份：Host-owned linkDependencyFile 读取真实 linker depfile，校验 expected executable、有限 Make 转义及预算；不执行变量或 shell 语法。Manifest 保存持久文件 SHA/size/realpath，缺失项逐项保留，不凭 LTO 文件名猜测完整覆盖。
- freshness 比较链接输入内容、symlink 目标和依赖集合；库变化判 needs-build，证据缺失保持 unknown。未提升 dependenciesComplete。
- 实际 build-studio-cpu 只读采集 75 个现存文件、119779535 bytes，包含 libm.so.6；另 98 个缺失项。原始身份留 .local/integration/link-inputs-20261002T041128Z.json。
- 受影响 Build/link suites 12/12 PASS，typecheck/lint PASS。尚待真实 Host Build 持久化验证；完整工具链、Linux UI UAT、Run/Restart 与后续科学阶段未完成。

### 2026-10-02 — Actual linker-aware Host Build

- clean bb036640d10e5294a0e92b0cd7ca046e7161c940：真实 Host Build succeeded，buildId=a48d762c-e99c-4368-87e2-eb49ca76e313；Manifest 保存 75 个链接文件、98 个明确缺失项。guard 2.014s、peak owned RSS 145008 KiB、swap 0。
- StudioLinkManifestBuildSummary.json 记录 source/binary/depfile 身份及本地证据索引。freshness 保持 unknown，未执行模拟，不等同 UI 或演化验收。
- 回查联合计划 4.2：允许单独确认“按已编译版本运行”，但不能声称覆盖当前源码。因此后续继续 3C 的显式 binary/input 确认与独立终端 Run/Restart 生命周期；完整 current 证明仍作为构建出口保留，不将工具链审计扩张为其他工作绝对前置。

### 2026-10-02 — Independent Linux run supervisor

- 3C 新增未暴露 HTTP 的终端 worker：精确 binary/saved config 身份复核、逐 run 的 input copy、直接文件日志、持久状态/exit code、进程 PID/start ticks、独占 worker claim。stdio 不依赖 Studio/Host 管道。
- 真实 Linux fixture 验证启动器退出后继续运行、错误 run ID 不触发 Stop、无关进程保留，以及组首进程退出后 TERM-ignoring 后代仍受 owned Stop 清理。带空格/中文路径通过；短命进程退出早于 /proc 观察时明确标识，不伪造 start ticks。
- 5/5 lifecycle tests PASS；不是 ARCH 演化/Restart 科学验收。当前尚未接入 Core 预检、用户确认、HTTP/UI 或 Linux 可见终端 launcher，不能宣称 Run/Restart 可用。
- 本机 xterm/gnome-terminal/xfce4-terminal/konsole 均缺失，DISPLAY=:0 可见。后续入口需要明确缺依赖 UX 与实际终端安装/窗口验证，不静默改为无窗口后台计算；Windows 仍范围外。

### 2026-10-02 — Selected binary Run/Restart preparation

- 3C 新增 RunPreparationRunner 与 POST /api/run/prepare：只接受 projectId/caseId/configRevision/mode；从当前关联保存文件读取精确字节，使用已选 binary 的 list-cases/config-schema/inspect-config，复用 v3 类型/身份和 Host 路径预检，前后再次验证 config/binary 未变化。
- 不依赖仅 Sod/Cellular Preview Profile，不把注册列表硬编码；明确 compiled-version-only 与 core-startup-pending。声明完整性、资源存在性和 Core Setup/EOS/backend/checkpoint 检查分开。Restart 必须匹配显式保存的 restart/restart_file，不写配置、不创建科学输出。
- 运行预检/HTTP/project 新增 6 项测试通过；与 Configure/configuration 的相关回归共 14/14 PASS（不是独立累计计数）。最终初始化调整后相关 11/11 复验、lint PASS，typecheck 通过。
- 真实 CPU binary 的保存 Sod v3 输入静态检查 canConfirm=true，无 issue；这只允许下一步用户确认，不是科学运行就绪。完整响应留 .local/integration/run-preflight-ffa7f582-2ccd-4034-9db0-d0cd526677da.json。
- 该静态检查 binary SHA-256：ae6192573b41761aaad325d6eaf6346ad7d6a90a87c8ad8fbdd850792a86e403；config SHA-256：9c8ba5b67718bf3bde6a14c447a5bfe442015813461b9a7ea8417a3e9ec79843。

- WSL root 管理入口安装 xterm 390 及 8 个必要依赖（无系统升级、无密码/sudo 配置改变）；连接 WSLg 的 1 秒 sleep smoke exit 0。默认 fixed 字体加载有警告，需后续明确字体及视觉验证，不能冒充 Linux manual UAT。
- 尚未实现确认计划持有/消费、终端启动 endpoint/UI、真实 ARCH Run/Restart；后续继续这些出口，不开展 Windows 适配。

### 2026-10-02 — Confirmed terminal handoff and Run routes

- 3C Host 保存最近一次预检计划（5 分钟有效），显式 compiled-binary/saved-input 确认只能消费一次；消费前复核配置、binary 和资源可达性。前端修改响应不改变 Host-held plan。
- 新增 Linux xterm 交接、逐 run 的只读输入副本/确认记录、GET 状态及无 body 的 owned Stop。命令/argv/cwd/env 由 Host 固定；项目 Build/Configure/Preview 与交接阶段互斥。Host 生命周期不接管已交付终端的计算。
- 终端未确认交接时保留 run ID、记录 Stop 且不盲重试；迟到 worker 在启动 Core 前检查已有 Stop。无可见终端环境时明确失败，不回退后台运行。
- 当前工程回归 25/25 PASS，包含计划重放/篡改、文件及 checkpoint 路径变化、HTTP 权限、独立进程与后代清理；尚未进行真实 ARCH terminal run，UI 确认面板与 Restart 继续演化仍待实现/验收。

### 2026-10-02 — Real xterm/ARCH t=0 handoff

- clean 301517c2b43d8f56f5c1a5c3972901397e29ca2b：复用既有获准 Sod tmax=0 输入，仅 out_dir 改为唯一持久本地目录；经真实 RunPreparationRunner/RunController/xterm/worker 启动当前 CPU ARCH。
- runId=926efbfc-5fd5-4dfd-b9cd-51453c1da819；exit 0，Total Steps=0、Final Time=0，初始 12 AMR leaves。现有 h5wasm/node 只读确认 PLT time=0、checkpoint step=0/time=0；原始文件留本机，处理摘要见 StudioTerminalT0Summary.json。
- xterm 使用 Monospace 后 terminal.log 为空；已核对本次 terminal/Core/worker PID 均结束。不是视觉/manual UAT、非零演化或 Restart 继续演化验收。
- guard 1.008s、swap 0，仅两次采样；RSS 读数不能代表此短任务内存峰值，不作性能测量。后续仍需 UI 确认/状态/Stop、真实 Restart 与完整 Linux 工作流验收。

### 2026-10-02 — Run/Restart confirmation and status UI

- 工作流接入独立 Run/Restart 预检、保存输入与 compiled binary 显式确认、真实终态轮询和 owned Stop。配置编辑使旧确认失效；Undo 恢复相同文字也需要重新准备。已交付运行保留原始输入身份，不随编辑重命名。
- 共享 Host/client Run 类型，响应核对项目、case、配置 SHA、binary 路径和任务身份。失败不盲目重试；明确 Core startup pending，不将静态 inspection 或 freshness unknown 称为模拟就绪/current。
- npm test 216/216 PASS（包含 Host，不能重复累计），lint PASS，production build/typecheck PASS；git diff --check PASS。日志与哈希见 StudioRunUiRegressionSummary.json；原始日志保留本机。
- 尚缺 Linux 交互 UAT、重开后的运行恢复及真实 Restart 继续演化验收；当前工程检查不替代这些出口。先前 t=0 只覆盖初始化/输出/终端交接，不覆盖后续演化或性能。
- 当前授权以 compute/optim 联合计划为准：O7.0 G 接线及明确 t=0 AMR 对照已获准且已实施；历史非物理 G 等效换算仍待维护者确认，冻结物理定义/阈值不变。后续只推进 Linux/WSL，不开展 Windows 适配。

### 2026-10-02 — Restart input contract migration

- 既有 SmoothAdvection source/resumed 样例在 config v3 下缺 hll_wave_speed/min_eint；执行前静态检查明确拒绝。核对发布基线 25adec4224497981a0c124a3485f786194975be4 的 StandardParameters/GlobalDefs，显式保留 roe/1e-10，未改物理终点或容差。
- 两份样例当前 CPU binary inspect-config 均 exit 0/status ok，无 diagnostics。仅证明静态配置完整；尚未生成 source checkpoint 或进行真实续算，路径/布局/继续演化仍待验证。

### 2026-10-02 — Real CPU terminal Restart continuation

- clean f840c4999a8ea55ccd2c64c6e1a2ab1ef0822194：沿既有 SmoothAdvection 输入与 tmax=0.1，源运行及 Restart 均通过真实 RunPreparation/RunController/xterm/worker，exit 0。只重定位本地输出及真实 checkpoint 路径，不改变物理控制。
- checkpoint 第25步/time=0.025227987917244142 恢复至第100步/time=0.1。最终根属性、4块拓扑、密度/动量/能量/ENUC 及控制数组逐值相同；处理摘要见 StudioTerminalRestartSummary.json。原始 H5、输入与完整日志留本机持久目录。
- 本次 terminal/worker/Core owned process 均无残留；不是视觉 UAT、CUDA、燃烧或动态 AMR 验收。短任务 guard 采样不足，不作性能声明。重开运行恢复、完整 Linux UI 工作流与后续科学计划仍未完成。

### 2026-10-02 — Live Run supervisor identity

- Run worker 记录 Linux boot ID 与自身 /proc start ticks；Host 对 starting/running 读取核验仍是原 supervisor，拒绝旧记录、重启、PID 复用或已退出 supervisor。拒绝时明确 computation outcome unknown，不虚构 failed/succeeded，不按进程名终止或自动重试。完成记录仍可读取。
- Run preparation/HTTP/worker/supervisor 18/18 PASS，typecheck/lint PASS。旧活跃记录缺身份时需人工核对日志；持久运行列表及 UI 重开恢复仍待接入。当前 selected local CPU 配置目录仍受旧 Preview profile 绑定，是后续需解开的独立缺口。

### 2026-10-02 — Persistent Run history Host endpoint

- 新增只读 GET /api/runs，按项目本地 job/state 恢复原 case、config SHA/path 和时间，不依赖旧 project session ID。活跃状态沿 supervisor 身份核验；损坏或未知状态保留记录及诊断，不伪造结束状态。
- 读取 job 有16 KiB预算，历史超过1000条明确拒绝而非静默截断；继承现有 HTTP Origin/protocol 和无 body 约束。新 controller 读取完成记录及损坏状态测试通过，Run suite 11/11、lint/typecheck PASS。
- UI 历史展示/重开后 Stop 尚待接入；此提交只完成 Host 恢复入口，不宣称端到端恢复验收。

### 2026-10-02 — Saved Run history UI

- 工作流新增 Saved run history，从当前 Host/project 读取持久记录，显示独立输入 SHA/path、case、状态、exit 与诊断；旧项目迟到响应不覆盖新项目。活跃记录提供按 run ID 的 Stop，不自动恢复执行或重试未知状态。
- 新增 history 响应校验，拒绝旧项目、重复 ID 和无诊断的 unknown。完整 npm test 218/218 PASS（含 Host）、lint、typecheck+production build PASS；保留既有 bundle warning。
- 真实桌面重开/Stop UAT 尚待执行；当前是 UI 接线与自动回归证据，不能替代人工操作验收。下一步继续 CPU binary 配置关联与 Linux production 工作流。

### 2026-10-02 — Selected binary static configuration adapter

- 无 Preview profile 的项目现在装配静态 ConfigurationAdapter，执行固定 config-schema/list-cases/inspect-config，基于选定 executable SHA 前后核验。selected-binary:SHA 是内容 scope，不是成功 Build/current 证明；现有 Preview readiness 路径不放宽。
- 无 Preview 项目的真实子进程 fixture 验证 schema/inspection 与未知模型拒绝；与旧配置和 Run 回归14/14 PASS，lint/typecheck PASS。前端 ConfigurationBridge/buildScope 与 discovery 仍待迁移，因此尚不宣称 local CPU 参数面板端到端可用。

### 2026-10-02 — Selected binary configuration scope and registry

- 前端在没有 ready Preview build 时使用当前项目 executable SHA 的 selected-binary scope，读取静态 schema/inspection；真实 Preview 仍要求原 readiness。Host /api/cases 在无 WorkflowRunner 时读取真实注册表，fieldModels 为空且 AMR execution capability=null，不以注册冒充已接入执行。
- 完整218/218 tests、lint、typecheck/production build PASS。真实 build-studio-cpu binary 返回94标准参数/14注册模型，未保存 SmoothAdvection 文本 inspection status=ok/no diagnostics；摘要见 StudioSelectedBinaryConfigurationSummary.json。未运行 Setup/Preview/simulation，未改变 build freshness。
- Linux production UI UAT 和 selected CPU Preview/AMR profile 接入仍待完成；不能将静态接线通过当作整个工作流验收。

### 2026-10-02 — Native Linux production desktop entry

- 审计确认旧 desktop/arch-studio 仍转发 Windows exe，本机 Linux UAT 仅临时脚本。现替换为 Linux Electron 原生入口，生产 dist 与直接 Node Host，复用受控端口/token/原生路径及 Save As；Linux packaging 默认输出原生 runtime。不执行 Windows 适配。
- Host desktop 接入固定 build-studio-cpu profile，并使用真实静态 registry；无需 Preview current 才能打开配置。未自动 configure/build，missing binary 仍明确报错。
- 实际 launch 从当前项目打开 SmoothAdvection，desktop.log readiness 记录 Host PID425/port37059；Electron PID378、窗口标题 ARCH Studio—ARCH-compute-optim，renderer bootstrap 指向独立本地 production origin。定向 TERM 结束 launcher 后会话 exit0，Host425消失。该进程证据不等同窗口 close/manual UAT。
- Computer Use 两次返回 [WARN:COPY MODE] WSLg 窗口，截图显示其他窗口，无法可靠交互；停止 UI 点击，未标 UAT PASS。218/218 tests、lint/typecheck PASS；packaged 分发、原生选择器及完整桌面操作仍待验收。

### 2026-10-02 — Packaged Linux production smoke

- clean f488fcaa6f92edcc109a5db3429a455f5a3dc40d：实际 Electron Linux package 成功；从 validation/restart 嵌套目录启动，包内 Host readiness 成功，无 Vite。
- 通过包内 production origin 获取 index（与包内文件字节相同）、94参数schema、14模型registry和3条持久run历史。处理证据见 StudioLinuxPackagedSmokeSummary.json；未运行科学任务。
- 本轮 launcher PID298 定向 TERM 后 session exit0，Host433与launcher均消失。仍不替代原生文件对话框、窗口点击/close和完整视觉 UAT；WSLg COPY MODE 限制未解除。

### 2026-10-02 — Linux launch recovery controls

- Linux 项目页隐藏跨 WSL 发行版输入，增加显式 binary/case/source 字段；提交期间禁用重复 Open，空可选项不发出，错误提示改为 Local Host。沿用同一 Host 参数边界，不增加任意命令能力。
- 实际 desktop Host 负向检查：missing binary 与未注册模型均 exit1，给出明确错误，未发布 readiness；摘要见 StudioLinuxLauncherFailureSummary.json。lint PASS。原生 UI/对话框操作验收仍待 WSLg 图形问题解决，未宣称完成。

### 2026-10-02 — WSLg Wayland diagnostic and launch argument fix

- 只读 WSLg1.0.73.2日志确认 enable_copy_warning_title=1；不能仅凭标题认定截图根因。DISPLAY/Wayland socket存在；单次 Wayland 对照报告 drmGetDevices2 无设备，Computer Use显示空画面，未记作UI通过，未修改系统配置。
- 对照发现开发入口固定 argv.slice(2) 误读 Electron runtime flag。改为定位实际 main.cjs 后提取项目参数；lint PASS，真实 --ozone-platform=wayland 重试成功发布 SmoothAdvection Host readiness（PID424）。定向关闭 launcher后Host消失。
- WSLg视觉/原生对话框UAT仍未完成。没有把错误页或后台readiness当视觉验收，也未放宽渲染/科学标准。

### 2026-10-02 — Desktop asset-connection shutdown fix

- 上轮实测Host退出后Electron仍存活；关闭流程等待assets.close，而renderer长连接尚存。现先停止接入，再closeAllConnections，仅在Host/Build排空后关闭资产/proxy连接，不终止已交付Run。
- 真实HTTP未结束响应回归及launcher suite 3/3 PASS，lint/typecheck PASS。旧卡住进程按已记录PID/start-ticks精确清理，旧会话exit1，未冒充正常退出。
- 修复后真实Linux Wayland启动→Host readiness→TERM，launcher exit0且desktop clean shutdown，Host消失；见StudioDesktopShutdownSummary.json。这是退出工程验证，不替代原生窗口点击/Save As UAT。

### 2026-10-02 — Local CPU Preview identity integration audit

- 直接启用本地CPU Preview会使project.ts选择Preview依赖的ConfigurationAdapter，破坏无manifest时独立静态编辑；临时接线已完整撤回，无实现/构建变化。
- 形成StudioCpuPreviewIntegrationAudit.md：下一步先分离静态binary scope与Preview manifest scope，再绑定既有模型profile。保持当前静态编辑可用及freshness unknown，不以简单开关伪造完成。

### 2026-10-02 — Separate configuration and Preview identity scopes

- CoreParameter context新增configurationScope；ConfigurationBridge从项目选定binary SHA生成静态scope，schema/inspection与参数catalog只匹配此scope。Preview metadata/marker/AMR仍匹配原buildScope，不将静态检查充作Preview provenance。
- 项目装配的ConfigurationAdapter始终选择当前binary，Preview readiness变化不撤销静态配置能力；discovery读取与schema请求分开。完整219/219 tests、lint、typecheck/production build PASS。
- 本地CPU Preview profile尚未启用；下一步验证manifest缺失/Build后的双scope状态切换和真实Preview。桌面UAT仍未完成。

### 2026-10-02 — Local CPU Preview profiles

- 本地CPU profile绑定现有Sod/CellularDet Preview，保持原manifest/input/binary核验及full freshness unknown；配置适配器独立。缺manifest时registry回退静态binary scope，不因此隐藏所有注册模型。
- Configure/Preview/Run相关28/28 PASS，lint/typecheck PASS；真实Build与warm Preview仍待刷新profile身份后验证。本次不扩展模型支持，不修改科学Core。

### 2026-10-02 — Real local CPU Build and warm Sod Preview

- clean9b1bf8bf：标准Host Build成功，buildId=0e348e93-5fbe-4d0a-92b0-5d053f8764bf；binary SHA不变，full freshness仍unknown。guard2.013s、peak owned RSS146820KiB、swap0，非性能benchmark。
- 当前本地CPU profile真实Sod init-only32样本请求两次成功，未保存注释产生不同configRevision；同processToken/generation1，sequence1→2，六个真实字段DENS/PRES/TEMP/VELX/ENER/EINT。session最终shutdown。摘要见StudioLocalCpuPreviewSummary.json。
- 本次不是simulation/桌面UAT，Cellular2D/AMR实际接线及取消/失败恢复仍待验证，未宣称整个3C或联合目标完成。

### 2026-10-02 — 用户授权范围确认与恢复

- 当前以 compute/optim 联合交付计划为依据；本次用户确认取代旧 Phase 3B 对 O7.0 科学 Core G 接线及明确 t=0 AMR checkpoint 对照的限制。允许共享 CGS 常数接线、退役可写 gravity_G、调用方/配置/checkpoint 身份/相关测试同步。
- 冻结物理定义和验收阈值不变；历史非物理 G 输入的等效换算仍须整理依据交维护者确认，不自动迁移。
- 明确 t=0 的既有对照可生成本地验证输出；原始 H5/checkpoint 留本机持久目录，仅提交处理指标、摘要、输入/构建身份和必要诊断。t=0 只覆盖初态/checkpoint 对照，不替代演化验收。
- 恢复前 HEAD 为64148c7a1c6e7c718f14309c6115d51d8b74bad4，工作树干净。沿用已完成证据，不重复 baseline。继续 Linux/WSL，不开展 Windows 适配。具体命令仍按正常执行审批逐项评估。

### 2026-10-02 — Local CPU CellularDet and initial AMR

- 现有有效 CellularDet 输入：二维采样 shape=[12,20]、x1-fastest，共240点；七个真实字段成功返回。初始 AMR status=ok、complete=true，20叶块（level1=4、level2=16），两轮初始化细化。
- 使用同一 project/configRevision/build/binary；本轮不宣称完成 renderer overlay 的 EOS identity/视觉验收。AMR 不含 cell field array，普通采样值不等于 AMR cell average。
- Core execution 明确 timeStepping=not_executed、scientificOutput=not_created、simulationReadiness=not_checked。这不是新的生产 t=0 checkpoint 对照或演化验证。
- 精简证据见 StudioLocalCellularAmrSummary.json；原始响应留 studio/.local/integration/local-cellular-amr-response.json，不提交数组。桌面 UAT、取消/失败恢复及联合计划其他出口仍未完成。

### 2026-10-02 — Real CPU Preview cancellation and error recovery

- 真实本地 CPU Sod：成功 → start acknowledgement 后立即取消 → cancelled 且保留上一成功结果 → 恢复成功 → 重复 solver 配置被 Core DUPLICATE_PARAMETER 拒绝且保留成功结果 → 再恢复成功。所有断言通过，finally 等待 session shutdown。
- 取消后新 session generation=3；错误后恢复沿用同一 processToken，sequence=3。精简结果见 StudioLocalPreviewRecoverySummary.json。
- 本轮只证明 Host 请求状态/保留/恢复，不证明耗时 Core 计算中途取消，也不是桌面 UAT。没有 simulation、科学阈值变更或原始数组提交。


### 2026-10-02 — CPU profile full regression and next-stage audit

- ce447252 clean source：npm test 219/219 PASS（包括 Host，不重复计数）；lint PASS；npm run build 中 tsc --noEmit 和 Vite production build PASS。既有大 bundle warning 保留；日志留本地 cpu-profile-full-regression.log。
- 全模型入口实际缺口：src/api/preview/Preview.cpp 支持判断及 capabilities 仅 Sod 1D/CellularDet 2D；Sampling.h 按 case 选择采样维度；Host previewProfile.ts 也仅两 profile。不能仅扩大前端/Host 白名单冒充 Core 支持。
- 依联合计划4.3，配置/Run出口完成后才扩展科学数据展示；当前 Linux原生视觉/文件对话框UAT未完成，未启动全模型实现。进程/HTTP验证不能替代该验收。顺序清单已纠正过时“未实施”项，但不将部分验证标成整体完成。

### 2026-10-02 — Reconnected Host Run control regression

- 补强既有独立进程测试：启动者退出后，创建新 project session 的 RunController；从持久记录恢复原 run ID/config SHA/running 状态，通过公开 stop() 停止，再读取 stopped 历史。无需原 Host 内存对象。
- Run worker/supervisor 7/7 PASS，lint PASS。本轮为真实 OS 子进程 fixture，不执行 ARCH 科学轨迹，不冒充桌面重开/按钮 UAT。生产实现未修改。

### 2026-10-02 — Known build changes survive incomplete dependency evidence

- 修正 refreshFreshness 的提前返回：Ninja 依赖读取失败记为 unknown evidence，继续收集 link 输入；已有 tracked/link 变化优先给 needs-build。只有没有已知变更时才给依赖 unavailable 的 freshness-unknown。
- 真实临时 CMake/Ninja fixture 验证 header 变化，并在删除 fixture .ninja_deps 后修改 tracked CMakeLists，仍准确报告 needs-build。Build suite 10/10、lint/typecheck PASS。
- 不改变 dependenciesComplete=false、不放宽 Preview readiness、不重新编译 ARCH，不将工具链/临时LTO输入覆盖缺口宣称已解决。

### 2026-10-02 — Compiler driver freshness comparison

- 既有 Manifest 已记录 compilerDrivers，但 refreshFreshness 未消费。现在比较 CMake 选定驱动 language/path/resolvedPath/SHA/size/id/version；已知变化给 needs-build，缺失/不可读取给 freshness-unknown，保持已知源码变更优先。
- Build/CMake evidence 14/14 PASS，lint/typecheck PASS；fixture 覆盖同路径驱动内容变更与驱动缺失。未替换本机编译器、未重新编译 ARCH。
- 这仅补齐 driver 变化检测；compiler subprogram/linker/implicit libraries 完整身份仍未证明，dependenciesComplete=false 保留。不据此宣布干净构建或完整 freshness 通过。

### 2026-10-02 — Restart preparation/confirmation checkpoint identity

- Host-held plan 记录 checkpoint realpath/dev/inode/size/mtimeNs/ctimeNs，确认时重读并拒绝替换/修改；不读取或解释 HDF5、不写 checkpoint。Core 仍执行权威身份/布局兼容检查。
- Run preparation 12/12 PASS（包含同长度 checkpoint 替换后拒绝、重新准备成功），lint/typecheck PASS。
- 这是 prepare→confirm 的文件系统身份边界，不是内容 SHA，也不消除 confirm→Core open 的竞态；不宣称完整 immutable checkpoint handoff。输出目录并发隔离仍待设计与验证，未在本轮修改。

### 2026-10-02 — Current Linux desktop capture revalidation

- clean f4816808 当前 Linux launcher 启动 Sod，Host readiness 成功；当前生产资产服务正常。Computer Use 选中真实 msrdc ARCH 窗口，但截图仍返回无关应用像素，仅外层pane可访问；停止点击，未操作其他应用。
- 同 production 资产的浏览器诊断显示 React 可渲染；因没有 Electron preload/bootstrap 默认显示 demo，不能将此作为 managed项目或原生desktop UAT。临时浏览器页已关闭。
- 当前原生窗口保留供用户核对实际显示，已请求“正常显示/空白/未看到窗口”反馈。本地 desktop-current-diagnostic.json 记录本次 PID/start ticks/端口；不能把活跃进程状态当长期静态验收。
- O7执行细则再次确认数值实现依赖3C主链路及现有模型预览；待图形观测判定期间只读核对，未提前实施 JENS/RZ 或开展 Windows 适配。

### 2026-10-02 — User-confirmed missing desktop window and local X11 comparison

- 用户确认默认启动没有看到窗口，故该路径桌面可见性验收失败，不能只解释为截图限制。Linux/WSLg仍建立了RDP窗口及Host readiness，但这些证据不足。
- 按本轮记录的PID/start ticks终止默认launcher，旧Host414正常退出/clean shutdown。仅局部参数 --disable-gpu --ozone-platform=x11 对照启动，Host1310 readiness成功；没有修改源码或WSLg全局配置。
- Computer Use仍返回无关像素，停止输入。已请求用户确认软件X11窗口是否出现；对照保持运行供核对。本地desktop-software-x11-summary.json登记精确进程身份与结果，不标桌面UAT通过。

### 2026-10-02 — WSLg visibility diagnosis and compositor recovery

- 用户确认软件X11对照也仅有任务栏图标；Codex侧边栏页面不能视为独立Linux窗口。临时Electron诊断显示visible=true/minimized=false/bounds=(560,250,1440,940)，处于2560×1440屏幕内；该诊断源码改动已逐项撤回。
- 沿用用户此前明确Weston重启授权：关闭本轮自有launcher/Host1852并确认clean shutdown；以系统root核对/usr/bin/weston后TERM旧PID16，WSLGd恢复为2236，未终止工程发行版。
- 默认launcher重开Host421 readiness成功；Computer Use仍返回无关画面，停止UI输入。新窗口保留供用户确认，afterRestartUserObservation仍pending。本地精简记录desktop-after-weston-summary.json，未标桌面验收通过。

### 2026-10-02 — WSLg shared-memory EIO blocker identified

- 用户确认Weston重启后仍只有图标。系统shared_memory virtiofs存在，但Weston首次分配报Input/output error/use_gfxredir=0；与Microsoft openvmm4274/WSLg1456报告吻合。详见StudioWslgVisibilityBlocker.md。
- 临时诊断代码已撤回，当前launcher/Host421正常关闭；三条已交付Run均succeeded。完整WSL shutdown会终止所有发行版进程，超出单独compositor重启授权，须用户确认后才执行。
- 不改tmpfs/global settings，不用侧边栏页面冒充原生验收。3C桌面UAT仍未通过，完整联合目标未完成。

### 2026-10-02 — Restart checkpoint identity reaches independent worker

- 将prepare/confirm共用文件系统身份抽到host/runCheckpoint.ts；确认后的path/filesystemIdentity写入本地job.json。独立worker启动Core前再次复核，缺失身份或确认后替换明确拒绝。普通Run禁止附带checkpoint身份，不新增浏览器命令权限。
- controller→真实独立worker fixture覆盖延迟terminal交付期间同长度文件替换：job保留原身份，worker failed且无Core processId。worker fixture另覆盖身份缺失拒绝和未改变身份正常启动；这些fixture不是HDF5科学兼容性检查。
- 完整npm test 223/223 PASS（含Host）、lint、typecheck/production build PASS；日志restart-handoff-regression.log留本地，既有bundle warning保留。
- 仍非内容SHA/immutable handle：最终worker复核与Core实际open之间的竞态未消除。Core继续负责HDF5布局/身份/物理兼容；未宣称续算资源完全冻结。旧待执行Restart job缺新身份时失败，必须重新prepare/confirm；旧已完成历史可读。
- WSLg完整VM重启等待用户确认；本轮继续允许的本地实现，没有执行待审批shutdown，没有把桌面UAT标为通过。

### 2026-10-03 — Real Restart worker-handoff regression

- clean ed3fa4fd：复用已验证 SmoothAdvection 输入和 step25 checkpoint，仅将 out_dir 改到新的本地持久目录；未重跑原 source 轨迹、未改变科学参数或误差阈值。新 prepare→confirm→独立 worker→真实 Core 续算 succeeded/exit0。
- job.json 保留 checkpoint 文件系统身份；原 checkpoint SHA 在复验后不变。最终 step100/time0.1，与 retained source 最终 checkpoint 的16对象路径/属性、14数据集布局及全部值逐一相同，maxAbs=0。worker/Core/本轮terminal均退出。
- 精简证据 StudioRestartHandoffSummary.json；后处理脚本 studio/scripts/compareRestartCheckpoints.mjs 可复现比较，不含原始数组。H5/checkpoint/运行日志仍留 .local，未上传。本轮仅固定网格CPU续算工程回归，不是独立科学精度、adaptive AMR、CUDA或原生桌面验收。
- WSLg完整重启仍等用户授权；桌面可见性和原生文件对话框UAT未通过，未进入依赖3C出口的全模型/JENS/RZ实现。
- 后处理脚本实际读取 retained/new 最终 checkpoint 得到相同结论；新增脚本 lint PASS，git diff --check PASS。未重复运行不受影响的223项完整回归。

### 2026-10-03 — Independent Run output-directory reservation

- 只消费Host确认的Core schema output-directory路径；applicable resolved路径/规范化目录写入job，worker在Core启动前复核并用固定/usr/bin/flock取得nonblocking锁。别名规范化相同目录共用锁，不改.par，不新增浏览器命令/路径控制权。
- 锁描述符由worker及Core继承：跨Host/project session独立；fixture中worker SIGKILL后存活Core仍持锁，另一个同目录Run失败且无Core PID；不同目录成功，原Core退出后同目录可重新运行。部分多目录锁失败释放已取得锁；dangling/重定向symlink拒绝；preflight不创建scientific output。
- scoped24/24和完整227/227 npm tests PASS（Host包含其中），lint、tsc/production build PASS。初次lint要求保留cause，修正后输出锁3/3再次通过；完整日志run-output-reservation-regression.log留本地。既有bundle warning未改变。
- 范围是同Linux用户、Studio管理的相同canonical目录；不声称阻止外部Core/nested-directory/remount/最后检查后的symlink竞态，也不代表允许覆盖历史输出。旧pending job缺output身份必须重新准备；历史状态读取保持。原生WSLg窗口/文件选择器UAT仍未通过，完整WSL重启等待授权。
- README顶部更新为当前Linux/WSL实际状态，旧Phase3A/Windows说明明确下移为历史。新增锁通过真实OS fixture验证；当前ARCH binary受影响真实续算尚待新提交上的定向复验，不将fixture当科学输出验收。

### 2026-10-03 — Real ARCH output-reservation regression

- clean913c3d5f：同一既有CPU binary和step25 checkpoint，仅修改out_dir到新的持久本地目录，实际prepare/confirm→独立terminal/worker→ARCH Restart succeeded/exit0。out_dir/log_dir两个Host确认条目去重为一个canonical目录锁，未改变输入物理值。
- 最终step100/time0.1；16对象路径/属性、14数据集布局/全部值与retained source checkpoint完全一致，maxAbs=0，原restart checkpoint SHA不变。terminal/worker/Core均退出，完成后重新获取同一目录锁成功，未执行第二次Core run。
- StudioRunOutputReservationSummary.json保存精简身份/指标；raw H5、完整日志及对照留.local，不上传。并发/worker crash证据来自前述真实OS fixture；本轮真实ARCH单任务结果不冒充科学并发、CUDA或原生桌面验收。

### 2026-10-03 — O7.0 registered model/load case identity

- 审计发现SetupChecked只核对加载完整性/配置改写，没有核对Registry创建的模型实例与加载case。现由Registry私有接线实例case ID，RuntimeParams保留loader-owned私有case身份；错误配对在Setup前返回CASE_IDENTITY_MISMATCH，同case正常准备。未改Setup/Init公式、参数默认或科学阈值。
- direct C++ fixture证明错误配对不进入模型；有效同case通过。CPU initialization_probe、input_resolution、case_configuration、configuration_input 4项PASS；重链ARCH后preview_api_contract、configuration_entry_contract、configuration_v3_contract、case_inspection_contract、preview_session_contract 5项PASS。未将CTest项与内部方法重复相加。
- 28并发增量CPU构建24.455s，最低可用15094724KiB、peak owned RSS9017400KiB、swap0，保护器未停止。首次probe编译因不完整ConfigurationInput类型失败，改为loader私有身份字段后重编通过；没有循环include或绕过失败。精简证据O7RegisteredCaseIdentitySummary.json。
- 这是Registry-created实例的配对检查，不给未注册直接构造子类推断身份；不宣告所有任意C++访问可追踪。build-cpu binary已更新，Studio的build-studio-cpu/Manifest未替换；不冒充Studio已消费新binary。无simulation/CUDA/raw上传/push。
- 顺序清单顶部修正过时v3迁移描述，保留整体科学/原生UAT未签收、特殊G输入换算待批准。联合计划允许O7.0与3C并行，本轮未越过3C去实现全模型/JENS/RZ。

### 2026-10-03 — Host Build consumes registered-case identity change

- clean4cf374a8通过原studio-cpu-release Host Build流程成功更新受管binary/Manifest，buildId02ad2cf2-6c7f-4bb5-9f0a-090b18722620。未复制build-cpu产物、未独立configure。四个改变的Core输入指纹均与新Manifest compilerInputs记录一致；sourceGitHead正确、repositoryDirty=false。
- 新binary SHA e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7；原ae619257已替换。guard42.172s、minimum available19708608KiB、peak ownedRSS3744516KiB、swap0，无guard stop；不是benchmark。完整依赖证据仍不齐，freshness-unknown保留。
- 新binary的Host v3 schema/registry/inspection身份匹配；有效Sod输入ok、空输入error。Sod两次未保存32样本请求同token/generation1，sequence1→2；六字段成功。Cellular shape[12,20]/x1-fastest共240点、七字段；AMR complete=true、20叶块，level1=4/level2=16。
- Field/AMR project/case/configRevision/build/binary及EOS sourceFingerprint一致；execution明确not_executed/not_created。原输入字节不变，finally关闭Preview，/proc确认无该受管binary Preview worker。精简证据StudioRegisteredCaseManagedSyncSummary.json；新目录原始响应/日志只留本机。
- 这是受影响新Core的Host集成复验，不是UI/原生file-dialog/renderer UAT或独立科学演化验收；未重跑未改动Studio完整227项基线。WSLg完整重启仍待授权，未提前全模型/JENS/RZ或CUDA。
- 新受管binary随后实际执行已批准SmoothAdvection retained checkpoint续算：succeeded/exit0，step100/time0.1；16对象/14数据集路径、属性、布局和所有值与旧source最终checkpoint相同，maxAbs0，checkpoint SHA不变。进程退出，raw输出留新的本地目录。Run发生时仅本轮两份报告未提交，因此记录repositoryDirty=true；无未提交Core改动，Build发生时为clean4cf374a8。Preview/AMR仍无timestep；这条显式Restart包含真实演化，不能笼统写成整个复验无simulation。

## 历史 G 输入：只读换算依据（未批准）

新增 analyze_g_input_migration.py 与 GravityConstantMigrationAnalysis.json，
报告见 GravityConstantInputMigrationPending.zh-CN.md。连续 Euler–Poisson
相似条件 q*b*a²=c² 得到密度缩放候选；离散等效、floor 激活、实际演化终点
仍未证明。核实历史径向中心 0 与当前公共 center_x=5e7 的独立初态差异。
Jeans 仅计算新旧 G 的线性频率敏感性，FLASH 编译 G 尚未确认。
没有修改 .par、运行 ARCH/FLASH 或调整阈值；待维护者批准后才迁移输入。
完整目标及先后顺序不变，不能将该分析记为科学验收 PASS。

## WSLg recovery / partial native 3C UAT

Current boot differs from the prior failing session; gfxredir=1 and the actual
independent Linux ARCH Studio window is capturable. Native Open Config and Save As
to a new ignored local file were exercised; saved bytes exactly match original.
See StudioWslgVisibilityBlocker.md for identities and remaining checks.
The earlier visibility blocker is historical; full native 3C acceptance remains
incomplete. No new simulation, configure, build or scientific input modification
was performed in these recovery checks. A combined-catalog standard-count label
discrepancy was recorded for follow-up; actual Core schema remains 94 parameters.

### Native workflow continuation / unresolved Open Config replacement

Desktop Configure and no-work Build succeeded; explicit Preview refresh restored
Current. Native Open Config replacement is not passed: selecting a distinct new
file closed the chooser without changing the displayed config identity. The
earlier same-file dialog observation did not prove loading and is corrected.
Save As byte identity remains proved. New Sod output/checkpoint fixture remains
prepared-not-run; no simulation starts until saved input association is verified.

## Linux native Open Config association fix

Desktop Open Config now uses an explicit Linux native path picker and
POST /api/config/open with only projectId/relativePath. Host reuses safe readConfig
and the serialized selection lifecycle; successful reads establish saved
fingerprint/project association without writing the selected file. Browser import
continues as an unassociated Working Copy; Windows adaptation is not extended.
Existing unsaved replacement protection remains in front of native selection.

Regression: 6 scoped configuration checks, then full Studio/Host 229/229 tests,
lint/typecheck/production build/diff check PASS. New tests cover exact BOM/CRLF
bytes, selection identity, missing/outside/symlink/invalid paths, stale session,
injected fields, active operation rejection and Save restricted to selected input.
Only Studio code changed; scientific Core and binary were not rebuilt.

Native UAT: old window closed normally; launcher PID380, Host427 and warm worker586
all exited. Production assets reopened in Linux. Distinct short-path config
studio/.local/UatSod.par was selected via native Open project configuration and
explicit Open button. UI header, Host association, Saved and Disk in-sync changed
to that file; Core inspection completed. Long-path Return confirmation did not
establish selection and is not marked passed. Native cancellation/unsaved-choice
matrix, Run/Restart and remaining desktop lifecycle still need completion.

Local log: studio/.local/integration/native-config-open-regression.log.
The local UatSod.par is byte-identical to the previously prepared workflow fixture;
only original out_dir and chk_dt differ, physics/tmax unchanged. Still not run.
No raw data/input files from .local are committed; no push or tag.

## Native Sod Run evidence / user-visible desktop confirmation pending

On clean 7d9b95f9, native saved config selection and Run confirmation launched
Sod once in an independent terminal: run795e6be7-105e-4b3d-a48a-eae68a6f4303,
succeeded/exit0, final step280/time0.2. Frozen input matches saved bytes; original
Sod input and e506619f binary remain unchanged. Only out_dir/chk_dt differ from
the original. Core/worker exited; completed terminal is held by design.
Processed evidence: validation/backend/results/studio-native-sod-run-20261003/.
Raw H5/checkpoints/logs remain ignored locally. This is engineering workflow
evidence, not independent scientific accuracy, AMR/CUDA or full 3C PASS.

Restart was prepared from actual time0.05/step67 checkpoint, then its pending
confirmation was cancelled without starting Core. Restart remains not passed.
Latest user reply still reports only a taskbar icon. The independent window was
captured and activated, but that does not prove visibility to the user. A fresh
visibility confirmation is pending; no further native computation was started.
Historical agent-captured recovery evidence remains, user acceptance is not closed.
No unchanged automatic suite was rerun and no physics/threshold/input migrated.

## 3B catalog-count truthfulness correction

ConfigPanel passes the merged Core/case/auxiliary schema to StandardCatalog.
Its count was incorrectly labelled standard keys. The count now says catalog keys;
search/group/accessibility wording likewise refers to the parameter catalog.
No frontend count constant, schema rewrite, parameter insertion or physics change.
Current binary standard schema remains94; combined catalog102 is not94 standards.

After this UI-only change: npm test229/229 (Host included), lint, typecheck,
production build and git diff --check PASS. Local log:
studio/.local/integration/catalog-wording-regression.log.
Existing bundle-size warning remains. No scientific Core/build/simulation rerun.
Current running window has not been relaunched to load these assets; native UAT
of the new wording and user-visible independent window acceptance remain pending.
The top stage table now distinguishes historical177-test evidence, current229,
agent-captured native interactions and incomplete user-visible acceptance.

## O7.0 architecture audit failure / migration proposal awaiting approval

At clean2589a8e8, existing architecture audit exits1: obsolete RuntimeParams
parser.GetBool marker, and exact CUDA controller-test source tuple missing the
CompositionInput.cpp added by strict-loader migration. Existing106 audit tests:
105PASS/1FAIL (repository-tree test); no skips. Do not mark audit/O7.0 PASS.
See O7ArchitectureAuditMigrationReview.zh-CN.md for exact evidence, bounded
proposal and negative mutation requirements. Automatic approval rejected applying
the audit/test update; no rule/test/source/CMake file was changed. Await explicit
confirmation of that proposal. No new simulation/build/raw upload/push/tag.

## O7.0 pi/two_pi shared authority

Four consumers now include existing PhysicalConstants.h:
ControlRelations, CoordinateSeamPlan, CartesianPoisson, CompositePoisson.
Geometry/seam/Poisson formulas and all thresholds unchanged; independent test
oracles retain their own constants. g++13.3 strict flags, FE_TONEAREST probe:
old/runtime acos(-1) and shared pi exact bits400921fb54442d18;
old2*pi and shared two_pi exact bits401921fb54442d18. Other rounding modes/CUDA
are not certified by this probe.

Six affected CPU targets incrementally built at28 concurrency with memory guard:
6.032s, minimum available21427488KiB, peak ownedRSS1539016KiB, swap0, no stop.
Eight existing configuration/constants/AMR/uniform+composite Poisson tests PASS,
including analytic references; original budgets unchanged. O7PiAuthoritySummary.json
records identities, scope and local logs. No new simulation/scientific output.

Architecture audit repeated because new production includes affect its scope:
still FAIL with exactly the two previously recorded failures, no additional issue.
Rules/tests/CMake unchanged; migration approval pending. Managed Studio ARCH binary
not refreshed, and prior Run/Preview evidence remains tied to its prior binary.
Do not claim full Core CPU/CUDA/3C or scientific acceptance from these8 tests.
No push/tag, raw upload or historical G input migration.

## Managed CPU Build after pi authority consolidation

Cleanf5e2c001: existing authenticated desktop Host, fixed studio-cpu-release
profile, normal /api/build(projectId,profileId) succeeded exit0.
Builda7c719a8-5851-4331-a020-f131a38843b8 records sourceHEADf5e2c001,
repositoryDirty=false, tracked and compiler inputs stable. All four changed
consumers' SHA values match the735-file compiler input record.
Old warm worker5586 exited through Host beforeStart; no active compiler remains.

Output binary SHA remains e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7,
size7177152, mtime updated. Constant expressions compiled to identical binary.
Prior API/Preview/Run evidence remains for that SHA; no identical binary checks
were repeated. New Build ID/mtime identities must still govern next request.
Full dependency freshness remains unknown, not Current.
StudioPiAuthorityManagedBuildSummary.json contains processed identities only.

No independent configure, simulation, CUDA, raw upload/push/tag or native
Build-click/visible-window acceptance claimed. Architecture rule migration
approval and user-visible native 3C acceptance remain open.

## Native Sod Restart captured / exact final checkpoint equality

Clean cf572e0f: from the saved UatSodRestart.par, native Restart preparation,
explicit saved-input/binary/output confirmation and independent-terminal Start
ran once:30db4e5c-7c43-4cac-8b30-9af8fad393db, succeeded/exit0.
Actual checkpoint time0.05/step67 continued to time0.2/step280.
Existing full-data comparator verified24 object paths/attributes and21 dataset
layouts/all values equal to uninterrupted source final checkpoint, maxAbs0.
Selected checkpoint SHA, saved/frozen input SHA and binary SHA unchanged.
Core13707/worker13695 exited; completed independent terminal13694 held by design.

Processed record validation/backend/results/studio-native-sod-restart-20261003/.
Raw H5/plt/checkpoints/logs stay ignored locally, no field arrays uploaded.
This is fixed-grid CPU continuation engineering evidence, not independent
scientific accuracy/AMR/CUDA or full3C PASS. The maximized independent window,
confirmation and terminal were captured; user-visible acceptance remains pending.
No silent assumption of approval: existing joint goal authorized Run/Restart;
this captured engineering check does not replace the pending human visibility
answer. Native cancellation/unsaved/close/Stop matrix remains unfinished.
Architecture-audit migration approval and historicalG scientific approval pending.

## Native file lifecycle partial UAT

Clean4df8367a: old idle desktop closed normally; Electron5388/Host5434/warm5586
and completed Restart worker/Core absent. Existing launcher reopened as14246,
Host14292; current production assets show102 catalog keys truthfully.
Native Open Cancel preserves association; Dirty edit cfl0.4→0.41 and Cancel
replacement preserves unsaved value. Native SaveAs to a new ignored space/Unicode
path saves exactly the one token edit; original/local source and binary SHA unchanged.
Open original then Reopen edited copy establishes new header/Host association,
Saved/Disk in-sync and cfl0.41. Missingdiff_cfl not inserted.

StudioNativeFileLifecycleUat.zh-CN.md and Summary.json record processed evidence.
Existing automatic init-only Preview refresh operated; no new Run/Restart,
scientific timestep, build or unchanged229-test rerun. Unicode path operations
work, but Chinese glyphs render as boxes; readability remains incomplete.
Agent capture of separate maximized window does not prove user visibility:
latest human report only taskbar icon, fresh confirmation pending.
Other cancellation/overwrite/active-close/Run-lifetime/Stop matrix still incomplete.
No whole3B/3C/O7 PASS, no full-model/JENS/RZ advance, raw input/upload/push/tag.

## Host/Core explicit Stop verified; native visibility still open

Clean6b2caa34: saved4096-cell CPU Sod, existing authenticated desktop Host,
controlled prepare/confirm/start and real independent xterm.
Run6c0d8ba8-ec35-44ca-83c1-a1e626264a20 observed Core16728 running with
matching /proc startTicks, then owned Stop -> stopped/SIGTERM.
Worker16716/Core16728 absent after; Electron14246/Host14292/warm14445
PID/startTicks/parent/group unchanged. Held terminal16715 is expected.
Frozen input equals saved; original Sod and selected binary SHA unchanged.

Earlier native1024-cell9192f783 finished naturally1.743s/exit0; no Stop PASS.
Second run produced initial output and one timestep before interruption;
no scientific accuracy/benchmark/final-time claim.
StudioHostStopLifecycleUat.zh-CN.md and Summary.json record reduced evidence.
All raw inputs/H5/checkpoints/logs remain ignored locally.

Native Stop button and user-visible window NOT VERIFIED. User taskbar-only
report remains authoritative; tool activation/maximization does not supersede it.
Run survival across active Studio/Host close and native matrix remain open.
No whole3C/O7 completion/full-model/JENS/RZ advance, unchanged-suite rerun,
source edit, build, raw upload, push, tag or main merge.

## Live Run survives production Host close and can be stopped after Host reopen

Clean d3ccc711: isolated real production desktop.ts Host, same valid CPU4096
Sod with unique local output. Run e475ea66-60cf-4b9d-88d0-98415cb9579c.
Host17389 exits0 via formal stdin close and is absent; worker17413/Core17425/
terminal17412 retain startTicks, Core running. Console grows1658→13970 bytes.
Reopened Host17453 finds original running job in history and stops it:
stopped/SIGTERM, worker/Core absent. Recovery Host exits0 and is absent;
held terminal is expected. Current Electron14246/Host14292/warm14445 untouched.
Frozen/saved inputs match; original Sod and binary SHA unchanged.

StudioHostCloseSurvivalUat.zh-CN.md/Summary.json are reduced evidence.
Production Host lifecycle PASS does not replace native Electron close-button
or human-visible terminal UAT. WSLg readonly session/monitor/log audit does
not prove display repaired. Latest human taskbar-only report remains.
Remaining native close/cancel/file conflict matrix and audit/G approvals open.
No whole3C/O7 PASS, full-model/JENS/RZ advance, source/build/unchanged-suite rerun,
raw upload, push, release tag or main merge.

## Native Save As Cancel on clean Working Copy verified

Cleanfbb0fe3b: same Linux Studio window, actual GTK Save dialog78253042
opened from Save Working Copy As then nativeCancel; modal disappeared.
Host project/config association and SHA/mtime/size unchanged,
default project-root StopSod4096_copy.par absent, binary SHA unchanged.
StudioNativeFileLifecycleUat/Summary extended with exact scope.
Dirty-copy Cancel and other remaining native matrix are still uncovered;
human-visible window still NOT VERIFIED. No source/preview/run/build/test rerun,
raw upload/push/tag or whole3B/3C completion claim.

## Native external-change Save refusal and Dirty Save As recovery verified

Clean8f2a30af: independent ignored copy, real GTK Open -> CFL working edit0.41
(Saved0.4); external append-only comment to that copy. Native Save refuses,
Disk changed-externally and explicit retained-copy conflict. Original external
SHA bb76a913... unchanged after refusal and Dirty GTK SaveAs Cancel.
DirtyCancel retains0.41 and conflict. Native SaveAs new RecoveredSod.par saves
1260bytes/SHAf452da1c... exactly original text with onlycfl0.4→0.41.
Original/source input unchanged, diff_cfl missing remains unwritten.
Host/header associates new file, Saved/DiskinSync; existing init-only refresh.
StudioNativeFileLifecycleUat/Summary carry processed identities and scope.

This closes native external-conflict refusal/retention, DirtySaveAsCancel and
SaveAs recovery, not explicit overwrite or active window-close matrix.
Human-visible independent window still NOT VERIFIED, no whole3C/O7 PASS.
No simulation/build/source change/unchanged-suite rerun/raw upload/push/tag/
main merge/full-model/JENS/RZ advance.

## Native active-close attempt finished before close; scope remains open

Clean10ad80b1: actual Linux Run prepare/explicit confirmation/native Start once.
Runafb95966-335d-47d4-8620-39d4ca6f8035 naturally succeeded/exit0,
04:57:55.833Z–04:58:12.341Z, step8994/time0.2. Worker19122/Core19134 absent;
heldterminal19121 expected. Saved/frozen bytes match and original inputs/binary
SHA unchanged. Active Electron-close was NOT EXERCISED; no automatic replay.
StudioNativeActiveCloseAttempt.zh-CN.md/Summary.json retain exact reduced evidence.
Latest user still reports taskbar-only; subsequent tool activation/maximization
does not prove human visibility. Current desktop/Host/warm Preview not closed.
3C native active-close/Stop/overwrite/display acceptance remain incomplete.
No source/build/unchanged-suite repeat/raw upload/push/tag/main merge or
full-model/JENS/RZ/CUDA advance.

## Native Dirty close -> Keep editing protection verified

Clean7d2b9513: test-copy CFL0.4→0.41; actual close button opens native
unsaved-copy dialog18549204. Keep editing removes modal and retainsDirty/0.41,
Saved0.4. Electron14246/Host14292/warm14445 PID/startTicks/parent/group unchanged;
project/file association and diskSHA/mtime/size unchanged. Own test edit restored
to0.4 withoutSave; Saved/DiskinSync and automatic init-only PreviewCurrent observed.
StudioNativeFileLifecycleUat/Summary updated. No new Run/Build/source change.
Discard/activeClose/nativeStop/overwrite/humanVisibility remain open; no whole3C,
full-model/JENS/RZ advance, unchanged-suite rerun/raw upload/push/tag/main merge.

## 2026-10-03 补充：用户可见性确认与 Sod 平台曲线显示修正

用户提供独立 Linux Studio 窗口截图并明确报告“已显示linux界面”。
此前 taskbar-only 为历史状态，不再作为当前阻断。窗口标题及 Host 项目关联
ARCH-compute-optim；可见性确认不等于完整流程验收。任务栏中的已结束 ARCH
终端是保留日志窗口，不根据缩略图判断 Core 仍运行。

对照真实 init-only response：Sod DENS 共512点，前256点为1、后256点为0.125，
x_pos=0.5。原图自动纵轴恰好等于字段min/max，使两段平台压在边框上；
本次只为1D自动纵轴添加5%显示留白，Linear/Log均在各自变换空间处理。
手动范围、clipping、2D颜色范围、原始数组及科学初始化均未改变。
非正值Log仍明确拒绝；极端不可表示范围不引入无穷或伪造数据。

Production build刷新当前原生窗口13437218后，最大化捕获两段平台均在图框内部，
纵轴约0.08125到1.04375；分界和x_pos标记保持0.5。最大化输入后的refresh
曾返回window is not a usable app window；重新枚举同一窗口并捕获确认已最大化，
未重复输入或重启。工作副本Saved、Disk in-sync、Preview Current。
刷新沿既有策略产生新的init-only request877e5181-a8b1-4fce-921a-ca70316783e6，
未启动Run/Restart或simulation。配置SHA仍3a772a8866d5e4cd77d81d2f8bb7cabb51521f3f70223d2522aec9fc8101865c，
Build仍a7c719a8-5851-4331-a020-f131a38843b8，binary仍e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。
512点真实密度值未变；build完整依赖freshness仍unknown，未改称current source。

检查：plot presentation scoped8/8、Studio/Host总231/231、lint、typecheck、
production build与diff check PASS。已有bundle-size warning保留。
原始日志在studio/.local/integration/line-viewport-headroom-regression.log，
不提交dist或完整response arrays；无Core重新编译，无push/tag/main merge。
仍待原生Stop、active Preview/AMR关闭、active Run原生关闭及显式overwrite等矩阵，
不宣称3C或整个联合计划完成，不提前进入全模型/JENS/RZ/CUDA。

## 2026-10-03 原生 Stop 尝试边界


2026-10-03，clean源码a68aee854307102a39de636923f2f3968a787244。
本次从独立Linux窗口原生选择器显式Open选择新测试副本，页面与Host关联一致，
Run预检确认已保存输入、CPU binary及新输出目录，再点击Start in independent terminal。
第一次选择器Return未更换关联，因此未用旧输入运行；显式Open后才继续。

配置复制既有4096-cell Sod UI测试输入，仅更换out_dir为独立ignored目录，
保持tmax=0.2/cfl=0.4及所有物理参数。不是新科学benchmark或阈值验收。
Run 8befed9e-b6cd-44e8-97e8-6ec44491e81b：
2026-10-03T05:45:14.747Z启动，05:45:30.313Z自然完成，exitCode=0。
真实窗口捕获了running、活动终端步进及Stop run控件。

点击Stop时Computer Use拒绝输入：
point (2498,1315) is over explorer.exe FolderView, not target msrdc Studio。
截图当时呈最大化布局；重新激活后实际窗口较小且run已经succeeded。
没有对桌面输入，没有绕过目标校验，没有重复Start、没有延长物理终点。
因此**native Stop NOT EXERCISED**，不能以自然完成或已有Host Stop证据替代。

worker26373/startTicks749053、Core26385/startTicks749060均已退出。
Electron14246、Host14292、warm Preview14445的PID/startTicks/父进程/组保持一致。
保留终端用于查看日志，不称其为orphan Core。saved和frozen input逐字节一致。
精简状态及指纹见StudioNativeStopAttemptSummary.json；完整输入/日志/output
仅在studio/.local/integration/native-stop-20bbee1d-5e30-4988-9617-ee4cfd0e6812
及studio/.local/runs/8befed9e-b6cd-44e8-97e8-6ec44491e81b，不上传原始数据。

本轮无代码修改/Build/CUDA，不重复未变的231项通过检查。
只对新增报告执行diff check。3C仍缺可靠原生Stop、active Run原生close、
active Preview/AMR close及明确overwrite等证据。下一次原生Stop应先确认窗口
真实几何与截图一致，或采用原生键盘焦点导航；不自动重跑本任务。

## 2026-10-03：GNU 工具链组件身份接线

基线a95f49465f7d6d17f571f9dacf496dd403ba7705，执行前工作树clean。
现有CMake File API确认C/CXX均GNU13.3.0，编译器分别/usr/bin/cc和/usr/bin/c++。
原driver指纹不能发现同driver调用的子程序被替换；增加Host-owned固定只读查询，
shell=false，Linux固定PATH/LC_ALL，5秒/1MiB响应预算。
GNU C/CXX记录cc1/cc1plus、collect2、as、ld、lto1、liblto_plugin.so的路径、
realpath、SHA-256、size及-dumpspecs SHA；外部specs存在时另记文件指纹。
不接受浏览器提供compiler/argv/env，也不使用browser或manifest任意路径执行。
组件文件读取保留预算/稳定性检查；缺失或畸形查询失败，不假定无影响。

成功Build以后Manifest可记录这些可选扩展；loadManifest拒绝畸形组件/重复role。
freshness重新从当前CMake工具链取得证据并比较；子程序、linker、plugin、
specs或组件集合变化触发needs-build。旧GNU Manifest缺少组件继续unknown，
不会追溯修改旧成功Build身份。本轮未重新Build ARCH，现有运行Host尚未重启
加载此Host变更，不声称现有a7c719a8 Manifest已经含组件。

定向16/16；完整Studio/Host233/233、lint、typecheck、production build和
diff check PASS。原bundle-size warning仍在。新增测试覆盖组件变化、builtin
specs变化、缺失/畸形响应、实际GNU工具链Manifest消费及旧证据unknown。
真实build-studio-cpu只读采集C/CXX各6组件成功，见
StudioGnuToolchainComponentEvidence.json；完整检查日志在本机ignored
studio/.local/integration/toolchain-component-regression.log。

dependenciesComplete仍false。现有ARCH.link.d含已删除的/tmp/*.ltrans.o，
不按文件名忽略它们；仍缺每次实际编译参数覆盖、implicit libraries及Build前后
完整工具链稳定性。工具链组件默认查询证据不是所有实际调用的完整证明。
没有修改Core/物理/冻结阈值、架构审计规则或历史G输入；
无新simulation/Preview/CUDA、无push/tag/main merge。3C原生生命周期剩余
矩阵继续推进，未提前进入全模型/JENS/RZ。

## 非最大化原生 Stop 尝试：自然完成，未证明停止

2026-10-03，源码 2b4fc090628eac09a3d591e4e2039ad30066637c。本轮沿既有授权使用独立 ignored Sod 副本，只有 out_dir 更换；未延长物理终点、未修改科学参数或阈值。

从原生窗口显式打开输入，确认保存状态、compiled binary 和独立输出。窗口保持非最大化，重新选取唯一 Studio 窗口和新截图；点击 Start，重新激活后观察 running 和 Stop run，再点击 Stop run。没有对背景应用或终端输入。

运行 47e96a35-86aa-4ec1-9c81-3fa6c022dddf 从 2026-10-03T05:58:06.885Z 到 2026-10-03T05:58:25.243Z，终态 succeeded、exitCode=0、signal=null。点击后的页面显示 succeeded。因此未证明 Stop 导致停止，不能算 native Stop PASS；也不把点击本身作为行为成功。未自动重复运行。

worker/Core 已退出，现有 Electron/Host/warm Preview 的进程身份记录见同名 Summary.json。saved/frozen input 一致，binary SHA 与确认身份一致。保留日志终端不等于 orphan Core。完整输入、输出和日志留本机 ignored 路径，不上传。

3C 原生 Stop、active Run/Preview/AMR close 和显式覆盖保存矩阵仍待验收。本轮不改代码、不重复未变的 233 项检查、不 Build、不 push/tag/main merge，也不提前进入全模型/JENS/RZ。现有运行 Host 未重启，本次不验证最新 GNU 组件身份功能。

## Build 前后工具链稳定性核验


2026-10-03，基线 ad6ddb899de85d2880fa88e2dada7710c85cd61a。

Host 在标准 Build spawn 前采集当前 CMake compiler driver、GNU components 与 specs，成功 Build 后再次采集。Manifest 保留 preBuildCompilerDrivers 与 compilerDriversStableDuringBuild；比较语言、compiler 身份、路径/realpath、SHA/size、组件角色及 specs hash，忽略数组顺序。缺少任一端不伪造稳定结论。

两端变化即使 post-Build 与当前磁盘一致仍 needs-build。缺失旧证据保持 freshness-unknown；已确认当前文件漂移仍优先 needs-build。Manifest loader 校验前快照与布尔字段，不回写历史成功 Manifest。

回归通过：17 项 Build/CMake 定向检查；最终完整 Studio/Host 234/234、lint、typecheck、production build、diff check PASS。新测试在 spawn 边界确定性更换 fixture driver，验证成功构建状态与 provenance readiness 分离、重载仍 needs-build、下一次稳定快照、旧证据 unknown 和已知漂移优先。真实 GNU 固定查询在已有回归中核对。测试仅使用临时工程和 fixture，未重新编译 ARCH。

日志留 studio/.local/integration/toolchain-stability-final.log。dist 不提交。当前运行 Host 尚未重启，本轮不宣称真实 ARCH Manifest 已包含新字段。

限制：前后采样不能发现构建期间修改后恢复的 ABA 变化，不是不可变工具链隔离；每次编译实际参数、隐式库及完整依赖覆盖仍未证明。dependenciesComplete=false 保持不变。当前实际 linker depfile 中已消失的 LTO 临时文件仍使链接覆盖不完整。本项不代表整个 3C/O7 完成。

不修改 Core、科学公式、阈值、架构审计或历史 G 输入；无 simulation、Preview、CUDA、push/tag/main merge。

## 当前源码 Host 原生关闭/重启验证


2026-10-03，源码 40dd783b50c5c382d1c2dae80ac22b0fc6df31da，开始前工作树 clean。

当前窗口 Saved、Disk in-sync、Preview Current，Run history 无活动任务。通过原生 Studio 关闭按钮关闭，旧 Electron14246、Host14292、warm Preview14445 均从 /proc 消失；desktop.log 记录 owned Host exited 和 desktop clean shutdown。未通过信号或 PID 强制关闭。

从已有 Linux arch-studio entry 重新启动同一项目/CPU binary/已保存测试副本；未启动 Vite、未安装软件、未 Configure/Build 或 Run。新 Electron35303/start857946、Host35350/start857958、warm worker35775/start858688。Host parent 为本次 Electron，worker parent 为本次 Host，PID 均有 startTicks 证据。

新的原生窗口15730862显示正确项目、Sod1D、Saved、Disk in-sync、Preview Current 和 x_pos0.5。生产页面启动沿既有流程执行 init-only Preview，身份为2fd9a2cf-38f5-4557-b9ea-fbbf198bf781；configRevision 和 binarySha256 与磁盘相符。没有运行 simulation，新旧 binary 指纹一致。

新 Host 已加载当前工具链前后稳定性实现，但历史成功Build Manifest没有该证据，build状态如实显示 freshness-unknown，原因Compiler toolchain identity is incomplete or unavailable；不伪造新Manifest、不追溯标记当前源码已构建。UI Build ready只表示profile可用，与binary freshness不同。

本项证明 idle warm-session 的原生正常关闭、进程清理和当前源码重启；不替代 active Preview/AMR、active Run关闭、原生Stop或覆盖保存矩阵。窗口可见证据来自工具捕获，本次没有要求用户重复确认既已确认的Linux可见性。

只有报告和清单提交；原始日志和输入在 studio/.local/integration/native-relaunch-40dd783b，旧科学输出仍留本地。未改代码、不重复234项检查、未编译ARCH、无CUDA/push/tag/main merge。

## 最新 Host 的原生 Build 与实际稳定性 Manifest


2026-10-03，源码 f1875686ebb49cdcb317fc87f7dcdb3792015b08，执行前工作树 clean。通过原生 Linux Studio Build 按钮一次启动 Host-owned CPU profile；此前预览已完成、无活动Run。没有单独configure、clean或更改编译缓存。

Build f6449e3d-ccc0-4d51-ac96-eaf767de62a8，2026-10-03T06:07:32.777Z 至 2026-10-03T06:07:33.689Z，exit0。原生有界terminal drawer显示 Re-checking globbed directories、ninja: no work to do、succeeded。这是无工作增量构建，不是干净编译证明。

tracked固定3项及binary SHA/size/mtime均与Build前一致。实际compiler依赖735项、object66个；三种前后稳定性字段均true。新Manifest确实包含GNU C/CXX driver、各自组件/specs的前后身份，已补入真实Build证据；不是仅单元测试或历史Manifest推断。

binaryState仍freshness-unknown，原因 Linker input evidence is incomplete or unavailable.。link depfile中的missing临时输入保持unavailable，未按文件名忽略；dependenciesComplete=false不变。前后相等不证明ABA不可变、全参数覆盖或所有implicit libraries；不能标为全部当前源码已认证。

Build后页面将旧Preview明确标为previous/stale并保留曲线，没有自动Preview或运行simulation。当前Core binary指纹仍e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。未改配置、不Save、不生成科学输出。

完整Manifest/status留在studio/.local/integration/current-host-toolchain-build；提交仅精简工具链指纹/计数/摘要和清单。没有源码修改、未重复234项检查、没有CUDA/push/tag/main merge。原生Stop和活动任务关闭矩阵仍待完成。

## 原生 Stop PASS 与日志抽屉确认面板修复


2026-10-03，运行源码 cb3108df9cef7408c016c64bc83fff33270808b5。使用既有CPU4096 Sod输入，tmax0.2/cfl0.4不变，仅新out_dir。未扩大物理终点。原生选择器打开独立副本，身份和预检确认后Start；一次输入后等待700ms再捕获，观察真实running和Stop run，直接点击Stop；未对xterm输入、未用Host API代替原生按钮。

Run 83b1f0ec-dadb-4122-8d3f-2507364f79c0，2026-10-03T06:11:12.579Z 至 2026-10-03T06:11:18.185Z，终态stopped/SIGTERM。UI依次显示stopping、Latest run stopped；worker44329/Core44341均从/proc消失。Electron35303/Host35350身份不变。日志终端保留符合既有策略。saved/frozen input完全相同，binary SHA保持e506619f473a85e639df37f332ad1aa2d7105c63519674f9a9947ffc03813bc7。本项可标native Stop PASS；历史自然完成尝试仍保留原未覆盖结论，不追改。

实际UAT发现日志抽屉打开后覆盖Run确认面板：两个absolute sibling默认堆叠且drawer后绘制。仅为.run-confirmation加入z-index:1。没有改变Run确认门槛、请求、生命周期或科学代码。生产窗口刷新后保持drawer打开，prepare Run显示完整确认、复选框、Start/Cancel，点击Cancel返回drawer；未勾选或再次运行。既有输出目录提示保持可见。

最终Studio/Host234/234、lint、typecheck、production build、diff-check PASS；已有bundle warning保留。不添加镜像CSS的测试。完整日志留studio/.local/integration/run-confirmation-stacking-regression.log。

本次刷新沿既有行为执行init-only Preview，非simulation。所有本次Run输入/H5/checkpoint/完整日志留本机ignored目录；仅提交精简摘要。3C仍需active Run原生close、active Preview/AMR close和明确覆盖保存等证据；不以Stop或CSS修复宣布整个计划完成。未Compile ARCH/CUDA，无push/tag/main merge，不提前进入全模型/JENS/RZ。

## 原生 active Run close 与历史恢复通过


2026-10-03，源码 0ddc7ca6453bdbac59b693daac41c703b395a5a8，开始前工作树clean。沿已授权4096-cell Sod测试输入，仅换新out_dir；tmax0.2/cfl0.4和物理输入不变。通过原生选择器打开、Run预检、显式checkbox和Start启动独立终端，700ms后捕获running及真实步进；随后点击原生Studio关闭按钮。没有发送进程信号、没有关闭xterm或改写工作副本。

Run 76f06645-6aa0-4d9d-b9df-f3b4f637fd83 在 2026-10-03T06:16:29.118Z 开始。desktop.log记录Host35350 exited及desktop clean shutdown于 2026-10-03T06:16:37.242Z；Run在 2026-10-03T06:16:46.612Z 才自然succeeded/exit0，晚于关闭 9.370 秒。因此本项证明关闭没有取消已交付计算。关闭后只读进程观察时任务已结束，不声称捕获关闭后的live worker或日志增长；历史未覆盖尝试不改判。

旧Electron35303、Host35350、warm43430均退出；Run worker53400/Core53412结束后均退出。日志终端保留符合既有策略，不将它作为orphan Core。保存输入和frozen input一致，binary指纹不变。

同一Linux正式入口重开，新窗口93457630正确关联项目/测试副本/Sod1D。原生Saved run history显示本次Sod succeeded以及此前native Stop的stopped记录；新Host读取同一持久记录，state逐字段相同。恢复只读取历史，不重复运行或停止已完成任务。

本轮只有验收和报告，无源码改动、不重跑234项检查、不Compile ARCH、不CUDA。完整输入/output/H5/checkpoint/logs留本机studio/.local/integration和studio/.local/runs；提交精简状态/身份摘要与清单。没有push/tag/main merge。native active Run close可标通过，active Preview/AMR关闭和明确overwrite等其他矩阵仍待完成；不宣告整个3C或联合科学计划完成。

## 原生 Cellular Preview 关闭：owned 清理通过，活动 Core 未确认

2026-10-03，基线 e73d226a clean。原生打开 tracked 二维 Preview 样本后发现
tmax 缺项使工作台禁用 Preview；未猜值/改文件/绕过检查。改为原生打开已有
AMR t=0 对照输入（use_burn=false/tmax=0），保持全部参数。
UI 显示 generating 后原生关闭。06:25:40.948Z desktop clean shutdown，
Electron53611/Host53657/直接 ARCH54079 均退出，无信号替代操作。

只读监测0.5秒 timeout 太短，248样本无成功 status 读取；未捕获 request ID
或关闭瞬间 Core stage。UI generating 不是 Core active 的充分证据，
active Preview close 仍 NOT VERIFIED；owned 清理只按本轮实际范围通过。
报告 StudioNativePreviewCloseAttempt.zh-CN.md/Summary.json 保存边界。
不自动重放，不以过往其他关闭记录代替。当前独立窗口已关闭。
无源码修改/Save/Run/Build/CUDA，不重复234项检查；只有报告/diff check。
未 push/tag/main merge，未进入全模型/JENS/RZ。

## 活动 Preview 请求原生关闭：显式 worker stall 覆盖通过

2026-10-03，cbc66c49 clean。实测status读取0.77秒，5秒监测成功捕获普通warm
generating，但请求e5cee974在06:30:08.673Z已succeeded，关闭06:30:10.265Z；
该自然active-close仍NOT EXERCISED，不用UI更新中状态补判。

另以既有CellularDet t=0/use_burn=false输入启动正式Linux窗口，startup成功后，
严格核对唯一owned --preview-session worker72375/start1005444/parent71947，
SIGSTOP故障注入带90秒同PID/start保护。原生Update Preview后请求1272c28f
持续generating/stage=request；native close06:33:14.327Z，Host于15.348Z退出。
Electron71901/Host71947/worker72375均消失，保护未介入，config/binary SHA未变。

活动受阻Preview请求原生关闭/owned清理PASS（明确fault injection scope）。
不宣称自然Setup/Init执行在关闭瞬间被覆盖。报告
StudioNativeStalledPreviewClose.zh-CN.md/Summary.json记录准确边界；
active AMR close/保存矩阵和完整3C仍待完成。当前窗口已关闭，无Run/Build/Save/
Core改动/CUDA，不重复234项通过检查，只执行report JSON/diff check；
raw证据留ignored目录，无push/tag/main merge/full-model/JENS/RZ advance。

## 原生 CPU Configure 验收通过

2026-10-03，1fca3381 clean；对照联合计划第4.2、5节补齐原生Configure证据。
Linux正式entry/production窗口19270674，Host-owned studio-cpu-release，
原生Configure一次，operation c160d385-b4bc-4ea6-ad67-0a1671486fbd succeeded/exit0；
捕获running及完成，原生Show terminal显示真实CMake输出。
Release/Ninja/source root当前集成工程/CUDA OFF；缓存、CMakeLists/Presets、
保存Sod输入及binary SHA/size/mtime均未变，故不重复Build。
freshness-unknown/dependenciesComplete=false保留，不冒充clean编译或current。

报告StudioNativeConfigureUat.zh-CN.md/Summary.json；full File API inputs/events
留ignored目录。Hide drawer可能晚于任务完成，不扩大原生active-hide覆盖。
当前窗口与Host仍运行。无产品/Core修改、Run/Save/CUDA，不重复234项检查；
仅JSON/diff-check，无push/tag/main merge。仍收尾3C文件及生命周期出口，
不将额外自然初始化时序测试自动升级为新的科学前置。

## 原生活动 AMR 请求关闭：受阻共享 worker 覆盖通过

2026-10-03，2deb2810 clean。复用当前Linux窗口19270674与保存Sod输入，
workflow起始none/Preview succeeded。严格核对owned --preview-session
worker77360/start1038828/parent76933，SIGSTOP带90秒同身份T状态恢复保护。
原生Generate initial AMR一次，请求9ba1e717持续preview-amr/running，
06:44:22.053Z直接复核worker仍T；原生close06:44:35.035Z，
Host38.701Z退出/desktop38.704Z clean。Electron76887/Host76933/worker77360
均消失，保护未介入，config/binary SHA未变。

未完成AMR请求原生关闭/owned清理PASS（fault injection scope），不声称
自然AMR数学运行时序或本次已返回hierarchy/response identity/数值通过。
报告StudioNativeStalledAmrClose.zh-CN.md/Summary.json；原始证据ignored。
无产品/Core修改、Run/Save/Build/CUDA，不重复234项检查；JSON/diff check。
当前窗口已关闭，3C文件覆盖/阶段报告继续收束；不push/tag/main merge，
不提前进入全模型/JENS/RZ。


## 原生显式覆盖保存通过与长路径确认布局修复

2026-10-03，a946d982 clean。独立 Sod 输入刻意使用 CellularDet.par 文件名，
仅编辑 cfl 0.4→0.41；Preview pairing confirmation 全部取消，无真实 Preview/AMR/Run。
原生 Save 的明确 overwrite 确认先 Cancel，磁盘逐字节未变、Dirty/0.41 保留；
再次显式 Overwrite 后落盘内容逐字节等于唯一 cfl 修改预期，原 source/backup/binary 未变，
缺失 diff_cfl 未插入。原生 Reload disk version 后 Working/Saved 均0.41，Disk in-sync。

UAT 发现确认 grid min-content 与32px固定按钮高度造成长路径越界/按钮文字重叠。
只修复 styles.css 的确认网格列宽、换行及 auto-height。重载 production 后观察路径与按钮
正常分行，滚动可访问；相同内容 overwrite 验证按钮操作成功，不扩称第二次 Dirty-save。
仍需要滚动查看位于 Save 上方的确认，未实现自动聚焦。

完整 Studio/Host234/234、lint/typecheck/build/diff PASS，bundle warning保留。
报告 StudioNativeOverwriteUat.zh-CN.md/Summary.json；完整输入与日志仅 ignored 本地目录。
没有 ARCH compile/CUDA、push/tag/main merge。继续做3C逐项出口审计，不提前全模型/JENS/RZ。


## 3C 出口审计及真实编译失败恢复

2026-10-03，e240bbd9 clean。逐项核对联合4.1/4.2/5/6要求与当前实现/证据。
原生文件/Configure/Build/Run/Restart/Stop及关闭主链路通过；
Preview取消与不同项目重开时delivered Run的同场景直接证据仍缺，完整出口pending。
不扩大为自然短Init瞬间关闭的额外门槛。

新增host-build-real.test.ts在空格/中文临时目录真实CMake/Ninja编译微型C++，
成功→明确#error失败→修正成功；失败保留旧成功Manifest/binary/错误输入，
freshness changed/unknown准确。没有构建ARCH或simulation。
定向1/1、最终235/235、lint/typecheck/build/diff PASS。
既有Sod Restart持久job/state/input/binary身份及本机H5存在只读复核一致，不重跑轨迹。

报告Studio3CExitAudit.zh-CN.md/Summary.json；日志raw本机ignored。
顶部顺序清单更新当前证据，旧记录按当时范围保留。
历史G/architecture迁移批准独立待处理，无push/tag/main merge/full-model/JENS/RZ/CUDA advance。

### 3C工程出口：活跃Run并发隔离补证

基线9a5c8958；StudioRunPreviewProjectIsolation.zh-CN.md/精简Summary记录真实生产Host验证。
Run740d4d6c，CPU Sod既有4096cell/t0.2，仅out_dir改为独立目录；
取消pending Preview、关闭原Host、打开另一项目后同PID/start且日志增长，自然到step8994/exit0。
两项目源码/输入/binary不变，原生窗口未操作，两个测试Host/Preview/Run进程清理完成。
3C工程工作流出口已验证；未知Build完整依赖和fault覆盖边界保留。
未重复235项不变回归。历史G与architecture批准独立待处理，下一步按计划审计全模型/plt。
无CUDA/push/tag/main merge，新raw全部保留本机ignored目录。

### 全模型初态/AMR与plt进入实现前审计

基线d81620ad；FullModelInitialAndPlotContractAudit.zh-CN.md/精简Summary记录当前binary真实能力、
14-model Setup/Init域、Core采样/3D容量/Host validation限制及具体迁移顺序。
所有注册模型有point initializer不等于当前完整场支持；仅Sod/Cellular支持。
当前二维cylindrical仍(r,phi)，不得改名RZ。plt实际writer用Cartesian中心，
既有本机H5的shape和缺失case/config/build身份已只读确认，不上传raw。
下一步在原所有者扩展按dimension的有界采样/三轴验证，逐模型复用Setup/Init/EOS；
全模型与plt仍in progress，不宣称实现/科学通过。本轮无新Preview/AMR/simulation/build或不变测试重复。

### 全模型初态实施：采样预算基础

基线fa9d63db。Sampling.h新增按解析dimension的1/2/3轴有界计划，内部samples_x3，
旧Sod/Cellular调用保留，尚未发布新模型/三维能力。内部3D预算32^3默认/每轴64/总32768，
待真实响应与资源验证后才可发布。既有CPU sampling目标实际重编exit0、CTest1/1及diff check PASS；
旧未更新target的观察不计，新测试指针类型编译错误修复后最终通过。
详见FullModelDimensionalSamplingProgress.zh-CN.md；ARCH binary未重编，不冒称source current。
下一步三轴grid/root capacity及实际GeneratePreview接线；全模型/AMR/plt整体仍未完成。

### 全模型初态实施：三轴网格/根容量

基线583ac18e；ResourceEstimates统一RootBlockCount/ValidateInitialPreviewGrid，
Preview两元素校验补三轴，真实BuildInitialMesh根预算含x3，leaf几何补第三轴。
根计数非法/溢出明确错误，合法超工作容量limited/none。
原sampling和新真实root geometry fixture两个CPU scoped CTest PASS；
实际生产Preview/Resource对象编译PASS，不声称ARCH重新链接或新capability。
几何fixture1x1x2/lref0覆盖第三轴容量、真实叶块/bounds/spacing及metadata一致性，
不是科学模型或三维细化验收。详见FullModelThreeAxisGridProgress.zh-CN.md。
下一步真实GeneratePreview按dimension接线/CLI-session/逐模型回应，再Host/UI；
当前public完整场仍两模型，ARCH binary未重编，源码freshness不能声称current。


### 全模型初态实施：真实生成/传输与根AMR

基线8522e85b。GeneratePreview按实际配置dim接通1/2/3D Setup/Init/shared EOS，
CLI/session三轴请求、VELZ精确缓存、runtime发现共用已审阅域；旧Sod/Cellular兼容。
14维护模型字段和真实root AMR通过；非立方体Gaussian三种几何及SNIa曲线3D参考通过。
实际CPU ARCH编译/链接成功，无swap增长；最终生成/CLI/session/Cellular四组PASS，
缓存目标重编及VELZ回归PASS。测试字段名/固定模型数/缺轴fixture失败保留后修正，
不放宽Core校验。详见FullModelInitialGenerationProgress.zh-CN.md及精简Summary。

当前生产UIbinary未更新，不伪造Manifest或宣称Studio通用显示已完成。
下一步Host validators/provider、模型工作区及3D/单区视图，再实际AMR混合层级和独立plt。
科学/CUDA/JENS/RZ未验收；历史G/architecture审批独立pending。
没有simulation/scientific output/Windows适配/push/tag/main merge。

### 全模型 Host 增量：动态能力与查询清理

基线9164222c；真实CPU capabilities经相同TS校验生成22个case/dimension Profiles。
Host增量最终237/237、lint/typecheck/production build/diff PASS；首轮失败及受阻查询人工释放边界保留。
详见FullModelHostIntegrationProgress.zh-CN.md。未运行simulation或替换生产UIbinary。
实际Host多模型请求、provider/3D/state/曲线/AMR UI/UAT仍待实施，不宣告阶段完成。

### 全模型工作区增量：维度选择与三维显示

基线5bd1e780。按匹配的当前inspection选择Profile，三维各原生轴切片保留全局raw索引；
uniform-state独立表达，固定非活动坐标/单位由Core提供。slice不编辑配置/自动Preview。
真实CPU三种几何[2,3,5]八字段三个切面经当前TS校验逐项PASS；无timestep/scientific output。
最终240/240及lint/typecheck/production/diff PASS；原始JSON与logs仅ignored本地。
详见FullModelWorkspaceProgress.zh-CN.md。真实Host/production desktop UAT、AMR通用显示及plt待完成。
无push/tag/main merge/CUDA，不以display adapter验证宣布全阶段完成。

### 通用 AMR 增量：native/三维切面与真实混合层级

基线227b682e。当前TS/视图支持3D及曲线metadata，三轴切面过滤原始leaf、投影/点击同轴，Inspector逐轴单位。
真实三种几何3D两根块及Sod10叶/Cellular20叶混合层级经当前validators/hit testing PASS。
无simulation/scientific output；最终243/243及lint/typecheck/build/diff PASS。
详见FullModelAmrDisplayProgress.zh-CN.md/Summary。生产binary/实际Host/desktop UAT仍待完成，
不以root mesh推广三维混合细化或科学验收；独立plt/JENS/RZ/CUDA未完成。无push/tag/main merge。

### 生产更新前实际 freshness 门槛修复

基线1c97f9b0。旧生产binary仍为e506619f；实际Build检测到10个compiler inputs变化/needs-build，
但旧Preview readiness仅检查固定inputs而错误ready=true。本次明确拒绝needs-build，
实际复核变为ready=false/Build required，保留full-dependency unknown的原有边界。
新增回归与最终244/244、lint/typecheck/production/diff PASS；日志ignored本地。
旧Linux窗口已通过原生close正常退出，owned Electron/Host均消失，未覆盖未保存输入。
随后须正式Host-owned CPU Build和真实多模型Host/desktop UAT；本项不是完整阶段封箱。

### 生产 CPU binary 与14模型真实 HTTP 验证

正式Host-owned Build b122233f，在clean a299395c上12项增量编译/链接，binary ee3de6cf；
actual Manifest保持dependenciesComplete=false/freshness-unknown，不冒充clean/current。
14模型unsaved inspect/field/AMR及全身份/EOS/native匹配PASS；RT limited如实保留。
UI修正精确selected-binary SHA与successful Build分层匹配，最终244/244及lint/typecheck/build/diff PASS。
详见FullModelProductionHostProgress.zh-CN.md/Summary；原始响应/Manifest/events仅本地ignored。
旧Linux窗口正常关闭，desktop实际新能力UAT待进行；没有simulation/Save/push/tag/main merge。

### Gaussian 三维真实 Linux 桌面代表

production独立窗口已完成32³真实场、z/x切片、原始sample1807、根AMR叠加/几何及正常关闭清理；field/config身份和数组未变。完整模型/曲线/非立方体/取消矩阵仍待完成；slice清空选择已如实记录。详见FullModelNativeDesktopProgress.zh-CN.md。本次仅补充证据，不重跑未变更的244项检查。

切片选择保留后续：两个UI handler移除selected清空，production Linux窗口实测sample783在z索引/固定轴变化后保留原始8字段，切面外提示及无marker正确。请求/数组/配置身份未变，244项及lint/typecheck/build通过，关闭exit0。未重新编译Core，完整桌面矩阵仍待完成。

### 球坐标非立方体桌面代表

865f49bf production Linux独立窗口实测Nx5/Ny3/Nz2→shape[2,3,5]、30样本；phi/r切面分别5×3和3×2，Core原生cm/rad单位、sample12原值、根AMR叠加及关闭exit0通过。轮滚未发生可见缩放，不计PASS；完整矩阵/缩放/取消/非空间模型桌面仍待完成。证据见FullModelSphericalDesktopProgress.zh-CN.md。

### O7.1 实施前调用映射

已按执行细则核对共享数学/EOS/PhysicalSpacing、AMR flag与候选父态、Driver接受步、plot/API/Studio和CUDA消费者，形成O7_1_JENS_IMPLEMENTATION_MAP.zh-CN.md。当前JENS仍unavailable，未实施或运行演化；一般EOS/父态/独立预算批准ref待提供。前序桌面出口仍待完成。

### 独立 plt：元数据有界原型

基线41aa5354；仅本地metadata primitive，未接 production Host/UI。
现有writer无完成发布契约/单位/native cell geometry/科学身份，原型始终
completion unknown、renderEligible false。真实维护Sod及已有t=0 12叶块/192cells
只读读取，field arrays禁止读取测试通过。5/5 scoped tests、lint/typecheck PASS；
无新模拟/IO改动。详见PlotfileMetadataPrototype.zh-CN.md及精简Summary。
正式Reader/LOD/Inspector待契约freeze与隔离实现；不把原型计为plt出口完成。

### 全模型原生桌面：BurnOneZone 非空间状态 Inspector

基线7426b516；真实production Linux窗口验证均匀初态表，发现Inspector误显示
协议采样坐标后按Core uniform-state语义修正，仅显示原值和来源身份。
249/249 Studio/Host、lint/typecheck/build/diff PASS；新production原生复验通过，
warm更新六字段/数据/配置/Build身份不变；两轮正常关闭exit0，实际worker已回收。
详见FullModelUniformStateDesktopProgress.zh-CN.md及精简Summary。
非燃烧演化/AMR/性能验收，完整桌面矩阵仍未完；状态过宽文案单独记录。

### O7.2–O7.5 实施前：独立 RZ 消费者映射

基线af18b44a；按handoff §2完成RZ独立当前调用方→修改点→参考→测试映射。
真实源码仍为2D polar：physical position/spacing/source/seam/log gravity均未改。
50个tracked源码文件229词法命中及间接transfer/IO路径、source hashes/test入口、
O7.4有限环体近远场/缓存/独立oracle决定清单见O7_RZ_IMPLEMENTATION_MAP及Summary。
checkpoint仅geometry string不能区分旧polar与未来RZ，明确保留semantic revision gate。
未改Core/开放能力/执行不变测试；先O7.1 CPU，科学确认与历史迁移仍pending。

### 构建身份收尾：CMake 输入、LTO 保留及选定链接器

568bf0c3/5ba4a957 通过 GNU LTO plugin debug 和 Host-owned TMPDIR 保留实际链接依赖；先前真实 link 已验证 172 项无缺失，浮点/IPO 定义未调整。
1f80abd8 接入 CMake File API 的构建前后输入；164de218 接入 ARCH target 选定 GNU linker 指纹与 legacy unknown。最终 Studio/Host 260/260，lint/typecheck/production build/diff PASS。
clean 164de218 标准 Host-owned 增量 Build 39eb4f9e 成功；Ninja no work to do，binary ee3de6cf 不变。82 CMake/735 compiler/172 link 输入，三个稳定标志 true；独立 Node 重载同 build ID、changedInputs=[]，完整覆盖仍 false/freshness-unknown。
详见 BuildSelectedLinkerIntegration-20261003.zh-CN.md/Summary.json。本项不是干净重建或实际 linker 执行观察；未运行 simulation/push/tag/main merge。
下一阶段仍按联合计划推进；全模型桌面矩阵、正式 plt、O7.1–O7.5、CPU/CUDA 科学与性能验证尚未封箱。

### O7.1 数值基础：隔离共享 Jeans 叶函数

a06b8d51 基线上增加 JeansDiagnostics::evaluate，消费总密度/声速平方/max(h_active) 与共享 CGS G；二进制指数缩放保留可表示极端结果。无生产消费者、能力仍 unavailable，不选 EOS/AMR 阈值或新增 floor。
独立 Decimal 80/120 位参考、8 个数值 case、12 个非法输入位置和2个最终不可表示结果；现有 build-cpu scoped target 编译与 CTest 1/1 PASS。首次选 production BUILD_TESTING=OFF 的 unknown-target 失败保留。
详见 JeansNumericLeaf-20261003.zh-CN.md/Summary；一般 EOS/候选父态/条件规则与独立科学预算 pending，完整桌面矩阵和 O7.1 出口未完成。
未重编生产 ARCH、未运行 simulation/Preview/CUDA、不 push/tag/main merge。

### O7.1 数值基础：活动物理间距绑定

abdbf87d 基线上 evaluate_cell 复用 GridMetrics::PhysicalSpacing，逐活动轴校验后取最大值；忽略非活动轴，非法值不由max/abs/floor掩盖。
各向异性 Cartesian/当前polar/3-D cylindrical/spherical赤道、极区、首径向单元及非法几何/维度/间距通过独立尺度参考；既有曲线度量回归共同 CTest 2/2 PASS。
详见 JeansPhysicalSpacing-20261003.zh-CN.md。未接接受态EOS/AMR/plot，能力仍 unavailable，科学确认与前序桌面矩阵门槛保留；无 production ARCH build/simulation/CUDA/push/tag/main merge。

### O7.1 静态数值证据：真实 IdealGas / 组成闭合

30820aeb 基线上仅扩充现有 scoped test：真实 IdealGas pressure/sound speed→evaluate_cell，与独立 caloric 解析参考比较。
24个密度/内能/静止/运动/单闭合/双组分组合，最大相对误差1.9657272738823614e-16，CTest1/1 PASS；双组分独立gamma27/14不等于构造fallback1.4。详见 JeansIdealGasReference-20261003.zh-CN.md/Summary。
fetch后compute/optim仍8fc0dd25，无新科学批准引用。一般EOS/AMR/演化/restart科学预算仍pending，JENS production unavailable；无simulation/CUDA/production ARCH build/push/tag/main merge。

### 原生滚轮复核：输入目标不一致证据

8d8e6617 production RT128²双向滚轮及普通参数滚动对照均无可见变化；一次点击返回explorer/目标msrdc不匹配，重新activate后截图布局改变。没有证据将其定性为绘图数学bug，也不计native zoom PASS。Alt+F4正常关闭exit0，9个owned进程按PID/start均消失，config/binary SHA不变。详见FullModelNativeWheelDiagnostic-20261003.zh-CN.md/Summary.json；全模型桌面出口仍pending。本轮无源码/Save/Build/simulation/CUDA改动，不重复未变更回归。

### Production renderer 缩放/平移/选点/Fit 实际事件诊断

8557b900：正式入口/dist/RT128²，Electron wheel 与 DOM wheel 均到SVG并改变轴域，反向wheel/Fit恢复；缩放后pointer pan改变轴域，选点marker随Fit重投影。全部fetch为GET状态轮询，无Preview/Save/Build/Run mutation，config/binary SHA不变。正常close/Host退出，四轮desktop/Host PID均消失；临时harness两项失败如实保留。详见FullModelRendererWheelDiagnostic-20261003.zh-CN.md/Summary。renderer范围PASS不替代native WSLg UAT；完整桌面出口pending。无正式源码改动，不重复260项不变回归。

### Plotfile有界字段切片原语

ae71df6d上复用同一pinned读取事务，单block hyperslab最多512样本，原始values/Cartesian中心/global索引对齐，NaN显式编码。二维非方形/三维/边界/恢复5项PASS，完整265/265及lint/typecheck/build/diff PASS。真实Sod H5的DENS8样本只读通过，raw本地保留。详见PlotfileBoundedSliceProgress-20261003.zh-CN.md/Summary。slice尚未接隔离worker/endpoint/UI，completion unknown/renderEligible false，正式plt出口未完成；无simulation/Core build/CUDA/push/tag/main merge。

### Plotfile隔离/Project切片接线

54747a7e上slice复用固定worker/15s/64KiB/Node heap与单任务容量，cancel/timeout/超限exit后恢复；pure请求校验不加载WASM。Project请求必须携带expected SHA，post-read还核对路径dev/ino/size/mtime/ctime；异步修改请求不能重标记返回值。完整269/269与typecheck/lint/build/diff PASS，超响应真实H5退出测试通过。详见PlotfileIsolatedSliceProgress-20261003.zh-CN.md/Summary；无endpoint/正式Viewer，completion unknown/renderEligible false，正式plt出口仍pending。无scientific Core/simulation/ARCH build/CUDA/push/tag/main merge。

### Plotfile真实HTTP审计接线

508595c2上metadata/slice固定只读POST audit路由接入，精确Origin/protocol/token及Project/file身份保护；真实Sod global32值通过，late Project变化409。初次global/local索引错误由worker正确拒绝后修正测试请求。完整272/272及静态/build/diff PASS。详见PlotfileHostAuditApiProgress-20261003.zh-CN.md/Summary及host/PLOTFILE_AUDIT_API。断开abort signal已接，live HTTP时序证据pending；无正式审计UI，completion unknown/科学身份缺失/renderEligible false，正式plt出口未完成。无simulation/Core build/CUDA/push/tag/main merge。

### Plotfile live HTTP断开清理补证

86800bb7：实际loopback metadata/slice请求，/proc复核直接owned worker argv/parent/start后SIGSTOP，同身份T与pending100ms确认，再断开HTTP；worker回收约14.897/9.678ms，后续读取200/SHA一致，字节未变。故障注入scope，不冒称自然HDF解析/native close。首轮proc ESRCH竞态失败保留后修正validator。完整273/273、lint/typecheck/diff PASS，生产源码未变不重复build。详见PlotfileHttpDisconnectVerification-20261003.zh-CN.md/Summary；正式plt UI/科学语义出口仍pending。

### Project Plotfile原始审计UI接入

5cd70044上接Host metadata/slice、Project请求保护及bounded原始值表，未知completion/单位/science/native几何诚实显示；旧1D文案纠正但保留local H5需求。真实reader→客户端拒绝错位/伪认证测试通过，最终275/275+静态/build/diff PASS。最终production renderer/Host显示Sod4×16与PRES global32原值，正常退出/owned Electron Host消失，config/binary不变。详见PlotfileProjectAuditUiProgress-20261003.zh-CN.md/Summary。native Manual UAT/正式科学Viewer/LOD/native-cell仍pending，已请求Core IO准确契约；无simulation/ARCH build/CUDA/push/tag/main merge。


### 正式 Plotfile Core IO 最小契约草案

6eef34f5 clean 基线上只读核对 writer/PlotIO，形成 PlotfileCoreIoContractProposal.zh-CN.md。
明确 direct truncate、close 前 Saved 日志、异常仅记录及字段/layout/Cartesian centers 的现行含义。
提出版本化 completion/identity/units/native-cell 与 scoped acceptance；发布、体积、LOD 待 Core 定案，不称实现或验收通过。
仅文档，无 Core/Host/UI 修改、build/simulation/Preview/CUDA/push/tag/main merge；正式科学 Viewer 出口 pending。


### O7.1 静态接口链路：manufactured Native Tabular

7473158e 基线上扩充既有 native fixture，真实守恒态 EOS→Jeans 数值叶与独立 caloric 参考比较，240 项最大相对误差1.0004323370200706e-14；既有2e-12容差未变，scoped编译/CTest1/1 PASS。
详见 JeansNativeTabularReference-20261003.zh-CN.md/Summary。未传真实表参数，不能称全 EOS/科学通过；JENS production仍 unavailable，一般EOS/AMR/预算 pending。仅测试与精简报告，raw H5/日志留本地，无production ARCH build/simulation/CUDA/push/tag/main merge。


### 原生 RT 点击／滚轮再次区分

61cf9142 production main/preload/dist，最大化原生窗口后点击sample9664/坐标/密度2与Inspector身份一致，点击链路有效；双向wheel与稳定观察domain不变，native zoom仍NOT VERIFIED。首次CLI flag顺序错误保留，改测试命令后重开。Alt+F4 exit0，8个owned进程全消失、config/binary不变。详见FullModelNativeWheelRecheck-20261003.zh-CN.md；下一步需被动wheel delivery证据，非重复发送或renderer模拟冒充。无源码/build/simulation/CUDA/push/tag/main merge。


### 原生 wheel 被动投递取证

a2e4b9c5 上仅临时 capture/passive observer：Computer Use plot负wheel/普通参数正wheel均0 wheel事件；同位置click有2个trusted svg pointer事件、sample9664选中。排查收窄至跨WSLg投递，不能宣布native zoom PASS。详见FullModelNativeWheelDelivery-20261003.zh-CN.md/Summary。用户硬件鼠标对照pending，当前owned Electron355883/session29013 live暂保留；未声称cleanup已完成。config/binary未变，无正式源码/build/simulation/CUDA/push/tag/main merge。


### 测量工具：物理终点模式工程补齐

5a44cbec 上既有 curved runner 增 endpoint-pair，max_steps=-1/tmax显式、逐backend即时终点校验、实际步数可不同，原科学/parity/AMR检查保留。8项synthetic H5/stub测试与CLI/diff PASS；依赖与编辑错误日志本机保留。详见PhysicalEndpointRunnerProgress-20261003.zh-CN.md/Summary。不是正式benchmark：预热/交替/线程/manifest/冻结输入预算仍pending；无真实ARCH/CPU-CUDA轨迹、scientific Core/production build/push/tag/main merge。


### 测量调度：预热、交替、显式 CUDA Host 线程

2cc0b9ab上补 paired_trials，endpoint显式CPU/CUDA Host threads与>=3pairs，warmup单列、交替顺序、逐attempt失败保留、所有计时/min/max/median/负收益完整。13项synthetic/stub suite与diff PASS，qualified_benchmark=false，正式冻结/线程/manifest/资源出口未完。详见PhysicalEndpointScheduleProgress-20261003.zh-CN.md。无真实simulation/CPU-CUDA计时/push/tag。


### Plotfile 分工与候选实现授权更新

47d99ab1 clean 基线上核对联合计划 35c5b7b114069621901386bfc4bc2a656e65af06（origin/codex/o8-boundaries）。本地契约草案移除等待完整 HDF5 布局的旧实现门槛，改为 writer/查询/Viewer 三个小交付；Core review 单位、坐标、测度、身份、发布与原值一致性。首版仅 Sod 1D + Cartesian 2D AMR，原始 H5 本机，明确首次全域叶块扫描成本。本次仅更新范围文档，不宣称 writer/科学 Viewer 已完成；无源码修改、Build、simulation、push/tag/main merge。


### Plotfile 候选发布基础增量

835edb77 上共享 writer 增 checked flush/close + 同目录临时文件 atomic replace，失败向调用方传播，Saved 移到成功发布后；raw double/Grid/Data/shape 与 checkpoint 格式保留。Sod-shaped 1D/非方形2D多块/NaN/Inf、旧文件保护及 create/rename 失败、cleanup 测试与既有 checkpoint suite 2/2 PASS。详见 PlotfilePublicationProgress-20261003.zh-CN.md。尚无科学身份/单位/native bounds/测度，关闭故障注入与真正原生查询/Viewer pending；不自动认证旧文件。无 production ARCH build/simulation/CUDA/push/tag。


### Plotfile Cartesian 原生网格候选

eb12c90c 上 NativeGrid candidate-cartesian-1：真实 PlotIO 原 loop 同步写 bounds/shared CellVolume/logical coordinates，raw Data/Grid 不变；仅1D/2D Cartesian，单位与科学身份 unknown。ng=2 manufactured双块1D/2D raw/measure/bounds/logical回读、invalid拒绝与 checkpoint 2/2 PASS，PlotIO object 编译通过。详见 PlotfileNativeGridProgress-20261003.zh-CN.md。native query/真实AMR输出/review未完；无 production binary替换/simulation/CUDA/push/tag。


### Plotfile 候选原生只读查询

198aa969 上候选 header/512 bounded nativeCells 接入现有 pinned reader/worker，严格版本、布局、发布标记、bounds/measure校验；logical file-local，unknown身份/单位/正式能力不提升。C++writer真实H5→Node查询1D/2D原值/测度/identity/字节不变通过，Studio277/277+Host127/127+lint/typecheck/build/diff PASS。详见 PlotfileNativeQueryProgress-20261003.zh-CN.md/Summary。输入64MiB与full-file hash保留，actual HDF read bytes/RSS未测，client/Inspector/Viewer/真实模型review未完；无 simulation/production ARCH/CUDA/push/tag。


### Plotfile 原生 Inspector 候选

9e284bcc 上客户端严格校验候选nativeCells/header与file-local identity，逐样本 Inspector显示raw/fileSHA/time/index/no-ghost i/j/k/center/bounds/shared measure及unknown警示；legacy不补算。真实C++writer→reader→client→React SSR1D/2D/legacy通过，npm279/279+lint/typecheck/build/diff PASS。详见 PlotfileNativeInspectorProgress-20261003.zh-CN.md/Summary；不是native desktop UAT或全域Viewer，科学身份/单位/真实模型对照/review pending。无simulation/CUDA/production ARCH/push/tag。


### Plotfile case / EOS 部分来源身份

ae3af9b7 上实际 DriverIO 的 resolved checkpoint EOS evidence + immutable ConfigurationInput.case_id传到共享writer SourceIdentity候选；gamma/ordered species/tableSHA复用、不重载EOS。unknown config/build/binary/unit/system保留，partial不冒认证。DriverIO/PlotIO object与IO scoped2/2 PASS、独立h5py回读通过、diff PASS；详见 PlotfileSourceIdentityProgress-20261003.zh-CN.md/Summary。reader/UI未消费新身份、完整原始config/binary与真实模型review待续；无simulation/production ARCH替换/CUDA/push/tag。


### Plotfile 加载时原文身份

9a34e0db 上复用parser InputText，由成功RuntimeParams Load/LoadText捕获 immutable raw_text，再由PlotIO shared SHA传writer；不重读路径、不归一化raw。注释/CRLF/no-final-LF、独立hashlib参考、parsed同raw不同、磁盘load后替换不改变identity及partial不认证测试通过；配置+IO scoped3/3 PASS、真实PlotIO/DriverIO object与diff PASS。详见 PlotfileRawConfigProgress-20261003.zh-CN.md。binary/build/effective/units/reader显示/真实模型review未完；无simulation/production替换/CUDA/push/tag。


### Plotfile 实际运行 binary

5a8bd920 上 shared process digest 经/proc/self/exe按进程缓存并传PlotIO→writer，主executable范围明确；build/source/dependency freshness仍unknown。配置/IO3/3 PASS与PlotIO object编译；独立owned ELF启动后launch路径替换，fresh proc/cached SHA保持原inode，与独立Python hash一致，exit0子进程消失。详见 PlotfileRunningBinaryProgress-20261003.zh-CN.md/Summary。reader/client新身份、effective/units/真实模型/Viewer/review未完成，无 production替换/simulation/CUDA/push/tag。


### Plotfile 部分来源证据 UI

82ab50fa 上SourceIdentity候选有界读取+Host/client共享语义校验，逐文件metadata与原生Inspector显示case/raw SHA/running binary/EOS/species，partial/unknown不升级科学认证。真实C++H5→reader→client→React SSR及legacy通过，npm282/282+lint/typecheck/build/diff PASS；详见 PlotfileSourceEvidenceUiProgress-20261003.zh-CN.md/Summary。SSR非native UAT，units/effective/build/真实模型/Viewer/LOD/I/O/review未完。无simulation/production ARCH/CUDA/push/tag。


### Plotfile 发布失败/中断证据

f309d8f0 上Linux test-only linker wrap注入真实H5Dwrite/H5Fflush/H5Fclose负返回，异常/无成功log/旧digest不变/临时清理检查通过。owned writer child flush前暂停后SIGKILL，旧final不变、partial留ignored、child消失；reader打开前拒绝partial保留命名。CPU scoped1/1、npm283/283+lint/typecheck/diff PASS；详见 PlotfilePublicationFaultsProgress-20261003.zh-CN.md/Summary。非hardware/断电/全窗口覆盖，真实模型与Viewer/科学review未完，无simulation/production binary/CUDA/push/tag。


### Plotfile真实t=0 writer对照

24f448ce clean CPU -j8增量ARCH23.4s/采样groupRSS4.8GiB，desktop production未替换。精确既有授权t=0方法Sod12叶/Cellular20叶与Preview/Checkpoint keys相同、step0/time0；DENS192/5120 bit mismatch0，center/measure差0，Cellular bounds差1.776e-15如实提交owner review不增容差。case/raw/binary/EOS/species和真实reader→client通过。详见 PlotfileRealT0Progress-20261003.zh-CN.md/Summary及repeatable validation/io脚本；raw/ARCH副本本机ignored。非evolution/全field/Viewer/单位科学验收，无CUDA/push/tag。


### 2026-10-03：Plotfile 只读查询成本基线

新增 validation/io/measure_plotfile_query.mjs；既有真实 Sod/CellularDet t=0 文件各做 metadata/slice 查询，源 SHA 不变。Cellular 返回 2058/3058 bytes，而每次 digest 扫描 472888 bytes；rchar 499642/522186 bytes、存储 read_bytes 0（缓存条件），单独说明计数语义和 RSS 范围。详见 PlotfileQueryCostProgress-20261003.zh-CN.md 和处理后的 Summary.json。无科学输出/前端修改；全域 Viewer/LOD 尚未完成。


### 2026-10-03：原生 Plotfile 单块视图

新增 1D bounds 线段/2D native rectangles、Viridis raw range、zoom/pan/Fit 和图表→Inspector 同一 row 的选择；显示不修改输入或发起读取。286 测试及 lint/typecheck/build PASS；真实 Sod 16 / CellularDet 256 单元 H5→reader→client→SVG SSR PASS，原始 H5 不变。原生窗口 UAT、全域/LOD 尚未完成，不声称正式 Viewer 完成。见 PlotfileNativeViewProgress-20261003.zh-CN.md。


### 2026-10-03：跨块候选总览与原生回查

新增受控 audit-overview、固定输出 streaming display LOD、全域 zoom/pan/Fit、representative native cell→同 digest Inspector。真实 Sod 192 / CellularDet 5120 单元完整扫描，HTTP/client/SSR/raw回查 PASS，原 H5 不变。合并512以内连续行后 Cellular rchar 34271170→3551170 bytes，完整response优化前后exact一致。294 tests + lint/typecheck/build PASS。未声称桌面UAT/完整AMR outlines/大型文件支持完成。见 PlotfileGlobalOverviewProgress-20261003.zh-CN.md。


### 2026-10-03：全域原生 leaf outlines 与 Block Inspector

同digest输出最多128个leaf native bounds/level/key/shape，显式complete/limited；全域SVG共享物理映射和clip，level开关只改轮廓。296 tests+lint/typecheck/build PASS；真实Sod12/Cellular20与checkpoint/Preview keys和H5 native包络一致。129块fixture证明limited outlines不截断全域field。真实窗口交互UAT和viewport/index仍待完成。见 PlotfileLeafOutlinesProgress-20261003.zh-CN.md。


### 2026-10-03：全域坐标点击→精确原生cell

新增受控audit-point，存盘bounds半开/全域最大边界包含，one exact match回查raw slice；gap/overlap明确失败。client/Inspector校验同digest和point，保留失败前结果。300 tests+lint/typecheck/build PASS；真实Sod index96/Cellular index3074 raw值和文件SHA保持。当前仍full scan，非indexed查询；窗口UAT、viewport/index及owner review待完成。见 PlotfileNativePointProgress-20261003.zh-CN.md。


### 2026-10-03：显式viewport LOD与full Fit保留

严格viewport请求、显示domain/globalDomain分离，完整nativeBlocks身份不裁切；手动finer按钮，缓存一份full+一份refined，Fit不读文件。revision guard防止过期视口覆盖。302 tests+lint/typecheck/build PASS；真实Sod/Cellular局部HTTP/client/SSR PASS，仍full scan。现有WSLg窗口恢复可见，但保留旧RT会话，不声称新Viewer native UAT。见 PlotfileViewportProgress-20261003.zh-CN.md。


2026-10-03：Plotfile Linux native UAT 发现并修复 stale binary 阻断只读启动；Sod extrema/outline 遮挡已修 display 范围，native 复验待办。304 tests及静态/production检查PASS。见 PlotfileNativeDesktopFindings-20261003.zh-CN.md。未 Core Build/simulation/push/tag。

2026-10-03：Linux native Sod extrema留白复验通过；Cartesian2D热图/level轮廓/zoom-pan-fit/原生point已实测，h5py独立核对两单元FP64指标一致。修复1D导航保留场纵轴及只读控件对比度，305 tests+lint/typecheck/build PASS，新assets native复验待办。无科学输出或发布。


2026-10-03：Plotfile candidate 字段声明增量：共享固定 CGS unit owner、producer-supplied
dataset metadata、Cartesian 1D/2D measure normalization 和坐标/时间标签已实施。
publication/checkpoint scoped tests 与独立 synthetic h5py FP64 位级读回通过；
未重链 production ARCH、未生成新 t=0、未做完整身份或科学 review。
见 PlotfileFieldDeclarations-20261003.zh-CN.md。旧 native UAT/305项 Studio检查不重复；
完整联合目标、O7物理 review/平台验证及新声明的 Host/Viewer 消费继续未完成。


2026-10-03：从clean ab23bad7构建CPU ARCH c294f0d1…，既有Sod/CellularDet t=0方法通过。
新DENS/native/单位声明经checkpoint独立读回与Host/client四种只读查询通过；
Host与UI已迁移recorded declaration，旧文件unknown保留。
309项回归/lint/typecheck/build通过，新单位native UAT及完整科学/来源review未完成。
见PlotfileFieldRealT0-20261003.zh-CN.md；完整联合目标不收缩到本切片。


2026-10-03：e7181420 production Linux Electron实际完成新单位Sod/Cellular metadata→LOD→point Inspector。
轴cm、DENS g/cm^3、time s与低维cm/cm^2 normalization实际显示；index51/3074经独立h5py一致。
更新准确writer构建SHA/HostUI SHA/数组映射对接文档；旧pending记录由新报告补充，不追溯改写旧证据。
见PlotfileNativeUnitUat-20261003；来源仍partial、发布未认证、Cellular bounds finding待owner review。
无新Build/Preview/simulation/重复测试/push/tag；完整联合目标未完成。


2026-10-03：关闭Linux启动registry/schema busy和freshness共享中间态两个实际finding。
两个反例先复现失败，再修复；311/311全回归+lint/typecheck/build通过。
最终native多轮轮询registry/source保持、dirty binary needs-build及初始化禁用稳定。
中间旧Host一次false-ready自动Sod Init Preview已如实保留失败记录，非simulation验收。
见StudioStartupFreshnessConcurrency-20261003；Core和完整科学/平台目标未完成。


2026-10-03：真实t=0多字段切片Sod9/Cellular28，ALL仅改变输出字段/新目录；新旧checkpoint守恒量和组分位级差0。
37字段Host/client slice/LOD/point及h5py原生回查通过。发现ENTR canonical名称冲突并最小修正metadata；
scoped2/2及孤立ENTR header通过，完整unknown-extra fixture/旧生产文件边界如实保留。
见PlotfileMultiFieldT0-20261003；新canonical生产输出及科学/平台review未完成。


## 2026-10-03 Plotfile Driver failure propagation

Canonical t=0 来源 a2b0658b/b360c662 已完成；报告提交 94a094f4。
本轮在真实 CPU Driver 复现发布失败推进 plot index；修复为成功返回后递增。
五种故障及同编号重试通过，既有 IO 两项回归通过；原始文件留本机。
见 PlotfileDriverFailurePropagation-20261003.zh-CN.md。科学 review、完整身份、
checkpoint 原子发布以及大文件/缓存/资源证据保持待办；全联合目标未封箱。


## 2026-10-03 production Plotfile worker cost

真实 Sod9/Cellular28 多字段文件经 production 隔离 worker 查询、BUSY、启动期取消和恢复通过。
Cellular 32x24 响应28512bytes仍扫描5120单元；每请求新worker，没有跨查询cache。
见 PlotfileIsolatedWorkerCost-20261003.zh-CN.md / Summary.json；不外推大文件/HDF深度取消/同机演化影响。


## 2026-10-03 resolved EOS constituents

新增 /SourceIdentity/species_A,Z,gamma,Cv，FP64[Ns]，严格按 species_names 顺序，
直接来自 runtime checkpoint provenance；state/version/source 与缺失 reason 明确。
Sod1/Cellular19 组分四组属性与 checkpoint 位级一致；Host 旧路径兼容，整体仍 partial。
见 PlotfileEosConstituents-20261003.zh-CN.md / Summary.json。当前 Viewer 未消费新属性，不声称完整 EOS verified。


## 2026-10-03 recorded EOS properties Host/Viewer

之前“Viewer未消费新属性”为历史状态：本轮已接入 optional speciesProperties，严格FP64[Ns]及来源验证。
Sod1/Cellular19真实属性经Host/独立HDF逐位一致；315项全回归及production检查通过。
Linux原生窗口确认两模型表格可访问，保留partial/unknown，不做单位推断或EOS verified。
见 PlotfileEosSourceUi-20261003.zh-CN.md / Summary.json。完整联合目标和其余科学/资源验收仍待。


## Plotfile FP64 wire 与读取对接增量（2026-10-03）

已完整核对 owner 23ff77c4f contract，刷新 PlotfileReaderAdapterHandoff 的实际字段/数组/身份/发布映射。
真实 HDF5 负零在 worker/HTTP JSON 中丢失已先复现，最小传输及显示补丁后 1D/非正方形 2D 位级反例通过。
316 项全回归、lint/typecheck/build/diff check 通过；见 PlotfileFp64Wire-20261003.zh-CN.md/Summary.json。
本轮不重建科学 Core、不运行 simulation、不变更 checkpoint；完整科学与资源验收仍待，未封箱联合目标。


## Plotfile 大叶块工程成本与读取期取消（2026-10-04）

基线33a49399，52万单元合成 fixture 首次/重复总览仍全扫；仅复用单查询 bounds dataset 对象。
三组交替测量大夹具总览/point中位下降约26%，rchar未显著减少，保留负收益与资源限制。
4文件×4查询完整响应与冻结基线一致，真实Sod/Cellular文件保持只读。
读取期取消后约4ms完成reap，BUSY/recovery通过；316项全回归及生产检查通过。
见PlotfileLargeQuery-20261004.zh-CN.md/Summary.json；不宣称真实AMR全验收、同机演化影响或科学目标完成。


## 全模型三维混合初始 AMR 补证（2026-10-04）

复用现有 CPU API，RT 1×3×1 返回 L0=2/L1=8 的真实 last-completed-balanced hierarchy；
仍 limited，不提升为complete；三轴81次命中、精确Cartesian无重叠/域覆盖/2:1与四个反例拒绝通过。
保留初次CLI语法错误、Sedov只有root、RT四root limited和外部域假设修正的完整处理摘要。
同步INITIAL_AMR_API.md的动态全模型/native轴单位；没有生产源码改动，不重复先前316项检查。
见FullModel3DMixedAmr-20261004.zh-CN.md/Summary.json；native UI与完整科学/平台出口仍待。


## 2026-10-04：受管生产 CPU Build 与 Sod 初始化恢复

从清洁 a28c6576 的 Linux 桌面执行既有 CPU Build，Manifest c99c8486、binary 1bf5a00d；通过 Refresh Project State 后 Sod field/根级 Initial AMR 均 Current。完整依赖 freshness 仍 unknown，compiler-input 前后稳定标记 false；未改科学实现或运行 simulation。详情见 StudioDesktopBuildRefresh-20261004.zh-CN.md 与对应 Summary。总体目标仍未完成。


## 2026-10-04：Build 后项目身份自动衔接

修复 ffcd32fd 上实测需要手动刷新的问题，成功受管 Build 后只刷新同项目同输出路径的 executable 身份；晚到/失败响应保护及去重回归通过，320/320 + lint/typecheck/build/diff PASS。新 production assets 在现有 Linux 窗口无需手动 Refresh Project State 即恢复 Sod Current。完整 freshness unknown 与科学/平台未完成项不变。见 StudioBuildProjectIdentity-20261004.zh-CN.md/Summary。


## 2026-10-04：Plotfile 全域 stored bounds finding

补全真实 Sod/Cellular 全叶单元精确覆盖诊断：逻辑分区均无遗漏/重叠，但 Cellular native FP64 bounds 出现极窄 gap/overlap，production point 在 [0.1,1.2]/[0.1,2.6] 实际拒绝零/双命中。5项反例测试通过，原H5不变；不添加容差或宣布几何验收。详见 PlotfileFullCoverage-20261004.zh-CN.md/Summary；下一步按 shared face 定义修复并 review。


## 2026-10-04：Plotfile shared y-face 修复

b26fb8a2 最小 writer 面索引求值修复，C++反例先失败后publication/checkpoint2/2通过。cleanCPU binary1bdd71ed仅增量PlotIO/link；新Sod/Cellular明确t=0对照stored全域gap/overlap均0，除Cellular1280个y upper外Plotdatasets与20项checkpoint数值字节一致。原Inspector失败两点唯一命中，旧文件保留；见 PlotfileSharedFaceRepair-20261004.zh-CN.md/Summary。科学/其他几何/演化/平台任务仍待。


## 2026-10-04：修复后的37字段补证

复用b26fb8a2 CPU binary执行既有t=0 ALL，Sod9/Cellular28字段完整数组与早期canonical原bits全一致；新ALL文件stored全域覆盖、37次production worker raw点查与Host/client链路通过。三项反例测试通过，原H5/全数组本机保留。见 PlotfileAllFieldsFaceRepair-20261004.zh-CN.md/Summary；独立EOS/diagnostic科学oracle、native UAT及总目标保持未完成。


## 2026-10-04 Jeans 指数边界补证

301项独立Decimal参考与共享数值叶函数CPU比对通过，197正有限（2次正规）/31下溢/73溢出，最大相对误差1.5432483614913696e-16。沿用既有工程界限，无floor或生产能力开放。见 JeansExponentGrid-20261004.zh-CN.md / Summary；一般EOS/父态/RZ近场科学决策仍待确认。


## 2026-10-04 Plotfile run identity

DriverIO输出会话UUID贯通writer/Host/client/Viewer，legacy unknown兼容，其他未确认身份仍unknown。321项Studio回归及CPU构建/发布检查PASS；四次真实CPU t=0的原始字段/20项checkpoint数据bit一致，隔离点查身份匹配。见 PlotfileRunIdentity-20261004.zh-CN.md / Summary。未演化、未CUDA、未push。


## 2026-10-04 RZ finite-ring axis reference candidate

独立Decimal轴线解析参考8例80/120位收敛，完整环体势/力/质量与有界tensor积分对照；源内/边缘发现较慢收敛，保留具体误差，不冻结生产阶数或科学预算。5项工具检查PASS。见 RZFiniteRingAxisReference-20261004.zh-CN.md / Summary；生产Core/capability未改、未ARCH运行/Build/CUDA。


## 2026-10-04：Linux 3D mixed AMR desktop 补证

清洁35654249经native UI受管CPU Build（binary2c53420c），RT32³与limited10-leaf混合初始AMR实窗显示、
三固定轴切面、图上块命中/Inspector及level过滤补证。发现hidden L1旧Inspector遗漏，最小显示过滤修复后
321项/lint/typecheck/build/diff通过，新production native hide/show复验通过。未simulation/科学Core改动/重复Build。
详见FullModel3DMixedAmrDesktop-20261004.zh-CN.md/Summary；native wheel、完整freshness及整体科学/平台目标仍待。


## 2026-10-04 RZ finite-volume off-axis reference candidate

新增独立Decimal K/E有限环体离轴点值参考与direct 3D Newton积分对照；
六例60/100位算术一致，固定源分区揭示近外缘整体64阶仍有约1.98e-15 cm/s²径向力变化。
完整旋转质量/四极矩给远场候选误差，未冻结生产阶数/开角/预算。8项工具检查与diff PASS。
见RZFiniteRingOffAxisReference-20261004.zh-CN.md/Summary。未改生产Core/capability/Build/ARCH运行/CUDA/push。


## 2026-10-04 RZ full-rotation metric primitives

在现有GridMetrics所有者下实现显式CPU/device完整环体volume、radial/axial face area与r/z spacing。
10组独立70/100位参考一致；现有CPU curvilinear_metrics定向构建/CTest通过，
RZ最大relative error1.64562e-16，旧metric3.03178e-16。未切GeometryView dispatch或开放RZ；
未编ARCH/演化/CUDA/push。见RZMetricPrimitives-20261004.zh-CN.md/Summary。
O7.1科学规则、RZ整条接线/近场预算和最终平台出口仍待。


## 2026-10-04 RZ 几何源项共享入口

显式 r/z/phi 适配与旧圆柱源项共用数学入口；生产二维 dispatch 不变。
限定 CPU curvilinear_metrics 1/1 PASS，包含旧 1/2/3D 非零旋流公式精确回归。
详见 RZGeometricSourcePrimitives-20261004.zh-CN.md / Summary.json；完整 RZ、CUDA 与科学 gates 尚未关闭。


## 2026-10-04 RZ 轴线 topology/ghost 增量

显式 RZ seam chart 已复用现有 donor/transfer executor，默认 polar chart 不变。
真实均匀与混合 L0/L1 轴 parity、非零内边界及旧 seam 回归 CPU 1/1 PASS。
详见 RZAxisSeam-20261004.zh-CN.md / Summary.json；生产调度、CUDA、完整 RZ 演化仍待接入。


## 2026-10-04 RZ Host exchange/cache 身份

显式 chart 已贯穿 GetPlans/ExecuteExchange，native geometry 进入 exact cache key。
真实均匀/混合 topology 的切换、命中、bounds 失效和完整 Host 轴 ghost 执行 CPU 1/1 PASS。
详见 RZExchangeIdentity-20261004.zh-CN.md / Summary；不代表 RZ 保守 AMR/CUDA/演化出口通过。


## 2026-10-04 RZ 共享 GeometryView

内部显式 chart 已贯穿 metric/source/代表位置转换，旧 Grid/default chart 保持。
CPU curvilinear_metrics + amr_operation_plans 2/2 PASS。
详见 RZGeometryView-20261004.zh-CN.md / Summary；运输/AMR/elliptic/IO/CUDA 与科学出口未关闭。


## 2026-10-04 RZ velocity diagnostics

修复显式 RZ view 误走 2D polar curl；复用既有 3D cylindrical 公式。
轴正则旋流/轴向结构 27-cell 解析 div/curl 和受影响 CPU regression 2/2 PASS。
详见 RZVelocityDiagnostics-20261004.zh-CN.md / Summary；黏性未解析方位连接及全路径仍待接线。


## 2026-10-04 RZ 黏性连接

显式 RZ 未解析 phi 连接复用 3D cylindrical 数学，源项及 row bound 同步。
Cartesian 线性场对应的 RZ 局部 shared face/source momentum/work flux 检查通过，CPU 2/2 PASS。
详见 RZViscousConnection-20261004.zh-CN.md / Summary；完整运输/变系数演化/CUDA 与科学出口未关闭。


## 2026-10-04 RZ composite Poisson/MG

显式 mesh 身份贯穿完整环体 volume/face、dr/dz、轴线与物理 z 边界，沿用现有 coarse hierarchy。
四组规则/混合制造解与旧 contract/curved/singular 回归通过，求解后 force/phi 指标单独报告。
详见 RZCompositePoisson-20261004.zh-CN.md / Summary；生产环体边界与科学预算仍未获验收。


## 2026-10-04 RZ isolated boundary guard

发现新内部 RZ mesh 可误入旧 2D log 核；构造与 cached values 双入口已明确拒绝。
RZ analytic operator 继续通过；旧 boundary/curved 回归保持。详见 RZBoundaryGuard-20261004.zh-CN.md / Summary。
有限环体生产方案和预算尚待 Core review，未 fallback/开放能力。


## 2026-10-04 RZ 完整 Host diffusion

显式 chart 贯穿 face/divergence/source/operator/dt，真实 padded 512 active-cell 线性场及独立 dt 一致。
变密度/动态黏度 work 的原生 volume-average 参考三分辨率二阶，CPU 受影响检查通过。
详见 RZHostDiffusion-20261004.zh-CN.md / Summary；实际边界/AMR/RKL演化、其它材料/CUDA/科学出口未关闭。

## 2026-10-04 RZ Block / coarse-fine 环体测度
显式 chart 贯穿 Block prolong/restrict 与 Host coarse-fine source weights，复用既有 shared transfer。
真实 parent/four-child/restored-parent 体积积分工程检查与 CPU regression 2/2 PASS。
精确 r*dV 角动量 finding：细化相对改变 4.9850588267931159e-4 / 1.0396136426979989e-4，
粗化恢复不代表 AMR 演化守恒；未改阈值或科学状态，需 Core discrete transfer contract。
详见 RZBlockTransfer-20261004.zh-CN.md / Summary；完整 RZ/capability/CUDA/科学出口未关闭。

## 2026-10-04 RZ Host Hydro 组合层
同一显式 chart 接通真实 face traversal/divergence/source 与 stage repair 的完整环体计量。
真实 HLLC/PCM/IdealGas 768-cell 压力/轴向/旋流组合及预算反例、CPU 2/2 PASS。
显式 RZ gravity/AMR reflux 未迁移时先拒绝；生产 IHydroSolver/runtime 未切换。
详见 RZHostHydro-20261004.zh-CN.md / Summary；角动量 AMR finding、实际演化/CUDA/科学出口仍待。

## 2026-10-04 RZ Host acoustic CFL
显式 RZ chart 贯穿 Hydro dt 的 dr/dz 与原 shared归约；非等距1024-cell夹具串/并行一致，
最大独立参考差4.33681e-19。发现并修复单 invalid active cell 被Ignore-NaN掩盖，
legacy反例与36项归约边界/CPU2/2通过；不修改共享归约数学。
详见 RZHostCfl-20261004.zh-CN.md / Summary；强旋流source稳定性/全路径/CUDA/科学出口未完成。

## 2026-10-04 RZ AMR flux/reflux
共享 topology/native view 接通完整环体注册面与 reflux A/V，chart/bounds纳入缓存身份；
累计通量不能跨chart/geometry消费，显式Clear重注册恢复，uniform empty计划no-op。
四组真实L0/L1 radial/axial实际Host修正和CPU3/3通过，最大积分算术差3.53183e-17。
详见 RZReflux-20261004.zh-CN.md / Summary；实际scheduler调用/演化/CUDA和角动量科学finding仍未关闭。

## 2026-10-04 RZ mixed Hydro stage
共享 Hydro registration 已传同一chart，解除内部AMR guard，保留未迁移gravity拒绝。
四组真实5-leaf/5120-cell exchange→HLLC/PCM→stage→reflux联动及CPU3/3通过，
final最大常态误差3.55618e-17。详见 RZMixedHydro-20261004.zh-CN.md / Summary。
生产scheduler、物理边界/非均匀演化、扩散/RKL、gravity/IO/checkpoint/CUDA与angular finding仍待。

## 2026-10-04 RZ 物理 BC
内部logical axis规则/实际BCHandler按native r=0选择，scalar/z even、r/phi odd，无half turn；
272个ghost/corner/signed-zero逐位参考、nonzero inner普通BC及旧冻结指纹保持通过。
CPU3/3与随后仅变化mixed Hydro BC→exchange→stage→BC→exchange→reflux检查通过。
详见 RZPhysicalBoundary-20261004.zh-CN.md / Summary；production scheduler/科学出口/CUDA与angular finding未关闭。


## 2026-10-04 RZ Host Hydro policy

真实 HydroSolverImpl 已传递固定内部 chart 并通过 IHydroSolver 暴露身份。四个 mixed-AMR fixture 改经真实虚接口，CPU scoped 3/3 PASS；详见 RZHydroPolicy-20261004.zh-CN.md。Euler/RK scheduler chart propagation 仍待完成，不宣称生产 RZ/演化验收。


## 2026-10-04 RZ actual Host scheduler

Euler/RK2/RK3 真实lane统一preflight、exchange和reflux chart；12组mixed-AMR实际ledger测试通过。错误BC profile保留clock/ledger与既有flux。受影响scoped4/4及最终flux guard1/1 PASS；详见RZScheduledHydro-20261004.zh-CN.md。公共runtime/CUDA/科学验收仍未完成。


## 2026-10-04 RZ DriverRuntime Host halo

初始/刷新halo已使用固定RZ chart。四个真实mixed-AMR Runtime案例PASS；受影响scoped3/3 PASS。device/regrid尚未迁移，明确拒绝而非静默旧chart。重编译的fixture与Runtime源码身份见RZRuntimeHalo-20261004.Summary.json；不代表production binary/演化验收。


## 2026-10-04 RZ Driver timestep candidates

真实calculate_timestep_candidates的Hydro/diffusion调用已传Runtime chart。mixed-AMR四组独立Hydro参考maxabs1.0842e-19，8组RKL候选路由接线一致；active NaN明确失败。scoped3/3 PASS。实际RKL evolution尚待迁移；详见RZRuntimeTimestep-20261004.zh-CN.md。


## 2026-10-04 RZ actual Driver RKL

真实single/composite RKL1/2贯通统一RZ operator/source/register/reflux/halo；16组常态零算子实际scheduler检查通过（RKL1两stages/RKL2五stages），maxerror1.42109e-14，scoped4/4 PASS。不是非零演化/科学验收；详见RZRuntimeRkl-20261004.zh-CN.md。


## 2026-10-04 RZ nonzero actual RKL

非零quadratic热方程实际Driver RKL独立参考8组通过，maxabs5.68434e-14；nonzero mixed-AMR闭域full-ring能量收支8组maxrelative2.38262e-17。原16组零算子同fixture通过。仅工程证据，不替代科学场值/长期验收；见RZNonzeroRkl-20261004.zh-CN.md。


## 2026-10-04 RZ checkpoint geometry identity
独立 revision/chart 保护已接入 HDF 与 Host read_chk；旧二维 cylindrical 文件不能按 RZ 恢复。checkpoint_compatibility scoped PASS，公共 RZ write/restart 尚未完成。见 RZCheckpointIdentity-20261004.zh-CN.md。未 push/tag。


## 2026-10-04 native RZ checkpoint writer binding
DriverIO 传递 Runtime profile，write_chk 写前校验并记录几何身份。实际二维 native FP64/controller roundtrip scoped PASS；DriverIO CPU TU 编译成功。公共 dispatch/restart/evolution 未因此完成。见 RZCheckpointWriter-20261004.zh-CN.md 与 Summary.json。未 push/tag。


## 2026-10-04 actual Driver checkpoint profile evidence
当前 Driver/Runtime/IO 源码重编后，Cartesian 与内部 RZ 两例实际写出/恢复 PASS；time=0 step=0，原生字段与 controller 未变。补齐此前仅 TU 编译的缺口。首次 fixture ledger 初始化缺失及修正保留证据。见 RZDriverCheckpoint-20261004.zh-CN.md / Summary。公共 RZ 演化/CUDA 未验收，未 push/tag。


## 2026-10-04 checkpoint failure index
实测 Driver checkpoint serializer 拒绝消耗序号，已将递增移到 writer 成功返回后。当前源码实际 Cartesian/RZ serializer/create 失败及同编号恢复 PASS，原值/controller不变；未改 checkpoint 发布机制。见 CheckpointFailureIndex-20261004.zh-CN.md / Summary。未 push/tag。


## 2026-10-04 native RZ coordinates
Grid 显式profile轴名/物理坐标/domain 已接入；PointCoords.r 保持球半径，x2=z长度不受角度限幅。旧路径不变，3/3相关CPU scoped checks PASS。PopulateState authoritative context 接线尚待执行，不能声称模型IC贯通。见 RZNativeCoordinates-20261004.zh-CN.md / Summary。未 push/tag。
