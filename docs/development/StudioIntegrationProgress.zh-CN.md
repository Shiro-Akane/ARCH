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
| 1 3B 源码接收与复验 | 封箱源码已引入；Linux 打开/另存/重开复验通过 | npm ci、177 tests、lint/typecheck/build PASS；Linux 原生打开、Host Save As、重开 PASS（范围见报告） | 不适用 |
| 2 O7.0 + 配置 v3/Host/Studio | 候选契约、14模型声明、部分输入共同检查及CLI/初态前置门已实现；v3序列化/受控构造/客户端在迁移 | 相关Core单元和真实入口检查通过；完整v3/Host/Studio未验证 | 科学条件按唯一计划；疑点交维护者 |
| 3 Linux/WSL 3C 启动/Configure/Build | 待实施 | 待执行 | 不适用 |
| 4 3C Run/Restart/进程隔离 | 待实施，依赖新配置契约 | 待执行 | 小型有效输入 |
| 5 全模型初态/AMR | 待实施 | 待逐模型验收 | 真实域/预算需明确 |
| 6 O7.1 JENS | 待实施 | 先 CPU | 独立参考/预算由维护者确认 |
| 7 O7.2–O7.5 RZ | 待实施 | 分层 CPU | O7.4 科学方案须 review |
| 8 CUDA/第二平台短测 | 待 CPU 完成 | 未编译/未计时 | 冻结同物理终点；保留负收益 |
| 9 批准的 O9 长时子集 | 待冻结输入/预算 | 未执行 | 未批准项不能称完成 |
| 独立 plt 只读出口 | 待 3C 后/文件语义确认 | 未执行 | 原生单元与数据身份 |

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
