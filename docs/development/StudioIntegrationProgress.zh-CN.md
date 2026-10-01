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
| 2 O7.0 + 配置 v3/Host/Studio | 候选规范与共享 schema/Sod/缺项/语法 fixture 已整理；运行实现待迁移 | 候选一致性检查通过；生产 v3 未验证 | 科学条件按唯一计划；疑点交维护者 |
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
