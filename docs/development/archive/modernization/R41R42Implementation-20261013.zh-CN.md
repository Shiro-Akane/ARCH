# R4.1／R4.2实现与独立验收

2026-10-13，`compute/optim`，HEAD `34a6ccd5f409ec14ec11c4b798e5795e7592f26f`。本轮按[R3交接](R33CloseoutAndR4Handoff-20261013.zh-CN.md)分文件并行实施，两包已由Codex与独立worker验收；**R4.3尚未开始，整个R4尚未完成**。[主计划](../../../../ARCH_MODERNIZATION_PLAN.md)维护当前进度，[机器索引](../../modernization/R41R42Implementation-20261013.json)保存身份、检查和原始证据入口。

## 已完成清单

| 包／负责人 | 实际改动 | 独立验收与结论 |
| --- | --- | --- |
| R4.1／W2 DeepSeek | 原runner按完整manifest判定组成员；完整选组调用原门槛；部分／混合组选取在执行、provenance和输出前拒绝并列缺失ID；更新原工具测试和backend用法说明 | Codex源码核对＋独立worker审查；当前工作区42项runner、40项provenance、11项日志测试通过；真实CLI缺项选择控制通过 |
| R4.2／W1 DeepSeek | 12份canonical输入显式声明`hll_wave_speed=roe`、`min_eint=1e-10` erg/g；说明区分当前模板与历史结果，修正HLL文档默认值表述 | 独立逐输入／历史表格核对＋Codex现有CPU静态API：12原输入、26派生配置、8项O7负对照，共46项通过；13消费者、100份派生参数覆盖 |
| Codex／W3 | Codex唯一集成、实际检查与记录；W3为独立Codex worker，准备配置检查并审查实际diff及返修 | 两个隔离工作树实现，主工作区使用`work` Python直接执行，未用旧构建CTest代签；原数值预算、生产代码、manifest、ELF／比较器保持 |

## R4.1行为与保护范围

| 选择 | 当前行为 |
| --- | --- |
| 不传`--case`或显式全选 | 原完整组检查；二者组摘要一致 |
| 一个或多个完整组，可带非组case | 按完整manifest确定成员，执行原`validate_resolution_groups`；原`resolutions/orders`结构保持 |
| 触及任何部分组，含完整组＋部分组 | 运行前拒绝，列出该组缺失ID，不创建输出、捕获provenance或执行ARCH；不自动补选 |
| 只选非组case | 限定逐case检查、空组摘要；原完整矩阵消费者仍拒绝子集 |
| 未知ID | 原明确拒绝保持；`--unit-test`仍只检查manifest，不是科学验收 |

四个新增main级测试覆盖全选／完整组正对照、部分／混合组预执行拒绝、CPU与CUDA有限值阶数失败／CUDA非有限值／缺失指标不发布成功证据，以及非组子集不能冒充完整矩阵。缺失指标沿用原异常，不新增防御schema、诊断模式或第二套runner。原组函数、科学case执行、checkpoint资格与runtime输入辅助函数均未改；原time/order与final/all测试继续通过。

真实CLI另查manifest 22项及部分组、混合组、未知ID三项负对照，均在实际执行物不存在的条件下按预期拒绝，没有创建输出。上述mock及入口结果只证明工具控制流，不构成双后端科学PASS。测试的权威入口为当前工作区的三个绝对路径，详见机器索引；旧CPU／CUDA构建CTest仍绑定主仓库，不用于此次签收。

## R4.2输入、历史身份与O7

12份输入的其余参数键值逐份保持，仅新增两项已证历史来源的显式声明；来源为[R33逐输入核对](R33CloseoutAndR4Handoff-20261013.zh-CN.md#r4212份当前hydro输入)。原Git blob／SHA与原结果保持，当前输入取得新SHA，不转签旧raw输入的科学PASS。两个Hydro README各21行历史表格原文保持；每份模板注释只说明估计器、内能下限和单位，历史来源集中说明。

| 检查 | 实际结果与边界 |
| --- | --- |
| 12份canonical静态inspection | 两项有效值、单位及`valueSource=input`符合预期，退出0 |
| 13消费者的双后端参数派生 | 26份终点配置由当前工作区原renderer生成并经CPU静态API检查；74份accepted-step参数仅渲染核对，两项控制均继承canonical |
| SmoothAdvection／Sod负对照 | 各对两项控制删除及显式非法，共8项；全部退出3，报对应缺项／非法，未回填默认 |
| 静态执行边界 | 46次CPU进程检查；无setup、EOS加载、CUDA初始化、物理演化或模拟H5输出，单次10秒上限无触发 |
| periodic第13消费者 | 复用`ppm_n64.par`，自身t=2.05／至少1000步要求、无独立模板或显式manifest override；本轮仅核继承与配置，不跑长程 |

R4.2静态检查先于R4.1候选集成执行；集成后另核原两renderer函数逐字未变，因此不重复46次检查。输入、原manifest、CPU ELF和实际检查期间源码身份完整记录。生产关键配置的required／conditional及非法值拒绝未修改，不恢复任何默认值；已通过的70键全矩阵不机械重跑。

## 协作、返修与证据边界

两个真实DeepSeek CLI任务并行；各一次定点返修，合计四次推理。W1修正将历史输入控制值误写成验收值的说明，明确原科学阈值保持；首次交付的格式失败保留。Codex曾给来源链接多写一个父目录，worker／独立审查指出，集成时纠正两处并验证目标，不追加付费润色。

W2返修只补CUDA有限值阶数失败及缺失指标两项main负对照。其完整JSON只列增量测试文件，网关按累计三个文件核对而报`FAILED/file_report_mismatch`；该provider交付按既有规则**REJECT／none**，未改网关或重写原元数据。项目工作包另外以完整实际diff、独立审查及当前工作区93项测试签收，不将网关拒收说成provider ACCEPT，也没有借此解锁网关full依赖。无需第三次付费格式返修；Gemini没有额外冻结文本编辑收益，未派发。完整产物、初次失败及返修检查均保留。

原本地LFS缓存／CI补丁未改，未下载表、未提交／推送或触发托管CI。本机测试通过与CI测试已注册分别记录；托管CI仍未运行。旧RZ／Cellular和发布科学未决项保持。

## 下一工作包与停止点

R4.3具备两包的项目独立验收前提，但本轮未启动。下一轮仍按R33冻结范围：重新绑定当前源码／输入／执行物，检查设备与宿主容量，再对PCM／MUSCL／PPM／Sod四个完整短组、12case执行原CPU→CUDA相关验收，使用新输出目录及原门槛。不运行全部22case、periodic1000、RKL或旧RZ长程；不把本轮静态API、mock、子集或旧输入结果当新的双后端数值通过。

回退按包撤其原owner改动并更新状态；R4.1回退重新开放已知跳组缺口，R4.2回退重新产生已知关键缺项。失败证据及原始结果不删除。R5及之后未开始。

两个临时实现工作树已请求可恢复归档；工具返回`queued`，本轮尚未确认移除，补丁与身份已保留。文档／证据检查通过，具体计数见机器索引。
