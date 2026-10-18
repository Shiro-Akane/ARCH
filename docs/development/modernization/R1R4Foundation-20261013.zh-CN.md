# ARCH R1–R4基础阶段收尾

2026-10-13，`compute/optim`，HEAD `34a6ccd5f409ec14ec11c4b798e5795e7592f26f`。**R4.3及整个R4已完成，R1–R4基础阶段收尾。** 本文件保留R4收尾时点；后续R5及其取舍见[主计划](../../../ARCH_MODERNIZATION_PLAN.md)。 当前决策统一在[主计划](../../../ARCH_MODERNIZATION_PLAN.md)，本文件保存基础成果、R4实际签收和候选去向，[机器索引](R1R4Foundation-20261013.json)提供身份、命令、失败与迁移证据。

这里的“基础阶段完成”指全仓映射、代表基线、联合审计及相关验证保护已完成，不表示全部物理路由、旧计算优化表R1–R6或发布科学出口已关闭，也不承诺必须继续重构。

## 已建立的基础

| 阶段 | 已完成 | 适用范围与证据 |
| --- | --- | --- |
| R1 全仓现状 | 路径分类、模块／文件职责、共享数学与必要后端边界、构建／测试／CI入口 | [全仓地图](../archive/modernization/ModernizationR1-Stage1Completion-20261012.zh-CN.md)、[路径索引](../archive/modernization/ModernizationR1-RepositoryCoverage-20261012.json)、[构建索引](../archive/modernization/ModernizationR1-BuildInventory-20261012.json)；静态覆盖不等于全路由运行证明 |
| R2 代表基线 | CPU27项、CUDA12项；原十应用51对checkpoint及三组收敛；运行／目标编译／RAM与O7复核 | [原收尾](../archive/modernization/R23PerformanceAndConfiguration-20261012.zh-CN.md)；四小负载32次运行、三目标18构建阶段，不是完整工程冷构建或跨版本提速 |
| R3 联合审计 | 保留共享核心；确认输入缺项、组门槛跳过；两次RKL编译归因、未活动重启身份评估；完整候选排序 | [原审计](../archive/modernization/R31AuditTriage-20261013.zh-CN.md)、[归因](../archive/modernization/R32RklCompilationAndInactiveIdentity-20261013.zh-CN.md)；消融减少覆盖，不作为优化采用 |
| R4 验证保护与集成 | 原runner完整组门槛、12输入显式声明、93项工具／46项静态配置检查；本轮四组新输入双后端实际签收 | [工具／输入原批次](../archive/modernization/R41R42Implementation-20261013.zh-CN.md)及下文；没有完整22-case矩阵、托管CI或旧长程重放 |

## 模块与文件归属入口

以下复用R1的实际所有者和现有[目录归属规范](../ImplementationOwnership.md#directory-and-file-placement)，不再建立另一套目录规则。完整调用与生成／第三方／历史分类在R1索引中。

| 范围 | 维护所有者与边界 |
| --- | --- |
| Hydro数学／阶段调度 | [`src/numerics`](../../../src/numerics)共享重构、通量与更新；[`src/driver`](../../../src/driver)安排阶段，CUDA执行由[`src/cuda/hydro`](../../../src/cuda/hydro)绑定 |
| AMR／网格／边界 | [`src/amr`](../../../src/amr)、[`src/grid`](../../../src/grid)及现有driver阶段；拓扑／ghost／reflux／状态生命周期不能用局部短测代签 |
| EOS／network／burn／NSE | [`src/physics`](../../../src/physics)、原数值积分owner及[`src/cuda/microphysics`](../../../src/cuda/microphysics)；生成由[`tools/network`](../../../tools/network)和现有CMake注册管理 |
| 重力／扩散 | 原physics与numerics owner；设备执行差异和独立oracle保留，RKL测试成本问题不授权删数学覆盖 |
| API／I/O／Studio | [`src/api`](../../../src/api)、[`src/io`](../../../src/io)、[`studio`](../../../studio)及各自host／renderer边界；GUI存在不等于真实Core运行验收 |
| 用户问题／工具／验证 | [`simulation`](../../../simulation)通过既有注册；[`tools`](../../../tools)、[`tests`](../../../tests)、[`validation`](../../../validation)各保有执行、控制检查及科学参考职责 |

新增、移动与拆分先核owner、消费者和实际构建归属。冻结行为注释按[既有规范](../CommentAndDocumentationStyle.md#comments-in-behavior-frozen-source)解释职责、流程、数学约定、单位及生命周期；不按文件大小拆分，不改公式／预算，也不改锁定原始结果。

## R4.3实际数值签收

当前原runner使用同一已有CUDA-capable ARCH的真实cpu／cuda执行lane，对PCM／MUSCL／PPM／Sod各三档64／128／256运行，共12 case：37对固定accepted-step对照＋12对物理终点对照，**49对CPU/CUDA比较、98条实际lane、49份CUDA trace通过，无失败、skip或截断**。独立后验复核重用原科学资格、组门槛、plan／trace／参数、文件哈希和provenance检查，原完整矩阵消费者明确拒绝这份12-case子集。

| 组 | 物理终点 | CPU／CUDA实测原门槛指标 | 原要求 |
| --- | --- | --- | --- |
| PCM | t=0.1 | 末对L1阶均0.994350543 | ≥0.9 |
| MUSCL | t=0.1 | 末对L1阶均2.040364969 | ≥1.8 |
| PPM | t=0.1 | 末对L1阶均3.999388436 | ≥2.7 |
| Sod | t=0.2 | 两相邻对最低L1／L2阶均0.919556866／0.601149782 | 每对L1≥0.7、L2≥0.3 |

Smooth按原mean定义的全部检查最大质量漂移为`7.77156e-16`，小于`1e-12`；Sod终点最大冲击位置误差`0.427593` cell，小于2.5，正性保持。逐case不同的解析误差限值仍直接来自[原manifest](../../../validation/backend/cases.json)。Smooth后端容差为`rtol=2e-10 / atol=2e-12`，Sod为`5e-9 / 5e-12`，没有统一或放宽。MUSCL的L2阶是诊断，不增设验收门槛。原比较字段的49对`max_abs`均为0，不能据此宣称全状态或全仓逐位一致。

本轮约154.05秒；护栏峰值owned RSS约309.7MiB，Swap增长0，无护栏停止；输出约55.6MiB。宿主E盘批次前约22GiB可用。Linux系统I/O PSI出现短峰但未达到持续停止条件；这不是Windows磁盘利用率、单进程热点或运行提速证据。

### 执行身份与入口修正

旧CUDA构建的真实CMake源为`/home/shiroakane/ARCH`；R33交接模板直接传当前工作区`--source-root`会被原provenance拒绝，因此未按该错误模板启动。实际使用当前工作区runner，保留真实build／source-root，仅在本轮派生manifest中把12个input路径映射至当前canonical绝对路径；22个case成员及其全部其他字段保持，实际只选择四组12case。原manifest、源码、CMake缓存和来源校验都未修改，未向主仓库复制输入。

真实构建来源、当前harness及新输入身份分别绑定；473份实际生产／构建源码与主目录逐字节一致，旧ELF／比较器哈希保持。此证据支持本轮复用，不是新构建或可复现构建认证。**后续生产源码变化必须重新建立匹配执行物和来源，不能继续沿用本轮旧ELF结果。** 实际命令、输入路径映射及独立核查在机器索引；原始H5仅在本机`output/modernization/r43-20261013/`保存。

### O7、工具与CI边界

原runner按完整manifest确定组选取；部分／混合组在输出、provenance、执行前拒绝并列缺失ID，完整组选原收敛函数；无新诊断模式、证据schema或兼容路径。12份输入显式`hll_wave_speed=roe`／`min_eint=1e-10` erg/g，其他键值保持；静态API的8项缺项／非法值负对照继续拒绝。本轮实际运行参数也核到两项显式控制，没有恢复生产默认。

R4.1／R4.2当前工作区93项工具和46项静态检查按未变harness／输入身份复用，不重复执行。现有Tooling job会发现新增main用例，最低必要CI注册已有；托管CI未运行，本地LFS缓存补丁未推送／托管验收，不能称CI全绿。R2两binary的70活动关键键和25项登记默认结论保持原范围，不宣称已证明全仓所有路由均无静默回退。

## 后续取舍：哪些推进，哪些保留

| 候选 | 当前决定 | 继续的触发条件／停止边界 |
| --- | --- | --- |
| A01 当前Hydro输入 | **本范围关闭** | 12模板缺项已修复并取得四组新科学签收；第13消费者periodic只核继承，未新增长程PASS |
| A02 选择case跳组 | **关闭** | main正／负控制及真实四组原门槛通过；未来触及选择语义再查，不重复本轮模拟 |
| C01 共享Hydro核心 | **保留** | 有具体数学／接口改动才扩大对应保护，不为现代化强行归并已有公共实现 |
| C02 RZ成本 | **后置** | 另设有界成本包；旧失败／截断与长程科学出口保留，不自动重开五声穿越 |
| C03 Cellular差异 | **科学未决，独立后置** | 原精确状态报告保留；手工模板缺11项声明的维护／退役独立决定，不由Hydro短组代签 |
| C04／A03 RKL编译成本 | **后续R5已完成，最小拆分采用** | [主计划](../../../ARCH_MODERNIZATION_PLAN.md)记录实测局部／RAM收益、完整集合及体积代价；本文件的R1–R4科学身份保持 |
| C05／A05 目录与注释 | **按实际阅读障碍局部维护** | 本轮关闭日期进度入口重复这一窄问题；使用原ownership／style，不做全仓迁移或批量注释改写 |
| C06 可选能力／生成网络 | **保留，按触及范围验证** | 不强制下载／生成／测试全部组合，不增第二套注册体系 |
| C07 生命周期／耦合出口 | **保留owner，科学未决仍开放** | 改动触及regrid／restart／状态才补关联保护；短Hydro不能替代耦合或长期轨迹 |
| C08 后端层／转发／oracle | **保留合理职责** | 新的具体重复或成本证据才重开；不要求兼容也不等于必须删除全部层 |
| A04 未活动重启身份 | **保持严格，按具体需求后置** | 完整审计字段不删；只有真实需求及字段／版本／受保护契约决定后再讨论活动投影，不恢复默认 |

没有新的架构组件因本轮被强制退役。后续R5已领取并关闭有证据的一个构建效率包，结果统一见主计划；原分工为：Codex冻结目标／基线与排程，W1维护实际测试owner，W2测增量／总成本／RAM，W3核原覆盖与复杂度。不得自动展开其他候选，非阻断优化无收益时停止。

## 文档与证据归一

当前只维护主计划和本综合报告的阅读入口；7份日期人读报告归档，科学内容与失败保持，仅修相对链接。原文byte副本和迁移前后SHA留在本轮本机记录。7份旧机器JSON按原路径／原字节保留为历史收据，不继续当当前计划更新。

| 历史范围 | 归档人读记录 | 原机器收据 |
| --- | --- | --- |
| R2.1身份 | [基线](../archive/modernization/BaselineIdentity-20261012.zh-CN.md) | [JSON](BaselineIdentity-20261012.json) |
| R2.2数值 | [原批次](../archive/modernization/R22NumericalChecks-20261012.zh-CN.md) | [JSON](R22NumericalChecks-20261012.json) |
| R2.3／R2收尾 | [性能与O7](../archive/modernization/R23PerformanceAndConfiguration-20261012.zh-CN.md) | [JSON](R23PerformanceAndConfiguration-20261012.json) |
| R3.1审计 | [审计取舍](../archive/modernization/R31AuditTriage-20261013.zh-CN.md) | [JSON](R31AuditTriage-20261013.json) |
| R3.2归因 | [RKL／身份](../archive/modernization/R32RklCompilationAndInactiveIdentity-20261013.zh-CN.md) | [JSON](R32RklCompilationAndInactiveIdentity-20261013.json) |
| R3.3交接 | [冻结工作包](../archive/modernization/R33CloseoutAndR4Handoff-20261013.zh-CN.md) | [JSON](R33CloseoutAndR4Handoff-20261013.json) |
| R4.1／R4.2 | [实现与独立验收](../archive/modernization/R41R42Implementation-20261013.zh-CN.md) | [JSON](R41R42Implementation-20261013.json) |

旧JSON中的人读路径／哈希描述其原快照；本轮机器索引提供明确`relocation_map`，不反写旧记录或制造跳转stub。[既有档案导航](../archive/README.md)保留R1与前置入口。工作流失败、provider拒收、原始检查日志继续在原收据和本机产物，不复制成多份当前规则。
